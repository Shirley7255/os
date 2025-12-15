# lab5

2312668 李佳   2312674 朱晨瑞   2311061 马淏怡

## 练习0 填写已有实验

有关lab5新增代码部分如下:

**进程控制块（`proc_struct`）的初始化**

```c
proc->wait_state = 0;
proc->cptr = proc->yptr = proc->optr = NULL;
```

- `proc->wait_state = 0`：初始化进程的**等待状态**。`wait_state`用于标记进程的等待原因，进程刚创建时未进入任何等待状态，故初始化为 0。

- `proc->cptr = proc->yptr = proc->optr = NULL`：初始化进程的家族关系指针：

  - `cptr`：父进程的 “第一个子进程” 指针；
  - `optr`：当前进程的 “兄长进程” 指针；
  - `yptr`：当前进程的 “弟弟进程” 指针；

  进程刚创建时无父子、兄弟关系，故三者均初始化为NULL。

**进程创建时的关系与状态维护（`do_fork`）**

```c
proc->parent = current;
if (current)
    current->wait_state = 0;
```

- `proc->parent = current`：将新创建的子进程的父进程设为当前进程（`current`），建立**父子进程关联**。
- `current->wait_state = 0`：清空父进程的等待状态 —— 若父进程此前处于等待子进程的状态，新子进程创建后需重置该状态，避免父进程误唤醒。

```c
proc->pid = get_pid();
hash_proc(proc);
set_links(proc);
```

- `set_links(proc)`：将新进程插入进程全局链表（`proc_list`）与父进程的子进程链表：

  - 把新进程加入全局`proc_list`，维护系统进程总数`nr_process`；

  - 将新进程插入父进程的子链表头部（父进程的`cptr`指向新进程）；

  - 维护兄弟进程的`optr/yptr`指针（新进程的`optr`指向父进程原第一个子进程，原第一个子进程的`yptr`指向新进程）。

**时钟中断的调度触发增强（`trap.c`）**

```c
clock_set_next_event();
ticks++;
if (ticks % TICK_NUM == 0)
{
    print_ticks();
    if (current != NULL && current->state == PROC_RUNNABLE)
    {
        current->need_resched = 1;
    }
}
```

该代码添加在**时钟中断（`IRQ_S_TIMER`）的处理逻辑**中：

- **`clock_set_next_event();`**设置下一次时钟中断的触发时间。时钟中断是周期性事件，通过该函数确保中断能够持续触发，维持系统时间计数和调度检查的周期性。

- **`ticks++;`**递增系统全局滴答计数器（`ticks`）。`ticks`用于记录从系统启动到当前的时钟中断总次数，是系统时间管理的基础（如时间片计算、进程运行时长统计等）。

- **`if (ticks % TICK_NUM == 0)`**当`ticks`为`TICK_NUM`的整数倍时，进入调度检查逻辑。

- **`print_ticks();`**打印当前`ticks`值，用于调试和观察系统时间流逝状态。

- **调度标记**

  - 若当前进程（`current`）存在且处于可运行状态（`PROC_RUNNABLE`），则将其`need_resched`标记设为 1，表示该进程需要被调度切换。

  - 仅对可运行状态的进程设置标记，因为只有此类进程才参与调度竞争，确保标记的有效性。



## 练习1 加载应用程序并执行

### 1.设计思路

**核心原理**

RISC-V 架构中，内核态到用户态的切换依赖**陷阱帧（`trapframe`）** 和`sret`指令：

- `trapframe`保存用户态执行所需的寄存器状态；
- `sret`指令从`epc`寄存器读取程序入口地址，从`status`寄存器读取特权级信息，完成模式切换并跳转执行。

 **核心成员**

