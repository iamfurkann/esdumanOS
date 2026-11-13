/* Compile the real RTC driver against a deterministic CMOS/IRQ model. */
#include "types.h"
#include "rtc.h"
#include "errno.h"

extern int printf(const char *format, ...);
int printk(const char *format, ...) { (void)format; return 0; }
void draw_status_bar(const char *left, const char *right) {
    (void)left; (void)right;
}

static uint8_t registers[128], selected;
static int irq_enabled = 1, inject_irq, in_irq, delivered, failures;
uint8_t get_RTC_register(int reg);

uint32_t rtc_test_irq_save(void) {
    uint32_t flags = irq_enabled ? 0x200u : 0;
    irq_enabled = 0;
    return flags;
}
void rtc_test_irq_restore(uint32_t flags) {
    irq_enabled = (flags & 0x200u) != 0;
}

static void outb(uint16_t port, uint8_t value) {
    if (port == 0x70) {
        selected = value & 0x7f;
        /* IRQ0 reads RTC registers for the status bar. Deliver it precisely
         * between index selection and the caller's data-port access. */
        if (inject_irq && irq_enabled && !in_irq) {
            in_irq = 1;
            irq_enabled = 0;
            (void)get_RTC_register(0x00);
            delivered++;
            irq_enabled = 1;
            in_irq = 0;
        }
    } else if (port == 0x71) {
        registers[selected] = value;
    }
}
static uint8_t inb(uint16_t port) {
    return port == 0x71 ? registers[selected] : 0;
}

/* Only the privileged port/interrupt primitives and console are replaced.
 * All encoding, register selection and clock-setting code is the driver. */
#define KERNEL_H
#define RTC_HOST_TEST
#define OS_STATUS_LABEL "rtc-host-test"
#include "../../../drivers/rtc.c"

#define CHECK(condition, description) do { \
    if (!(condition)) { printf("FAIL: %s\n", description); failures++; } \
} while (0)

static void reset_clock(uint8_t mode) {
    for (unsigned i = 0; i < sizeof(registers); i++) registers[i] = 0;
    registers[0x0b] = mode;
    selected = 0;
    irq_enabled = 1;
    inject_irq = 0;
    in_irq = 0;
}

int main(void) {
    esd_time_t want = { .year = 2030, .month = 6, .day = 15,
                       .hour = 13, .minute = 45, .second = 30 };
    esd_time_t got;
    reset_clock(0x02);
    registers[0x00] = 0x30;
    registers[0x02] = 0x45;
    inject_irq = 1;
    CHECK(get_RTC_register(0x02) == 0x45, "an IRQ cannot redirect a CMOS read");
    CHECK(irq_enabled == 1, "a read restores enabled interrupts");
    CHECK(rtc_set_utc(&want) == E_OK, "valid time is accepted with IRQ injection");
    inject_irq = 0;
    rtc_read_utc(&got);
    CHECK(got.year == want.year && got.month == want.month && got.day == want.day &&
          got.hour == want.hour && got.minute == want.minute && got.second == want.second,
          "an IRQ cannot redirect a CMOS write");
    CHECK(irq_enabled == 1, "a clock write restores enabled interrupts");

    irq_enabled = 0;
    inject_irq = 1;
    CHECK(rtc_set_utc(&want) == E_OK, "clock writes work with interrupts already masked");
    (void)get_RTC_register(0x02);
    CHECK(irq_enabled == 0, "nested CMOS access never enables masked interrupts");

    for (uint8_t mode = 0; mode <= 6; mode += 2) {
        reset_clock(mode);
        for (unsigned hour = 0; hour < 24; hour++) {
            want.hour = (uint8_t)hour;
            CHECK(rtc_set_utc(&want) == E_OK, "each hour can be written in every RTC mode");
            rtc_read_utc(&got);
            CHECK(got.hour == want.hour && got.minute == want.minute && got.second == want.second,
                  "12/24-hour BCD/binary round trip preserves midnight and noon");
            CHECK(registers[0x0b] == mode, "clock setting restores the original register B");
        }
    }
    printf("RTC deterministic model: %d failures; %d injected interrupts delivered.\n",
           failures, delivered);
    return failures != 0;
}
