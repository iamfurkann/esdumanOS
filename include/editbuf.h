#ifndef EDITBUF_H
#define EDITBUF_H

/*
 * The buffer a text editor edits: a flat run of bytes and the line arithmetic
 * over it.
 *
 * Header-only, and pure. It makes no system calls, touches no globals and knows
 * nothing about a screen - every function takes a buffer and an offset and
 * returns an offset. That is not tidiness either: /bin/edit is a freestanding
 * translation unit with no link step, exactly like every other program in /bin,
 * so the only way its logic can also be reached by the test suite is for the
 * logic to live in a header both can include. umalloc.h is header-only for the
 * same reason.
 *
 * What is under test here is the arithmetic, which is where an editor is wrong
 * in ways that look right: an off-by-one in "where does this line start" moves
 * the cursor one column for the rest of the session, and nothing about the
 * screen says which of the two numbers is the wrong one.
 *
 * Flat rather than a list of lines, and that is a decision about size rather
 * than elegance. A file here is at most 64 KB - MAX_FILE_WRITE_BYTES, the most
 * the VFS can write back - so an insert is a move of at most 64 KB and a scan
 * for a line boundary is at most 64 KB. Both are nothing at this scale, and a
 * line list would need its own allocation, its own invalidation rule and its own
 * bugs.
 *
 * Offsets are byte indices into data, from 0 to len inclusive. len is a valid
 * cursor position: it is where a cursor sits at the end of the file.
 */

/**
 * @brief A text buffer and the space it has.
 */
typedef struct {
    char *data;   /**< Bytes; the caller owns the allocation. */
    int   len;    /**< Bytes in use. */
    int   cap;    /**< Bytes available, including room for the terminator. */
} editbuf_t;

/**
 * @brief Offset of the first byte of the line containing off.
 *
 * @param b Buffer.
 * @param off Any offset from 0 to len.
 * @return Offset of the line's first byte; 0 for the first line.
 */
static inline int eb_line_start(const editbuf_t *b, int off) {
    if (off > b->len) off = b->len;
    if (off < 0) off = 0;

    while (off > 0 && b->data[off - 1] != '\n') off--;
    return off;
}

/**
 * @brief Offset of the newline ending the line containing off, or len.
 *
 * The newline itself, not the byte after it, so that the difference from
 * eb_line_start() is the line's length in characters.
 *
 * @param b Buffer.
 * @param off Any offset from 0 to len.
 * @return Offset of the '\n', or len when the last line has no terminator.
 */
static inline int eb_line_end(const editbuf_t *b, int off) {
    if (off > b->len) off = b->len;
    if (off < 0) off = 0;

    while (off < b->len && b->data[off] != '\n') off++;
    return off;
}

/**
 * @brief Column of an offset within its line, counting from 0.
 *
 * @param b Buffer.
 * @param off Any offset.
 * @return Characters between the line's first byte and off.
 */
static inline int eb_col(const editbuf_t *b, int off) {
    return off - eb_line_start(b, off);
}

/**
 * @brief Start of the line after the one containing off.
 *
 * @param b Buffer.
 * @param off Any offset.
 * @return Offset of the next line's first byte, or -1 when there is none.
 */
static inline int eb_next_line(const editbuf_t *b, int off) {
    int end = eb_line_end(b, off);

    /* end == len means the last line, whether or not it ends in a newline: there
     * is no line after it either way. */
    if (end >= b->len) return -1;
    return end + 1;
}

/**
 * @brief Start of the line before the one containing off.
 *
 * @param b Buffer.
 * @param off Any offset.
 * @return Offset of the previous line's first byte, or -1 when there is none.
 */
static inline int eb_prev_line(const editbuf_t *b, int off) {
    int start = eb_line_start(b, off);

    if (start == 0) return -1;
    return eb_line_start(b, start - 1);
}

/**
 * @brief Moves an offset to a column of another line, clamped to that line.
 *
 * The rule a vertical cursor move needs: a column that does not exist on the
 * target line lands at its end rather than spilling into the line after it.
 *
 * @param b Buffer.
 * @param line_start First byte of the target line.
 * @param col Desired column.
 * @return An offset on that line.
 */
static inline int eb_clamp_to_line(const editbuf_t *b, int line_start, int col) {
    int end = eb_line_end(b, line_start);
    int want = line_start + col;

    return (want > end) ? end : want;
}