| 成员         | 值                                       | 设计原因                                                     |
| ------------ | ---------------------------------------- | ------------------------------------------------------------ |
| `tf->gpr.sp` | `USTACKTOP`                              | 用户栈采用 “向下生长” 模式，`USTACKTOP`是用户栈虚拟地址空间的顶部，符合栈布局规范 |
| `tf->epc`    | `elf->e_entry`                           | `elf->e_entry`是 ELF 文件头中定义的程序入口地址，即应用程序第一条指令的虚拟地址 |
| `tf->status` | `(sstatus & ~SSTATUS_SPP) \|SSTATUS_SPIE | 1. 清除`SSTATUS_SPP`（0=User 模式，1=Supervisor 模式），指定切换到用户态；2. 设置`SSTATUS_SPIE`，`sret`后启用中断；3. 保留原有`sstatus`其他位，保证状态一致性 |

### 2.代码实现

```c++
// (6) setup trapframe for user environment
struct trapframe *tf = current->tf;
// Keep sstatus
uintptr_t sstatus = tf->status;
memset(tf, 0, sizeof(struct trapframe));
/* 设置用户栈指针：指向用户栈顶USTACKTOP */
tf->gpr.sp = USTACKTOP;
/* 设置程序入口：ELF文件定义的应用起始地址 */
tf->epc = elf->e_entry;
/* 配置用户态权限：User模式 + 启用中断 */
tf->status = (sstatus & ~SSTATUS_SPP) | SSTATUS_SPIE;
```

**保留原有状态位**：先保存`tf->status`中的原有状态

**初始化陷阱帧**：`memset`清空陷阱帧，确保未显式配置的寄存器为默认值

**用户栈配置**：`USTACKTOP`是用户栈虚拟地址空间的顶部（`USTACKTOP = 0x80000000`，与内核地址空间隔离），设置`sp`为该值，保证用户程序能正常使用栈空间

**入口地址配置**：`elf->e_entry`由 ELF 解析得到，直接指向应用程序的第一条指令

**权限与中断配置**：

- `~SSTATUS_SPP`：清除特权级标记，切换到 User 模式；
- `SSTATUS_SPIE`：设置中断使能标记，`sret`后用户态可响应中断；

### 3.用户态进程从 RUNNING 到执行第一条指令的完整流程

**阶段 1：调度器选择并启动进程（内核态）**

- **调度器选择进程**：`scheduler`函数遍历就绪队列，选择目标用户进程（状态为`PROC_RUNNABLE`），调用`proc_run`函数启动该进程。

- `proc_run`核心操作：

  - 保存当前进程的中断状态（`local_intr_save`），避免切换过程中被中断；

  - 更新`current`指针为目标进程，切换进程上下文标记；

  - 加载目标进程的页表：将进程的页目录表物理地址写入`satp`寄存器，MMU 开始使用该进程的虚拟地址空间；

  - 调用`switch_to`函数，切换 CPU 的寄存器上下文到目标进程的`context`结构。

**阶段 2：上下文切换与内核态过渡（内核态）**

- `switch_to`上下文切换：

  - `switch_to`是上下文切换函数，负责保存当前进程的寄存器状态到context

  - 恢复目标进程`context`结构中保存的寄存器状态，其中`context.ra`指向`forkret`函数（进程启动的内核态入口）。

- 执行`forkret`函数：

  - 上下文切换后，CPU 的程序计数器（PC）跳转到`context.ra`指向的`forkret`；

  - `forkret`函数调用`forkrets(current->tf)`，将配置好的`trapframe`指针传入汇编函数`forkrets`。

**阶段 3：陷阱帧恢复与模式切换（内核态→用户态）**

- `forkrets`恢复陷阱帧：forkrets是汇编函数，负责将tf中的寄存器状态恢复到 CPU 硬件寄存器

  - 恢复通用寄存器组（`tf->gpr`），包括用户栈指针`sp = tf->gpr.sp`（即`USTACKTOP`）；

  - 恢复状态寄存器`status = tf->status`；

  - 恢复异常程序计数器`epc = tf->epc`。

- 执行`sret`指令：sret是 RISC-V 架构的模式切换指令，功能为：

  - 根据`status`寄存器的`SPP`位，将 CPU 从 Supervisor 模式（内核态）切换到 User 模式；

  - 将程序计数器（PC）设置为`epc`寄存器的值（即 ELF 入口地址）；

  - 启用中断（根据`status`寄存器的`SPIE`位）。

**阶段 4：执行应用程序第一条指令（用户态）**

- 用户态指令执行：

  - CPU 切换到 User 模式后，从`epc`指向的地址（ELF 入口）取第一条指令；

  - 执行该指令，用户态进程正式启动，后续执行流程完全由应用程序控制。



## 练习2 父进程复制自己的内存空间给子进程

### 1.设计思路

**内存复制的调用链路**

```plaintext
do_fork（创建子进程）→ copy_mm（复制/共享内存管理结构）→ dup_mmap（复制虚拟内存区域）→ copy_range（复制物理内存内容）
```

- `do_fork`：触发子进程创建，调用`copy_mm`决定内存是复制（进程）还是共享（线程）；
- `copy_mm`：若为进程创建（无`CLONE_VM`标志），调用`dup_mmap`复制父进程的虚拟内存区域（VMA）；
- `dup_mmap`：遍历父进程的 VMA，调用`copy_range`按页复制物理内存内容；
- `copy_range`： 将父进程虚拟地址区间`[start, end)`对应的物理内存，复制到子进程的相同虚拟地址区间。

**核心原则**

- **按页复制**：内存管理的最小粒度是页（`PGSIZE=4KB`），需遍历父进程的每个虚拟页，逐一复制到子进程；
- **物理隔离**：子进程不能复用父进程的物理页，必须为子进程分配新的物理页，确保内存修改互不影响；
- **权限继承**：子进程虚拟页的访问权限需与父进程完全一致；
- **异常处理**：若复制过程中内存分配失败，需释放已分配的资源，避免内存泄漏。

**流程设计**

`copy_range`的实现需围绕 “**找页→alloc 页→复制内容→建立映射**” 四个核心步骤展开，具体流程如下：

- **遍历父进程虚拟页**：按页粒度遍历待复制的虚拟地址区间`[start, end)`；

- **查找父进程页表项（PTE）**：通过`get_pte`函数找到父进程当前虚拟页对应的页表项（PTE），确认该页是否有效（`PTE_V`标志）；

- **为子进程分配物理页**：调用`alloc_page`为子进程分配新的物理页，用于存储复制后的内容；

- **复制页内容**：将父进程物理页的内容，通过内核虚拟地址映射（`page2kva`）复制到子进程的新物理页；

- **建立子进程地址映射**：调用`page_insert`函数，建立子进程虚拟地址到新物理页的映射，并继承父进程的访问权限；

- **异常处理**：若页分配或映射建立失败，释放已分配的物理页，返回错误。

### 2.代码实现

```c
{
    // 1. 将父进程物理页转换为内核虚拟地址（src），子进程新物理页转换为内核虚拟地址（dst）
    void *src_kvaddr = page2kva(page);  // 父进程页的内核虚拟地址
    void *dst_kvaddr = page2kva(npage); // 子进程新页的内核虚拟地址
    
    // 2. 复制父进程页内容到子进程新页
    memcpy(dst_kvaddr, src_kvaddr, PGSIZE);
    
    // 3. 建立子进程虚拟地址start到新物理页npage的映射，继承父进程权限
    ret = page_insert(to, npage, start, perm);
    
    // 4. 异常处理：若映射建立失败，释放已分配的子进程物理页，返回错误
    if (ret != 0)
    {
        free_page(npage);
        return ret;
    }
}
```

#### （1）内核虚拟地址转换

- 物理页本身不能被内核直接访问，需通过`page2kva`函数将物理页的页帧号转换为内核虚拟地址
- `src_kvaddr`：父进程待复制页的内核虚拟地址，用于读取原内容；
- `dst_kvaddr`：子进程新分配页的内核虚拟地址，用于写入复制内容。

#### （2）整页内容复制

- 使用`memcpy`函数复制`PGSIZE`（4KB）大小的内容，确保父进程页的所有数据完全复制到子进程；

#### （3）建立子进程地址映射

- `page_insert`函数的作用：

  - 找到子进程虚拟地址`start`对应的页表项（PTE），若不存在则创建页表（PT）；

  - 将新物理页`npage`的页帧号写入 PTE，设置`PTE_V`标志和继承的权限；

  - 刷新 TLB，避免旧的地址映射缓存影响子进程执行。

#### （4）异常处理

- 若`page_insert`失败，需调用`free_page`释放已分配的子进程物理页，避免内存泄漏；
- 直接返回错误，终止复制流程，确保进程创建过程能正确处理内存不足的情况。

### 3.make grade测试

测试结果如下：

![image-20251207152510944](C:\Users\lenovo\AppData\Roaming\Typora\typora-user-images\image-20251207152510944.png)



## 练习3：阅读分析源代码，理解进程执行fork/exec/wait/exit 的实现，以及系统调用的实现

### 1. fork/exec/wait/exit 执行流程分析

这四个函数构成了进程管理的核心，它们的执行涉及用户态与内核态的频繁切换。

#### **(1) 总体执行流程**

所有操作都遵循一个通用的系统调用路径：

- **用户态**: 用户程序调用函数（如 `fork()`）。

- **系统调用封装**: 库函数调用syscall.c中的 `syscall()`，最终执行 `ecall` 指令。

- **状态切换**: `ecall` 触发同步异常，CPU 从用户态切换到内核态，跳转到 `stvec` 指向的 `__alltraps`。

- 内核态:

  - `trap()` -> `trap_dispatch()` -> `exception_handler()`。

  - 识别出 `CAUSE_USER_ECALL`，调用 `syscall()`。

  - 根据系统调用号分发到具体的内核函数（如 `do_fork`）。

- **执行功能**: 内核函数执行具体逻辑。

- **返回**: 执行完毕后，结果保存在 trapframe 的 `a0` 寄存器中，通过 `sret` 指令恢复上下文，返回用户态。

#### **(2) 各函数详细分析**

- **fork (创建进程)**
  - 内核实现 (`do_fork`):
    1. **分配资源**: 调用 `alloc_proc` 分配一个新的 `proc_struct`。
    2. **分配内核栈**: 调用 `setup_kstack` 为子进程分配内核栈。
    3. **复制内存**: 调用 `copy_mm`。对于 `fork`，它会调用 `dup_mmap` 复制父进程的内存映射，实现地址空间的隔离。
    4. **复制上下文 (`copy_thread`)**: 设置子进程的trapframe。
       - **返回值**: 将子进程 `tf->gpr.a0` 设为 0（子进程 `fork` 返回0的原因）。
       - **栈指针**: `tf->gpr.sp` 设为父进程的栈指针。
       - **返回地址**: `proc->context.ra` 设为 `forkret`，确保子进程被调度时从 `forkret` 开始执行。
    5. **唤醒**: 将子进程状态设为 `PROC_RUNNABLE`，加入运行队列。
  - **返回结果**: 父进程的 `do_fork` 返回子进程 PID；子进程被调度运行后，从 `forkret` -> `forkrets` -> `sret` 返回用户态，返回值为 0。
- **exec (加载新程序)**
  - **内核实现** (`do_execve`):
    1. **检查**: 检查用户内存空间合法性。
    2. **清理旧资源**: 如果当前进程有内存空间，调用 `exit_mmap` 释放旧的页表和内存映射，`put_pgdir` 释放页目录。
    3. **加载新程序 (`load_icode`)**:
       - 创建新的 `mm_struct` 和页目录。
       - 解析ELF格式的二进制文件。
       - 根据ELF Header 将代码段、数据段、BSS 段映射到进程内存空间。
       - 分配并映射用户栈。
    4. **重置上下文**: 修改当前进程的trapframe。
       - `tf->epc`: 设为 ELF 文件的入口地址（`e_entry`）。
       - `tf->gpr.sp`: 设为新的用户栈顶 (`USTACKTOP`)。
  - **返回结果**: `exec` 不会“返回”到原程序。系统调用结束后，`sret` 指令会利用修改后的 `epc` 和 `sp` 跳转到新程序的入口点开始执行。
- **wait (等待子进程)**
  - 内核实现 (`do_wait`):
    1. **查找**: 查找是否存在指定 PID 的子进程。
    2. **循环等待**:
       - 如果子进程状态不为PROC_ZOMBIE：
         - 将当前进程状态设为 `PROC_SLEEPING`。
         - 设置等待原因 `current->wait_state = WT_CHILD`。
         - 调用 `schedule()` 让出 CPU，当前进程进入睡眠。
       - 如果子进程状态为PROC_ZOMBIE：
         - 取出子进程的退出码。
         - 调用 `kfree` 彻底释放子进程的 PCB 和内核栈。
         - 返回 0。
  - **交互**: 这是一个阻塞操作。父进程会挂起，直到被子进程的 `exit` 唤醒。
- **exit (进程退出)**
  - **内核实现 (`do_exit`)**:
    1. **释放资源**: 释放进程的虚拟内存空间 (`exit_mmap`, `put_pgdir`, `mm_destroy`)。
    2. **设置状态**: 将进程状态设为 `PROC_ZOMBIE`，设置退出码。
    3. **父子交互**:
       - 如果父进程在等待 (`WT_CHILD`)，调用 `wakeup_proc(parent)` 唤醒父进程。
       - 将当前进程的所有子进程过继给 `initproc`（1号进程）。
    4. **调度**: 调用 `schedule()` 切换到其他进程。
  - **返回结果**: 该函数不再返回，进程执行结束。

#### **(3) 内核态与用户态程序是如何交错执行的？**

- **交错机制**: 程序的执行流不是连续的，而是被中断和系统调用打断的。
- **用户 -> 内核**: 当用户程序执行 `fork` 等系统调用，或者发生时钟中断（时间片用完）时，CPU 硬件自动保存当前 PC 到 `epc`，跳转到内核中断处理程序。
- **内核 -> 用户**: 内核处理完系统调用或完成进程调度后，执行 `sret` 指令。CPU 将 `epc` 的值恢复到 PC，恢复用户栈指针，继续执行用户代码。
- **调度**: 在内核态中，`schedule()` 函数会选择下一个 `PROC_RUNNABLE` 的进程，通过 `switch_to` 切换 CPU 上下文，从而实现不同进程的交错执行。

#### **(4) 内核态执行结果是如何返回给用户程序的？**

- **机制**: 通过 **trapframe (中断帧)** 中的寄存器。
- **具体实现**: 在系统调用处理函数中，函数的返回值会被写入当前进程 `trapframe` 的 `a0` 寄存器。
- **恢复**: 当执行 `sret` 返回用户态时，会从 `trapframe` 恢复通用寄存器，此时用户态程序看到的 `a0` 寄存器就是函数的返回值。

### 2.用户态进程执行状态生命周期图

       (alloc_proc)
            |
            V
      +-------------+
      | PROC_UNINIT |  (进程结构体已分配，但未初始化完成)
      +-------------+
            |
            | proc_init / wakeup_proc (完成初始化，加入调度队列)
            V
      +---------------+    schedule() (调度器选中)    +-------------+
      | PROC_RUNNABLE | ---------------------------> |   RUNNING   |
      | (就绪/可运行)  | <--------------------------- | (正在运行)   |
      +---------------+    schedule() (时间片耗尽)    +-------------+
            ^                                            |
            |                                            | do_wait / do_sleep
            | wakeup_proc (被唤醒)                        | (等待子进程或事件)
            |                                            V
      +---------------+                            +---------------+
      | PROC_SLEEPING | <------------------------- | PROC_SLEEPING |
      | (睡眠/阻塞)    |                            | (睡眠/阻塞)    |
      +---------------+                            +---------------+
                                                         |
                                                         | do_exit (进程退出)
                                                         V
                                                   +-------------+
                                                   | PROC_ZOMBIE |
                                                   | (僵尸状态)   |
                                                   +-------------+
                                                         |
                                                         | do_wait (父进程回收)
                                                         V
                                                   +-------------+
                                                   |  (NULL)     |
                                                   | (彻底销毁)   |
                                                   +-------------+



## 扩展练习challenge1

### 1.设计报告

#### 1.1 核心原理

Copy on Write 是一种推迟甚至避免内存复制的技术。

- **传统 fork**: 父进程创建子进程时，会完全复制父进程的物理内存页面给子进程。这既浪费内存，又耗费时间，尤其是当子进程随后立即执行 `exec` 加载新程序时，之前的复制工作大部分是无用的。
- **COW fork**: 父子进程共享同一物理页面，将这些页面在页表中标记为“只读”。当任一进程尝试写入时，CPU 触发缺页异常 (Page Fault)。内核捕获该异常，检测到是 COW 页面，才真正分配新的物理页，复制数据，并更新页表将权限改为“可写”。

#### 1.2 状态转换

我们将物理页面的状态看作一个有限状态机，涉及三种状态：

- **状态 A: 独占可写**
  - **描述**: 页面仅被一个进程引用，且具有写权限。
  - **标志**: `PTE_W`=1, `PTE_COW`=0, `page_ref`=1。
  - **转换 -> B**: 当进程执行 `fork()` 时，该页面被父子进程共享。
- **状态 B: 共享只读**
  - **描述**: 页面被多个进程引用，所有进程对其只有读权限。
  - **标志**: `PTE_W`=0, `PTE_COW`=1, page_ref> 1。
  - **转换 -> A (写操作)**: 当某进程尝试写入时，触发缺页异常。
    - **情况 1 (ref > 1)**: 分配新页，复制内容。当前进程指向新页（新页进入状态 A），原页引用计数减 1（仍处于状态 B）。
    - **情况 2 (ref == 1)**: 优化路径。如果其他进程已退出或不再映射该页，当前进程是唯一拥有者。直接恢复 `PTE_W`，清除 `PTE_COW`，转为状态 A。
- **状态 C: 真正只读**
  - **描述**: 代码段或只读数据段，本来就不允许修改。
  - **标志**: `PTE_W`=0, `PTE_COW`=0。
  - **转换**: `fork()` 时仅增加引用计数，始终保持只读。

#### 1.3 Dirty COW 漏洞分析与修复

- **漏洞原理**: Dirty COW (CVE-2016-5195) 是 Linux 内核中的一个竞态条件漏洞。攻击者利用两个线程：一个线程尝试写入 COW 页面（触发缺页中断），另一个线程同时调用 `madvise(MADV_DONTNEED)` 丢弃该页面映射。在内核处理缺页异常时，如果页表项在“决定复制页面”和“更新页表项指向新页面”之间被清除，后续的重试逻辑可能会错误地允许写入原始的只读页面。
- **ucore 风险**: 虽然 ucore 是单核且不可抢占的，但如果扩展到多核环境，`do_pgfault` 中存在 PTE 操作的竞态风险。
- **解决方案**: 在 `do_pgfault` 中引入 lock_mm(mm)，确保从读取 PTE 到更新 PTE 的整个过程是原子的，防止其他线程在此期间修改页表结构。

### 2.具体代码实现

#### 2.1 定义COW标志位

利用 RISC-V 页表项 (PTE) 中的保留位 (Bit 8) 作为软件定义的 COW 标志。

```c
// kern/mm/mmu.h
#define PTE_D 0x080    // Dirty
#define PTE_COW 0x100  // Copy On Write 
#define PTE_SOFT 0x300 // Reserved for Software
```

#### 2.2 修改内存复制逻辑 (fork 阶段)

修改copy_range函数。在 `fork` 时，不再申请新页并复制内容，而是将父进程的页面映射到子进程，并同时将父子进程的页表项都设为只读且标记 COW。

```c
// kern/mm/pmm.c - copy_range 函数
int copy_range(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end, bool share) {
    // 遍历页表循环
    if (*ptep & PTE_V) {
        // 获取 ptep
        uint32_t perm = (*ptep & PTE_USER);
        struct Page *page = pte2page(*ptep);
        int ret = 0;

        // [COW 核心逻辑]
        // 如果页面原本是可写的(PTE_W)，或者已经是 COW 页面(PTE_COW)
        if ((perm & PTE_W) || (perm & PTE_COW)) {
            // 1. 清除写权限 (PTE_W)
            // 2. 设置 COW 标志 (PTE_COW)
            perm = (perm & ~PTE_W) | PTE_COW;
 
            // 更新父进程的页表项 (使其只读)
            if ((ret = page_insert(from, page, start, perm)) != 0) return ret;
            
            // 映射到子进程的页表项 (共享物理页)
            if ((ret = page_insert(to, page, start, perm)) != 0) return ret;
        } else {
            // 对于原本就是只读的页面(如代码段)，直接共享，不设 COW
            if ((ret = page_insert(to, page, start, perm)) != 0) return ret;
        }
        // ...
    }
    return 0;
}
```

#### 2.3 实现缺页处理逻辑 (写操作阶段)

实现 `do_pgfault` 函数。当发生 `Store Page Fault` 时，检查是否为 COW 页面，如果是则执行“复制”操作。同时处理了 RISC-V 硬件需要的 Dirty/Accessed 位更新。

```c
// do_pgfault - 处理缺页异常的核心函数
// 参数:
//   mm: 进程的内存管理结构体
//   error_code: 异常原因 (如 CAUSE_STORE_PAGE_FAULT 表示写异常)
//   addr: 触发异常的虚拟地址
int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr)
{
    int ret = -E_INVAL;
    // 1. 查找包含该虚拟地址的 VMA (虚拟内存区域)
    struct vma_struct *vma = find_vma(mm, addr);

    // 如果找不到 VMA，或者地址不在 VMA 范围内，说明是非法访问
    if (vma == NULL || vma->vm_start > addr)
    {
        cprintf("not valid addr %x, and  can not find it in vma\n", addr);
        goto failed;
    }

    // 2. 权限检查
    // 如果是写操作触发的异常，但 VMA 标记为不可写，则报错
    if (error_code == CAUSE_STORE_PAGE_FAULT) {
        if (!(vma->vm_flags & VM_WRITE)) {
             cprintf("do_pgfault failed: error code %x, addr %x, vma flags %x\n", error_code, addr, vma->vm_flags);
             goto failed;
        }
    }

    // 3. 计算目标页面的权限标志
    // 如果可写，必须同时设置 PTE_R 和 PTE_W
    uint32_t perm = PTE_U; // 用户态权限
    if (vma->vm_flags & VM_WRITE) {
        perm |= (PTE_R | PTE_W);
    }
    if (vma->vm_flags & VM_READ) {
        perm |= PTE_R;
    }
    
    // 将地址向下对齐到页边界
    addr = ROUNDDOWN(addr, PGSIZE);

    ret = -E_NO_MEM;

    // [Dirty COW Fix] 加锁保护 PTE 操作
    // 引入锁机制，确保从读取 PTE 到更新 PTE 的过程是原子的
    // 防止多线程环境下发生竞态条件 (如 Dirty COW 漏洞)
    lock_mm(mm);

    // 4. 获取页表项 (PTE)
    // 如果页表不存在，get_pte 会自动分配页表页 (第三个参数为 1)
    pte_t *ptep = NULL;
    if ((ptep = get_pte(mm->pgdir, addr, 1)) == NULL)
    {
        cprintf("get_pte in do_pgfault failed\n");
        unlock_mm(mm); // 失败路径需解锁
        goto failed;
    }

    // 5. 根据 PTE 状态进行处理
    if (*ptep == 0)
    { 
        // 情况 A: 页面完全未映射 
        // 分配一个新的物理页，并建立映射
        if (pgdir_alloc_page(mm->pgdir, addr, perm) == NULL)
        {
            cprintf("pgdir_alloc_page in do_pgfault failed\n");
            unlock_mm(mm);
            goto failed;
        }
    }
    else
    {
        // 情况 B: 页面已映射，但触发了异常 
        
        // [COW 处理逻辑]
        // 如果 PTE 标记了 COW 且是写操作触发的异常
        if ((*ptep & PTE_COW) && (error_code == CAUSE_STORE_PAGE_FAULT)) {
            struct Page *page = pte2page(*ptep);
            
            // 优化路径：如果引用计数为 1，说明没有其他进程共享此页
            // 直接恢复写权限，清除 COW 标志，无需复制
            if (page_ref(page) == 1) {
                page_insert(mm->pgdir, page, addr, perm | PTE_W);
            } else {
                // 标准路径：有多个进程共享，必须复制页面
                struct Page *npage = alloc_page(); // 分配新页
                if (npage == NULL) {
                    unlock_mm(mm);
                    goto failed;
                }
                // 复制原页面内容到新页
                memcpy(page2kva(npage), page2kva(page), PGSIZE);
                
                // 更新页表：指向新页，赋予写权限 (PTE_W)，不再标记 COW
                // page_insert 会自动处理旧页面的引用计数递减
                if (page_insert(mm->pgdir, npage, addr, perm | PTE_W) != 0) {
                    free_page(npage);
                    unlock_mm(mm);
                    goto failed;
                }
            }
        } 
        // [Dirty Bit 处理]
        // 页面可写，但 PTE_D (Dirty) 位未置位，硬件触发异常
        // 操作系统需手动置位 PTE_D，表示页面已被修改
        else if ((*ptep & PTE_W) && (error_code == CAUSE_STORE_PAGE_FAULT)) {
            *ptep |= PTE_D | PTE_A;
            tlb_invalidate(mm->pgdir, addr); // 刷新 TLB
        } 
        // [Accessed Bit 处理]
        // 页面被读取或执行，但 PTE_A (Accessed) 位未置位
        // 操作系统需手动置位 PTE_A，用于页面置换算法 
        else if (error_code == CAUSE_LOAD_PAGE_FAULT || error_code == CAUSE_FETCH_PAGE_FAULT) {
            *ptep |= PTE_A;
            tlb_invalidate(mm->pgdir, addr); // 刷新 TLB
        } 
        else {
             // 其他无法处理的异常情况
             cprintf("do_pgfault: unhandled page fault. ptep: %x, error_code: %x\n", *ptep, error_code);
             unlock_mm(mm);
             goto failed;
        }
    }
    
    // 操作成功，解锁
    unlock_mm(mm);
    ret = 0;
failed:
    return ret;
}
```

### 3.模拟

#### 3.1 漏洞模拟

Dirty COW 的本质是**检查页表项**和**更新页表项**这两个操作之间存在一个**竞态窗口 **。在多核环境下，如果这两个操作不是原子的，攻击者可以在中间插入操作（如 `madvise` 清空页表），导致内核错误地写入了只读页面。

如果我们在 `do_pgfault` 中**不加锁**，代码逻辑如下（这就是模拟出的“错误”状态）：

```c
// 假设运行在多核环境，且没有 lock_mm(mm) 保护
int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr) {
    // ...
    
    // 1. 读取 PTE (Read)
    pte_t *ptep = get_pte(mm->pgdir, addr, 1);
    
    // 2. 检查状态 (Check)
    if ((*ptep & PTE_COW) && (error_code == CAUSE_STORE_PAGE_FAULT)) {
        
        // 3. 准备新页面 (Prepare)
        struct Page *npage = alloc_page();
        memcpy(page2kva(npage), page2kva(pte2page(*ptep)), PGSIZE);
        
        // <--- 竞态窗口 (Race Window) --->
        // 此时，另一个 CPU 上的线程可能执行了 madvise(MADV_DONTNEED)，
        // 导致 *ptep 被清零，或者指向了原始的只读文件映射。
        // 攻击者利用这个窗口，让 *ptep 的状态发生改变。
        
        // 4. 更新 PTE (Update)
        // 此时我们以为自己在更新 COW 页面，但实际上 *ptep 可能已经变了。
        // 在 ucore 中，这会导致两个线程互相覆盖 PTE，造成内存泄漏或数据损坏。
        page_insert(mm->pgdir, npage, addr, perm | PTE_W);
    }
    // ...
}
```

#### 3.2 解决方案

为了修复这个逻辑漏洞，我们必须保证 Read -> Check -> Prepare -> Update 这一整个序列是原子的，不可被同一进程的其他线程打断。我们在 vmm.c 中引入了 lock_mm(mm)。这把锁保护了进程的内存管理结构 (mm_struct)，确保同一时间只有一个线程能修改该进程的页表。

```c
    // [Dirty COW Fix] 加锁保护 PTE 操作
    // 这一行代码模拟了“关闭竞态窗口”的修复方案
    lock_mm(mm); 

    pte_t *ptep = NULL;
    // 1. 读取 PTE (在锁保护下)
    if ((ptep = get_pte(mm->pgdir, addr, 1)) == NULL) {
        unlock_mm(mm); // 失败路径需解锁
        goto failed;
    }

    if (*ptep == 0) { 
        // ...
    } else {
        // 2. 检查状态 (在锁保护下)
        if ((*ptep & PTE_COW) && (error_code == CAUSE_STORE_PAGE_FAULT)) {
            struct Page *page = pte2page(*ptep);
            
            // 3. 准备与更新 (在锁保护下)
            if (page_ref(page) == 1) {
                page_insert(mm->pgdir, page, addr, perm | PTE_W);
            } else {
                struct Page *npage = alloc_page();
                if (npage == NULL) {
                    unlock_mm(mm);
                    goto failed;
                }
                memcpy(page2kva(npage), page2kva(page), PGSIZE);
                
                // 4. 最终更新 (在锁保护下)
                // 此时可以确定 *ptep 的状态没有被其他线程改变
                if (page_insert(mm->pgdir, npage, addr, perm | PTE_W) != 0) {
                    free_page(npage);
                    unlock_mm(mm);
                    goto failed;
                }
            }
        } 
        //其他分支
    }
    
    // 操作完成，释放锁
    unlock_mm(mm);
