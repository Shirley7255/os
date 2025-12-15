#include <stdio.h>
#include <ulib.h>

static volatile int global_var = 100;

int main(void) {
    int pid;
    
    cprintf("COW Test Start\n");
    cprintf("Parent: global_var = %d, addr = %x\n", global_var, &global_var);

    pid = fork();
    if (pid == 0) {
        // Child
        cprintf("Child: global_var = %d, addr = %x\n", global_var, &global_var);
        cprintf("Child: Modifying global_var...\n");
        global_var = 200; // This should trigger COW
        cprintf("Child: global_var = %d\n", global_var);
        exit(0);
    } else {
        // Parent
        wait();
        cprintf("Parent: global_var = %d\n", global_var);
        if (global_var == 100) {
             cprintf("COW Test Passed: Parent saw original value.\n");
        } else {
             cprintf("COW Test Failed: Parent saw modified value.\n");
        }
    }
    return 0;
}
