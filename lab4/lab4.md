# lab4

2312668 李佳    2312674 朱晨瑞  2311061 马淏怡

## 练习1：分配并初始化一个进程控制块

### 1.初始化

进入proc.c文件中，找到对应代码，初始化为如下内容：

```c
proc->state = PROC_UNINIT;
proc->pid = -1;
proc->runs = 0;
proc->kstack = 0;
proc->need_resched = false;
proc->parent = NULL;
proc->mm = NULL;
memset(&proc->context, 0, sizeof(struct context));
proc->tf = NULL;
proc->pgdir = boot_pgdir_pa;
proc->flags = 0;
memset(proc->name, 0, sizeof(proc->name));
```

`pro->state`:state 存储进程的生命周期状态，刚通过 kmalloc 分配完 的PCB属于「未初始化完成」状态，因此用 PROC_UNINIT 标记。

`proc->pid`:pid 是进程的唯一标识符，用于操作系统区分不同进程,初始设为是为了避免与合法 PID 冲突。

`proc->runs`:runs 统计进程的累计运行次数（每次被调度器选中并执行，runs 会自增），用于调度算法的优先级计算。进程刚创建，尚未被调度器选中执行过，因此运行次数为0。

`proc->kstack`:kstack 存储进程内核栈的物理地址。刚创建 PCB 时，内核栈尚未分配，因此用 0 标记未分配状态。

`proc->need_resched`:need_resched 是调度请求标志，用于标记进程是否需要主动放弃 CPU。进程刚创建，尚未运行，不存在 “占用 CPU 需释放” 的情况，因此初始为false。

`proc->parent`:parent 是父进程指针，用于维护 ucore 的进程树结构。进程刚创建时，还没有与父进程建立关联，因此初始为NULL。

`proc->mm`:mm 是内存管理结构指针，存储用户进程的地址空间映射信息，内核线程无需独立的地址空间，因此 mm 为 NULL。

`memset(&proc->context, 0, sizeof(struct context))`:context 是进程上下文结构，存储进程切换时需要保存的关键寄存器值。进程刚创建，没有有效的寄存器数据，因此用 memset 清零所有字段。

`proc->tf`:tf 是陷阱帧指针，存储进程在用户态 / 内核态发生中断 / 异常时的寄存器状态，用于中断处理完成后恢复进程执行。进程刚创建，未发生任何中断 / 异常，因此不存在有效的陷阱帧数据，初始为 NULL。

`proc->pgdir`:pgdir存储页目录表的物理基地址，是分页机制的核心。boot_pgdir_pa 是内核启动时初始化的全局页目录物理地址，包含内核地址空间的完整映射。

`proc->flags`:flags 是进程状态标志位，用位图表示进程的特殊状态，内核通过标志位快速判断进程状态。进程刚创建没有任何特殊状态，因此初始为0。

`memset(proc->name, 0, sizeof(proc->name))`:name 是进程名称字符数组（长度 PROC_NAME_LEN+1），用于调试和进程识别。用 memset 清零后，后续可通过 set_proc_name 函数为进程赋值合法名称。

### 2.`struct context context` 和 `struct trapframe *tf`

#### （1）struct context

- 含义：struct context 是专门用于保存进程执行上下文的结构体，存储进程切换时必须保留的关键寄存器值，确保进程再次被调度时能恢复到切换前的执行状态。

  | 字段     | 寄存器类型         | 核心功能                                                     |
  | -------- | ------------------ | ------------------------------------------------------------ |
  | `ra`     | 返回地址寄存器     | 保存函数调用后的返回地址，进程恢复时能回到切换前的代码执行位置。 |
  | `sp`     | 栈指针寄存器       | 指向当前进程内核栈的栈顶，确保恢复后函数调用栈的完整性。     |
  | `s0~s11` | 被调用者保存寄存器 | 用于存储临时数据，这类寄存器在函数调用时不会被调用者覆盖，需手动保存恢复。 |

- 为什么只保存这些寄存器：寄存器分为”调用者保存寄存器“和”被调用者保存寄存器“（s0~s11、ra、sp）。进程切换通过 switch_to 函数实现，编译器会自动处理调用者保存寄存器的保存 / 恢复，因此 context 只需存储被调用者保存寄存器，以减小开销。