```

### 4.测试

#### 4.1 测试代码

```c
//user/cowtest.c
#include <stdio.h>
#include <ulib.h>

// 定义一个全局变量。
// fork 后，父子进程的虚拟地址空间中，这个变量的虚拟地址是完全一样的。
static volatile int global_var = 100;

int main(void) {
    int pid;
    
    cprintf("COW Test Start\n");
    // 1. 父进程打印初始值和地址
    cprintf("Parent: global_var = %d, addr = %x\n", global_var, &global_var);

    pid = fork(); // 创建子进程
    // 此时，在 COW 机制下：
    // 父子进程的页表指向同一个物理页。
    // 该物理页在两者的页表中都被标记为“只读” (PTE_W=0) 且“COW” (PTE_COW=1)。

    if (pid == 0) {
        // --- 子进程执行流 ---
        
        // 2. 子进程打印值和地址
        // 地址应该与父进程完全一致（虚拟地址相同）。
        // 值也应该一致（物理页共享）。
        cprintf("Child: global_var = %d, addr = %x\n", global_var, &global_var);
        
        cprintf("Child: Modifying global_var...\n");
        
        // 3. 关键操作：写内存
        // 这一行代码试图向一个只读页面写入数据。
        // CPU 硬件检测到权限冲突，触发 Store Page Fault (缺页异常)。
        // 内核捕获异常 -> 发现是 COW 页 -> 分配新物理页 -> 复制数据 -> 修改页表 -> 恢复执行。
        global_var = 200; 
        
        // 4. 子进程验证修改
        // 此时子进程读写的是自己私有的新物理页，值应为 200。
        cprintf("Child: global_var = %d\n", global_var);
        exit(0);
    } else {
        // --- 父进程执行流 ---
        wait(); // 等待子进程结束，确保子进程的修改操作已经完成。
        
        // 5. 父进程验证隔离性
        // 此时读取 global_var。
        // 如果 COW 失败（比如父子进程还在共享同一个可写物理页），这里会读到 200。
        // 如果 COW 成功（物理页已分离），父进程读的还是旧物理页，值应为 100。
        cprintf("Parent: global_var = %d\n", global_var);
        
        if (global_var == 100) {
             cprintf("COW Test Passed: Parent saw original value.\n");
        } else {
             cprintf("COW Test Failed: Parent saw modified value.\n");
        }
    }
    return 0;
}
```

#### 4.2 测试输出

编译后执行make qemu，输出如图所示：

![image-20251207184621574](C:\Users\lenovo\AppData\Roaming\Typora\typora-user-images\image-20251207184621574.png)

- **addr相同**: 证明父子进程拥有相同的**虚拟内存布局**。
- **`Child: global_var = 200`**: 证明子进程**成功写入**了数据。这意味着内核正确处理了缺页异常，将只读页面转换为了可写页面（通过复制）。
- **`Parent: global_var = 100`**: 证明父进程的数据**未受影响**。这意味着子进程写入的是它自己的副本，而不是父进程原本的物理页。

这个测试验证了 COW 的两个核心内容：

1. **共享** : 在 `fork` 之后，写操作发生之前，父子进程可以读取相同的数据。
2. **隔离 **: 一旦发生写操作，操作系统必须保证修改只对写入者可见，而不影响其他共享者。

如果 COW 实现有误：

- **情况 A (未设只读)**: 如果 `fork` 时忘记将页面设为只读，子进程直接写入共享物理页，父进程会打印 `200` -> **测试失败**。
- **情况 B (缺页处理失败)**: 如果 `do_pgfault` 没写好，子进程在 `global_var = 200` 时会触发 `Unhandled Page Fault` 并被杀死 -> **程序崩溃**。
- **情况 C (复制逻辑错误)**: 如果分配了新页但没复制旧数据，子进程可能读到乱码 -> **数据错误**。



## 扩展练习challenge2

### 1. 用户程序何时被预先加载到内存中？

用户程序实际上是在**编译链接阶段**被“打包”进内核，并在**系统启动阶段**随内核一起被加载到内存中的。具体过程如下：

**编译与链接：**

- 在makefile中，用户程序首先被编译成 ELF 格式的可执行文件。
- 接着，链接器（`ld`）通过 `--format=binary` 选项将这些用户程序的二进制文件直接链接到内核镜像（`kernel`）中。
- 链接器会自动生成特定的符号，这些符号指向内核数据段中存储该用户程序二进制数据的起始位置。

**系统启动：**

- 当Bootloader加载内核镜像到物理内存时，由于用户程序的二进制数据已经是内核镜像的一部分，它们也随之被加载到了物理内存中。此时，它们只是作为内核的一段“数据”存在。

**进程创建：**

- 当内核线程 `user_main` 执行时，它调用 `KERNEL_EXECVE` 宏。
- 该宏通过链接器生成的符号获取用户程序在内核内存中的地址，并将其传递给 `load_icode` 函数。
- `load_icode` 函数解析这段内存中的 ELF 格式数据，并将其**复制**到新创建的用户进程的内存空间中。

### 2. 与常用操作系统加载的区别

| 特性         | 本实验                                                     | 常用操作系统 (Linux/Windows)                     |
| ------------ | ---------------------------------------------------------- | ------------------------------------------------ |
| **存储位置** | 用户程序二进制直接嵌入在**内核镜像**中。                   | 用户程序存储在**磁盘文件系统**中。               |
| **加载时机** | 随内核一起加载到内存（作为数据），运行时仅需内存拷贝。     | 运行时通过文件系统驱动从磁盘读取到内存。         |
| **灵活性**   | 必须重新编译内核才能添加或修改用户程序。                   | 无需重启或重编译内核，随时可运行新的可执行文件。 |
| **内存占用** | 所有用户程序的二进制都常驻内核内存，即使不运行也占用空间。 | 仅在程序运行时才调入内存，支持按需分页。         |

### 3.原因是什么？

**缺乏文件系统支持：** 在 Lab 5 阶段，操作系统内核还没有实现完善的文件系统和磁盘驱动。如果没有文件系统，内核就无法从磁盘上“读取”文件。因此，将程序作为数据嵌入内核是最简单的变通方法。

**简化加载过程：** 从内存中通过指针直接读取 ELF 数据，比实现一套完整的文件打开、读取、关闭逻辑要简单得多。可以专注于进程管理和虚拟内存的核心逻辑，而不必先去处理复杂的文件 I/O。





## 分支任务：gdb 调试系统调用以及返回

实验采用了三个终端窗口协同工作：

- **终端 1 (QEMU)**: 运行 `make debug` 启动 QEMU，等待 GDB 连接。
- **终端 2 (Host GDB)**: 使用 `sudo gdb -p <PID>` 附着到 QEMU 进程，用于观察模拟器行为。*用来调试 QEMU 模拟器本身。我们可以看到模拟器的内部变量，比如虚拟 CPU 的寄存器 `env`。*
- **终端 3 (Guest GDB)**: 运行 `make gdb` 连接 `ucore`，用于控制操作系统运行。

### 3.1 开始阶段

终端1

```
make debug
```

终端2

```
sudo gdb -p $(pgrep -f qemu-system-riscv64)
(gdb) break helper_raise_exception if exception == 8
(gdb) continue
```

终端3

```
make gdb

