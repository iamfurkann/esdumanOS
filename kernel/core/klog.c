/*
 * File: klog.c
 * Purpose: Kernel logging system with multi-level support.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "klog.h"
#include "kernel.h"
#include "stdio.h"
#include "tty.h"

int current_log_level = LOG_LEVEL_INFO;

/**
 * @brief klog_itoa
 * @param n
 * @param buf
 */
static void klog_itoa(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0, is_neg = 0;
    if (n < 0) { is_neg = 1; n = -n; }
    while (n > 0) { buf[i++] = (n % 10) + '0'; n /= 10; }
    if (is_neg) buf[i++] = '-';
    buf[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
}

/**
 * @brief klog_itoa_hex
 * @param n
 * @param buf
 */
static void klog_itoa_hex(uint32_t n, char *buf) {
    static const char hex_chars[] = "0123456789ABCDEF";
    if (n == 0) { 
        buf[0] = '0'; 
        buf[1] = '\0'; 
        return; 
    }
    
    char temp[9];
    int i = 0;

    while (n > 0) {
        temp[i++] = hex_chars[n & 0xF];
        n >>= 4;
    }

    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    
    buf[j] = '\0';
}

static const char* level_strings[] = {
    "[DEBUG]",
    "[INFO ]",
    "[WARN ]",
    "[ERROR]",
    "[FATAL]"
};

static uint8_t level_colors[] = {
    VGA_COLOR_LIGHT_GREY, // DEBUG
    VGA_COLOR_LIGHT_CYAN, // INFO
    VGA_COLOR_BROWN,      // WARN
    VGA_COLOR_LIGHT_RED,  // ERROR
    VGA_COLOR_WHITE       // FATAL/CRITICAL
};

static uint8_t level_bg_colors[] = {
    VGA_COLOR_BLACK, VGA_COLOR_BLACK, VGA_COLOR_BLACK, VGA_COLOR_BLACK, VGA_COLOR_RED
};

/*
 * Every level string is exactly this long, which is what lets the coloured
 * prefix be printed separately from the rest of an already-composed line.
 */
#define KLOG_PREFIX_LEN 7
#define KLOG_LINE_MAX 192

/**
 * @brief Appends a string to a line under construction, bounded.
 */
static void klog_append(char *line, uint32_t *pos, const char *src) {
    while (src && *src && *pos < KLOG_LINE_MAX - 2) line[(*pos)++] = *src++;
}

/**
 * @brief Composes one log line, records it, and prints it.
 *
 * The recording used to be printk's job: it fed every character the kernel
 * printed into the log buffer, so the boot banner, the ASCII art and the
 * first-boot password prompts all competed for the 8 KB with actual records.
 * A log is a record of events, not a transcript of the screen. printk no longer
 * feeds it and this does, one composed line at a time.
 *
 * The value that klog_int() and klog_hex() carry arrives as a tail on this same
 * line. Both used to call klog() and then print the value afterwards, and klog()
 * had already ended the line - so every value in the log sat orphaned on a line
 * of its own beneath its message.
 *
 * @param level Severity; clamped into range.
 * @param module Subsystem name.
 * @param message The text.
 * @param tail Appended after a space when non-empty; may be null.
 */
static void klog_emit(int level, const char *module, const char *message,
                      const char *tail, int to_screen) {
    if (level < current_log_level) return;
    if (level < 0) level = 0;
    if (level > 4) level = 4;

    char line[KLOG_LINE_MAX];
    uint32_t p = 0;

    klog_append(line, &p, level_strings[level]);
    klog_append(line, &p, " ");
    klog_append(line, &p, module);
    klog_append(line, &p, ": ");
    klog_append(line, &p, message);

    if (tail && tail[0]) {
        klog_append(line, &p, " ");
        klog_append(line, &p, tail);
    }

    line[p++] = '\n';
    line[p] = '\0';

    for (uint32_t i = 0; i < p; i++) klog_write_char(line[i]);

    if (!to_screen) return;

    /*
     * The screen and the serial port, through printk so both still receive it.
     * "%s" rather than passing the text as the format: a message containing a
     * '%' was previously read as a conversion, which is a way to print whatever
     * happened to be next on the stack.
     */
    terminal_setcolor(level_colors[level], level_bg_colors[level]);
    printk("%s", level_strings[level]);
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("%s", &line[KLOG_PREFIX_LEN]);
}

/**
 * @brief Records a line in the log without printing it.
 *
 * For an event that already has a rendering on screen. The boot milestones are
 * the case this exists for: they appear as a green "[OK] subsystem up" line,
 * which is boot UI, and they also belong in dmesg, which is a record. Logging
 * them through klog() would print them a second time in a different shape.
 *
 * This is the entry point the split between the two makes necessary - once the
 * log stops being a transcript of the screen, some events need to reach one and
 * not the other.
 *
 * @param level Severity.
 * @param module Subsystem name.
 * @param message The text.
 */
void klog_record(int level, const char *module, const char *message) {
    klog_emit(level, module, message, 0, 0);
}

/**
 * @brief klog
 * @param level
 * @param module
 * @param message
 */
void klog(int level, const char *module, const char *message) {
    klog_emit(level, module, message, 0, 1);
}

/**
 * @brief klog_int
 * @param level
 * @param module
 * @param message
 * @param val
 */
void klog_int(int level, const char *module, const char *message, int val) {
    if (level < current_log_level) return;

    char num_str[16];
    klog_itoa(val, num_str);
    klog_emit(level, module, message, num_str, 1);
}

/*
 * A ring, at last.
 *
 * This was a fill-once buffer that stopped accepting at 8191 and silently
 * dropped everything after - so dmesg showed the oldest 8 KB of the boot and
 * nothing that had happened since, while both this file and the README called it
 * a ring buffer. A log that discards the newest records is the opposite of a log.
 *
 * dmesg_written counts every byte ever handed in and never wraps; the buffer
 * index is derived from it. Keeping the count rather than a head index is what
 * makes "how much is available" a subtraction instead of a flag-and-index pair
 * that has to stay in step.
 */
static char dmesg_buffer[KLOG_BUF_SIZE];
static uint32_t dmesg_written = 0;

/**
 * @brief Bytes currently held, which is everything until the ring first wraps.
 */
static uint32_t klog_available(void) {
    return (dmesg_written < KLOG_BUF_SIZE) ? dmesg_written : KLOG_BUF_SIZE;
}

/**
 * @brief klog_write_char
 * @param c
 */
void klog_write_char(char c) {
    dmesg_buffer[dmesg_written % KLOG_BUF_SIZE] = c;
    dmesg_written++;
}

/**
 * @brief dump_klog
 */
void dump_klog(void) {
    uint32_t avail = klog_available();
    uint32_t start = dmesg_written - avail;

    for (uint32_t i = 0; i < avail; i++) {
        terminal_putchar(dmesg_buffer[(start + i) % KLOG_BUF_SIZE]);
    }
}

/**
 * @brief Copies a slice of the log into a caller-supplied buffer.
 *
 * dump_klog() writes to the screen with terminal_putchar(), so its output never
 * reaches the calling process's descriptor 1 - "dmesg | head" fed an empty pipe
 * and "dmesg > file" wrote nothing. Handing the bytes back instead lets the
 * caller write them itself, through whatever its descriptor 1 happens to be.
 *
 * Handing them back rather than writing them from in here is the whole point.
 * A write into a full pipe blocks, and a blocked syscall resumes by re-running
 * from the int 0x80 - so a kernel-side dump that blocked halfway would start
 * over and emit everything twice. A caller reading a slice at a time keeps the
 * position in its own loop, and each write it issues is a syscall of its own
 * that can block and restart harmlessly.
 *
 * @param dst    Kernel buffer receiving the bytes.
 * @param max    Capacity of dst.
 * @param offset Byte position in the log to start from.
 * @return Bytes copied; 0 once offset reaches the end of the log.
 */
int klog_read(char *dst, int max, int offset) {
    if (dst == 0 || max <= 0 || offset < 0) return 0;

    uint32_t avail = klog_available();
    if ((uint32_t)offset >= avail) return 0;

    /* The oldest byte still held, in the same units offset counts in. */
    uint32_t start = dmesg_written - avail;

    uint32_t n = avail - (uint32_t)offset;
    if (n > (uint32_t)max) n = (uint32_t)max;

    for (uint32_t i = 0; i < n; i++) {
        dst[i] = dmesg_buffer[(start + (uint32_t)offset + i) % KLOG_BUF_SIZE];
    }
    return (int)n;
}

/**
 * @brief klog_hex
 * @param level
 * @param module
 * @param message
 * @param val
 */
void klog_hex(int level, const char *module, const char *message, uint32_t val) {
    if (level < current_log_level) return;

    char hex_str[20];
    hex_str[0] = '0';
    hex_str[1] = 'x';
    klog_itoa_hex(val, &hex_str[2]);

    klog_emit(level, module, message, hex_str, 1);
}
/**
 * @brief Writes the log out to /var/log/kern.log.
 *
 * /var/log has existed since the FHS hierarchy was created and has been empty
 * ever since; the log lived in RAM and went with the machine.
 *
 * The whole file is replaced rather than appended to, because the format cannot
 * append: a stored file is one AES-CBC blob authenticated over its entire
 * plaintext, so adding a line means rewriting all of it. That is also why this
 * is called at checkpoints - sync, halt and reboot - rather than per record.
 *
 * The ring is snapshotted into a contiguous buffer first. It is not contiguous
 * once it has wrapped, and the write path takes one buffer; the snapshot also
 * means the records this write itself produces do not chase their own tail into
 * the file.
 *
 * @return E_OK, or a negative errno. Failure is reported and not fatal - a
 *         machine that will not halt because it could not save its log would be
 *         a worse bargain than a lost log.
 */
int klog_persist(void) {
    uint32_t avail = klog_available();
    if (avail == 0) return E_OK;

    char *staging = (char *)kmalloc(avail);
    if (staging == 0) return E_NOMEM;

    uint32_t got = 0;
    int chunk;
    while (got < avail &&
           (chunk = klog_read(&staging[got], (int)(avail - got), (int)got)) > 0) {
        got += (uint32_t)chunk;
    }

    int var_id = fs_get_entry_idx("var", 0);
    int log_id = (var_id >= 0) ? fs_get_entry_idx("log", (uint8_t)var_id) : -1;

    if (log_id < 0) {
        kfree(staging);
        klog(LOG_LEVEL_WARN, "KLOG", "/var/log is missing; the log was not written out.");
        return E_NOENT;
    }

    int rc;
    if (fs_get_entry_idx("kern.log", (uint8_t)log_id) >= 0) {
        rc = fs_atomic_update("kern.log", (const uint8_t *)staging, got, (uint8_t)log_id);
    } else {
        rc = fs_create_file("kern.log", (const uint8_t *)staging, got, (uint8_t)log_id);
    }

    kfree(staging);

    /*
     * Say so when it fails. This returned an errno that all three callers
     * dropped, so a checkpoint that could not write left no trace anywhere - the
     * file simply was not there, with nothing to say why. The record lands in
     * the ring rather than in this write, which has already been snapshotted, so
     * the next one carries it.
     */
    if (rc != E_OK) {
        klog_int(LOG_LEVEL_WARN, "KLOG", "Writing /var/log/kern.log failed with errno", rc);
    }

    return rc;
}
