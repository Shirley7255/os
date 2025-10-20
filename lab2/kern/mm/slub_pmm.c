#include <pmm.h>
#include <list.h>
#include <string.h>
#include <slub_pmm.h>
#include <stdio.h>
#include <assert.h>
#include <mmu.h>
#include <memlayout.h>
#define OBJ_CNT 5 
// 手动定义KADDR宏，将物理地址转换为内核虚拟地址
// 根据memlayout.h，虚拟地址 = 物理地址 + PHYSICAL_MEMORY_OFFSET
#define KADDR(pa) ((void *)((uintptr_t)(pa) + PHYSICAL_MEMORY_OFFSET))

// 页级内存分配器的自由内存区域管理
static free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

// 从链表项获取对应的slab结构
#define le2slab(le, member)                 \
    to_struct((le), struct Slab, member)

/**
 * Slab结构：管理一页内存中的多个相同大小的对象
 * 每个Slab对应一个物理页
 */
typedef struct Slab {
    list_entry_t list;       // 用于链接到所属Cache的链表
    size_t free_cnt;         // 空闲对象数量
    void *objs;              // 对象存储区起始地址
    unsigned char *bitmap;   // 位图，标记对象是否被分配(1=已分配, 0=空闲)
} slab_t;

/**
 * Cache结构：管理相同大小对象的缓存
 * 每个Cache对应一种固定大小的对象
 */
typedef struct Cache {
    list_entry_t slabs;      // 管理的Slab链表
    size_t obj_size;         // 单个对象的大小
    size_t objs_num;         // 每个Slab中可容纳的对象数量
} cache_t;

// 预定义3种大小的缓存(32B, 64B, 128B)
static cache_t caches[3];
static size_t cache_n = 0;  // 缓存数量

/**
 * 计算单个Slab中可容纳的对象数量
 * 公式：总空间 = slab结构体大小 + 对象总大小 + 位图大小
 * @param obj_size 对象大小
 * @return 最大可容纳的对象数量
 */
static size_t calculate_objs_num(size_t obj_size) {
    size_t slab_struct_size = sizeof(slab_t);
    // 计算最大可容纳的对象数量，确保总大小不超过一页
    size_t objects_per_slab = ((PGSIZE - slab_struct_size) / (obj_size + 1.0 / 8.0));
    // 确保至少能容纳一个对象
    return objects_per_slab > 0 ? objects_per_slab : 1;
}

/**
 * 初始化缓存结构
 * 创建3种大小的缓存：32B, 64B, 128B
 */
static void cache_init(void) {
    cache_n = 3;
    size_t sizes[3] = {32, 64, 128};  // 三种对象大小
    
    for (int i = 0; i < cache_n; i++) {
        caches[i].obj_size = sizes[i];
        caches[i].objs_num = calculate_objs_num(sizes[i]);
        list_init(&caches[i].slabs);
    }
}

/**
 * 初始化页级内存分配器
 */
static void default_init(void) {
    list_init(&free_list);
    nr_free = 0;  // 初始无空闲页
}

/**
 * 初始化SLUB内存分配器
 * 先初始化页级分配器，再初始化缓存结构
 */
static void slub_init(void) {
    default_init();         // 初始化第一层：页级分配器
    cache_init();           // 初始化第二层：SLUB缓存
}

/**
 * 初始化物理页映射
 * @param base 物理页起始地址
 * @param n 页数量
 */
static void default_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    
    for (; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    
    base->property = n;  // 标记为空闲块头部
    SetPageProperty(base);
    nr_free += n;
    
    // 将新的空闲块插入到合适位置，保持链表有序
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t *le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page *page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }
}

/**
 * 初始化SLUB内存分配器的物理页映射
 * @param base 物理页起始地址
 * @param n 页数量
 */
static void slub_init_memmap(struct Page *base, size_t n) {
    default_init_memmap(base, n);
}

/**
 * 页级分配器：分配连续的物理页
 * @param n 所需页数
 * @return 成功返回页指针，失败返回NULL
 */