- 本实验中作用：

  - 初始化阶段：通过 `memset(&proc->context, 0, sizeof(struct context))` 清零所有字段。此时进程未运行，无有效执行状态，清零可避免 `kmalloc` 残留的垃圾数据导致切换时寄存器异常。

  - 进程创建阶段（copy_thread 函数）：为新进程设置初始上下文，确保进程首次运行能正确启动。

    ```c
    proc->context.ra = (uintptr_t)forkret; // 恢复后执行 forkret 函数
    proc->context.sp = (uintptr_t)(proc->tf); // 栈指针指向内核栈顶的 trapframe
    ```

    这里 ra 设为 forkret 地址，是进程首次被调度时的入口；sp 绑定到 trapframe 地址，确保栈空间合法。

  - 进程切换阶段（proc_run 函数）：调用 `switch_to(&(prev->context), &(next->context))` 完成上下文切换：
    - 保存当前进程的 context 到其 PCB 中
    - 从目标进程的 context 中恢复寄存器值（ra、sp、s0~s11）
    - 切换后，CPU 会从 next->context.ra 指向的地址继续执行，实现进程切换。

#### （2）struct trapframe *tf

- 含义：struct trapframe 是中断 / 异常帧结构体，用于保存进程在中断 / 异常发生时的完整寄存器状态，包括 32 个通用寄存器和异常相关控制寄存器。

  | 核心字段                                                 | 功能说明                                                     |
  | -------------------------------------------------------- | ------------------------------------------------------------ |
  | 32 个通用寄存器（zero、ra、sp、a0~a7、s0~s11、t0~t6 等） | 保存中断发生时所有通用寄存器的值，覆盖进程执行的完整数据状态。 |
  | `status`                                                 | 保存中断发生时的 CPU 状态寄存器，恢复时需还原该状态。        |
  | `epc`                                                    | 异常程序计数器，指向中断发生前正在执行的指令地址，用于中断返回后继续执行。 |
  | `badvaddr`                                               | 存储触发异常的错误地址。                                     |
  | `cause`                                                  | 存储异常原因，用于中断处理函数判断类型。                     |

- 本实验中作用：

  - 初始化阶段：初始化为 NULL。此时进程未运行，未发生任何中断 / 异常，NULL标记 “未初始化” 状态。

  - 进程创建阶段（copy_thread 函数）：为新进程复制父进程的中断帧，同时设置子进程的初始执行状态。

    ```c
    // 1. 在新进程内核栈顶分配 trapframe 空间
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    // 2. 复制父进程的 trapframe
    *(proc->tf) = *tf;
    // 3. 设置子进程 fork 返回值为 0（区分父进程的 PID 返回值）
    proc->tf->gpr.a0 = 0;
    // 4. 绑定栈指针
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;
    ```

    通过复制父进程 tf 让子进程继承执行环境，同时修改 a0 寄存器为 0，实现 fork 系统调用 “父进程返回子进程 PID，子进程返回 0” 。

  - 内核线程启动阶段：新进程首次被调度时，context.ra 指向 forkret，最终调用 forkrets(current->tf)，从 tf 中恢复所有寄存器状态（包括 epc、通用寄存器），启动进程执行。
  - 中断处理准备：若进程运行中触发中断，内核会将当前寄存器状态写入 tf，中断处理完成后通过 tf 恢复进程执行，确保中断不破坏进程原有执行状态。



## 练习2：为新创建的内核线程分配资源

### 1.实现目标

我们要完成proc.c中do_fork函数的处理过程，大致执行步骤包括：

- 调用alloc_proc，首先获得一块用户信息块。
- 为进程分配一个内核栈。
- 复制原进程的内存管理信息到新进程（但内核线程不必做此事）
- 复制原进程上下文到新进程
- 将新进程添加到进程列表
- 唤醒新进程
- 返回新进程号

### 2.代码实现

