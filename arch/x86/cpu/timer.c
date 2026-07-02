#include "types.h"
#include "io.h"

#define PIT_CMD_PORT 0x43
#define PIT_CH0_PORT 0x40
#define PIT_CMD_INIT 0x36
#define PIT_BASE_FREQ 1193180
uint32_t timer_ticks = 0;
extern void rtc_timer_callback(void);

void timer_interrupt_handler(void) {
    timer_ticks++;
    rtc_timer_callback();
}

void init_timer(uint32_t freq) {
    uint32_t divisor = PIT_BASE_FREQ / freq;
    outb(PIT_CMD_PORT, PIT_CMD_INIT);

    uint8_t low = (uint8_t)(divisor & 0xFF);
    uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);

    outb(PIT_CH0_PORT, low);
    outb(PIT_CH0_PORT, high);
}