static struct Page *default_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;  // 内存不足
    }
    
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    
    // 查找第一个足够大的空闲块
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    
    if (page != NULL) {
        list_entry_t *prev = list_prev(&(page->page_link));
        list_del(&(page->page_link));
        
        // 如果有剩余空间，分割成新的空闲块
        if (page->property > n) {
            struct Page *p = page + n;
            p->property = page->property - n;
            SetPageProperty(p);
            list_add(prev, &(p->page_link));
        }
        
        nr_free -= n;
        ClearPageProperty(page);
    }
    
    return page;
}

/**
 * 创建新的Slab
 * @param obj_size 对象大小
 * @param objs_num 每个Slab可容纳的对象数量
 * @return 成功返回slab指针，失败返回NULL
 */
static slab_t *create_slab(size_t obj_size, size_t objs_num) {
    // 分配一个物理页
    struct Page *page = default_alloc_pages(1);
    if (!page) {
        return NULL;
    }
    
    // 转换为内核虚拟地址
    void *kva = KADDR(page2pa(page));
    slab_t *slab = (slab_t *)kva;
    
    // 初始化Slab结构
    slab->free_cnt = objs_num;
    slab->objs = (void *)slab + sizeof(slab_t);  // 对象存储区起始地址
    // 位图起始地址 = 对象存储区起始地址 + 对象总大小
    slab->bitmap = (unsigned char *)((void *)slab->objs + obj_size * objs_num);
    // 初始化位图(全部置0，表示所有对象空闲)
    memset(slab->bitmap, 0, (objs_num + 7) / 8);
    list_init(&slab->list);
    
    return slab;
}

/**
 * SLUB分配器：分配指定大小的对象
 * @param size 所需对象大小
 * @return 成功返回对象指针，失败返回NULL
 */
static void *slub_alloc_obj(size_t size) {
    if (size <= 0) {
        return NULL;
    }
    
    // 查找合适的缓存(对象大小不小于所需大小的最小缓存)
    cache_t *cache = NULL;
    for (int i = 0; i < cache_n; i++) {
        if (caches[i].obj_size >= size) {
            cache = &caches[i];
            break;
        }
    }
    if (cache == NULL) {
        return NULL;  // 没有合适的缓存
    }
    
    // 在缓存的Slab链表中查找有空闲对象的Slab
    list_entry_t *le = &cache->slabs;
    while ((le = list_next(le)) != &cache->slabs) {
        slab_t *slab = le2slab(le, list);
        if (slab->free_cnt > 0) {
            // 查找第一个空闲对象
            for (size_t i = 0; i < cache->objs_num; i++) {
                size_t byte = i / 8;
                size_t bit = i % 8;
                if (!(slab->bitmap[byte] & (1 << bit))) {
                    // 标记为已分配
                    slab->bitmap[byte] |= (1 << bit);
                    slab->free_cnt--;
                    // 返回对象地址
                    return (void *)slab->objs + i * cache->obj_size;
                }
            }
        }
    }
    
    // 没有可用的Slab，创建新的Slab
    slab_t *new_slab = create_slab(cache->obj_size, cache->objs_num);
    if (!new_slab) {
        return NULL;  // 内存不足
    }
    
    // 将新Slab加入缓存的Slab链表
    list_add(&cache->slabs, &new_slab->list);
    // 分配第一个对象
    new_slab->bitmap[0] |= 1;  // 标记第一个对象为已分配
    new_slab->free_cnt--;
    
    return new_slab->objs;
}

/**
 * 页级分配器：释放物理页
 * @param base 要释放的物理页起始地址
 * @param n 页数
 */
static void default_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    
    // 重置页属性
    for (; p != base + n; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    
    // 将释放的页插入到合适位置
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t *le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page *page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }
    
    // 尝试与前一个空闲块合并
    list_entry_t *le = list_prev(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (p + p->property == base) {
            p->property += base->property;
            ClearPageProperty(base);
            list_del(&(base->page_link));
            base = p;
        }
    }
    
    // 尝试与后一个空闲块合并
    le = list_next(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }
    }
}

