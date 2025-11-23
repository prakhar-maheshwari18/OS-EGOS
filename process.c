/*
 * (C) 2025, Cornell University
 * All rights reserved.
 *
 * Description: helper functions for process management
 */

#include "process.h"
#include <stdlib.h>

#define MLFQ_RESET_PERIOD     100000000         /* 10 seconds */
#define MLFQ_LEVEL_RUNTIME(x) (x + 1) * 100000 /* e.g., 100ms for level 0 */
extern struct process proc_set[MAX_NPROCESS + 1];

static void proc_set_status(int pid, enum proc_status status) {
    for (uint i = 0; i < MAX_NPROCESS; i++)
        if (proc_set[i].pid == pid) proc_set[i].status = status;
}

void proc_set_ready(int pid) { proc_set_status(pid, PROC_READY); }
void proc_set_running(int pid) { proc_set_status(pid, PROC_RUNNING); }
void proc_set_runnable(int pid) { proc_set_status(pid, PROC_RUNNABLE); }
void proc_set_pending(int pid) { proc_set_status(pid, PROC_PENDING_SYSCALL); }

// Initially, we loop through the PCB to find a process that isnt running and set an id for it. Later we initialize all the variables that we created in the struct in process.h
/* In grass/process.c */
int proc_alloc() {
    static uint curr_pid = 0;
    
    //Find a Free Spot
    for (uint i = 1; i <= MAX_NPROCESS; i++)
        if (proc_set[i].status == PROC_UNUSED) {
            proc_set[i].pid    = ++curr_pid;
            proc_set[i].status = PROC_LOADING;

            // We set up the tracking variables so we can measure performance later.
            proc_set[i].arrive_time = mtime_get();          // Record the exact time of birth
            proc_set[i].start_time = 0;                     // It hasn't started running yet
            proc_set[i].cpu_time = 0;                       // Total time used on CPU is 0
            proc_set[i].n_interrupts = 0;                   // It hasn't been interrupted yet

            //Initialize MLFQ Priority
            proc_set[i].priority = 0;                       
            proc_set[i].current_priority_time = 0;          // Reset the timer for this level

            return curr_pid;
        }

    FATAL("proc_alloc: reach the limit of %d processes", MAX_NPROCESS);
}

void proc_free(int pid) {
    ulonglong current_time = mtime_get();

    //Killing a Single Specific Process
    if (pid != GPID_ALL) {

        struct process *p = NULL;
        // Find the specific process in our list
        for (int i = 0; i < MAX_NPROCESS; i++) {
            if (proc_set[i].pid == pid) {
                p = &proc_set[i];
                break;
            }
        }

        //Calculate and Print the Final Score
        if (p) {
            // Turnaround = Time it died - Time it was born
            ulonglong turnaround = current_time - p->arrive_time;
            // Response = Time it first ran - Time it was born
            ulonglong response = (p->start_time > 0) ? (p->start_time - p->arrive_time) : 0;
            
            printf("[INFO] Process %d Finished:\n", pid);
            printf("  Turnaround Time: %d us\n", (int)turnaround);
            printf("  Response Time:   %d us\n", (int)response);
            printf("  CPU Time:        %d us\n", (int)p->cpu_time); // Actual work done
            printf("  Interrupts:      %d\n",    p->n_interrupts);
        }

        // Free the memory and mark the slot as empty
        earth->mmu_free(pid);
        proc_set_status(pid, PROC_UNUSED);

    } else {
        //Killing ALL User Processes (System Shutdown)
        for (uint i = 0; i < MAX_NPROCESS; i++) {
            // Check if it is a user app and is currently active
            if (proc_set[i].pid >= GPID_USER_START &&
                proc_set[i].status != PROC_UNUSED) {
                
                // Perform the same calculations and printing as above for every process
                struct process *p = &proc_set[i];
                ulonglong turnaround = current_time - p->arrive_time;
                ulonglong response = (p->start_time > 0) ? (p->start_time - p->arrive_time) : 0;

                printf("[INFO] Process %d Finished:\n", p->pid);
                printf("  Turnaround Time: %d us\n", (int)turnaround);
                printf("  Response Time:   %d us\n", (int)response);
                printf("  CPU Time:        %d us\n", (int)p->cpu_time);
                printf("  Interrupts:      %d\n",    p->n_interrupts);

                earth->mmu_free(proc_set[i].pid);
                proc_set[i].status = PROC_UNUSED;
            }
        }
    }
}

/* In grass/process.c */

void mlfq_update_level(struct process* p, ulonglong runtime) {
    //Update Usage Stats
    p->cpu_time += runtime;
    p->current_priority_time += runtime;
    
    //Get the allowed time limit for the current level
    ulonglong threshold = MLFQ_LEVEL_RUNTIME(p->priority);

    // If the process has used up all its allowed time at this level
    if (p->current_priority_time >= threshold) {
        // Move it down one level, unless it's already at the bottom.
        if (p->priority < MLFQ_NLEVELS - 1) {
            p->priority++;
        }
        // Reset the timer so it starts fresh at the new level
        p->current_priority_time = 0;
    }
}

/* In grass/process.c */

void mlfq_reset_level() {
    // If the user touched the keyboard, find the Shell process and make it VIP immediately.
    if (!earth->tty_input_empty()) {
        for (uint i = 0; i < MAX_NPROCESS; i++) {
            if (proc_set[i].pid == GPID_SHELL && proc_set[i].status != PROC_UNUSED) {
                proc_set[i].priority = 0;
                proc_set[i].current_priority_time = 0;
                break;  
            }
        }
    }
    
    //Global Priority Reset
    static ulonglong MLFQ_last_reset_time = 0;
    ulonglong current_time = mtime_get();

    // Every 10 seconds, we reset everyone.
    if (current_time - MLFQ_last_reset_time >= MLFQ_RESET_PERIOD) {
        INFO("RESET"); // Log that a reset happened
        
        // Loop through EVERY active process
        for (uint i = 0; i < MAX_NPROCESS; i++) {
            if (proc_set[i].status != PROC_UNUSED) {
                // Move them back to Level 0
                proc_set[i].priority = 0;
                proc_set[i].current_priority_time = 0;
            }
        }
        // Update the last reset time so we wait another 10 seconds
        MLFQ_last_reset_time = current_time;
    }
}

void proc_sleep(int pid, uint usec) {
    /* Student's code goes here (System Call & Protection). */

    /* Update the sleep-related fields in the struct process for process pid. */

    /* Student's code ends here. */
}

void proc_coresinfo() {
    /* Student's code goes here (Multicore & Locks). */

    /* Print out the pid of the process running on each CPU core. */

    /* Student's code ends here. */
}

