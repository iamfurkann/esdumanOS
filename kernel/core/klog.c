/*
 * File: klog.c
 * Purpose: Kernel logging system with multi-level support.
 *
 * The log was a byte ring holding composed text lines. v0.6.1 made it wrap and
 * stopped printk() feeding it, so it became a record of events rather than a
 * transcript of the screen - but a record was still just a string, and every
 * question about one was a question about parsing. When did this happen. How
 * many did we lose when it wrapped. Show me only the errors. None of them could
 * be answered, so none of them were asked.
 *
 * The ring holds records now. A record carries its own timestamp, its own
 * sequence number and its level as a field rather than as the first seven
 * characters of its text, which is what makes all three questions answerable and
 * what /dev/kmsg needs in order to hand out anything better than a line of text.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "klog.h"
#include "kernel.h"
#include "stdio.h"
#include "tty.h"
#include "rtc.h"

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
 * The ring.
 *
 * klog_seq_next is the number the next record will be given and never wraps; a
 * slot is derived from it, so "how many are held" is a subtraction rather than a
 * head index and a full flag that have to stay in step. Sequence numbers start
 * at 1 so that 0 can mean "no such record" everywhere.
 *
 * klog_floor_seq is what klog_clear() moves. Clearing does not zero the slots or
 * reset the numbering: a reader holding a cursor from before the clear has to be
 * able to tell that what it pointed at is gone, rather than be handed a
 * different record wearing the same number.
 */
static klog_record_t klog_ring[KLOG_RECORDS];
static uint32_t klog_seq_next = 1;
static uint32_t klog_floor_seq = 1;

/**
 * @brief The slot a sequence number lives in.
 */
static klog_record_t *klog_slot_for(uint32_t seq) {
    return &klog_ring[(seq - 1u) % KLOG_RECORDS];
}

/**
 * @brief Oldest sequence a reader may still see, whichever limit binds first.
 */
static uint32_t klog_visible_floor(void) {
    uint32_t wrapped = (klog_seq_next > KLOG_RECORDS)
                     ? (klog_seq_next - KLOG_RECORDS) : 1u;
    return (klog_floor_seq > wrapped) ? klog_floor_seq : wrapped;
}

uint32_t klog_held(void) {
    return klog_seq_next - klog_visible_floor();
}

uint32_t klog_oldest_seq(void) {
    return (klog_held() == 0) ? 0u : klog_visible_floor();
}

uint32_t klog_next_seq(void) {
    return klog_seq_next;
}

uint32_t klog_dropped(void) {
    uint32_t total = klog_seq_next - 1u;
    return (total > KLOG_RECORDS) ? (total - KLOG_RECORDS) : 0u;
}

const klog_record_t *klog_by_seq(uint32_t seq) {
    if (seq == 0 || seq >= klog_seq_next) return 0;
    if (seq < klog_visible_floor()) return 0;

    klog_record_t *rec = klog_slot_for(seq);
    /* The slot is reused every KLOG_RECORDS records, so confirm it still holds
     * the one that was asked for rather than its successor. */
    return (rec->seq == seq) ? rec : 0;
}

void klog_clear(void) {
    klog_floor_seq = klog_seq_next;
}

void klog_set_level(int level) {
    if (level < LOG_LEVEL_DEBUG) level = LOG_LEVEL_DEBUG;
    if (level > LOG_LEVEL_CRITICAL) level = LOG_LEVEL_CRITICAL;
    current_log_level = level;
}

int klog_get_level(void) {
    return current_log_level;
}

/**
 * @brief Copies a string into a fixed field, always terminated.
 */
static void klog_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    while (src && src[i] && i < cap - 1u) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/**
 * @brief Appends a string to a line under construction, bounded.
 */
static void klog_append(char *line, uint32_t *pos, uint32_t cap, const char *src) {
    while (src && *src && *pos < cap - 1u) line[(*pos)++] = *src++;
}

/**
 * @brief Appends a decimal number, right-aligned in a minimum width.
 */
static void klog_append_num(char *line, uint32_t *pos, uint32_t cap,
                            uint32_t value, uint32_t width) {
    char digits[12];
    uint32_t n = 0;

    if (value == 0) digits[n++] = '0';
    while (value > 0) { digits[n++] = (char)('0' + (value % 10u)); value /= 10u; }

    while (n < width && *pos < cap - 1u) { line[(*pos)++] = ' '; width--; }
    while (n > 0 && *pos < cap - 1u) line[(*pos)++] = digits[--n];
}

/**
 * @brief Writes a record's monotonic timestamp into a buffer.
 *
 * Hundredths, because TIMER_HZ is 100. Linux prints six decimals; printing six
 * here would be printing precision this clock does not have. The fraction is
 * derived rather than assumed, so a different TIMER_HZ still renders correctly.
 *
 * @param rec Record whose tick count to render.
 * @param dst Destination buffer.
 * @param cap Capacity of @p dst.
 * @return Bytes written, not counting the terminator.
 */