```c
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;  // 初始返回值：无空闲进程
    struct proc_struct *proc;   // 新线程的 PCB 指针
    if (nr_process >= MAX_PROCESS)  // 检查系统进程数是否超限
    {
        goto fork_out;  // 超限则直接返回错误
    }
    ret = -E_NO_MEM;  // 若后续步骤失败，默认返回“内存不足”错误

    // 步骤1：调用 alloc_proc 分配 PCB
    if ((proc = alloc_proc()) == NULL)  // 分配失败
    {
        goto bad_fork_cleanup_proc;  // 跳转到 PCB 清理标签
    }

    // 步骤2：调用 setup_kstack 为新线程分配内核栈
    if (setup_kstack(proc) != 0)  // 栈分配失败（无空闲物理页）
    {
        goto bad_fork_cleanup_kstack;  // 跳转到“释放栈+PCB”清理标签
    }

    // 步骤3：调用 copy_mm 复制内存管理信息（内核线程无需复制，current->mm=NULL）
    if (copy_mm(clone_flags, proc) != 0)  // 内存管理初始化失败
    {
        goto bad_fork_cleanup_kstack;  // 释放栈+PCB
    }

    // 步骤4：调用 copy_thread 复制父进程上下文（trapframe + context）
    copy_thread(proc, stack, tf);  // void 类型函数，无需返回值判断
    // 在新线程内核栈顶分配 trapframe，初始化 context.ra=forkret（启动入口）

    // 步骤5：将新线程加入进程管理结构
    proc->pid = get_pid();          // 分配唯一 PID
    hash_proc(proc);                // 加入 PID 哈希表
    list_add(&proc_list, &(proc->list_link));  // 加入全局进程列表
    nr_process++;                   // 系统进程总数加一

    // 步骤6：唤醒新线程
    wakeup_proc(proc);  // 将 proc->state 从 PROC_UNINIT 改为 PROC_RUNNABLE

    // 步骤7：设置返回值为新线程的 PID
    ret = proc->pid;
    
fork_out:
    return ret;  // 返回结果（成功：PID；失败：错误码）

// 错误处理标签：释放已分配的内核栈
bad_fork_cleanup_kstack:
    put_kstack(proc);  // 调用 put_kstack 释放 setup_kstack 分配的物理页
// 错误处理标签：释放已分配的 PCB
bad_fork_cleanup_proc:
    kfree(proc);       // 调用 kfree 释放 alloc_proc 分配的 PCB 内存
    goto fork_out;     // 跳转到返回逻辑
}
```

其中，copy_thread 是 do_fork 的核心子函数，负责为新线程初始化 “执行环境”。

```c
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    // 1. 在新线程内核栈顶分配 trapframe 空间
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    // 2. 复制父进程的 trapframe
    *(proc->tf) = *tf;
    // 3. 设置 a0=0（实现fork：子进程返回 0，父进程返回子进程 PID）
    proc->tf->gpr.a0 = 0;
    // 4. 初始化新线程的栈指针
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;
    // 5. 初始化新线程的 context：设置启动入口与栈指针
    proc->context.ra = (uintptr_t)forkret;  // 新线程首次调度时执行 forkret
    proc->context.sp = (uintptr_t)(proc->tf);  // context.sp 指向 trapframe
}
```

### 3.唯一PID

**ucore 能确保每个新 fork 的线程获得唯一的 ID（即 pid）。**

（1）get_pid函数

ucore 通过 proc.c 中的 get_pid 函数为新 fork 的线程分配 PID，该函数是保证 PID 唯一性的核心。

- PID范围

```c
static_assert(MAX_PID > MAX_PROCESS);  // 确保 PID 总数 > 系统最大进程数
static int next_safe = MAX_PID, last_pid = MAX_PID;  
```

static_assert 强制 MAX_PID > MAX_PROCESS（MAX_PROCESS 是系统支持的最大进程数），确保即使所有进程同时存在，也有足够的 PID 可用，从根本上避免 “PID 耗尽” 导致的重复。

- PID 自增与循环复用

```c
if (++last_pid >= MAX_PID)  // 每次调用先自增 last_pid
{
    last_pid = 1;           // 超过 MAX_PID 则重置为 1，循环复用
    goto inside;
}
if (last_pid >= next_safe)  // 若当前 last_pid 超过“安全边界”，重新扫描
{
inside:
    next_safe = MAX_PID;    // 重置安全边界，准备重新检测冲突
```

last_pid 是静态变量，每次分配 PID 前先自增，确保按 1→2→…→MAX_PID→1 的顺序循环分配，避免遗漏可用的 PID。

- 冲突检测

```c
repeat:
    le = list;
    while ((le = list_next(le)) != list)  // 遍历全局进程列表 proc_list
    {
        proc = le2proc(le, list_link);    // 从链表节点获取进程 PCB
        if (proc->pid == last_pid)        // 检测当前 last_pid 是否已被占用
        {
            // 若占用：自增 last_pid，若超限则重置，重新检测
            if (++last_pid >= next_safe)
            {
                if (last_pid >= MAX_PID) last_pid = 1;
                next_safe = MAX_PID;
                goto repeat;  // 重新进入循环，检测新的 last_pid
            }
        }
        // 更新 next_safe 为“当前 last_pid 之后的第一个已占用 PID”
        else if (proc->pid > last_pid && next_safe > proc->pid)
        {
            next_safe = proc->pid;
        }
    }
```

