/**
 * @file edit.c
 * @brief A modal text editor - the first full-screen program in this system.
 *
 * v0.8.0 taught the terminal to take orders and said outright that nothing sent
 * it any. v0.8.2 gave the keyboard the keys to send. This is what both were for.
 *
 * Modal, in the shape vi has: a normal mode where keys are commands and an
 * insert mode where they are text. The shape is not nostalgia. This system has
 * no Alt and no function keys past F3, so a modeless editor would have to spend
 * Ctrl-letter combinations on its commands - and Ctrl-C, Ctrl-D and Ctrl-Z are
 * already spoken for by the terminal, which is exactly the set a nano-shaped
 * editor wants for quit, save and cut.
 *
 * Syscall numbers are written out as literals here, as in every other program in
 * /bin. include/signal.h cannot be included from user code - it reaches arch.h
 * through registers.h, and USER_CFLAGS carries no -DARCH_X86 - and
 * tests/host/c/test_elf_sast.c asserts that each program contains the literal
 * call text for the syscalls it is supposed to make.
 */
#include "editbuf.h"
#include "umalloc.h"

/** @brief The most a file can hold: what the VFS is willing to write back. */
#define EDIT_MAX_BYTES 65536

/** @brief Text rows on screen. Row 0 is the OS status bar, row 24 is ours. */
#define EDIT_ROWS 23
#define EDIT_COLS 80

/**
 * @brief Invokes a system call.
 * @param num System call number.
 * @param arg1 First argument.
 * @param arg2 Second argument.
 * @param arg3 Third argument.
 * @return Return value from the system call.
 */
int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Measures a null-terminated string.
 * @param s The string.
 * @return Its length.
 */
static int e_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/**
 * @brief Compares two null-terminated strings.
 * @param a First string.
 * @param b Second string.
 * @return 0 when they match.
 */
static int e_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/**
 * @brief Copies a string, bounded.
 * @param dst Destination.
 * @param src Source.
 * @param max Capacity of dst including the terminator.
 */
static void e_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/**
 * @brief Renders an integer as decimal.
 * @param n Value, may be negative.
 * @param out At least 12 bytes.
 */
static void e_itoa(int n, char *out) {
    char tmp[12];
    int i = 0, j = 0;
    int neg = (n < 0);
    unsigned int v = neg ? (unsigned int)(-n) : (unsigned int)n;

    if (v == 0) tmp[i++] = '0';
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }

    if (neg) out[j++] = '-';
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
}

/**
 * @brief Reads a decimal string.
 * @param s The string.
 * @return Its value, or 0 when it is not a number.
 */