/**
 * @brief Number of lines in the buffer.
 *
 * A newline separates lines rather than terminating them, so a buffer ending in
 * one has an empty line after it and this counts it. An empty buffer is one
 * empty line.
 *
 * That is a choice, and the other one was tried first: treat a trailing newline
 * as belonging to the line before it, the way vi presents a file. It does not
 * survive contact with a flat buffer. Pressing Enter at the end of the last line
 * produces exactly that trailing newline, and the user is then standing on a line
 * the count says is not there - so either the cursor is somewhere unreachable or
 * the newline they just typed did nothing. vi gets away with the other model
 * because it stores a list of lines and the final newline is not in any of them;
 * here the bytes are the truth, and what the file holds is what the screen shows.
 *
 * The rest of this header already worked this way - eb_next_line() hands back the
 * offset after a trailing newline, and eb_goto_line() walks it - so this function
 * was the one disagreeing, and nothing but a test had noticed.
 *
 * @param b Buffer.
 * @return Line count, at least 1.
 */
static inline int eb_line_count(const editbuf_t *b) {
    int lines = 1;

    for (int i = 0; i < b->len; i++) {
        if (b->data[i] == '\n') lines++;
    }
    return lines;
}

/**
 * @brief Start of the nth line, counting from 1.
 *
 * @param b Buffer.
 * @param n Line number.
 * @return Offset of its first byte, clamped to the last line.
 */
static inline int eb_goto_line(const editbuf_t *b, int n) {
    int off = 0;

    if (n < 1) n = 1;
    for (int i = 1; i < n; i++) {
        int next = eb_next_line(b, off);
        if (next < 0) break;
        off = next;
    }
    return off;
}

/**
 * @brief Inserts one byte, moving everything after it up.
 *
 * @param b Buffer.
 * @param off Where to place it, 0 to len.
 * @param c The byte.
 * @return 1 when it was placed, 0 when the buffer is full or off is out of range.
 */
static inline int eb_insert(editbuf_t *b, int off, char c) {
    if (off < 0 || off > b->len) return 0;
    if (b->len + 1 >= b->cap) return 0;

    for (int i = b->len; i > off; i--) b->data[i] = b->data[i - 1];
    b->data[off] = c;
    b->len++;
    return 1;
}

/**
 * @brief Removes the byte at off, moving everything after it down.
 *
 * @param b Buffer.
 * @param off Byte to remove, 0 to len-1.
 * @return 1 when a byte was removed, 0 when off is out of range.
 */
static inline int eb_delete(editbuf_t *b, int off) {
    if (off < 0 || off >= b->len) return 0;

    for (int i = off; i < b->len - 1; i++) b->data[i] = b->data[i + 1];
    b->len--;
    return 1;
}

/**
 * @brief Removes the whole line containing off, and its newline.
 *
 * Deleting the last line of a file that ends in a newline has to take the
 * newline *before* it instead, or the buffer keeps an empty line the user just
 * removed.
 *
 * @param b Buffer.
 * @param off Any offset on the line.
 * @return Offset the cursor should take afterwards.
 */
static inline int eb_delete_line(editbuf_t *b, int off) {
    int start = eb_line_start(b, off);
    int end = eb_line_end(b, off);
    int from = start;
    int to;

    if (end < b->len) {
        /* The line has a newline of its own, and it goes with it. */
        to = end + 1;
    } else {
        /*
         * The last line of a buffer that does not end in a newline. There is
         * none of its own to remove, so the one that ends the line before goes
         * instead - otherwise the file keeps an empty line where this one was.
         *
         * The test for this is "the line has no newline", not "the span reaches
         * the end of the buffer". Those are the same thing only when the file
         * does not end in a newline: for one that does, deleting its last real
         * line reaches the end too, and taking the newline before as well would
         * swallow the line above it.
         */
        to = end;
        if (start > 0) from = start - 1;
    }

    for (int i = from; i < b->len - (to - from); i++) b->data[i] = b->data[i + (to - from)];
    b->len -= (to - from);
    if (b->len < 0) b->len = 0;

    if (from > b->len) from = b->len;
    return eb_line_start(b, from);
}

#endif /* EDITBUF_H */