通过 list_next 遍历 proc_list（存储所有已创建的进程），逐一对比每个进程的 pid 与当前 last_pid，若发现重复则立即自增 last_pid 并重新检测。

（2）do_fork函数

get_pid 分配 PID 后，do_fork 会进一步验证PID 的唯一性。

```c
proc->pid = get_pid();  // 将唯一 PID 写入新进程的 PCB
hash_proc(proc);        // 将 PCB 加入 hash_list（按 PID 哈希映射）
```

hash_list 是基于 PID 的哈希表，每个 PID 对应唯一的哈希桶，后续通过 find_proc(pid) 查找进程时，会直接定位到对应哈希桶，若该 PID 已存在则返回对应进程，否则返回 NULL，进一步验证了 PID 的唯一性。

## 练习3：编写 proc_run 函数

### 1. 代码实现

我们编写的proc_run函数如下：

```C
void proc_run(struct proc_struct *proc)
{
    if (proc != current)
    {
        bool intr_flag;          // 用于保存中断状态
        struct proc_struct *prev = current;  // 保存当前进程

        // 1. 禁用中断
        local_intr_save(intr_flag);

        // 2. 切换当前进程标记
        current = proc;

        // 3. 切换页表
        lsatp(proc->pgdir);

        // 4. 上下文切换
        switch_to(&(prev->context), &(proc->context));

        // 5. 恢复中断
        local_intr_restore(intr_flag);
    }
}
```

首先判断目标进程`proc`是否与当前正在运行的进程`current`一致，不一致则直接退出。

定义 `intr_flag` 保存切换前的中断状态，`prev` 记录待切换出的旧进程；调用 `local_intr_save(intr_flag)`，保存中断状态并关闭中断，确保切换过程不被打断；将全局变量 `current` 指向 `proc`，让系统识别目标进程为当前运行进程；调用 `lsatp(proc->pgdir)`，修改 `SATP` 页表控制寄存器，使 CPU 用目标进程的页表，实现进程内存隔离；调用 `switch_to(&prev->context, &proc->context)`，保存旧进程寄存器状态并恢复新进程状态，让新进程从上次暂停处继续执行。

完成后，调用 `local_intr_restore(intr_flag)`，传入之前保存的中断状态`intr_flag`，按切换前状态恢复中断。

### 2.测试结果

输入make qemu，结果如下所示。

![image-20251105202144041](C:\Users\lenovo\AppData\Roaming\Typora\typora-user-images\image-20251105202144041.png)

### 3.问题

本实验中创建并运行了**2个内核线程**，分别是`idleproc`和`initproc`。

`idleproc`是 ucore 启动的第一个内核线程，会循环检查自身`need_resched`标记触发调度，完成内核子系统初始化后会调度其他进程运行；`initproc`是 ucore 的第二个内核线程，通过执行`init_main`函数完成实验相关的初始化逻辑。

## challenge1：如何实现开关中断

首先找到相关` local_intr_save(intr_flag);....local_intr_restore(intr_flag);`的代码文件，以`kern\driver\console.c`为例，先放出一个函数

```
/* cons_putc - print a single character @c to console devices */
void cons_putc(int c) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        sbi_console_putchar((unsigned char)c);
    }
    local_intr_restore(intr_flag);
}
```





找到kern\sync\sync.h，这个文件含有这两句的定义

```
#ifndef __KERN_SYNC_SYNC_H__
#define __KERN_SYNC_SYNC_H__

#include <defs.h>
#include <intr.h>
#include <riscv.h>

static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);

#endif /* !__KERN_SYNC_SYNC_H__ */

```

### **第一步: local_intr_save(intr_flag); - 保存当前中断状态并关闭中断**

`local_intr_save`是一个宏，其定义如下：

```
#define local_intr_save(x) \
do {                       \
    x = _intr_save();      \
} while (0)
```

这个宏的作用是调用`_intr_save()`函数，并将其返回值存储在传入的参数`intr_flag`中。

`_intr_save()`函数的实现是关键：

```
static inline bool _intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}
```

1. **read_csr(sstatus)**： 这条指令会读取sstatus（Supervisor Status Register，监控模式状态寄存器）的值。sstatus是RISC-V架构中的一个核心控制寄存器，其中包含了许多关于当前处理器状态的信息。