static int e_atoi(const char *s) {
    int v = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

/**
 * @brief Writes a run of bytes to the screen.
 * @param s Bytes.
 * @param n How many.
 */
static void out(const char *s, int n) {
    if (n > 0) syscall(4, 1, (int)s, n);
}

/**
 * @brief Writes a null-terminated string to the screen.
 * @param s The string.
 */
static void puts_raw(const char *s) {
    out(s, e_strlen(s));
}

/*
 * The frame buffer.
 *
 * A screen is built here in full and written with one call. It was written the
 * obvious way first - position the cursor, print the row, erase the rest, per
 * row - and that is around a hundred writes for one frame. Each of them crossed
 * into the kernel, took a bounce buffer off the kernel heap and gave it back;
 * worse, the terminal repainted all 80 by 24 cells on every completed escape
 * sequence, so a frame cost roughly fifty repaints. The editor was visibly
 * behind the keyboard.
 *
 * Both halves were wrong and both are fixed. The terminal now refreshes once per
 * write rather than once per sequence, and this makes a frame one write.
 *
 * 4096 is comfortably over what a frame needs: 24 rows of at most 80 characters
 * plus a short sequence each.
 */
#define FRAME_MAX 4096

static char frame[FRAME_MAX];
static int frame_len = 0;

/**
 * @brief Appends a byte to the frame, dropping it if the frame is full.
 * @param c The byte.
 */
static void fb_put(char c) {
    if (frame_len < FRAME_MAX) frame[frame_len++] = c;
}

/**
 * @brief Appends a null-terminated string to the frame.
 * @param s The string.
 */
static void fb_str(const char *s) {
    while (*s) fb_put(*s++);
}

/**
 * @brief Appends a run of bytes to the frame.
 * @param s Bytes.
 * @param n How many.
 */
static void fb_bytes(const char *s, int n) {
    for (int i = 0; i < n; i++) fb_put(s[i]);
}

/**
 * @brief Appends a decimal number to the frame.
 * @param n Value.
 */
static void fb_num(int n) {
    char num[12];
    e_itoa(n, num);
    fb_str(num);
}

/**
 * @brief Writes the frame and empties it.
 */
static void fb_flush(void) {
    out(frame, frame_len);
    frame_len = 0;
}

/**
 * @brief Appends a cursor move, in 1-based screen coordinates.
 * @param row Row, 1 to 24.
 * @param col Column, 1 to 80.
 */
static void fb_goto(int row, int col) {
    fb_put(27); fb_put('[');
    fb_num(row);
    fb_put(';');
    fb_num(col);
    fb_put('H');
}

/** @brief Appends an erase from the cursor to the end of its line. */
static void fb_clear_eol(void) {
    fb_str("\033[K");
}

/** @brief Appends a switch to reverse video, for the status line. */
static void fb_reverse(void) {
    fb_str("\033[7m");
}

/** @brief Appends a return to normal attributes. */
static void fb_plain(void) {
    fb_str("\033[0m");
}

/** @brief Erases the whole screen, on its own. */
static void ansi_clear_all(void) {
    out("\033[2J", 4);
}

/* ------------------------------------------------------------------------- */

/** Keys that arrive as escape sequences, numbered above any byte value. */
#define K_UP    1000
#define K_DOWN  1001
#define K_LEFT  1002
#define K_RIGHT 1003
#define K_HOME  1004
#define K_END   1005
#define K_DEL   1006
#define K_PGUP  1007
#define K_PGDN  1008
#define K_NONE  1009

/** Editor modes. */
#define MODE_NORMAL 0
#define MODE_INSERT 1

static editbuf_t buf;
static char filename[64];
static int cursor = 0;        /**< Byte offset of the cursor. */
static int top = 0;           /**< Byte offset of the first displayed line. */
static int leftcol = 0;       /**< First displayed column, for long lines. */
static int want_col = 0;      /**< Column a vertical move aims for. */
static int mode = MODE_NORMAL;
static int dirty = 0;
static int quitting = 0;
static char message[80];

/*
 * The undo log.
 *
 * 256 records and 8 KB of removed text. The records are the cheap half - a run
 * of typing collapses into one - and the arena is what a dd on a long line
 * spends, so the two are sized for different things. Neither is a limit the
 * user meets by editing normally; a session that does exhaust them gives up its
 * oldest changes rather than its newest, and says so.
 *
 * A pointer, so that a failed allocation is a working editor without undo
 * rather than an editor that will not start. eb_undo_* take a null log and do
 * the edit without recording it.
 */
#define EDIT_UNDO_RECS  256
#define EDIT_UNDO_ARENA 8192

static ebundo_t undo_store;
static ebundo_t *undo = 0;

/** @brief The last pattern searched for, so n and N have something to repeat. */
static char last_pattern[64];

/*
 * One byte of pushback, for the same reason the shell keeps one: an escape
 * sequence is read a byte at a time, and a byte read to find out whether a
 * sequence was starting has to go somewhere when it turns out one was not.
 */
static int pushback = -1;

/**
 * @brief Reads one byte, blocking, taking the pushed-back one first.
 * @return The byte, or -1 when the read reported no byte.
 */
static int read_byte(void) {
    char c = 0;

    if (pushback >= 0) { int b = pushback; pushback = -1; return b; }

    /* 3 is SYSCALL_READ. A read cut short by a signal reports a negative value;
     * the caller retries, because the editor declines the interrupt. */
    if (syscall(3, 0, (int)&c, 1) != 1) return -1;
    return (unsigned char)c;
}

/**
 * @brief Whether a byte is already waiting to be read.
 *
 * 63 is SYSCALL_POLL, and this is the whole reason it exists. ESC is both the
 * Escape key - the most pressed key in a modal editor - and the first byte of
 * every arrow. Nothing here has a timer to measure the gap between them, so the
 * question has to be asked of the ring instead: if the rest of the sequence is
 * not already there, the user pressed Escape.
 *
 * @return 1 when a read would not block.
 */
static int input_waiting(void) {
    return syscall(63, 0, 0, 0) == 1;
}

/**
 * @brief Reads a key, decoding escape sequences into K_ codes.
 * @return A byte value, or one of the K_ constants.
 */
static int read_key(void) {
    int c = read_byte();

    if (c < 0) return K_NONE;
    if (c != 27) return c;

    if (!input_waiting()) return 27;          /* the Escape key itself */

    c = read_byte();
    if (c != '[') { if (c >= 0) pushback = c; return 27; }

    c = read_byte();
    switch (c) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case 'H': return K_HOME;
        case 'F': return K_END;
        default: break;
    }

    if (c >= '0' && c <= '9') {
        int n = 0;
        while (c >= '0' && c <= '9') { n = n * 10 + (c - '0'); c = read_byte(); }
        if (c != '~') return K_NONE;
        if (n == 3) return K_DEL;
        if (n == 5) return K_PGUP;
        if (n == 6) return K_PGDN;
    }
    return K_NONE;
}

