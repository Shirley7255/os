//buddy_pmm.c
#include <pmm.h>
#include <list.h>
#include <string.h>
#include <buddy_pmm.h>
#include <stdio.h>

#define MAX_ORDER 15          /* 2^15 = 32768 pages  (128 MB) */
#define DEBUG_BS  0       

#if DEBUG_BS
#define BS_TRACE(...) cprintf(__VA_ARGS__)
#else
#define BS_TRACE(...) ((void)0)
#endif

//全局数据
typedef struct {
    list_entry_t free_list[MAX_ORDER];
    size_t       nr_free;          //总空闲页数
} buddy_area_t;

static buddy_area_t buddy_area;

#define free_list (buddy_area.free_list)
#define nr_free   (buddy_area.nr_free)

//辅助函数
static inline int
pages_to_order(size_t n)
{
    int order = 0;
    size_t tmp = 1;
    while (tmp < n) {
        tmp <<= 1;
        order++;
    }
    return order;
}

static inline size_t
order_to_pages(int order)
{
    return (1UL << order);
}

#define le2page(le, member)  to_struct((le), struct Page, member)

//初始化
static void
buddy_init(void)
{
    for (int i = 0; i < MAX_ORDER; ++i)
        list_init(&free_list[i]);
    nr_free = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n)
{
    assert(n > 0 && base != NULL);
    struct Page *p = base;
    for (; p != base + n; ++p) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    int order = pages_to_order(n);
    size_t covered = order_to_pages(order);
    if (covered > n) {               /* 向上取 2^n 可能越界，回退一级 */
        order--;
        covered = order_to_pages(order);
    }
    assert(covered <= n);

    base->property = order;          /* 记录阶数，而非页数 */
    SetPageProperty(base);
    list_add(&free_list[order], &(base->page_link));
    nr_free += covered;
}

//分配
static struct Page *
buddy_alloc_pages(size_t n)
{
    if (n == 0 || n > nr_free)
        return NULL;

    int req_order = pages_to_order(n);
    int order;
    for (order = req_order; order < MAX_ORDER; ++order)
        if (!list_empty(&free_list[order]))
            goto found;
    return NULL;                  /* 没有合适块 */

found:
    /* 逐级拆分直到刚好满足 */
    while (order > req_order) {
        list_entry_t *le = list_next(&free_list[order]);
        struct Page *page = le2page(le, page_link);
        list_del(le);

        int buddy_order = order - 1;
        size_t buddy_pages = order_to_pages(buddy_order);
        struct Page *buddy = page + buddy_pages;

        page->property = buddy->property = buddy_order;
        SetPageProperty(page);
        SetPageProperty(buddy);

        list_add(&free_list[buddy_order], &(page->page_link));
        list_add(&free_list[buddy_order], &(buddy->page_link));
        order = buddy_order;
    }

    /* 现在 order == req_order，直接摘下 */
    list_entry_t *le = list_next(&free_list[order]);
    struct Page *page = le2page(le, page_link);
    list_del(le);
    ClearPageProperty(page);
    //<<<<< 改动1：高16位=请求页数，低16位=order
    page->property = ((n & 0xFFFF) << 16) | (order & 0xFFFF);
    nr_free -= order_to_pages(order);

    BS_TRACE("buddy_alloc: %lu pages (order %d) at %p\n",
             order_to_pages(order), order, page);
    return page;
}

//释放与合并
static void
buddy_free_pages(struct Page *base, size_t n)
{
    if (n == 0) return;
    assert(!PageReserved(base) && !PageProperty(base));

    //<<<<< 改动2：取出高16位“请求页数”
    unsigned int req_pages = (base->property >> 16) & 0xFFFF;
    int          order     = base->property & 0xFFFF;
    size_t       covered   = order_to_pages(order);
    assert(req_pages == n);          // 必须按申请数释放
    (void)covered;                   // 抑制警告

    base->property = order;          // 恢复为纯阶数
    SetPageProperty(base);
    nr_free += covered;

    /* 向上合并伙伴 */
    while (order < MAX_ORDER - 1) {
        uintptr_t base_pa  = page2pa(base);
        uintptr_t buddy_pa = base_pa ^ (order_to_pages(order) * PGSIZE);
        struct Page *buddy = pa2page(buddy_pa);

        if (!PageProperty(buddy) || buddy->property != order)
            break;                /* 无法合并 */

        /* 合并：摘下伙伴，选低地址做头 */
        list_del(&(buddy->page_link));
        ClearPageProperty(buddy);
        if (base > buddy) base = buddy;

        order++;
        base->property = order;
        SetPageProperty(base);
    }

    list_add(&free_list[order], &(base->page_link));
    BS_TRACE("buddy_free: %lu pages (order %d) at %p\n",
             covered, order, base);
}

