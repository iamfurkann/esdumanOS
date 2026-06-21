#include "kernel.h"
#include "isr.h"

#define MAX_TASKS 16

typedef enum { TASK_EMPTY, TASK_RUNNING, TASK_DEAD } task_state_t;

typedef struct {
    int pid;
    task_state_t state;
    registers_t regs;
} process_t;

process_t tasks[MAX_TASKS];
int current_task = -1;
int multitasking_enabled = 0;
int next_pid = 0;

void init_multitasking(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_EMPTY;
    }
    multitasking_enabled = 1;
}

int create_process(uint32_t eip, uint32_t esp) {
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_EMPTY || tasks[i].state == TASK_DEAD) break;
    }
    if (i == MAX_TASKS) return -1;

    tasks[i].pid = next_pid++;
    tasks[i].state = TASK_RUNNING;
    
    /* İçini tamamen sıfırla */
    uint8_t *ptr = (uint8_t *)&tasks[i].regs;
    for (uint32_t j = 0; j < sizeof(registers_t); j++) ptr[j] = 0;

    /* Donanımsal Ring 3 Segment Standartları */
    tasks[i].regs.ds = 0x2B;
    tasks[i].regs.cs = 0x23;
    tasks[i].regs.ss = 0x2B;
    
    tasks[i].regs.eip = eip;         
    tasks[i].regs.useresp = esp;     
    tasks[i].regs.eflags = 0x202;    /* Kesmeler açık olsun */

    /* KESİNTİ NUMARALARI: Scheduler ilk devraldığında işlemci sapıtmasın diye 
     * sahte bir IRQ0 (32) kesme verisi enjekte ediyoruz */
    tasks[i].regs.int_no = 32;
    tasks[i].regs.err_code = 0;

    if (current_task == -1) current_task = i;

    return tasks[i].pid;
}

void exit_current_process(void) {
    tasks[current_task].state = TASK_DEAD;
    asm volatile("int $32"); 
}

void schedule(registers_t *regs) {
    if (!multitasking_enabled || current_task == -1) return;

    /* EĞER User Mode'dan (Ring 3) kesintiye uğradıysak, mevcut durumu kaydet.
       Eğer kernel'dayken (ilk başlangıçtaki hlt anı) kesinti geldiyse,
       kayıtçıları EZMEMEK için sadece Ring 3 kontrolü yapıyoruz! */
    if ((regs->cs & 0x3) == 3) {
        if (tasks[current_task].state == TASK_RUNNING) {
            tasks[current_task].regs = *regs;
        }
    }

    int next_task = current_task;
    int found = 0;
    
    /* Sıradaki çalışan (RUNNING) görevi bul */
    for (int i = 0; i < MAX_TASKS; i++) {
        next_task = (next_task + 1) % MAX_TASKS;
        if (tasks[next_task].state == TASK_RUNNING) {
            found = 1;
            break;
        }
    }

    /* Eğer görev bulunduysa (Process 0 / Shell), kayıtçıları işlemciye yükle */
    if (found) {
        current_task = next_task;
        *regs = tasks[current_task].regs;
        
        /* MİMARİ DOKUNUŞ: Görev değiştirirken Interrupt Enable (IF) bitini zorla AÇIK tut. */
        regs->eflags |= 0x200; 
    }
}