/* ------------------------------------------------------------------------- */

/**
 * @brief Scrolls the view so that the cursor is on it.
 *
 * Both directions and both axes. The horizontal half exists because a line can
 * be longer than the screen is wide and the alternative - refusing to move the
 * cursor past column 80 - would make the end of such a line uneditable.
 */
static void scroll_to_cursor(void) {
    /* Vertically: walk from the top of the view towards the cursor. */
    if (cursor < top) top = eb_line_start(&buf, cursor);

    for (;;) {
        int row = 0;
        int off = top;

        while (row < EDIT_ROWS) {
            if (cursor <= eb_line_end(&buf, off)) return;   /* visible */
            int next = eb_next_line(&buf, off);
            if (next < 0) return;
            off = next;
            row++;
        }

        int next = eb_next_line(&buf, top);
        if (next < 0) return;
        top = next;
    }
}

/**
 * @brief Adjusts the horizontal offset so the cursor's column is on screen.
 */
static void scroll_columns(void) {
    int col = eb_col(&buf, cursor);

    if (col < leftcol) leftcol = col;
    if (col >= leftcol + EDIT_COLS) leftcol = col - EDIT_COLS + 1;
    if (leftcol < 0) leftcol = 0;
}

/**
 * @brief Draws the status line and leaves the cursor where the text is.
 */
static void draw_status(void) {
    char num[12];
    int line = 1;
    int off = 0;

    while (off < cursor) {
        int next = eb_next_line(&buf, off);
        if (next < 0 || next > cursor) break;
        off = next;
        line++;
    }

    fb_goto(24, 1);
    fb_reverse();

    if (message[0] != '\0') {
        fb_str(message);
    } else {
        fb_str(filename[0] ? filename : "[no name]");
        if (dirty) fb_str(" [+]");
        fb_str(mode == MODE_INSERT ? "  -- INSERT --  " : "  ");
        e_itoa(line, num); fb_str(num);
        fb_str(",");
        e_itoa(eb_col(&buf, cursor) + 1, num); fb_str(num);
        fb_str("   :w write  :q quit  i insert  ESC normal");
    }

    fb_clear_eol();
    fb_plain();
}

/**
 * @brief Draws the whole screen.
 *
 * Every row every time. A 24 by 80 screen is under two kilobytes and this system
 * writes it in one syscall per row; the bookkeeping needed to draw only what
 * changed would cost more than it saves, and would be the first thing to go
 * wrong after a scroll.
 */
static void render(void) {
    int off = top;

    scroll_columns();

    frame_len = 0;

    for (int row = 0; row < EDIT_ROWS; row++) {
        fb_goto(row + 1, 1);

        if (off >= 0) {
            int start = eb_line_start(&buf, off);
            int end = eb_line_end(&buf, off);
            int from = start + leftcol;

            if (from < end) {
                int n = end - from;
                if (n > EDIT_COLS) n = EDIT_COLS;
                fb_bytes(&buf.data[from], n);
            }
        } else {
            /* Past the end of the file. A tilde, as vi does, so that an empty
             * line in the file and a line that is not there look different. */
            fb_put('~');
        }

        fb_clear_eol();
        if (off >= 0) off = eb_next_line(&buf, off);
    }

    draw_status();

    /* And back to where the text is being edited. */
    {
        int row = 1;
        int scan = top;

        while (scan >= 0 && cursor > eb_line_end(&buf, scan)) {
            int next = eb_next_line(&buf, scan);
            if (next < 0) break;
            scan = next;
            row++;
        }
        fb_goto(row, eb_col(&buf, cursor) - leftcol + 1);
    }

    fb_flush();
}