2. **& SSTATUS_SIE**：SIE（Supervisor Interrupt Enable，监控模式中断使能）是sstatus寄存器中的一个特定位。如果SIE位为 **1**，表示当前处理核的监控模式中断是**开启**的，可以响应中断。如果SIE位为 **0**，表示中断是**关闭**的，处理核会忽略大部分中断。

3. **判断与操作**：

   **如果中断是开启的 (SIE位为1)**：代码会首先调用intr_disable()函数。这个函数会修改sstatus寄存器，将SIE位清零，从而**关闭中断**。随后，函数返回1。

   **如果中断已经是关闭的 (SIE位为0)**：函数什么也不做，直接返回0。

4. **保存状态**：`_intr_save()`的返回值（1或0）被赋给了`intr_flag`变量。这样，`intr_flag`就记录了**在执行`local_intr_save`之前，中断是开着（1）还是关着（0）的**。

* **local_intr_save** 完成了两件事：

  ① 确保代码往下执行时，中断一定是关闭的；

  ② 将原始的中断状态保存到intr_flag变量中。

### **第二步:  执行需要保护的关键代码**

在`local_intr_save`和`local_intr_restore`之间的代码块，就是需要被保护、不希望被中断打断的关键操作。在`console.c`的`cons_putc`函数中，这个关键操作就是`sbi_console_putchar((unsigned char)c);`。由于此时中断已经被关闭，这条语句的执行不会被其他中断事件抢占，保证了字符可以完整地输出。

### **第三步: local_intr_restore(intr_flag); - 恢复之前的中断状态**

`local_intr_restore`也是一个宏：

```
#define local_intr_restore(x) _intr_restore(x);
```

它调用了`_intr_restore()`函数，并将之前保存的`intr_flag`作为参数传入。

`_intr_restore()`函数的实现如下：

```
static inline void _intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}
```

1. **判断flag**：函数会检查传入的`intr_flag`的值。

2. **恢复操作**：

   **如果`intr_flag`为1**：这说明在调用`local_intr_save`之前，中断本来是**开启**的。因此，这里需要调用`intr_enable()`函数。该函数会重新将sstatus寄存器的SIE位置为1，**重新开启中断**。

   **如果`intr_flag`为0**：这说明在进入这段代码前，中断本来就是**关闭**的。那么为了维持原状，就什么也不做，让中断继续保持关闭状态。

-   **local_intr_restore**: 它根据`intr_flag`中保存的状态，**精确地恢复**进入临界区之前的中断状态，而不是简单地一律打开中断。

### **总结**

`local_intr_save`和`local_intr_restore`通过一个巧妙的组合，实现了“保存现场、关中断、执行任务、恢复现场”的逻辑。

- `local_intr_save`在进入重要区域前，先记下“门是开是关”（保存SIE状态到intr_flag），然后把“门”关上（关闭中断）。
- `local_intr_restore`在离开时，拿出之前的小本子（检查intr_flag），按照上面的记录把“门”恢复到原来的状态。

## challenge2：深入理解不同分页模式的工作原理

先定位到`get_pte`函数中的两段相似代码

```
pte_t *get_pte(pde_t *pgdir,// 指向顶级页表的虚拟地址 uintptr_t la,//要处理的虚拟地址  bool create)
//create如果为 true，在遍历过程中如果发现下一级页表不存在，函数就会分配一个新的物理页来充当它；如果为 false，遇到不存在的页表就直接返回失败
{
    // ==================== 代码段 1 ====================
    pde_t *pdep1 = &pgdir[PDX1(la)]; // 1.1 获取L2页表项(PDE)的地址
    if (!(*pdep1 & PTE_V)) // 1.2 如果这个L2条目无效即没有指向任何下一级的页表
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL) // 1.3 如果无效且不创建，则返回
        {
            return NULL;
        }
        // 1.4 分配一个物理页作为下一级（L1）页表
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGsize); // 1.5 清零新页表
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V); // 1.6 更新L2条目，指向新的L1页表
    }

    // ==================== 代码段 2 ====================
    pde_t *pdep0 = &((pte_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)]; // 2.1 获取L1页表项的地址
    if (!(*pdep0 & PTE_V)) // 2.2 检查其有效位
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL) // 2.3 如果无效且不创建，则返回
        {
            return NULL;
        }
        // 2.4 分配一个物理页作为下一级（L0）页表
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGsize); // 2.5 清零新页表
        *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V); // 2.6 更新L1 PDE，指向新的L0页表
    }

    // ==================== 返回最终PTE ====================
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}
```