/**
 * SLUB分配器：释放对象
 * @param obj 要释放的对象指针
 */
static void slub_free_obj(void *obj) {
    // 查找对象所属的Slab和Cache
    for (size_t i = 0; i < cache_n; i++) {
        cache_t *cache = &caches[i];
        list_entry_t *le = &cache->slabs;
        
        while ((le = list_next(le)) != &cache->slabs) {
            slab_t *slab = le2slab(le, list);
            // 检查对象是否在当前Slab的对象存储区内
            if (obj >= slab->objs && obj < (slab->objs + cache->obj_size * cache->objs_num)) {
                // 计算对象在Slab中的索引
                size_t offset = (char *)obj - (char *)slab->objs;
                size_t index = offset / cache->obj_size;
                size_t byte = index / 8;
                size_t bit = index % 8;
                
                // 标记为未分配
                if (slab->bitmap[byte] & (1 << bit)) {
                    slab->bitmap[byte] &= ~(1 << bit);
                    slab->free_cnt++;
                    // 清空对象内存，避免信息泄露
                    memset(obj, 0, cache->obj_size);
                    
                    // 如果Slab中所有对象都已释放，回收整个Slab
                    if (slab->free_cnt == cache->objs_num) {
                        list_del(&slab->list);
                        // 释放Slab对应的物理页
                        struct Page *page = pa2page(PADDR(slab));
                        default_free_pages(page, 1);
                    }
                }
                return;
            }
        }
    }
}

/**
 * 获取空闲页数量
 * @return 空闲页数量
 */
static size_t slub_nr_free_pages(void) {
    return nr_free;
}

/**
 * 测试SLUB分配器的正确性
 */