/* ------------------------------------------------------------------------- */

/**
 * @brief Reads a file into the buffer.
 * @param name Path.
 * @return 1 when a file was read, 0 when there is none and the buffer is empty.
 */
static int load_file(const char *name) {
    int fd = syscall(40, (int)name, 0, 0);   /* SYSCALL_OPEN, read */

    if (fd < 0) return 0;

    for (;;) {
        int room = buf.cap - 1 - buf.len;
        if (room <= 0) break;

        int n = syscall(3, fd, (int)(buf.data + buf.len), room);
        if (n <= 0) break;
        buf.len += n;
    }

    syscall(38, fd, 0, 0);                   /* SYSCALL_CLOSE */
    return 1;
}

/**
 * @brief Writes the buffer to a file.
 *
 * Opening for writing truncates, and a file that does not exist has to be
 * created first: open() does not create, which is the same dance the shell does
 * for "cmd > file".
 *
 * @param name Path.
 * @return 1 on success.
 */
static int save_file(const char *name) {
    int fd = syscall(40, (int)name, 1, 0);   /* SYSCALL_OPEN, write */

    if (fd < 0) {
        if (syscall(8, (int)name, (int)"", 0) != 0) return 0;   /* SYSCALL_CREATE_FILE */
        fd = syscall(40, (int)name, 1, 0);
    }
    if (fd < 0) return 0;

    int off = 0;
    while (off < buf.len) {
        int n = syscall(4, fd, (int)(buf.data + off), buf.len - off);
        if (n <= 0) { syscall(38, fd, 0, 0); return 0; }
        off += n;
    }

    syscall(38, fd, 0, 0);
    return 1;
}

/* ------------------------------------------------------------------------- */

/**
 * @brief Reads a line typed on the status row after a leading character.
 *
 * The line is edited with the same two keys the rest of the system uses for it -
 * backspace deletes, Escape abandons - and nothing more: this is a handful of
 * characters and giving it a cursor of its own would be a second line editor to
 * keep correct.
 *
 * Shared by ':' and '/' rather than written twice. They differ in the character
 * shown at the front and in what is done with the answer, which is not enough to
 * be two functions.
 *
 * @param lead Character shown at the start of the row.
 * @param out Receives the line, null-terminated.
 * @param max Capacity of out including the terminator.
 * @return 1 when the line was entered, 0 when it was abandoned.
 */
static int read_prompt_line(char lead, char *out, int max) {
    int n = 0;

    out[0] = '\0';

    for (;;) {
        frame_len = 0;
        fb_goto(24, 1);
        fb_reverse();
        fb_put(lead);
        fb_bytes(out, n);
        fb_clear_eol();
        fb_plain();
        fb_goto(24, n + 2);
        fb_flush();

        int k = read_key();

        if (k == 27) return 0;                             /* abandoned */
        if (k == K_NONE) continue;
        if (k == '\n' || k == '\r') return 1;
        if (k == '\b') { if (n > 0) n--; out[n] = '\0'; continue; }
        if (k >= 32 && k <= 126 && n < max - 1) {
            out[n++] = (char)k;
            out[n] = '\0';
        }
    }
}

/**
 * @brief Reads a command after ':' and acts on it.
 */
static void command_line(void) {
    char cmd[64];

    message[0] = '\0';

    if (!read_prompt_line(':', cmd, (int)sizeof(cmd))) { render(); return; }

    /* :w, :w name, :q, :q!, :wq, and a line number to jump to. */
    if (cmd[0] == 'w') {
        const char *target = filename;

        if (cmd[1] == ' ') {
            target = &cmd[2];
            e_strcpy(filename, target, sizeof(filename));
        }

        if (target[0] == '\0') {
            e_strcpy(message, "no file name: use :w <name>", sizeof(message));
        } else if (save_file(target)) {
            dirty = 0;
            e_strcpy(message, "written: ", sizeof(message));
            {
                int m = e_strlen(message);
                e_strcpy(&message[m], target, (int)sizeof(message) - m);
            }
            /* ":wq" - the q only happens if the w did. */
            if (cmd[1] == 'q' && cmd[2] == '\0') quitting = 1;
        } else {
            e_strcpy(message, "could not write the file", sizeof(message));
        }
    } else if (e_strcmp(cmd, "q") == 0) {
        if (dirty) e_strcpy(message, "unsaved changes: :w to write, :q! to discard", sizeof(message));
        else quitting = 1;
    } else if (e_strcmp(cmd, "q!") == 0) {
        quitting = 1;
    } else if (cmd[0] >= '1' && cmd[0] <= '9') {
        cursor = eb_goto_line(&buf, e_atoi(cmd));
        want_col = 0;
    } else {
        e_strcpy(message, "not a command: try :w, :q, :q!, / or a line number", sizeof(message));
    }

    if (!quitting) { scroll_to_cursor(); render(); }
}