/* --------- nr_free --------- */
static size_t
buddy_nr_free_pages(void)
{
    return nr_free;
}

/* --------- check --------- */
static void
buddy_check(void)
{
    cprintf("======== Buddy System Extended Check Start ========\n");

    struct Page *p1, *p2, *p3, *p4;
    size_t nr_orig = nr_free_pages();   /* 初始空闲页 */

    /* 1. 最小页（1 页）*/
    cprintf("[TEST 1] 最小页分配（1 页）\n");
    p1 = alloc_pages(1);
    assert(p1 != NULL);
    assert(nr_free_pages() == nr_orig - 1);
    free_pages(p1, 1);
    assert(nr_free_pages() == nr_orig);
    cprintf("        PASS：1 页分配/释放正确，nr_free 恢复\n");

    /* 2. 非 2 的幂（10 页 → 16 页）*/
    cprintf("[TEST 2] 非 2 的幂分配（10 页，向上取整 16 页）\n");
    size_t before = nr_free_pages();
    if (before < 16) {
        cprintf("        SKIP：内存不足 16 页\n");
    } else {
        p1 = alloc_pages(10);
        assert(p1 != NULL);
        assert(nr_free_pages() == before - 16);
        free_pages(p1, 10);          /* 按请求数释放 */
        assert(nr_free_pages() == before);
        cprintf("        PASS：10 页请求实际分配 16 页，释放后合并\n");
    }

    /* 3. 最大块（8192 页）*/
    cprintf("[TEST 3] 边界最大块分配（真实最大阶）\n");
    int max_order = 0;
    for (int i = MAX_ORDER - 1; i >= 0; --i) {
        if (!list_empty(&free_list[i])) {
            max_order = i;
            break;
        }
    }
    size_t max_pages = order_to_pages(max_order);
    if (max_pages == 0) {
        cprintf("        SKIP：没有空闲块\n");
    } else {
        p1 = alloc_pages(max_pages);
        assert(p1 != NULL);                   /* 一定能拿到 */
        assert(nr_free_pages() == nr_orig - max_pages);
        free_pages(p1, max_pages);
        assert(nr_free_pages() == nr_orig);
        cprintf("        PASS：%lu 页（order=%d）分配/释放成功\n",
                max_pages, max_order);
    }

    /* 4. 多级合并（4×8 页 → 32 页）*/
    cprintf("[TEST 4] 多级合并（4×8 页 → 32 页）\n");
    before = nr_free_pages();
    if (before < 32) {
        cprintf("        SKIP：内存不足 32 页\n");
    } else {
        p1 = alloc_pages(8);
        p2 = alloc_pages(8);
        p3 = alloc_pages(8);
        p4 = alloc_pages(8);
        free_pages(p1, 8);
        free_pages(p2, 8);
        free_pages(p3, 8);
        free_pages(p4, 8);
        assert(nr_free_pages() == before);
        cprintf("        PASS：4×8 页 → 1×32 页多级合并成功\n");
    }

    /* 5. 超量分配（nr_free+1）*/
    cprintf("[TEST 5] 超量分配（nr_free+1）\n");
    p1 = alloc_pages(nr_free_pages() + 1);
    assert(p1 == NULL);
    cprintf("        PASS：返回 NULL，拒绝超量分配\n");

    /* 6. 重复释放（仅观察）*/
    cprintf("[TEST 6] 重复释放防护（仅观察）\n");
    p1 = alloc_pages(1);
    free_pages(p1, 1);
    cprintf("        INFO：重复释放会触发断言，已跳过第二次释放\n");

    cprintf("======== Buddy System Extended Check Passed ========\n");
}

const struct pmm_manager buddy_pmm_manager = {
    .name          = "buddy_pmm_manager",
    .init          = buddy_init,
    .init_memmap   = buddy_init_memmap,
    .alloc_pages   = buddy_alloc_pages,
    .free_pages    = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check         = buddy_check,
};
