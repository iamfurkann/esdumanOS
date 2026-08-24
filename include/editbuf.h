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

/* ------------------------------------------------------------------------- *
 * Searching.
 *
 * Plain substring, no patterns. A regular expression engine is a program of its
 * own and the editor has no way to show what one matched beyond moving the
 * cursor there, so it would be a great deal of machinery for a feature nobody
 * could see working.
 *
 * Both searches wrap. Without it a match above the cursor is unreachable by n,
 * and a file with one occurrence would answer "not found" whenever the cursor
 * happened to be past it - which reads as a broken search rather than as a
 * search that only looks forward.
 * ------------------------------------------------------------------------- */

/**
 * @brief Measures a null-terminated pattern.
 * @param pat The pattern.
 * @return Its length.
 */
static inline int eb_pat_len(const char *pat) {
    int n = 0;
    while (pat[n] != '\0') n++;
    return n;
}

/**
 * @brief Whether the pattern sits at exactly this offset.
 *
 * @param b Buffer.
 * @param pat Pattern.
 * @param off Offset to test.
 * @param n Pattern length.
 * @return 1 on a match that fits inside the buffer.
 */
static inline int eb_match_at(const editbuf_t *b, const char *pat, int off, int n) {
    if (off < 0 || off + n > b->len) return 0;

    for (int i = 0; i < n; i++) {
        if (b->data[off + i] != pat[i]) return 0;
    }
    return 1;
}

/**
 * @brief First match after an offset, wrapping to the start of the buffer.
 *
 * Strictly after, so that repeating a search from a match moves off it rather
 * than finding the same one again.
 *
 * @param b Buffer.
 * @param pat Pattern; an empty one matches nothing.
 * @param from Offset to search from.
 * @return Offset of the match, or -1.
 */
static inline int eb_find_next(const editbuf_t *b, const char *pat, int from) {
    int n = eb_pat_len(pat);

    if (n == 0 || n > b->len) return -1;
    if (from < -1) from = -1;
    if (from > b->len) from = b->len;

    for (int off = from + 1; off + n <= b->len; off++) {
        if (eb_match_at(b, pat, off, n)) return off;
    }

    /* Wrapped, and the offset it started from is included on the way back: a
     * match there is one this pass has not looked at yet. */
    for (int off = 0; off <= from && off + n <= b->len; off++) {
        if (eb_match_at(b, pat, off, n)) return off;
    }
    return -1;
}

/**
 * @brief Last match before an offset, wrapping to the end of the buffer.
 *
 * @param b Buffer.
 * @param pat Pattern; an empty one matches nothing.
 * @param from Offset to search back from.
 * @return Offset of the match, or -1.
 */