/**
 * @brief Moves the cursor to the next or previous match of the last pattern.
 *
 * Whether the search wrapped is worked out here rather than reported by the
 * buffer: a forward search that lands at or before where it started went round
 * the end, and there is no other way to arrive there.
 *
 * @param forward Non-zero to search forwards.
 */
static void search_step(int forward) {
    int found;

    if (last_pattern[0] == '\0') {
        e_strcpy(message, "no pattern: use / to search", sizeof(message));
        return;
    }

    found = forward ? eb_find_next(&buf, last_pattern, cursor)
                    : eb_find_prev(&buf, last_pattern, cursor);

    if (found < 0) {
        e_strcpy(message, "pattern not found: ", sizeof(message));
        {
            int m = e_strlen(message);
            e_strcpy(&message[m], last_pattern, (int)sizeof(message) - m);
        }
        return;
    }

    if (forward ? (found <= cursor) : (found >= cursor)) {
        e_strcpy(message, forward ? "search wrapped to the top"
                                  : "search wrapped to the bottom", sizeof(message));
    }

    cursor = found;
    want_col = eb_col(&buf, cursor);
}

/**
 * @brief Reads a pattern after '/' and jumps to its first match.
 */
static void search_line(void) {
    char pat[64];

    message[0] = '\0';

    if (!read_prompt_line('/', pat, (int)sizeof(pat))) { render(); return; }

    /* An empty pattern repeats the last one, which is what makes pressing / and
     * Enter mean "again" rather than "forget what I was looking for". */
    if (pat[0] != '\0') e_strcpy(last_pattern, pat, sizeof(last_pattern));

    search_step(1);
    scroll_to_cursor();
    render();
}

/**
 * @brief Moves the cursor up or down a line, keeping the column it aims for.
 * @param down Non-zero to go down.
 */
static void move_line(int down) {
    int target = down ? eb_next_line(&buf, cursor) : eb_prev_line(&buf, cursor);

    if (target < 0) return;
    cursor = eb_clamp_to_line(&buf, target, want_col);
}

/**
 * @brief Enters insert mode, opening the change group the whole run shares.
 *
 * Everything typed until Escape records into this one group, so u gives back the
 * sentence rather than the letter. That is the difference between an undo a
 * person uses and one they stop pressing.
 */
static void enter_insert(void) {
    eb_undo_group(undo);
    mode = MODE_INSERT;
}

/**
 * @brief Takes back the newest group of changes.
 */
static void undo_key(void) {
    int back = eb_undo_apply(&buf, undo);

    if (back < 0) {
        e_strcpy(message, undo ? "nothing to undo"
                               : "undo is unavailable: there was no memory for the log",
                 sizeof(message));
        return;
    }

    if (back > buf.len) back = buf.len;
    cursor = back;
    want_col = eb_col(&buf, cursor);

    /*
     * Still dirty, whichever way the undo went. The buffer having been written
     * once does not mean it matches the file now, and the only undo that would
     * leave it matching is one that lands exactly on the state that was saved -
     * which this does not know, and guessing wrong would lose the file.
     */
    dirty = 1;

    if (undo->lost) {
        e_strcpy(message, "undo history was trimmed: the oldest changes are gone",
                 sizeof(message));
        undo->lost = 0;
    }
}

/**
 * @brief Handles one key in normal mode.
 * @param k The key.
 */
