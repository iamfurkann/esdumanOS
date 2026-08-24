/**
 * @file grep.c
 * @brief Searches for a pattern in a file or on standard input
 */

/**
 * @brief Invokes a system call
 * @param num System call number
 * @param arg1 First argument
 * @param arg2 Second argument
 * @param arg3 Third argument
 * @return Return value from the system call
 */
int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Prints a string to the standard output
 * @param str The null-terminated string to print
 */
void print(const char *str) {
    int len = 0;
    while(str[len]) len++;
    syscall(4, 1, (int)str, len);
}

/**
 * @brief Prints a newline character to the standard output
 */
void print_newline() {
    syscall(4, 1, (int)"\n", 1);
}

/*
 * A line is assembled here as bytes arrive, rather than the whole input being
 * read at once.
 *
 * The old version issued a single read of 511 bytes and searched what came back,
 * so it saw the beginning of a file and nothing else - a match on line 40 of a
 * 2 KB file was simply not found, silently. The same call could not have read
 * standard input at all: a pipe delivers whatever has been written so far, which
 * for a stage still running is usually a fraction of its output.
 *
 * Streaming fixes both, and the two sources become the same loop.
 */
#define LINE_MAX  256
#define CHUNK_MAX 128

static char line_buf[LINE_MAX];
static int line_len = 0;

/**
 * @brief Whether anything has matched since the search began.
 *
 * grep's exit status is an answer, not just a report of whether it ran: 0 means
 * something matched, 1 means nothing did, 2 means it could not look. This
 * carries the first of those out of the line-by-line scan.
 */
static int any_match = 0;

/**
 * @brief Reports whether a line contains the search term.
 *
 * @param line Line contents, not null-terminated.
 * @param len Length of the line.
 * @param term Search term.
 * @param term_len Length of the search term.
 * @return 1 when the term occurs in the line, 0 otherwise.
 */
static int line_contains(const char *line, int len, const char *term, int term_len) {
    if (term_len == 0 || term_len > len) return 0;

    for (int m = 0; m <= len - term_len; m++) {
        int match = 1;
        for (int n = 0; n < term_len; n++) {
            if (line[m + n] != term[n]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/**
 * @brief Prints the buffered line if it matches, then empties the buffer.
 *
 * @param term Search term.
 * @param term_len Length of the search term.
 */
static void flush_line(const char *term, int term_len) {
    if (line_contains(line_buf, line_len, term, term_len)) {
        any_match = 1;
        syscall(4, 1, (int)line_buf, line_len);
        print_newline();
    }
    line_len = 0;
}

/**
 * @brief Reads a descriptor to the end, printing every line that matches.
 *
 * A line longer than LINE_MAX is truncated rather than split: the excess is
 * dropped and the search runs against the first LINE_MAX-1 bytes. Splitting
 * would report one long line as two and could match across a boundary that does
 * not exist in the input.
 *
 * @param fd Descriptor to read; 0 is standard input.
 * @param term Search term.
 * @param term_len Length of the search term.
 */
static void grep_fd(int fd, const char *term, int term_len) {
    char buf[CHUNK_MAX];
    int n;

    while ((n = syscall(3, fd, (int)buf, CHUNK_MAX)) > 0) {
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\r' || c == '\0') continue;
            if (c == '\n') { flush_line(term, term_len); continue; }
            if (line_len < LINE_MAX - 1) line_buf[line_len++] = c;
        }
    }

    /* Input that does not end in a newline still has a last line. */
    if (line_len > 0) flush_line(term, term_len);
}

/**
 * @brief Main entry point for the application
 */
void main(void) {
    char args_buf[128];
    for (int k = 0; k < 128; k++) args_buf[k] = '\0';
    syscall(42, (int)args_buf, 0, 0); // SYSCALL_GET_ARGS

    // The shell will pass the canonical absolute path as the argument string.
    // E.g. for "touch a.txt", the shell passes "/current/path/a.txt"

    int status = 0;

    int i = 0;
    while (args_buf[i] && args_buf[i] != ' ') i++;

    /*
     * No file named means read standard input, which is what makes this usable
     * as the far end of a pipeline. It used to be a usage error, so "a | grep b"
     * could be parsed and connected and still had nothing that would consume it.
     */
    int has_file = (args_buf[i] == ' ');
    if (has_file) args_buf[i] = '\0';

    if (args_buf[0] == '\0') {
        print("Usage: grep <term> [file]"); print_newline();
        status = 2;
    } else {
        char *term = args_buf;
        int term_len = 0; while (term[term_len]) term_len++;

        if (has_file) {
            char *file = &args_buf[i + 1];
            int fd = syscall(40, (int)file, 0, 0); // SYSCALL_OPEN
            if (fd < 0) { print("grep: File not found"); print_newline(); status = 2; }
            else {
                grep_fd(fd, term, term_len);
                syscall(38, fd, 0, 0); // SYSCALL_CLOSE
                status = any_match ? 0 : 1;
            }
        } else {
            /* Descriptor 0 is never closed: it belongs to whoever started this
             * process, and closing it would take standard input away from them
             * as well. */
            grep_fd(0, term, term_len);
            status = any_match ? 0 : 1;
        }
    }

    /*
     * 0 matched, 1 did not, 2 could not look - the convention grep has
     * everywhere, and as of v0.9.2 the one this follows.
     *
     * It used to report 1 for both "nothing matched" and "no such file", which
     * made the status useless for the thing a status is for: `grep x f && ...`
     * ran the second half whether or not anything had been found, and there was
     * no way for a script to tell an empty result from a broken one. Moving the
     * errors to 2 is a behaviour change and is called out in the release notes.
     */
    syscall(1, status, 0, 0); // EXIT
    while(1);
}