static uint32_t klog_stamp(const klog_record_t *rec, char *dst, uint32_t cap) {
    uint32_t secs = rec->ticks / TIMER_HZ;
    uint32_t frac = ((rec->ticks % TIMER_HZ) * 100u) / TIMER_HZ;
    uint32_t p = 0;

    if (p < cap - 1u) dst[p++] = '[';
    klog_append_num(dst, &p, cap, secs, 5);
    if (p < cap - 1u) dst[p++] = '.';
    if (frac < 10u && p < cap - 1u) dst[p++] = '0';
    klog_append_num(dst, &p, cap, frac, 0);
    if (p < cap - 1u) dst[p++] = ']';

    dst[p] = '\0';
    return p;
}

/**
 * @brief Stores one record in the ring.
 *
 * @param level     Severity, already clamped.
 * @param module    Subsystem name.
 * @param message   The text.
 * @param tail      Appended after a space when non-empty; may be null.
 * @param uid       Author for a user record; 0 for the kernel.
 * @param from_user Non-zero when it arrived through /dev/kmsg.
 */
static void klog_store(int level, const char *module, const char *message,
                       const char *tail, uint32_t uid, uint8_t from_user) {
    klog_record_t *rec = klog_slot_for(klog_seq_next);

    rec->seq = klog_seq_next;
    rec->ticks = timer_get_ticks();
    rec->level = (uint8_t)level;
    rec->uid = uid;
    rec->from_user = from_user;
    rec->reserved = 0;

    klog_copy(rec->module, module ? module : "", KLOG_MODULE_MAX);
    klog_copy(rec->text, message ? message : "", KLOG_TEXT_MAX);

    /*
     * The value klog_int() and klog_hex() carry is part of the same text. Both
     * used to print it after klog() had already ended the line, so every value
     * in the log sat orphaned beneath the message it belonged to.
     */
    if (tail && tail[0]) {
        uint32_t p = 0;
        while (rec->text[p] && p < KLOG_TEXT_MAX - 1u) p++;
        if (p < KLOG_TEXT_MAX - 2u) {
            rec->text[p++] = ' ';
            uint32_t i = 0;
            while (tail[i] && p < KLOG_TEXT_MAX - 1u) rec->text[p++] = tail[i++];
            rec->text[p] = '\0';
        }
    }

    uint32_t len = 0;
    while (rec->text[len]) len++;
    rec->text_len = (uint8_t)len;

    klog_seq_next++;
}

/**
 * @brief Prints a record to the screen, level prefix coloured.
 */