static void normal_key(int k) {
    static int pending_g = 0;
    static int pending_d = 0;

    int was_g = pending_g;
    int was_d = pending_d;

    pending_g = 0;
    pending_d = 0;

    switch (k) {
        case 'h': case K_LEFT:
            if (cursor > 0 && eb_col(&buf, cursor) > 0) cursor--;
            want_col = eb_col(&buf, cursor);
            break;

        case 'l': case K_RIGHT:
            if (cursor < eb_line_end(&buf, cursor)) cursor++;
            want_col = eb_col(&buf, cursor);
            break;

        case 'j': case K_DOWN: move_line(1); break;
        case 'k': case K_UP:   move_line(0); break;

        case '0': case K_HOME:
            cursor = eb_line_start(&buf, cursor);
            want_col = 0;
            break;

        case '$': case K_END:
            cursor = eb_line_end(&buf, cursor);
            want_col = eb_col(&buf, cursor);
            break;

        case K_PGDN:
            for (int i = 0; i < EDIT_ROWS - 1; i++) move_line(1);
            break;

        case K_PGUP:
            for (int i = 0; i < EDIT_ROWS - 1; i++) move_line(0);
            break;

        case 'g':
            if (was_g) { cursor = 0; want_col = 0; }
            else pending_g = 1;
            break;

        case 'G':
            cursor = eb_line_start(&buf, buf.len);
            want_col = 0;
            break;

        case 'i': enter_insert(); break;

        case 'a':
            if (cursor < eb_line_end(&buf, cursor)) cursor++;
            enter_insert();
            break;

        case 'A':
            cursor = eb_line_end(&buf, cursor);
            enter_insert();
            break;

        /* The group is opened before the newline goes in, so that the line this
         * opens and everything typed onto it come back together. Splitting them
         * would leave u undoing the text and then the blank line it was on. */
        case 'o':
            enter_insert();
            cursor = eb_line_end(&buf, cursor);
            if (eb_undo_insert(&buf, undo, cursor, '\n', cursor)) { cursor++; dirty = 1; }
            break;

        case 'O':
            enter_insert();
            cursor = eb_line_start(&buf, cursor);
            if (eb_undo_insert(&buf, undo, cursor, '\n', cursor)) dirty = 1;
            break;

        case 'x': case K_DEL:
            eb_undo_group(undo);
            if (cursor < eb_line_end(&buf, cursor) &&
                eb_undo_delete(&buf, undo, cursor, cursor)) dirty = 1;
            break;

        case 'd':
            if (was_d) {
                eb_undo_group(undo);
                cursor = eb_undo_delete_line(&buf, undo, cursor, cursor);
                want_col = 0;
                dirty = 1;
            } else {
                pending_d = 1;
            }
            break;

        case 'u': undo_key(); break;

        case '/':
            search_line();
            return;

        case 'n': search_step(1); break;
        case 'N': search_step(0); break;

        case ':':
            command_line();
            return;

        case 12:   /* Ctrl-L: what every editor binds to "something drew on me" */
            ansi_clear_all();
            break;

        default:
            break;
    }
}

/**
 * @brief Handles one key in insert mode.
 * @param k The key.
 */
static void insert_key(int k) {
    switch (k) {
        case 27:
            mode = MODE_NORMAL;
            /* vi leaves the cursor on the last character typed rather than after
             * it, which is what makes x and d act on what you were writing. */
            if (eb_col(&buf, cursor) > 0) cursor--;
            want_col = eb_col(&buf, cursor);
            break;

        case '\b':
            if (cursor > 0 && eb_undo_delete(&buf, undo, cursor - 1, cursor)) {
                cursor--;
                dirty = 1;
            }
            want_col = eb_col(&buf, cursor);
            break;

        case K_DEL:
            if (eb_undo_delete(&buf, undo, cursor, cursor)) dirty = 1;
            break;

        case K_LEFT:  if (cursor > 0) cursor--; want_col = eb_col(&buf, cursor); break;
        case K_RIGHT: if (cursor < buf.len) cursor++; want_col = eb_col(&buf, cursor); break;
        case K_UP:    move_line(0); break;
        case K_DOWN:  move_line(1); break;
        case K_HOME:  cursor = eb_line_start(&buf, cursor); want_col = 0; break;
        case K_END:   cursor = eb_line_end(&buf, cursor); want_col = eb_col(&buf, cursor); break;

        case '\n': case '\r':
            if (eb_undo_insert(&buf, undo, cursor, '\n', cursor)) { cursor++; dirty = 1; }
            want_col = 0;
            break;

        default:
            if (k >= 32 && k <= 126) {
                if (eb_undo_insert(&buf, undo, cursor, (char)k, cursor)) { cursor++; dirty = 1; }
                else e_strcpy(message, "the file is full: 64 KB is the most that can be written",
                              sizeof(message));
                want_col = eb_col(&buf, cursor);
            }
            break;
    }
}