static inline int eb_find_prev(const editbuf_t *b, const char *pat, int from) {
    int n = eb_pat_len(pat);

    if (n == 0 || n > b->len) return -1;
    if (from < 0) from = 0;
    if (from > b->len) from = b->len;

    for (int off = from - 1; off >= 0; off--) {
        if (eb_match_at(b, pat, off, n)) return off;
    }

    for (int off = b->len - n; off >= from; off--) {
        if (eb_match_at(b, pat, off, n)) return off;
    }
    return -1;
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
 * @brief The byte range deleting a line would remove.
 *
 * Split out from the deletion because undo has to save those bytes before they
 * go, and a second answer to "which newline goes with this line" is exactly the
 * kind of disagreement that took two functions and a test to find the first
 * time. There is one answer and this is where it lives.
 *
 * @param b Buffer.
 * @param off Any offset on the line.
 * @param from Receives the first byte to remove.
 * @param to Receives one past the last.
 */
static inline void eb_line_span(const editbuf_t *b, int off, int *from, int *to) {
    int start = eb_line_start(b, off);
    int end = eb_line_end(b, off);

    if (end < b->len) {
        /* The line has a newline of its own, and it goes with it. */
        *from = start;
        *to = end + 1;
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
        *to = end;
        *from = (start > 0) ? start - 1 : start;
    }
}

/**
 * @brief Removes the whole line containing off, and its newline.
 *
 * Deleting the last line of a file that ends in a newline has to take the
 * newline *before* it instead, or the buffer keeps an empty line the user just
 * removed. eb_line_span() decides which.
 *
 * @param b Buffer.
 * @param off Any offset on the line.
 * @return Offset the cursor should take afterwards.
 */
static inline int eb_delete_line(editbuf_t *b, int off) {
    int from, to;

    eb_line_span(b, off, &from, &to);

    for (int i = from; i < b->len - (to - from); i++) b->data[i] = b->data[i + (to - from)];
    b->len -= (to - from);
    if (b->len < 0) b->len = 0;

    if (from > b->len) from = b->len;
    return eb_line_start(b, from);
}

/* ------------------------------------------------------------------------- *
 * Undo.
 *
 * A log of changes and the arithmetic to run them backwards, kept to the same
 * rule as the rest of this header: no allocation, no globals, the caller owns
 * the storage. The editor hands it memory from umalloc(), the test suite hands
 * it two static arrays, and the logic under both is the same logic.
 *
 * Each record says what a change did - how many bytes it put at an offset and
 * which bytes it took away - and undoing it is doing the opposite. The bytes
 * that were taken away have to be kept somewhere, so records point into an
 * arena that grows alongside them.
 *
 * Records carry a group, and undo takes back a whole group at a time. That is
 * the difference between an undo a person can use and one they cannot: a
 * sentence typed in insert mode is one thing the user did, and giving it back a
 * character per keystroke would mean pressing u forty times to unsay it.
 * ------------------------------------------------------------------------- */

/**
 * @brief One recorded change.
 */
typedef struct {
    int off;      /**< Offset the change was applied at. */
    int inserted; /**< Bytes it put there. */
    int removed;  /**< Bytes it took from there, held in the arena. */
    int text;     /**< Arena offset of those bytes. */
    int cursor;   /**< Where the cursor was before it. */
    int group;    /**< Changes sharing a group are undone together. */
} ebrec_t;

/**
 * @brief A change log and the space it has.
 */
typedef struct {
    ebrec_t *recs;   /**< Records, oldest first; the caller owns the array. */
    int      nrecs;  /**< Records in use. */
    int      rec_cap;/**< Records available; zero means undo is unavailable. */
    char    *arena;  /**< Removed bytes, in record order. */
    int      used;   /**< Arena bytes in use. */
    int      arena_cap; /**< Arena bytes available. */
    int      group;  /**< Group new records are recorded into. */
    int      lost;   /**< History was dropped to make room. */
} ebundo_t;

/**
 * @brief Empties the log.
 *
 * @param u Log; a null pointer is ignored.
 */
static inline void eb_undo_reset(ebundo_t *u) {
    if (u == 0) return;

    u->nrecs = 0;
    u->used = 0;
    u->group = 0;
    u->lost = 0;
}

/**
 * @brief Starts a new group, so the next change undoes on its own.
 *
 * @param u Log; a null pointer is ignored.
 */
static inline void eb_undo_group(ebundo_t *u) {
    if (u == 0) return;
    u->group++;
}

/**
 * @brief Discards the oldest group to make room.
 *
 * A whole group, never part of one. Half a group in the log would undo half of
 * something the user did in one go and leave the rest, which is worse than not
 * being able to undo it at all.
 *
 * Records are appended in order and so are their bytes, so the oldest group is
 * a run at the front of both and everything after it slides down.
 *
 * @param u Log.
 */
static inline void eb_undo_drop_oldest(ebundo_t *u) {
    int g, n = 0, bytes = 0;

    if (u->nrecs == 0) return;

    g = u->recs[0].group;
    while (n < u->nrecs && u->recs[n].group == g) {
        bytes += u->recs[n].removed;
        n++;
    }

    for (int i = bytes; i < u->used; i++) u->arena[i - bytes] = u->arena[i];
    u->used -= bytes;

    for (int i = n; i < u->nrecs; i++) {
        u->recs[i - n] = u->recs[i];
        u->recs[i - n].text -= bytes;
    }
    u->nrecs -= n;
    u->lost = 1;
}

/**
 * @brief Makes room for one record and the bytes it needs.
 *
 * @param u Log.
 * @param need Arena bytes wanted.
 * @return 1 when there is room, 0 when there cannot be.
 */
static inline int eb_undo_room(ebundo_t *u, int need) {
    if (u->rec_cap <= 0 || need > u->arena_cap) return 0;

    while (u->nrecs >= u->rec_cap || u->used + need > u->arena_cap) {
        int before = u->nrecs;

        eb_undo_drop_oldest(u);
        if (u->nrecs == before) return 0;   /* nothing left to give up */
    }
    return 1;
}

/**
 * @brief Records a change.
 *
 * @param u Log; a null pointer or a log with no records records nothing.
 * @param off Offset the change was applied at.
 * @param inserted Bytes put there.
 * @param removed Bytes taken from there, read before the change is made.
 * @param removed_n How many.
 * @param cursor Cursor before the change.
 */
static inline void eb_undo_push(ebundo_t *u, int off, int inserted,
                                const char *removed, int removed_n, int cursor) {
    ebrec_t *r;

    if (u == 0 || u->rec_cap <= 0) return;

    /*
     * Typing costs one record, not one per key. A single-byte insert in the
     * current group that continues where the last one ended is that same run
     * carrying on, so it grows the record instead of adding one.
     */
    if (inserted > 0 && removed_n == 0 && u->nrecs > 0) {
        r = &u->recs[u->nrecs - 1];
        if (r->group == u->group && r->removed == 0 && off == r->off + r->inserted) {
            r->inserted += inserted;
            return;
        }
    }

    if (!eb_undo_room(u, removed_n)) {
        /*
         * This change cannot be recorded, and every record already held
         * describes a buffer it is about to move out from under them - undoing
         * them afterwards would put bytes back at offsets that have moved. They
         * go, and the user is told the history was lost rather than being handed
         * an undo that corrupts the file.
         */
        eb_undo_reset(u);
        u->lost = 1;
        return;
    }

    r = &u->recs[u->nrecs++];
    r->off = off;
    r->inserted = inserted;
    r->removed = removed_n;
    r->text = u->used;
    r->cursor = cursor;
    r->group = u->group;

    for (int i = 0; i < removed_n; i++) u->arena[u->used + i] = removed[i];
    u->used += removed_n;
}

/**
 * @brief Inserts one byte and records it.
 *
 * @param b Buffer.
 * @param u Log, or null to edit without one.
 * @param off Where to place it.
 * @param c The byte.
 * @param cursor Cursor before the change.
 * @return 1 when it was placed.
 */
static inline int eb_undo_insert(editbuf_t *b, ebundo_t *u, int off, char c, int cursor) {
    if (!eb_insert(b, off, c)) return 0;

    eb_undo_push(u, off, 1, 0, 0, cursor);
    return 1;
}

/**
 * @brief Removes the byte at off and records it.
 *
 * The byte is handed to the log before the deletion, while it is still there to
 * be read.
 *
 * @param b Buffer.
 * @param u Log, or null to edit without one.
 * @param off Byte to remove.
 * @param cursor Cursor before the change.
 * @return 1 when a byte was removed.
 */
static inline int eb_undo_delete(editbuf_t *b, ebundo_t *u, int off, int cursor) {
    if (off < 0 || off >= b->len) return 0;

    eb_undo_push(u, off, 0, &b->data[off], 1, cursor);
    return eb_delete(b, off);
}

/**
 * @brief Removes the line containing off and records it.
 *
 * The span comes from eb_line_span(), the same answer the deletion itself uses,
 * so what is saved is exactly what goes.
 *
 * @param b Buffer.
 * @param u Log, or null to edit without one.
 * @param off Any offset on the line.
 * @param cursor Cursor before the change.
 * @return Offset the cursor should take afterwards.
 */
static inline int eb_undo_delete_line(editbuf_t *b, ebundo_t *u, int off, int cursor) {
    int from, to;

    eb_line_span(b, off, &from, &to);
    if (to > from) eb_undo_push(u, from, 0, &b->data[from], to - from, cursor);

    return eb_delete_line(b, off);
}

/**
 * @brief Takes back the newest group of changes.
 *
 * Newest record first within the group: they were applied in order, so they come
 * out in the opposite one, or an offset recorded before an earlier change would
 * be read against a buffer that has already moved.
 *
 * @param b Buffer.
 * @param u Log, or null.
 * @return Where the cursor belongs afterwards, or -1 when there was nothing.
 */
static inline int eb_undo_apply(editbuf_t *b, ebundo_t *u) {
    int g, cursor = -1;

    if (u == 0 || u->nrecs == 0) return -1;

    g = u->recs[u->nrecs - 1].group;

    while (u->nrecs > 0 && u->recs[u->nrecs - 1].group == g) {
        ebrec_t *r = &u->recs[--u->nrecs];

        for (int i = 0; i < r->inserted; i++) eb_delete(b, r->off);
        for (int i = 0; i < r->removed; i++) eb_insert(b, r->off + i, u->arena[r->text + i]);

        /* Its bytes are the newest in the arena, so giving them back is moving
         * the end of the arena to where they started. */
        u->used = r->text;
        cursor = r->cursor;
    }
    return cursor;
}

#endif /* EDITBUF_H */