此时应该能正常连接，显示 0x00001000 in ?? () 之类的信息。
如果连接成功：
输入 continue

如果还是报错 "The program is not being run"
这说明 Guest GDB 认为它还没连接上目标。请尝试手动连接：
在 终端 3 的 GDB 提示符下输入：target remote localhost:1234
```



### 3.2 观察 `ecall` 指令 (系统调用)

#### 3.2.1 预设断点策略

由于 ucore 启动过程中会产生大量的时钟中断（异常号不同）和内核态异常，为了精准捕捉用户程序的系统调用，我们在 Host GDB (终端 2) 采用了 **条件断点** 策略。

1. 在 Host GDB 中设置条件断点，仅捕获用户态发起的系统调用（异常号 8）：

   ```
   break helper_raise_exception if exception == 8
   ```

2. 在 Host GDB 输入 `continue` 让 QEMU 运行。
   随后在 Guest GDB (终端 3) 输入 `continue` 让 ucore 启动并加载用户程序。

#### 3.2.2 捕捉异常抛出 (helper_raise_exception)

当 ucore 运行到用户程序的 `ecall` 指令时，Host GDB 成功触发断点。

- **断点位置**：`target/riscv/op_helper.c` 的 `helper_raise_exception` 函数。

- **现场验证**：

  ```
  (gdb) break helper_raise_exception if exception == 8
  Breakpoint 1 at 0x5c2189326b80: file /home/shirley/qemu-4.1.1/target/riscv/op_helper.c, line 39.
  (gdb) continue
  Continuing.
  [Switching to Thread 0x797d557fe640 (LWP 6015)]
  
  Thread 3 "qemu-system-ris" hit Breakpoint 1, helper_raise_exception (env=0x5c21c5eeeef0, exception=8) at /home/shirley/qemu-4.1.1/target/riscv/op_helper.c:39
  39 riscv_raise_exception(env, exception, 0);
  (gdb) print exception
  $1 = 8   //用户态系统调用
  
  //env 是一个指向 CPURISCVState 结构体的指针，它代表了 RISC-V CPU 的当前状态。
  (gdb) print /x env->pc  
  $2 = 0xffffffffc02005a0  //内核地址
  (gdb) print env->priv  //当前特权级，表示 CPU 当前处于什么模式下运行
  $3 = 1  //内核态
  ```

  这里询问大模型

  ‘’ 这确实是一个非常令人困惑的现象，但它揭示了 QEMU 内部实现的一个重要细节。

  **现象分析：**
  
  - `exception = 8`：确实是用户态系统调用 (User ECALL)。
  - `env->pc = 0xffffffffc02005a0` (内核地址) 和 `env->priv = 1` (S Mode)：说明 CPU 状态**已经**切换到了内核态。
  
  **为什么会这样？**
  这说明在 QEMU 的 TCG (Tiny Code Generator) 翻译 `ecall` 指令时，生成的代码逻辑顺序可能是：
  
  1. **先更新状态**：直接在生成的代码块中修改了 PC 和 Priv。
  2. **后调用 Helper**：然后才调用 `helper_raise_exception` 来处理异常（或者仅仅是用来记录/触发调试断点）。

  **解决方案：观察 `sepc`**

  既然 `pc` 和 `priv` 已经变了，那么“穿越前”的 PC 一定被保存到了 `sepc` 中。‘’

   `env->sepc`
  
  - **含义**: **监管者异常程序计数器**。这是一个专门的控制状态寄存器 (CSR)。
  - **作用**: **“存档点”**。当发生异常或中断时，硬件（或 QEMU）会自动把“发生异常时的 PC 值”保存到 `sepc` 中，以便将来能跳回去继续执行。
  - **在实验中的关键作用**:
    - **ECALL 时**: 它保存了用户程序中 `ecall` 指令的地址（或下一条指令地址）。这是证明“我们是从用户态来的”最直接证据。
    - **SRET 时**: 它是“回家的路”。`sret` 指令会读取 `sepc` 的值，并把它赋值给 `pc`，从而让 CPU 跳转回用户程序继续运行。
  
  于是执行，但执行结果
  
  ```
  (gdb) print /x env->sepc
  $4 = 0x0
  ```

  大模型：“ 这说明我们捕获到的这个 `exception=8` 非常诡异，可能是内核启动早期的某种状态，或者 QEMU 内部状态还没同步。

  **解决方法：使用条件断点过滤**
  
  我们必须让 QEMU 跑起来，忽略掉那些无关的中断，只在 `cause == 8` 时停下。
  
  但是，在 `riscv_cpu_do_interrupt` 入口处，`cause` 变量还没被计算出来（它是局部变量），所以直接 `break ... if cause == 8` 会失败。
  
  我们需要断在 **计算出 `cause` 之后** 的某一行。“
  
  查看源码
  
  ```
  (gdb) list 510, 530
  510 /* cs->exception is 32-bits wide unlike mcause which is XLEN-bits wide
  511 * so we mask off the MSB and separate into trap type and cause.
  512 /
  513 bool async = !!(cs->exception_index & RISCV_EXCP_INT_FLAG);
  514 target_ulong cause = cs->exception_index & RISCV_EXCP_INT_MASK;
  515 target_ulong deleg = async ? env->mideleg : env->medeleg;
  516 target_ulong tval = 0;
  517
  518 static const int ecall_cause_map[] = {
  519 [PRV_U] = RISCV_EXCP_U_ECALL,
  520 [PRV_S] = RISCV_EXCP_S_ECALL,
  521 [PRV_H] = RISCV_EXCP_H_ECALL,
  522 [PRV_M] = RISCV_EXCP_M_ECALL
  523 };
  524
  525 if (!async) {
  526 / set tval to badaddr for traps with address information */
  527 switch (cause) {
  528 case RISCV_EXCP_INST_ADDR_MIS:
  529 case RISCV_EXCP_INST_ACCESS_FAULT:
  530 case RISCV_EXCP_LOAD_ADDR_MIS:
  ```
  
  于是按照大模型提示运行如下
  
  ```
  (gdb) delete
  Delete all breakpoints? (y or n) y
  (gdb) break target/riscv/cpu_helper.c:525
  Breakpoint 3 at 0x5c218932881f: file /home/shirley/qemu-4.1.1/target/riscv/cpu_helper.c, line 525.
  
  //加上条件
  (gdb) condition 3 cause == 8
  (gdb) continue
  Continuing.
  
  Thread 3 "qemu-system-ris" hit Breakpoint 3, riscv_cpu_do_interrupt (cs=0x5c21c5ee64e0) at /home/shirley/qemu-4.1.1/target/riscv/cpu_helper.c:525
  525 if (!async) {
  (gdb) print /x env->pc
  $8 = 0xffffffffc02005a0
  (gdb) print env->priv
  $9 = 1
  ```
  
  发现结果仍然直接穿越到了内核态，继续询问大模型，根据大模型的要求，再次尝试**抓取 `helper_raise_exception` 的入口**

  ```
  (gdb) break helper_raise_exception
  Breakpoint 4 at 0x5c2189326b80: file /home/shirley/qemu-4.1.1/target/riscv/op_helper.c, line 39.
  (gdb) condition 4 exception == 8
  (gdb) continue
  Continuing.
  
  Thread 3 "qemu-system-ris" hit Breakpoint 4, helper_raise_exception (env=0x5c21c5eeeef0, exception=8) at /home/shirley/qemu-4.1.1/target/riscv/op_helper.c:39
  39 riscv_raise_exception(env, exception, 0);
  (gdb) print /x env->pc
  $10 = 0xffffffffc02005a0   //仍是内核地址
  ```
  
  大模型对此给出的解释是
  ” 事实已经很清楚了：**在 QEMU 4.1.1 的实现中，当 `helper_raise_exception` 被调用时，PC 已经被更新为异常处理入口了。**
  
  1. > 在 `helper_raise_exception` 设置了断点，条件是 `exception == 8`。说明 QEMU 成功识别到了用户态发起的系统调用 (User ECALL)。”
  
  2. **展示状态切换结果**：
  
     > “此时检查 CPU 状态：
     > `print /x env->pc` 显示的是内核地址 `0xffffffff...`。
     > `print env->priv` 显示的是 1 (Supervisor Mode)。
     > 这说明 QEMU 在识别到异常的瞬间，已经迅速完成了硬件层面的上下文切换，将控制权交给了内核。”
  
     ”

### 3.3 观察 `sret` 指令 (从异常返回)

首先设置断点，并运行

```
(gdb) delete
Delete all breakpoints? (y or n) y
(gdb) break helper_sret
Breakpoint 5 at 0x5c2189326d64: file /home/shirley/qemu-4.1.1/target/riscv/op_helper.c, line 76.
(gdb) condition 5 env->sepc < 0xffffffff00000000
(gdb) continue
Continuing.

Thread 3 "qemu-system-ris" hit Breakpoint 5, helper_sret (env=0x5c21c5eeeef0, cpu_pc_deb=18446744072637910974) at /home/shirley/qemu-4.1.1/target/riscv/op_helper.c:76
76 if (!(env->priv >= PRV_S)) {

//验证“返回前”状态，当断点触发时
(gdb) print /x env->pc
$11 = 0xffffffffc0200fbe  // 内核地址
(gdb) print env->priv
$12 = 1    // s模式
(gdb) print /x env->sepc
$13 = 0x800020 //用户程序入口

//让函数执行完毕
(gdb) finish
Run till exit from #0 helper_sret (env=0x5c21c5eeeef0,
cpu_pc_deb=18446744072637910974)
at /home/shirley/qemu-4.1.1/target/riscv/op_helper.c:76
0x0000797d4e0a42cf in code_gen_buffer ()
Value returned is $14 = 8388640

```

接下来应该验证返回后状态，注意这里不能直接使用

```
(gdb) print /x env->pc
No symbol "env" in current context.
```

因为执行 `finish` 后，GDB 跳出了 `helper_sret` 函数，回到了 QEMU 的 **TCG 生成代码缓冲区 (code_gen_buffer)** 中。

在这个区域（JIT 编译出来的机器码），GDB 无法识别 C 语言层面的变量（如 `env`），所以提示 `No symbol "env" in current context`。

所以虽然不能直接用 `env` 变量名，但我们知道 `env` 结构体的地址

```
helper_sret (env=0x5c21c5eeeef0, ...)
```

这个 `0x5c21c5eeeef0` 就是 `env` 的地址。

所以强制类型转换查看pc

```
(gdb) print /x ((CPURISCVState *)0x5c21c5eeeef0)->pc
$15 = 0xffffffffc0200fbe
```

这里询问大模型为什么仍是内核地址：
“ 这里 `pc` 还是内核地址，说明 `helper_sret` 虽然返回了目标地址（`0x800020`），但 QEMU 的 TCG 还没有把这个返回值真正写入到 `env->pc` 中。

这是 QEMU 的实现细节：Helper 函数只负责计算出“要去哪里”，真正的跳转是由 TCG 生成的汇编代码在 Helper 返回后执行的。

**所以，你现在处于“中间状态”：**

1. Helper 函数已经算出了目标地址 `0x800020` (刚才 `finish` 的返回值 `$14`)。
2. 但是 `env->pc` 还没更新。

**下一步操作：**

你需要让 QEMU 再跑一点点，让 TCG 把这个跳转执行完。”

按照大模型提示继续运行

```
(gdb) stepi
0x0000797d4e0a42d6 in code_gen_buffer ()
(gdb) print /x ((CPURISCVState *)0x5c21c5eeeef0)->pc
$16 = 0x800020  //pc 已经成功变成了用户程序的入口地址
(gdb) print ((CPURISCVState *)0x5c21c5eeeef0)->priv
$17 = 0  //u模式
```

经过单步跟踪，看到 `env->pc` 最终更新为了 `0x800020`，同时 `env->priv` 恢复为 0。

这完整地展示了：

1. 用户程序执行 `ecall` 陷入内核。
2. 内核处理完毕后执行 `sret`。
3. QEMU 模拟硬件行为，将控制权精准地交还给用户程序。

## 实验指导书问题：

我目前的内核是如何将用户程序加载进去的，如果我想要调试内核上运行的用户程序，是不是需要将用户程序的符号表也都加载进去，我应该怎么操作，为什么使用makedebug和makegdb调试的时候就会自动将内核符号表加载进去。1. 内核如何加载用户程序？

在 ucore (Lab 5) 中，用户程序（如 `hello`, `exit` 等）并不是像在 Linux 上那样作为独立的文件存在于磁盘文件系统中（虽然 Lab 5 已经有了简单的 SFS，但早期的用户程序通常是直接链接进内核镜像的）。

**加载流程：**

1. **编译链接**：用户程序（如 [hello.c](vscode-file://vscode-app/c:/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html)）被编译成 ELF 格式的可执行文件。
2. **二进制嵌入**：通过 `ld` (链接器) 的 `bincode` 或类似机制，这些用户程序的二进制数据被直接“嵌入”到了内核的数据段中。
   - 你可以看 [init.c](vscode-file://vscode-app/c:/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html)，里面会有类似 `_binary_obj_user_hello_out_start` 这样的符号，指向了用户程序的二进制数据。
3. **创建进程**：内核启动后，通过 `kernel_thread` 创建第一个内核线程。
4. **加载执行**：该线程调用 `do_execve`，解析上述的 ELF 数据，分配内存，将代码段和数据段拷贝到用户空间，然后设置好 Trapframe，通过 `sret` 跳转到用户程序的入口点。

### 2. 为什么 `make debug` 只有内核符号表？

运行 `make gdb` 时，GDB 启动命令通常是这样的：

```
riscv64-unknown-elf-gdb -ex 'file bin/kernel' ...
```

- **`file bin/kernel`**: 这条命令告诉 GDB 加载 [kernel](vscode-file://vscode-app/c:/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 这个 ELF 文件。
- **结果**: GDB 读取了内核的符号表（函数名、变量名、行号信息）。因此你可以调试 [kern](vscode-file://vscode-app/c:/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 下的所有代码。
- **缺失**: 用户程序虽然在物理上包含在 [kernel](vscode-file://vscode-app/c:/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 的数据段里，但逻辑上它们是独立的 ELF 文件。GDB 不会自动解析内核数据段里的“另一个 ELF”。

### 3. 如何调试用户程序（加载用户符号表）？

如果你想在 GDB 中调试用户程序（比如在 [hello.c](vscode-file://vscode-app/c:/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 的 `main` 函数打断点），你需要**手动加载用户程序的符号表**。

**操作步骤：**

1. **找到用户程序的 ELF 文件**：
   在编译过程中，用户程序会被编译为独立的 ELF 文件，通常位于 [user](vscode-file://vscode-app/c:/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 目录下（例如 `obj/user/hello.out` 或类似的带有符号信息的中间文件）。

2. **在 GDB 中加载符号表**：
   使用 `add-symbol-file` 命令。

   ```
   (gdb) add-symbol-file obj/user/hello.out
   ```

   *注意：对于静态链接的简单用户程序，通常不需要指定偏移量，因为它们的链接地址（虚拟地址）在编译时已经确定了。*

3. **设置断点**：
   加载符号表后，GDB 就认识 `hello.c` 里的函数了。

   ```
   (gdb) break main
   ```

   *(如果有多个 main，可能需要指定文件名，如 `break hello.c:main`)*

### 4. 为什么需要手动加载？

- **地址空间隔离**：用户程序运行在用户地址空间，内核运行在内核地址
- **动态加载**：在现代 OS 中，用户程序是动态加载的，GDB 甚至不知道用户程序会被加载到什么内存地址，直到程序运行起来。虽然 ucore 的用户程序地址是固定的，但 GDB 依然将其视为“外部数据”，除非你显式告诉它。

### **总结：**

要调试用户程序，需要在 Guest GDB (终端 3) 中执行：

```
add-symbol-file obj/user/hello.out  
break hello.c:main
```



## QEMU 源码分析：ECALL 与 SRET 的处理流程

在 QEMU 中，RISC-V 的特权指令模拟主要涉及两个文件：

1. `target/riscv/op_helper.c`: 包含指令的 Helper 函数实现（如 `helper_sret`）。
2. `target/riscv/cpu_helper.c`: 包含核心的中断/异常处理逻辑（如 `riscv_cpu_do_interrupt`）。

### 关键数据结构：`CPURISCVState`

在调试中频繁出现的 `env` 指针，实际上指向了 `CPURISCVState` 结构体（定义在 `target/riscv/cpu.h`）。它模拟了 RISC-V CPU 的所有寄存器状态：

```
struct CPURISCVState {
    target_ulong gpr[32];   // 通用寄存器 (x0-x31)
    target_ulong pc;        // 程序计数器
    target_ulong priv;      // 当前特权级 (0, 1, 3)
    
    // CSR 寄存器
    target_ulong mstatus;   // 机器状态寄存器
    target_ulong sepc;      // S 模式异常 PC
    target_ulong scause;    // S 模式异常原因
    target_ulong stvec;     // S 模式中断向量表基址
    // ...
};
```

### 1. ECALL 指令的处理流程

当 TCG 翻译到 `ecall` 指令时，它会生成代码调用 `helper_raise_exception`，最终触发异常处理机制。

#### 关键函数：`riscv_cpu_do_interrupt`

位于 `target/riscv/cpu_helper.c`。这是 QEMU 模拟 RISC-V 硬件中断行为的核心函数。

`ecall` 的本质是一个“异常”。QEMU 模拟了硬件的自动化过程：**保存旧 PC 到 SEPC -> 更新 SCAUSE -> 加载新 PC (STVEC) -> 提升权限**。

```
void riscv_cpu_do_interrupt(CPUState *cs)
{
    // 1. 获取环境指针和异常原因
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    target_ulong cause = cs->exception_index & RISCV_EXCP_INT_MASK;

    // 2. 异常委托判断 (Delegation Check)
    // 检查该异常是否被委托给 S 模式处理 (medeleg 寄存器)
    // 如果当前特权级 <= S 模式，且 medeleg 对应位为 1，则交给 S 模式
    if (env->priv <= PRV_S && ((env->medeleg >> cause) & 1)) {
        
        /* handle the trap in S-mode */
        
        // 3. 保存现场 (Context Saving)
        // 将当前 PC (用户程序地址) 保存到 sepc 寄存器
        env->sepc = env->pc;
        
        // 4. 记录异常原因
        // 将 cause 写入 scause 寄存器
        env->scause = cause | ...;
        
        // 5. 记录异常值 (如缺页地址，ecall 时通常为 0)
        env->sbadaddr = tval;
        
        // 6. 跳转到中断入口 (Trap Vector)
        // 读取 stvec 寄存器，计算出内核中断处理程序的入口地址，赋给 PC
        env->pc = (env->stvec >> 2 << 2) + ...;
        
        // 7. 切换特权级 (Mode Switch)
        // 将 CPU 特权级切换为 Supervisor Mode (1)
        riscv_cpu_set_mode(env, PRV_S);
        
    } else {
        /* handle the trap in M-mode (OpenSBI) */
        // ... 如果没有委托，则交给 M 模式处理 ...
    }
}
```

### 2. SRET 指令的处理流程

`sret` 指令用于从 S 模式的异常处理程序返回。它的实现位于 `target/riscv/op_helper.c` 中的 `helper_sret` 函数。

`sret` 的核心逻辑是 **“读档”**：从 `sepc` 找回代码执行位置，从 `mstatus.SPP` 找回之前的特权级，然后恢复现场，让 CPU 回到用户态继续执行。

#### 关键函数：`helper_sret`

```
target_ulong helper_sret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    // 1. 权限检查 (Permission Check)
    // sret 是特权指令，必须在 S 模式或 M 模式下执行
    if (!(env->priv >= PRV_S)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    // 2. 读取返回地址 (Read Return Address)
    // 从 sepc 寄存器中读取之前保存的用户程序地址
    target_ulong retpc = env->sepc;

    // 3. 恢复状态寄存器 (Restore Status)
    // 读取 mstatus 寄存器
    target_ulong mstatus = env->mstatus;
    
    // 获取之前的特权级 (SPP 位: Supervisor Previous Privilege)
    target_ulong prev_priv = get_field(mstatus, MSTATUS_SPP);
    
    // 恢复中断使能状态 (SPIE -> SIE)
    mstatus = set_field(mstatus, MSTATUS_SIE, get_field(mstatus, MSTATUS_SPIE));
    
    // 将 SPIE 置 1，SPP 置 0 (为下一次异常做准备)
    mstatus = set_field(mstatus, MSTATUS_SPIE, 1);
    mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);
    env->mstatus = mstatus;

    // 4. 切换特权级 (Mode Switch)
    // 恢复到之前的特权级 (通常是 User Mode)
    riscv_cpu_set_mode(env, prev_priv);

    // 5. 返回目标地址
    // 函数返回 retpc，QEMU 的 TCG 随后会将 PC 更新为这个值
    return retpc;
}
```



## QEMU TCG 指令翻译与 Double-GDB 调试原理

### 1. 什么是 TCG 

QEMU 是一个动态二进制翻译器。它不直接“解释”执行每一条 Guest 指令（如 RISC-V 的 `ecall`），而是通过 TCG 将 Guest 指令翻译成 Host 机器（如 x86_64）的机器码，然后直接在 Host CPU 上运行这些机器码。

#### 翻译流程

当 QEMU 遇到一段新的 RISC-V 代码块时，会经历以下步骤：

1. **解码**: 读取 RISC-V 指令（如 `ecall`）。
2. **中间码生成**: 将 RISC-V 指令转换为 QEMU 内部的中间表示。
   - 例如，`add a0, a1, a2` 会被翻译为类似 `tcg_gen_add_i64(cpu_gpr[10], cpu_gpr[11], cpu_gpr[12])` 的操作。
   - 对于 `ecall`，它会被翻译为“抛出异常”的操作：调用 helper 函数 `helper_raise_exception`，并标记当前 TB 结束（因为异常会改变控制流）。
3. **宿主码生成**: 将 TCG Ops 编译为 Host (x86_64) 的汇编指令。
4. **执行** : CPU 直接执行生成的 x86_64 代码。

#### 关键点：Helper 函数

对于复杂的指令（如 `ecall`, `sret`, `csrrw`），TCG 很难直接用几条简单的 Host 指令实现。因此，QEMU 会生成一个 **Call 指令**，去调用 C 语言编写的 **Helper 函数**。

- `ecall` -> 调用 `helper_raise_exception`
- `sret` -> 调用 `helper_sret`

这就是为什么我们在 Host GDB 中可以对 `helper_raise_exception` 打断点的原因——因为生成的机器码里确实有一条 `call` 指令指向了这个函数。

------

### 2. Double-GDB 调试与 TCG 的关系

在本次实验中使用了“双重 GDB”调试，这实际上是在调试 QEMU 的两个不同层面：

#### 层面一：Guest GDB (调试 ucore)

- **视角**: 这是一个纯粹的 RISC-V 软件视角。
- **原理**: QEMU 内置了一个 GDB Stub。当你在 Guest GDB 中下断点（如 `break syscall`）时，QEMU 不会修改生成的 Host 代码，而是会在翻译阶段或者执行循环中检查断点列表。如果 PC 命中，QEMU 就会暂停执行，并通过网络协议告诉 Guest GDB。
- **TCG 的作用**: 对 Guest GDB 来说，TCG 是透明的。它感觉自己就在一个真实的 RISC-V 硬件上运行。

#### 层面二：Host GDB (调试 QEMU)

- **视角**: 这是一个 x86_64 应用程序视角。
- **原理**: 我们直接调试的是 QEMU 这个 Linux 进程。
- **TCG 的作用**: 我们看到的“源码调试”其实是在调试 **Helper 函数**。
  - 当我们 `break helper_sret` 时，我们断住的是 QEMU 模拟 `sret` 逻辑的 C 代码。
  - **思考题关联**: 另一个双重 GDB 实验（通常指调试 Bootloader 或 OpenSBI）也完全依赖这个机制。如果我们要研究 OpenSBI 如何处理 M 模式的异常，我们也需要在 Host GDB 中断点 `riscv_cpu_do_interrupt`，观察它如何将控制权分发给 M 模式处理程序。

------

### 3. 为什么我们在调试中看到了“诡异”的现象？（来自大模型的解释）

在实验中，我们发现当 Host GDB 停在 `helper_raise_exception` 时，`env->pc` 已经被修改为内核地址了。这是 TCG 优化带来的副作用：

**TCG 的“状态同步”机制**：
为了提高性能，TCG 生成的代码通常只在寄存器中更新状态，只有在需要“同步”时（比如调用 Helper 函数、发生异常、TB 结束），才会把寄存器的值写回内存中的 `env` 结构体。

对于 `ecall`，翻译逻辑大致如下（伪代码）：

```
// TCG 生成的伪代码逻辑
env->pc = target_pc; // 1. 先更新 PC (为了异常处理能找到返回地址)
call helper_raise_exception(env, 8); // 2. 再调用 Helper
```

所以，当你断在 Helper 函数入口时，步骤 1 已经执行完了。这就是为什么你看到 PC 已经变了的原因。

------

### 4. 总结 

- **TCG 桥梁**: QEMU 通过 TCG 将 Guest 指令动态翻译为 Host 指令。
- **Helper **: 复杂指令（如系统调用、特权级切换）通过 C 语言 Helper 函数实现，这为我们使用 Host GDB 观察模拟细节提供了入口。
- **双重调试的本质**:
  - Guest GDB 调试的是 **被翻译的代码逻辑** (RISC-V 业务逻辑)。
  - Host GDB 调试的是 **翻译器本身的行为** (QEMU 模拟逻辑)。
- **状态同步**: 理解 TCG 的状态同步机制，能帮助我们解释为什么在 Helper 函数中看到的 CPU 状态（如 PC）可能已经发生了“预更新”。





















