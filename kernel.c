/*
 * (C) 2025, Cornell University
 * All rights reserved.
 *
 * Description: kernel ≈ 2 handlers
 * intr_entry() handles timer and device interrupts.
 * excp_entry() handles system calls and faults (e.g., invalid memory access).
 */

#include "process.h"
#include <string.h>

uint core_in_kernel;
uint core_to_proc_idx[NCORES];
struct process proc_set[MAX_NPROCESS + 1];
/* proc_set[0] is a place holder for idle cores. */

#define curr_proc_idx core_to_proc_idx[core_in_kernel]
#define curr_pid      proc_set[curr_proc_idx].pid
#define curr_status   proc_set[curr_proc_idx].status
#define curr_saved    proc_set[curr_proc_idx].saved_registers

static void intr_entry(uint);
static void excp_entry(uint);

void kernel_entry() {
    /* With the kernel lock, only one core can enter this point at any time. */
    asm("csrr %0, mhartid" : "=r"(core_in_kernel));

    /* Save the process context. */
    asm("csrr %0, mepc" : "=r"(proc_set[curr_proc_idx].mepc));
    memcpy(curr_saved, SAVED_REGISTER_ADDR, SAVED_REGISTER_SIZE);

    uint mcause;
    asm("csrr %0, mcause" : "=r"(mcause));
    (mcause & (1 << 31)) ? intr_entry(mcause & 0x3FF) : excp_entry(mcause);

    /* Restore the process context. */
    asm("csrw mepc, %0" ::"r"(proc_set[curr_proc_idx].mepc));
    memcpy(SAVED_REGISTER_ADDR, curr_saved, SAVED_REGISTER_SIZE);
}

#define INTR_ID_TIMER   7
#define EXCP_ID_ECALL_U 8
#define EXCP_ID_ECALL_M 11
static void proc_yield();
static void proc_try_syscall(struct process* proc);

static void excp_entry(uint id) {
    if (id >= EXCP_ID_ECALL_U && id <= EXCP_ID_ECALL_M) {
        /* Copy the system call arguments from user space to the kernel. */
        uint syscall_paddr = earth->mmu_translate(curr_pid, SYSCALL_ARG);
        memcpy(&proc_set[curr_proc_idx].syscall, (void*)syscall_paddr,
               sizeof(struct syscall));
        proc_set[curr_proc_idx].syscall.status = PENDING;

        proc_set_pending(curr_pid);
        proc_set[curr_proc_idx].mepc += 4;
        proc_try_syscall(&proc_set[curr_proc_idx]);
        proc_yield();
        return;
    }
    /* Student's code goes here (System Call & Protection | Virtual Memory). */

    /* Kill the current process if curr_pid is a user application. */

    /* Student's code ends here. */
    FATAL("excp_entry: kernel got exception %d", id);
}




/* In grass/kernel.c */

static void intr_entry(uint id) {
    //Expect Timer interrupts. If it's something else, crash safely.
    if (id != INTR_ID_TIMER) FATAL("excp_entry: kernel got interrupt %d", id);

    // If there is a valid process currently running, record that it got interrupted.
    if (curr_pid < MAX_NPROCESS && proc_set[curr_proc_idx].status != PROC_UNUSED) {
        proc_set[curr_proc_idx].n_interrupts++;
    }

    //Call the Scheduler
    proc_yield();
}

static void proc_yield() {
    // If a process was running, change its status to "RUNNABLE".
    if (curr_status == PROC_RUNNING) proc_set_runnable(curr_pid);

    //Calculate Runtime.
    static ulonglong last_switch_time = 0;
    ulonglong current_time = mtime_get();
    if (last_switch_time == 0) last_switch_time = current_time;
    
    ulonglong runtime = current_time - last_switch_time;

    // Call the function we wrote to update CPU time and potentially demote the process if it ran too long.
    if (proc_set[curr_proc_idx].status != PROC_UNUSED) {
        mlfq_update_level(&proc_set[curr_proc_idx], runtime);
    }

    //Check for Reset
    mlfq_reset_level();
    
    int next_idx = MAX_NPROCESS;

    //Iterate through Priority Levels from 0 to 4
    for (int lvl = 0; lvl < MLFQ_NLEVELS; lvl++) {

        //Iterate through all processes to find one in the current Level
        for (uint i = 1; i <= MAX_NPROCESS; i++) {
            int idx = (curr_proc_idx + i) % MAX_NPROCESS;
            if (idx == 0) continue;

            struct process* p = &proc_set[idx];

            //Check if this process has any system calls waiting to finish.
            if (p->status == PROC_PENDING_SYSCALL) proc_try_syscall(p);

            if ((p->status == PROC_READY || p->status == PROC_RUNNABLE) && p->priority == lvl) {
                next_idx = idx;
                goto found;
            }
        }
    }

found:
    //Context Switch
    if (next_idx < MAX_NPROCESS) {
        // If this is the very first time the process is running, record the Start Time.
        if (proc_set[next_idx].start_time == 0) {
            proc_set[next_idx].start_time = mtime_get();
        }
        // Reset the stopwatch for the next run.
        last_switch_time = mtime_get();

    } else {
        // If we looked through all levels and found nothing, the system has crashed.
        FATAL("proc_yield: no process to run on core %d", core_in_kernel);
    }

    //Load New Process
    curr_proc_idx = next_idx;
    earth->mmu_switch(curr_pid); // Switch the memory mapping to the new process
    earth->mmu_flush_cache();    // Clear old memory cache

    if (curr_status == PROC_READY) {
        curr_saved[0]                = APPS_ARG;
        curr_saved[1]                = APPS_ARG + 4;
        proc_set[curr_proc_idx].mepc = APPS_ENTRY;
    }

    // Set status to RUNNING and restart the hardware timer.
    proc_set_running(curr_pid);
    earth->timer_reset(core_in_kernel);
}

static void proc_try_send(struct process* sender) {
    for (uint i = 0; i < MAX_NPROCESS; i++) {
        struct process* dst = &proc_set[i];
        if (dst->pid == sender->syscall.receiver &&
            dst->status != PROC_UNUSED) {
            /* Return if dst is not receiving or not taking msg from sender. */
            if (!(dst->syscall.type == SYS_RECV &&
                  dst->syscall.status == PENDING))
                return;
            if (!(dst->syscall.sender == GPID_ALL ||
                  dst->syscall.sender == sender->pid))
                return;

            dst->syscall.status = DONE;
            dst->syscall.sender = sender->pid;
            /* Copy the system call arguments within the kernel PCB. */
            memcpy(dst->syscall.content, sender->syscall.content,
                   SYSCALL_MSG_LEN);
            return;
        }
    }
    FATAL("proc_try_send: unknown receiver pid=%d", sender->syscall.receiver);
}

static void proc_try_recv(struct process* receiver) {
    if (receiver->syscall.status == PENDING) return;

    /* Copy the system call struct from the kernel back to user space. */
    uint syscall_paddr = earth->mmu_translate(receiver->pid, SYSCALL_ARG);
    memcpy((void*)syscall_paddr, &receiver->syscall, sizeof(struct syscall));

    /* Set the receiver and sender back to RUNNABLE. */
    proc_set_runnable(receiver->pid);
    proc_set_runnable(receiver->syscall.sender);
}

static void proc_try_syscall(struct process* proc) {
    switch (proc->syscall.type) {
    case SYS_RECV:
        proc_try_recv(proc);
        break;
    case SYS_SEND:
        proc_try_send(proc);
        break;
    default:
        FATAL("proc_try_syscall: unknown syscall type=%d", proc->syscall.type);
    }
}