static void klog_print(const klog_record_t *rec) {
    char stamp[24];

    klog_stamp(rec, stamp, (uint32_t)sizeof(stamp));

    terminal_setcolor(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    printk("%s", stamp);

    terminal_setcolor(level_colors[rec->level], level_bg_colors[rec->level]);
    printk(" %s", level_strings[rec->level]);

    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    /*
     * "%s" rather than passing the text as the format: a message containing a
     * '%' was previously read as a conversion, which is a way to print whatever
     * happened to be next on the stack.
     */
    printk(" %s: %s\n", rec->module, rec->text);
}

/**
 * @brief Records an event and optionally prints it.
 *
 * @param level Severity; clamped into range.
 * @param module Subsystem name.
 * @param message The text.
 * @param tail Appended after a space when non-empty; may be null.
 * @param to_screen Non-zero to also print it.
 */
static void klog_emit(int level, const char *module, const char *message,
                      const char *tail, int to_screen) {
    if (level < current_log_level) return;
    if (level < 0) level = 0;
    if (level > LOG_LEVEL_CRITICAL) level = LOG_LEVEL_CRITICAL;

    klog_store(level, module, message, tail, 0, 0);

    if (to_screen) klog_print(klog_slot_for(klog_seq_next - 1u));
}

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

/*
 * Rate limit for records that did not come from the kernel.
 *
 * A fixed window rather than a token bucket: the point is to stop a program in a
 * loop from pushing every diagnostic record out of the ring, and for that a
 * counter and a deadline say everything a bucket would.
 *
 * Kernel records are deliberately not limited. A storm of errors is exactly when
 * the records matter, and suppressing them to protect the ring would throw away
 * the evidence to preserve the container. klog_dropped() already makes the loss
 * visible, which is the honest answer.
 */
#define KLOG_USER_BURST  32
#define KLOG_USER_WINDOW TIMER_HZ

static uint32_t klog_user_window_end = 0;
static uint32_t klog_user_in_window = 0;

int klog_user_record(int level, const char *text, uint32_t uid) {
    if (text == 0) return E_INVAL;

    if (level < LOG_LEVEL_DEBUG) level = LOG_LEVEL_DEBUG;
    if (level > LOG_LEVEL_CRITICAL) level = LOG_LEVEL_CRITICAL;

    uint32_t now = timer_get_ticks();

    if (now >= klog_user_window_end) {
        klog_user_window_end = now + KLOG_USER_WINDOW;
        klog_user_in_window = 0;
    }

    if (klog_user_in_window >= KLOG_USER_BURST) return E_AGAIN;
    klog_user_in_window++;

    klog_store(level, "USER", text, 0, uid, 1);
    return E_OK;
}

int klog_format(const klog_record_t *rec, char *dst, int max) {
    if (rec == 0 || dst == 0 || max <= 1) return 0;

    uint32_t cap = (uint32_t)max;
    uint32_t p = klog_stamp(rec, dst, cap);

    if (p < cap - 1u) dst[p++] = ' ';

    klog_append(dst, &p, cap, level_strings[rec->level]);
    klog_append(dst, &p, cap, " ");
    klog_append(dst, &p, cap, rec->module);
    klog_append(dst, &p, cap, ": ");
    klog_append(dst, &p, cap, rec->text);

    if (p < cap - 1u) dst[p++] = '\n';
    dst[p] = '\0';

    return (int)p;
}

/**
 * @brief Renders one record in the /dev/kmsg form.
 *
 * `level,seq,ticks,flag;module: text`, which is Linux's shape - the machine
 * readable fields ahead of the semicolon, the human text after it. A reader that
 * wants only errors reads the first field instead of parsing a prefix out of the
 * message, which is the whole reason the ring holds records.
 *
 * The flag is `u` for a record a program wrote through /dev/kmsg and `-` for one
 * the kernel produced. A log that cannot distinguish those is a log a program
 * can put words into the kernel's mouth in.
 *
 * @param rec Record to render.
 * @param dst Destination buffer.
 * @param max Capacity of @p dst.
 * @return Bytes written, not counting the terminator.
 */
int klog_format_kmsg(const klog_record_t *rec, char *dst, int max) {
    if (rec == 0 || dst == 0 || max <= 1) return 0;

    uint32_t cap = (uint32_t)max;
    uint32_t p = 0;

    klog_append_num(dst, &p, cap, rec->level, 0);
    if (p < cap - 1u) dst[p++] = ',';
    klog_append_num(dst, &p, cap, rec->seq, 0);
    if (p < cap - 1u) dst[p++] = ',';
    klog_append_num(dst, &p, cap, rec->ticks, 0);
    if (p < cap - 1u) dst[p++] = ',';
    if (p < cap - 1u) dst[p++] = rec->from_user ? 'u' : '-';
    if (p < cap - 1u) dst[p++] = ';';

    klog_append(dst, &p, cap, rec->module);
    klog_append(dst, &p, cap, ": ");
    klog_append(dst, &p, cap, rec->text);

    if (p < cap - 1u) dst[p++] = '\n';
    dst[p] = '\0';

    return (int)p;
}

int klog_read(char *dst, int max, int index) {
    if (dst == 0 || max <= 0 || index < 0) return 0;

    uint32_t held = klog_held();
    if ((uint32_t)index >= held) return 0;

    const klog_record_t *rec = klog_by_seq(klog_visible_floor() + (uint32_t)index);
    if (rec == 0) return 0;

    return klog_format(rec, dst, max);
}

void dump_klog(void) {
    uint32_t floor = klog_visible_floor();
    uint32_t held = klog_held();
    char line[KLOG_LINE_MAX];

    for (uint32_t i = 0; i < held; i++) {
        const klog_record_t *rec = klog_by_seq(floor + i);
        if (rec == 0) continue;

        klog_format(rec, line, (int)sizeof(line));
        for (uint32_t j = 0; line[j]; j++) terminal_putchar(line[j]);
    }
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
 * The records are rendered into a contiguous buffer first. The ring is not
 * contiguous once it has wrapped, the write path takes one buffer, and the
 * snapshot also means the records this write itself produces do not chase their
 * own tail into the file.
 *
 * @return E_OK, or a negative errno. Failure is reported and not fatal - a
 *         machine that will not halt because it could not save its log would be
 *         a worse bargain than a lost log.
 */
int klog_persist(void) {
    uint32_t held = klog_held();
    if (held == 0) return E_OK;

    /*
     * Worst case, every record rendering to a full line. Real records are far
     * shorter, but sizing from the actual total would mean rendering everything
     * twice - and this runs at sync, halt and reboot, where a transient
     * allocation costs nothing anyone is waiting on.
     */
    uint32_t capacity = held * KLOG_LINE_MAX;
    char *staging = (char *)kmalloc(capacity);
    if (staging == 0) return E_NOMEM;

    uint32_t floor = klog_visible_floor();
    uint32_t got = 0;

    for (uint32_t i = 0; i < held; i++) {
        const klog_record_t *rec = klog_by_seq(floor + i);
        if (rec == 0) continue;

        int n = klog_format(rec, &staging[got], (int)(capacity - got));
        if (n <= 0) break;
        got += (uint32_t)n;
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
     * the ring rather than in this write, which has already been rendered, so
     * the next one carries it.
     */
    if (rc != E_OK) {
        klog_int(LOG_LEVEL_WARN, "KLOG", "Writing /var/log/kern.log failed with errno", rc);
    }

    return rc;
}