/* ------------------------------------------------------------------------- */

/**
 * @brief Redraws after the shell has written over the screen.
 *
 * Registered for SIG_CONT, which is delivered to a handler as of v0.8.2 for
 * exactly this: a stopped editor comes back to a screen holding whatever the
 * shell printed while it was away, and the moment it runs again is the only
 * moment it could know to repair it.
 *
 * Drawing from a handler is safe here in the one way that matters: the only
 * thing this program is ever doing when a continue arrives is waiting in read(),
 * so it cannot be halfway through a write of its own.
 */
static void on_continue(void) {
    ansi_clear_all();
    render();
    syscall(27, 0, 0, 0);   /* SYSCALL_SIGRETURN */
}

/**
 * @brief Main entry point.
 */
void main(void) {
    char args[128];

    for (int i = 0; i < 128; i++) args[i] = '\0';
    syscall(42, (int)args, 0, 0);   /* SYSCALL_GET_ARGS */

    buf.data = (char *)umalloc(EDIT_MAX_BYTES + 1);
    buf.len = 0;
    buf.cap = EDIT_MAX_BYTES + 1;
    message[0] = '\0';
    filename[0] = '\0';
    last_pattern[0] = '\0';

    if (buf.data == 0) {
        puts_raw("edit: not enough memory for the buffer\n");
        syscall(1, 1, 0, 0);
        while (1) { }
    }

    /*
     * The undo log, and the editor opens without one rather than refusing to
     * open. Losing undo is losing a convenience; losing the editor is losing the
     * only way to write a file on this system. The eb_undo_* calls take a null
     * log and do the edit without recording it, so nothing below has to ask.
     */
    undo_store.recs = (ebrec_t *)umalloc(EDIT_UNDO_RECS * (int)sizeof(ebrec_t));
    undo_store.arena = (char *)umalloc(EDIT_UNDO_ARENA);

    if (undo_store.recs != 0 && undo_store.arena != 0) {
        undo_store.rec_cap = EDIT_UNDO_RECS;
        undo_store.arena_cap = EDIT_UNDO_ARENA;
        undo = &undo_store;
        eb_undo_reset(undo);
    }

    /* One argument, a file name. A missing file is not an error: it is a new
     * file, and :w writes it. */
    if (args[0] != '\0') {
        e_strcpy(filename, args, sizeof(filename));
        if (!load_file(filename)) {
            e_strcpy(message, "new file", sizeof(message));
        }
    } else {
        e_strcpy(message, "no file name: :w <name> to write", sizeof(message));
    }

    /*
     * The interrupt is declined and the stop is not.
     *
     * A Ctrl-C that threw away an unsaved buffer would be a way to lose work by
     * touching the wrong key, and :q! is right there for the user who means it.
     * Ctrl-Z is left alone deliberately - being able to put the editor down and
     * pick it up again is what the last two releases were for, and the continue
     * handler below is what makes coming back look right.
     *
     * 24 is SYSCALL_SIGNAL_REG, 2 is SIG_INT, 1 is SIG_IGN, 18 is SIG_CONT.
     */
    syscall(24, 2, 1, 0);
    syscall(24, 18, (int)on_continue, 0);

    ansi_clear_all();
    render();

    while (!quitting) {
        int k = read_key();

        if (k == K_NONE) continue;

        message[0] = '\0';

        if (mode == MODE_NORMAL) normal_key(k);
        else insert_key(k);

        if (quitting) break;

        scroll_to_cursor();
        render();
    }

    /* Leave the screen to the shell rather than leaving the last frame on it. */
    ansi_clear_all();
    frame_len = 0;
    fb_goto(1, 1);
    fb_flush();

    /* The loop is what every program in /bin does after exit(): exit does not
     * return, and if a regression ever made it, falling off the end of main()
     * would execute whatever the linker put next. */
    syscall(1, 0, 0, 0);   /* SYSCALL_EXIT */
    while (1) { }
}