### 首先了解 RISC-V分页模式 (sv32, sv39, sv48)

RISC-V架构定义了多种分页模式，主要区别在于虚拟地址的长度和页表的级数：

- **Sv32**: 针对32位系统，使用**二级页表**。虚拟地址被拆分为两级页表索引和一个页内偏移。
- **Sv39**: 针对64位系统，使用**三级页表**。39位的虚拟地址被拆分为三级页表索引（各9位）和一个12位的页内偏移。
- **Sv48**: 同样针对64位系统，使用**四级页表**。48位的虚拟地址被拆分为四级页表索引（各9位）和一个12位的页内偏移。

**核心共同点：** 无论页表是几级，其**遍历逻辑都是相同的**。每一级页表（除了最后一级）中的一个条目如果有效，它就指向下一级页表的物理基地址。最后一级页表中的条目则指向最终数据物理页的物理基地址。

### 代码对应Sv39模式

代码中的宏 `PDX1(la)`、`PDX0(la)` 和 `PTX(la)` 暗示了它正在实现一个**三级页表**，这正好对应RISC-V的 **Sv39** 模式。

- `pgdir` 是顶级页表（L2）的基地址。

- `PDX1(la) `提取线性地址 la 中用于索引 L2 页表的索引值。前9位

  ```
  #define PDX1(la) (((la) >> 30) & 0x1FF)
  ```

  

- `PDX0(la)` 提取用于索引 L1 页表的索引值。中9位

- `PTX(la)` 提取用于索引 L0 页表（最底层页表）的索引值。后9位，偏移为剩下12位

### （1）这两段代码为什么如此相像？

**是因为它们在重复执行多级页表遍历的核心逻辑。**

多级页表本质上是一个前缀树（假设内核空间的所有虚拟地址都有相同的高位。那么在地址翻译时，所有内核地址都会共享顶级页表和中间页表中相同的路径，直到它们的地址出现分歧为止）结构。从根节点（顶级页表）开始，向下遍历到叶子节点的每一步所做的事情都是一样的：

1. **计算索引**：根据当前处理的虚拟地址部分，计算出在当前级别页表中的索引。
2. **查找条目**：通过索引找到对应的页表条目。
3. **检查有效性**：检查该条目的有效位。
4. **按需创建**：如果条目**无效**，意味着下一级的页表还不存在。如果create标志为真，就需要分配一个新的物理页来作为下一级页表，并将当前条目更新为指向这个新分配的页。如果条目**有效**，说明下一级页表已经存在。
5. **进入下一级**：使用当前条目中存储的物理地址，找到下一级页表的基地址，然后重复上述过程。

**即：**

- **代码段1** 执行的是从 **L2页表 -> L1页表** 的这一步遍历。
- **代码段2** 执行的是从 **L1页表 -> L0页表** 的这一步遍历。

因为它们执行的是完全相同的逻辑，只是作用在页表树的不同层级上，所以代码结构自然就几乎一模一样了。这种重复性也暗示了如果需要实现Sv48（四级页表），只需再增加一段几乎完全相同的代码来处理从L3到L2的遍历即可。

### （2）目前get_pte()函数将页表项的查找和页表项的分配合并在一个函数里，你认为这种写法好吗？有没有必要把两个功能拆开？

#### 优点 

1. **方便、代码简洁**：对于调用者来说最方便。当需要确保一个线性地址的映射存在时，只需调用 `get_pte(pgdir, la, 1) `即可。一行代码同时完成了检查和创建。
2. **效率**：一次页表遍历就可以完成两个任务。如果将功能拆分，可能会导致重复的页表遍历。例如，先调用`find_pte`失败，再调用`create_pte_path`，这两者都会从头开始遍历页表，造成浪费。

#### 缺点 

1. **违反单一职责原则** (即一个函数应该只做一件事) ：`get_pte `做了两件事：查找 和 创建 ，这会使函数的意图不够清晰，增加代码理解的复杂度。

2. **不够灵活的错误处理**：该函数返回 NULL 有两种可能的原因，而调用者无法区分这两种情况。

   - create 为 false 且页表项不存在

   - create 为 true 但 alloc_page() 分配内存失败。

#### 结论

ucore是一个小型内核，当前不拆分写法没有问题，如果对于一个生产级os来说最好拆分。