static void slub_check(void) {
    cprintf("Starting SLUB allocator tests...\n\n");
    cprintf("The slab struct size is %d bytes\n", sizeof(slab_t));
    cprintf("----------------------START-------------------------\n");
    
    // 验证初始化后的对象数量是否正确
    size_t nums[3] = {126, 63, 31};  // 32B, 64B, 128B对应的每个Slab对象数
    for (int i = 0; i < cache_n; i++) {
        assert(caches[i].objs_num == nums[i]);
    }
    
    size_t nr_1 = nr_free;  // 记录初始空闲页数量
    
    // 1. 边界检查
    {
        void *obj = slub_alloc_obj(0);
        assert(obj == NULL);
        
        obj = slub_alloc_obj(256);  // 超过最大缓存大小
        assert(obj == NULL);
        
        cprintf("Boundary check passed.\n");
    }
    
    // 2. 基本分配/释放功能检查
    {
        void *obj1 = slub_alloc_obj(32);
        assert(obj1 != NULL);
        cprintf("Allocated 32-byte object at %p\n", obj1);
        
        // 验证内存写入
        memset(obj1, 0x66, 32);
        for (int i = 0; i < 32; i++) {
            assert(((unsigned char *)obj1)[i] == 0x66);
        }
        cprintf("Memory alloc verification passed.\n");
        
        // 释放对象
        slub_free_obj(obj1);
        
        // 验证释放后重新分配的内存是否清零
        void *obj2 = slub_alloc_obj(32);
        cprintf("Allocated 32-byte object at %p\n", obj2);
        for (int i = 0; i < 32; i++) {
            assert(((unsigned char *)obj2)[i] == 0x00);
        }
        slub_free_obj(obj2);
        
        cprintf("Memory free verification passed.\n");
    }
    
    // 3. 多个对象分配/释放检查
    {
        const int NUM_TEST_OBJS = 10;
        void *test_objs[NUM_TEST_OBJS];
        
        cprintf("Allocating %d objects of size 64 bytes.\n", NUM_TEST_OBJS);
        for (int i = 0; i < NUM_TEST_OBJS; i++) {
            test_objs[i] = slub_alloc_obj(64);
            assert(test_objs[i] != NULL);
            memset(test_objs[i], i, 64);  // 每个对象写入不同的值
        }
        
        // 验证内存内容
        for (int i = 0; i < NUM_TEST_OBJS; i++) {
            for (int j = 0; j < 64; j++) {
                assert(((unsigned char *)test_objs[i])[j] == (unsigned char)i);
            }
        }
        cprintf("Memory verification for 64-byte objects passed.\n");
        
        // 释放并验证
        for (int i = 0; i < NUM_TEST_OBJS; i++) {
            slub_free_obj(test_objs[i]);
            cprintf("Freed 64-byte object at %p\n", test_objs[i]);
            // 验证内存已清零
            for (int j = 0; j < 64; j++) {
                assert(((unsigned char *)test_objs[i])[j] == 0x00);
            }
        }
        cprintf("Memory free verification for 64-byte objects passed.\n");
    }
    
    
    // 4. 混合分配释放流程检查
    {
        cprintf("Mixed allocation/free check start.\n");
        
        // 分配不同大小的对象
        void *obj1 = slub_alloc_obj(32);
        assert(obj1 != NULL);
        cprintf("Allocated 32-byte object at %p\n", obj1);
        assert(nr_free == nr_1 - 1);
        
        void *obj2 = slub_alloc_obj(64);
        assert(obj2 != NULL);
        cprintf("Allocated 64-byte object at %p\n", obj2);
        assert(nr_free == nr_1 - 2);
        
        void *obj3 = slub_alloc_obj(128);
        assert(obj3 != NULL);
        cprintf("Allocated 128-byte object at %p\n", obj3);
        assert(nr_free == nr_1 - 3);
        
        // 再分配一个32字节对象，应该使用同一个Slab
        void *obj4 = slub_alloc_obj(32);
        assert(obj4 != NULL);
        cprintf("Allocated second 32-byte object at %p\n", obj4);
        assert(nr_free == nr_1 - 3);
        
        // 分配29个128字节对象(当前Slab共30个)
        void *objs[30];
        for (int i = 0; i < 29; i++) {
            objs[i] = slub_alloc_obj(128);
            assert(objs[i] != NULL);
        }
        
        // 第31个128字节对象，应该还在同一个Slab
        void *obj5 = slub_alloc_obj(128);
        assert(obj5 != NULL);
        cprintf("Allocated 31th 128-byte object at %p\n", obj5);
        assert(nr_free == nr_1 - 3);
        
        // 第32个128字节对象，需要新的Slab
        void *obj6 = slub_alloc_obj(128);
        assert(obj6 != NULL);
        cprintf("Allocated 32th(new slab) 128-byte object at %p\n", obj6);
        assert(nr_free == nr_1 - 4);
        
        // 释放29个128字节对象
        for (int i = 0; i < 29; i++) {
            slub_free_obj(objs[i]);
        }
        assert(nr_free == nr_1 - 4);
        
        // 逐步释放对象，验证内存回收
        slub_free_obj(obj1);
        assert(nr_free == nr_1 - 4);
        
        slub_free_obj(obj2);
        assert(nr_free == nr_1 - 3);
        
        slub_free_obj(obj3);
        assert(nr_free == nr_1 - 3);
        
        slub_free_obj(obj4);
        assert(nr_free == nr_1 - 2);
        
        slub_free_obj(obj5);
        assert(nr_free == nr_1 - 1);
        
        slub_free_obj(obj6);
        assert(nr_free == nr_1);  // 回到初始状态
        
        cprintf("Mixed allocation/free check passed.\n");
    }
    
    cprintf("----------------------END-------------------------\n");
    cprintf("All SLUB allocator tests passed successfully!\n");
}
// SLUB内存分配器接口
const struct pmm_manager slub_pmm_manager = {
    .name = "slub_pmm_manager",
    .init = slub_init,
    .init_memmap = slub_init_memmap,
    .alloc_pages = (void *)slub_alloc_obj,  // 适配pmm_manager接口
    .free_pages = (void *)slub_free_obj,    // 适配pmm_manager接口
    .nr_free_pages = slub_nr_free_pages,
    .check = slub_check,
};
    
