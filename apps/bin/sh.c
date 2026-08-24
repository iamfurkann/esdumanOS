#include "syscall.h"
#include "errno.h"
#include "umalloc.h"

typedef unsigned int uint32_t;

/**
 * @brief Size of the buffer `ls` reads a directory listing into.
 *
 * This is the kernel's own ceiling - READDIR_STAGE_MAX in sys_fs.c - rather than
 * a number chosen here. SYSCALL_READDIR stages the listing in kernel memory and
 * caps that staging buffer whatever the caller asks for, so a larger buffer
 * gains nothing and a smaller one throws entries away: the listing stops at an
 * entry boundary with no indication that it stopped. It was 1024 bytes on the
 * stack, which is a quarter of what the kernel was prepared to hand over.
 */
#define LS_BUF_SIZE 4096

/**
 * @brief Slots in the argument vector, including the terminating NULL.
 *
 * The tokenizer used to fill this array with no bound at all while the input
 * line allowed 254 characters - about 127 whitespace-separated tokens. Typing
 * enough short words wrote past the end of main()'s own array, and the pass
 * that follows then read those slots back as pointers and dereferenced them.
 * At most MAX_ARGS - 1 tokens are accepted now, so the NULL terminator every
 * consumer scans for always has a slot to live in.
 */
#define MAX_ARGS 32

/*
 * Stages a single pipeline may have.
 *
 * Bounded by processes rather than by taste. An external stage costs two tasks,
 * not one: the shell forks a child, and that child runs the program through
 * exec(), which creates a task of its own and blocks waiting for it. Four stages
 * of externals is eight tasks, and MAX_TASKS is 16 with the idle task, init and
 * the shell already in it. Asking for more is refused with a message rather than
 * silently reinterpreted, which is what the parser used to do.
 */
#define MAX_STAGES 4

/*
 * Storage for '~' expansions, handed out per token and reset per command.
 *
 * One shared buffer used to serve every '~' in a line, so each expansion
 * overwrote the last and every token ended up pointing at the same string:
 * "cp ~/a ~/b" passed ~/b twice.
 */
static char tilde_arena[512];
static uint32_t tilde_used = 0;

/**
 * @brief Performs a system call.
 * 
 * @param num System call number.
 * @param arg1 First argument.
 * @param arg2 Second argument.
 * @param arg3 Third argument.
 * @return Return value of the system call.
 */
static inline int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" 
                 : "=a" (ret) 
                 : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) 
                 : "memory");
    return ret;
}

/**
 * @brief Compares two strings.
 * 
 * @param s1 First string.
 * @param s2 Second string.
 * @return Difference between first non-matching characters.
 */
int ft_strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * @brief Copies a string.
 * 
 * @param dest Destination buffer.
 * @param src Source string.
 */
void ft_strcpy(char *dest, const char *src) {
    while(*src) *dest++ = *src++;
    *dest = '\0';
}

/**
 * @brief Copies a string up to n characters.
 * 
 * @param dest Destination buffer.
 * @param src Source string.
 * @param n Maximum characters to copy.
 */
void ft_strncpy(char *dest, const char *src, int n) {
    int i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++) dest[i] = src[i];
    dest[i] = '\0';
}

/**
 * @brief Compares two strings up to n characters.
 * @param s1 First string.
 * @param s2 Second string.
 * @param n Maximum characters to compare.
 * @return Difference between first non-matching characters.
 */
int ft_strncmp(const char *s1, const char *s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == '\0') return 0;
    }
    return 0;
}

/**
 * @brief Calculates the length of a string.
 * 
 * @param s The string.
 * @return Length of the string.
 */
int ft_strlen(const char *s) {
    int i = 0; while(s[i]) i++; return i;
}

/**
 * @brief Locates a substring within a string.
 * 
 * @param haystack String to search in.
 * @param needle Substring to search for.
 * @return Pointer to the beginning of the located substring, or NULL.
 */
char *ft_strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while (haystack[i + j] && haystack[i + j] == needle[j]) {
            if (!needle[j + 1]) return (char *)&haystack[i];
            j++;
        }
    }
    return 0;
}

/**
 * @brief Converts an integer to a string.
 * 
 * @param n Integer to convert.
 * @param buf Buffer to store the resulting string.
 */
void ft_itoa(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char temp[16]; int i = 0;
    while(n > 0) { temp[i++] = (n % 10) + '0'; n /= 10; }
    int j = 0;
    while(i > 0) { buf[j++] = temp[--i]; }
    buf[j] = '\0';
}

/**
 * @brief Converts a hexadecimal string to an integer.
 * 
 * @param hex_str Hexadecimal string.
 * @return Converted integer value.
 */
uint32_t hex_to_int(const char *hex_str) {
    uint32_t val = 0;
    if (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X')) hex_str += 2;
    while (*hex_str) {
        char c = *hex_str++; val = val * 16;
        if (c >= '0' && c <= '9') val += (c - '0');
        else if (c >= 'a' && c <= 'f') val += (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val += (c - 'A' + 10);
        else return 0;
    }
    return val;
}

/**
 * @brief Converts a decimal string to a non-negative integer.
 *
 * Separate from hex_to_int() above, which is what the shell had and which would
 * read "10" as sixteen. Anything that is not a digit ends the number, and a
 * string with no leading digit at all reports -1 rather than 0 - "sleep abc"
 * should complain, not return instantly.
 *
 * @param str Decimal string.
 * @return The parsed value, or -1 when the string does not start with a digit.
 */
int dec_to_int(const char *str) {
    if (!str || *str < '0' || *str > '9') return -1;

    int val = 0;
    while (*str >= '0' && *str <= '9') {
        /* Stop well short of overflow; nothing here needs large numbers. */
        if (val > 100000000) return -1;
        val = val * 10 + (*str - '0');
        str++;
    }
    return val;
}

/**
 * @brief Computes a salted DJB2 hash for a string.
 *
 * @param str String to hash.
 * @return The computed hash.
 */
uint32_t hash_djb2_salted(const char *str) {
    uint32_t hash = 5381;
    while (*str) {
        hash = ((hash << 5) + hash) + *str++;
    }
    hash = ((hash << 5) + hash) + '4';
    hash = ((hash << 5) + hash) + '2';
    return hash;
}

/**
 * @brief Prints a string to standard output.
 * 
 * @param str String to print.
 */
void print(const char *str) {
    syscall(SYSCALL_WRITE, 1, (int)str, ft_strlen(str)); 
}

/**
 * @brief Sets the system DEFCON (security) level.
 * 
 * @param level The security level to set.
 */
void set_defcon(int level) {
    syscall(SYSCALL_SET_SEC_LEVEL, level, 0, 0);
    print("\n[!] System Security Level Changed!\n");
}


/* KERNEL SYSTEM CALL WRAPPERS */
/**
 * @brief Prints a string to standard output.
 * @param str The string to print.
 */
void printk(const char *str) { syscall(SYSCALL_WRITE, 1, (int)str, ft_strlen(str)); }
/**
 * @brief Reads a single character from the keyboard.
 * @return The character read.
 */
char get_keyboard_char(void) { char c = 0; syscall(SYSCALL_READ, 0, (int)&c, 1); return c;}
/**
 * @brief Reads one character, keeping the result the kernel returned.
 *
 * get_keyboard_char() folds every outcome into the character itself, so a read
 * cut short by a signal is indistinguishable from one that produced a zero byte.
 * Anything that has to tell those apart - the line reader, so that Ctrl-C can
 * abandon a half-typed line - asks through here instead.
 *
 * @param out Receives the character.
 * @return 1 when a character arrived, 0 at end of input, negative on error.
 */
int sys_read_char(char *out) { return syscall(SYSCALL_READ, 0, (int)out, 1); }
/**
 * @brief Creates a new file via system call.
 * @param name File name.
 * @param content File content.
 * @return System call status.
 */
int sys_create_file(const char *name, const char *content) { return syscall(8, (int)name, (int)content, 0); }
/**
 * @brief Deletes a file via system call.
 * @param name File name.
 * @return System call status.
 */
int sys_delete_file(const char *name) { return syscall(22, (int)name, 0, 0); }
/**
 * @brief Reads a file content via system call.
 * @param name File name.
 * @return System call status.
 */
int sys_cat_file(const char *name) { return syscall(11, (int)name, 0, 0); }
/**
 * @brief Reads raw file content via system call.
 * @param name File name.
 * @return System call status.
 */
int sys_cat_raw_file(const char *name) { return syscall(34, (int)name, 0, 0); } // 34 = SYSCALL_CAT_RAW 
/**
 * @brief Renames a file via system call.
 * @param old_name Current name.
 * @param new_name New name.
 * @return System call status.
 */
int sys_rename_file(const char *old_name, const char *new_name) { return syscall(23, (int)old_name, (int)new_name, 0); }
/**
 * @brief Receives an IPC message.
 * @param sender Pointer to store sender ID.
 * @param payload Pointer to store message payload.
 * @return System call status.
 */
int sys_receive_message(uint32_t *sender, uint32_t *payload) { return syscall(SYSCALL_IPC_RECEIVE, (int)sender, (int)payload, 0); }
/**
 * @brief Sets process priority.
 * @param pid Process ID.
 * @param priority Priority level.
 */
void sys_set_priority(int pid, int priority) { syscall(SYSCALL_SET_PRIORITY, pid, priority, 0); }
/**
 * @brief Exits the current process.
 */
void sys_exit(void) { syscall(SYSCALL_EXIT, 0, 0, 0); while(1); }
/**
 * @brief Exits with a status the parent can collect.
 * @param code Exit status, 0-255.
 */
void sys_exit_status(int code) { syscall(SYSCALL_EXIT, code, 0, 0); while(1); }
/**
 * @brief Duplicates this process.
 * @return 0 in the child, the child's pid in the parent, negative on failure.
 */
int sys_fork(void) { return syscall(SYSCALL_FORK, 0, 0, 0); }
/**
 * @brief Collects whatever a child has to report.
 *
 * One wrapper rather than three. There used to be a blocking one and a
 * non-blocking one, and every caller here now has to say whether it wants to
 * hear about a child that stopped as well as one that finished - which is a
 * third axis, and two more wrappers to hold every pairing of it.
 *
 * @param status Receives the status; may be NULL when only the pid is wanted.
 * @param flags WNOHANG, WUNTRACED, both, or neither.
 * @return The pid that reported, 0 when nothing is ready and WNOHANG was given,
 *         or E_CHILD when there is nothing left to wait for.
 */
int sys_wait_flags(int *status, int flags) { return syscall(SYSCALL_WAIT, (int)status, flags, 0); }

/*
 * Background jobs started with '&'.
 *
 * The shell has to remember them for two reasons. One is `jobs`, which has
 * nothing to list otherwise. The other is that a finished child's exit status
 * sits in a fixed table in the kernel until somebody collects it, and a shell
 * that never collects would fill it and start losing the statuses of other
 * people's children.
 */
#define MAX_JOBS 8

/**
 * @brief One job the shell is keeping track of.
 *
 * A job is a process group rather than a process, because that is what the
 * terminal addresses: "ls | grep etc &" is three tasks and one job, and
 * interrupting it, stopping it or handing it the terminal has to reach all
 * three.
 *
 * Every member is recorded, not just the leader. A stopped job is brought back
 * with fg, and fg has to wait for the whole of it - a pipeline whose middle
 * stage was still running would otherwise be left behind while the shell printed
 * its prompt. The group would be enough to signal them, but there is no call
 * that asks the kernel who is in a group, and the shell forked them all and
 * therefore already knows.
 *
 * The number is stable for as long as the job lives, which is what makes "%1"
 * mean something. `jobs` used to print the table position, and that renumbers
 * every time an earlier job is collected - so the name of a job changed under
 * the user between one listing and the next.
 */
typedef struct {
    int id;
    int pgid;
    int pids[MAX_STAGES];
    int npid;
    int stopped;    /**< 1 once a member has reported that it stopped. */
} job_t;

static job_t jobs[MAX_JOBS];
static int job_count = 0;
static int next_job_id = 1;

/** @brief wait_foreground() saying the job stopped rather than finished. */
#define JOB_STOPPED (-1000)

/**
 * @brief Records a job, if there is room to.
 *
 * @param pgid Group the job's processes are in.
 * @param pids Every process in it.
 * @param npid How many, at most MAX_STAGES.
 * @param stopped 1 when it is being recorded because it stopped.
 * @return The job number, or 0 when the table is full.
 */
static int job_add(int pgid, const int *pids, int npid, int stopped) {
    if (job_count >= MAX_JOBS) {
        /* Said out loud rather than dropped. The job still runs and is still
         * reaped by the sweep below - it just cannot be named. */
        printk("sh: too many jobs to track\n");
        return 0;
    }

    if (npid > MAX_STAGES) npid = MAX_STAGES;

    jobs[job_count].id = next_job_id++;
    jobs[job_count].pgid = pgid;
    jobs[job_count].npid = npid;
    jobs[job_count].stopped = stopped;
    for (int i = 0; i < npid; i++) jobs[job_count].pids[i] = pids[i];

    return jobs[job_count++].id;
}

/**
 * @brief Finds the job holding a pid.
 * @param pid Process to look for.
 * @return Index into jobs[], or -1.
 */
static int job_slot_by_pid(int pid) {
    for (int i = 0; i < job_count; i++) {
        for (int k = 0; k < jobs[i].npid; k++) {
            if (jobs[i].pids[k] == pid) return i;
        }
    }
    return -1;
}

/**
 * @brief Finds a job by its number.
 * @param id Job number as `jobs` prints it.
 * @return Index into jobs[], or -1.
 */
static int job_slot_by_id(int id) {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].id == id) return i;
    }
    return -1;
}

/**
 * @brief Drops a job from the table.
 * @param slot Index into jobs[].
 */
static void job_remove_at(int slot) {
    if (slot < 0 || slot >= job_count) return;
    for (int j = slot; j < job_count - 1; j++) jobs[j] = jobs[j + 1];
    job_count--;
}

/**
 * @brief Forgets one process of a job, and the job itself once it is empty.
 *
 * @param pid Process that exited.
 * @param job_id_out Receives the job number it belonged to, or 0.
 * @return 1 when that was the job's last process.
 */
static int job_forget_pid(int pid, int *job_id_out) {
    if (job_id_out) *job_id_out = 0;

    int slot = job_slot_by_pid(pid);
    if (slot < 0) return 0;

    if (job_id_out) *job_id_out = jobs[slot].id;

    int keep = 0;
    for (int k = 0; k < jobs[slot].npid; k++) {
        if (jobs[slot].pids[k] != pid) jobs[slot].pids[keep++] = jobs[slot].pids[k];
    }
    jobs[slot].npid = keep;

    if (keep == 0) {
        job_remove_at(slot);
        return 1;
    }
    return 0;
}

/**
 * @brief Reports a child that finished while the shell was doing something else.
 * @param pid Process that reported.
 * @param status Its exit status.
 */
static void job_report_done(int pid, int status) {
    char num[16];
    int job_id = 0;
    int last = job_forget_pid(pid, &job_id);

    /* A job is only done when its last process is. The stages of a pipeline
     * finish one at a time and the user asked about the pipeline. */
    if (job_id != 0 && !last) return;

    if (job_id != 0) {
        printk("[");
        ft_itoa(job_id, num); printk(num);
        printk("] done pid ");
    } else {
        printk("[done] pid ");
    }

    ft_itoa(pid, num);    printk(num);
    printk(" status ");
    ft_itoa(status, num); printk(num);
    printk("\n");
}

/**
 * @brief Notes that a job has stopped, and says so once.
 * @param pid Process that reported the stop.
 */
static void job_report_stopped(int pid) {
    int slot = job_slot_by_pid(pid);
    if (slot < 0) return;

    /*
     * Every member of a stopped pipeline parks a notice of its own, so this is
     * reached once per stage for one Ctrl-Z. The user pressed one key and is
     * told once.
     */
    if (jobs[slot].stopped) return;

    jobs[slot].stopped = 1;

    char num[16];
    printk("[");
    ft_itoa(jobs[slot].id, num); printk(num);
    printk("] stopped\n");
}

/**
 * @brief Collects anything a job has to report, without waiting.
 *
 * Called before each prompt. Reporting here rather than the moment it arrives is
 * deliberate: a job finishing in the middle of a line being typed would
 * otherwise print over it.
 */
static void jobs_reap(void) {
    for (;;) {
        int status = 0;
        int pid = sys_wait_flags(&status, WNOHANG | WUNTRACED);
        if (pid <= 0) return;   /* 0: nothing ready. negative: no children. */

        if (status & WSTATUS_STOPPED) {
            job_report_stopped(pid);
            continue;
        }

        job_report_done(pid, status);
    }
}

/**
 * @brief Resolves a job reference typed by the user.
 *
 * "%2" and a bare "2" both name job 2, and nothing at all names the most recent
 * job - which is what "fg" on its own has meant in every shell since the idea
 * existed. The newest is the highest number rather than the last table entry:
 * entries move when an earlier job is collected.
 *
 * @param word The argument, or NULL.
 * @return Index into jobs[], or -1 when it names nothing.
 */
static int job_resolve(const char *word) {
    if (job_count == 0) return -1;

    if (word == 0 || word[0] == '\0') {
        int best = 0;
        for (int i = 1; i < job_count; i++) {
            if (jobs[i].id > jobs[best].id) best = i;
        }
        return best;
    }

    if (word[0] == '%') word++;
    if (word[0] == '\0') return -1;

    for (int i = 0; word[i] != '\0'; i++) {
        if (word[i] < '0' || word[i] > '9') return -1;
    }

    return job_slot_by_id(dec_to_int(word));
}

/*
 * Whether this process is the shell at the prompt rather than something it
 * forked.
 *
 * A pipeline stage and a background job both go on running this same image after
 * fork(), and both start programs of their own through run_external(). Neither
 * may put those programs in a group of its own or touch the terminal: a stage
 * belongs to the pipeline it is part of, and a background job has deliberately
 * been kept out of the foreground. Only the shell the user is typing at owns
 * what the terminal points to.
 */
static int shell_owns_terminal = 1;

/**
 * @brief Waits for a foreground job until it finishes or stops.
 *
 * Anything else that reports along the way is dealt with rather than dropped: a
 * background job finishing while a foreground command runs would otherwise leave
 * a status parked in a table the kernel only has sixteen slots of, and the job
 * itself listed forever.
 *
 * @param pids   Every process in the job.
 * @param npid   How many.
 * @param pgid   The job's group, needed if it has to become a table entry.
 * @param lead   The process whose exit status is the job's - the last stage of a
 *               pipeline, which is what a shell reports for one.
 * @param job_id Existing job number when the job is already tracked (fg), 0 when
 *               it is a fresh foreground command that is not in the table.
 * @return The job's exit status, or JOB_STOPPED.
 */
static int wait_foreground(const int *pids, int npid, int pgid, int lead, int job_id) {
    int status = 0;
    int live = npid;

    while (live > 0) {
        int st = 0;
        int who = sys_wait_flags(&st, WUNTRACED);

        /* E_CHILD says the kernel has nothing left to report to us at all, which
         * is the only answer that ends this loop other than the job finishing. */
        if (who <= 0) break;

        int mine = 0;
        for (int i = 0; i < npid; i++) {
            if (pids[i] == who) { mine = 1; break; }
        }

        if (st & WSTATUS_STOPPED) {
            /*
             * One member reporting is the whole job stopping as far as the table
             * is concerned. Ctrl-Z went to the group, and the rest are parking
             * notices of their own behind this one; those are collected at the
             * next prompt and say nothing new, and the kernel withdraws whatever
             * is left of them when the job is continued.
             */
            if (mine && job_id == 0) job_add(pgid, pids, npid, 0);

            job_report_stopped(who);
            if (mine) return JOB_STOPPED;
            continue;
        }

        if (mine) {
            if (who == lead) status = st;
            /* Keep the table honest while the job is still in it: a later fg
             * must not wait for a stage that has already finished. */
            if (job_id != 0) job_forget_pid(who, 0);
            live--;
            continue;
        }

        /* Somebody else's job finished while this one held the terminal. */
        job_report_done(who, st);
    }

    if (job_id != 0) job_remove_at(job_slot_by_id(job_id));
    return status;
}
/*
 * Spelled out here rather than included from the kernel's signal.h, which is the
 * same thing the Ring 3 test payloads do: these programs are freestanding
 * translation units and that header reaches into kernel types. The numbers are
 * POSIX's, and signal.h has to agree.
 */
#define SIG_PIPE 13
#define SIG_INT   2   /* Ctrl-C, sent to the whole foreground group */
#define SIG_TSTP 20   /* Ctrl-Z, which stops the foreground group instead */
#define SIG_CONT 18   /* puts a stopped group back to work */
#define SIG_DFL   0   /* default action: terminate or stop, for the signals that have one */
#define SIG_IGN   1   /* discard the signal */

/**
 * @brief Registers a signal handler.
 * @param sig_num Signal number.
 * @param handler Pointer to the handler function, or SIG_DFL / SIG_IGN.
 */
void sys_register_signal(int sig_num, void *handler) { syscall(SYSCALL_SIGNAL_REG, sig_num, (int)handler, 0); }
/**
 * @brief Places a process in a process group.
 *
 * A pid of 0 means this process, and a group of 0 founds a new one named by that
 * pid. Called from both sides of a fork with the same arguments, which is the
 * ordinary answer to a race neither side can win.
 *
 * @param pid Process to place, or 0 for the caller.
 * @param pgid Group to join, or 0 to found one.
 * @return 0, or a negative errno.
 */
int sys_setpgid(int pid, int pgid) { return syscall(60, pid, pgid, 0); }
/**
 * @brief Hands the terminal to a process group, which is what Ctrl-C reaches.
 * @param pgid Group to put in the foreground.
 * @return 0, or a negative errno.
 */
int sys_tcsetpgrp(int pgid) { return syscall(61, pgid, 0, 0); }
/**
 * @brief Reads a process's group.
 * @param pid Process to ask about, or 0 for the caller.
 * @return The group, or a negative errno.
 */
int sys_getpgid(int pid) { return syscall(62, pid, 0, 0); }
/**
 * @brief Sends a signal to a process.
 * @param pid Process ID.
 * @param sig_num Signal number.
 */
/* Returns the syscall's verdict rather than discarding it, so the caller can
 * report whether the signal was actually delivered. */
int sys_kill(int pid, int sig_num) { return syscall(SYSCALL_KILL, pid, sig_num, 0); }
/**
 * @brief Returns from a signal handler.
 */
void sys_sigreturn(void) { syscall(SYSCALL_SIGRETURN, 0, 0, 0); }
/**
 * @brief Sets the user ID.
 * @param uid User ID.
 * @param password Password for authentication.
 * @return System call status.
 */
int sys_setuid(int uid, const char *password) { return syscall(SYSCALL_SETUID, uid, (int)password, 0); }
/**
 * @brief Creates a directory.
 * @param name Directory name.
 * @return System call status.
 */
int sys_mkdir(const char *name) { return syscall(26, (int)name, 0, 0); }
/*
 * SYSCALL_LS_DIR (28) used to back the "ls" builtin and no longer does. It
 * prints the listing from the kernel with terminal_putchar(), so its output
 * never reached this process's descriptor 1 - "ls | grep" saw an empty pipe and
 * "ls > file" produced an empty file. builtin_ls() below reads the same entries
 * through SYSCALL_READDIR and prints them itself. The syscall stays for any
 * caller that genuinely wants a screen dump.
 */
/**
 * @brief Gets the directory ID.
 * @param name Directory name.
 * @return Directory ID.
 */
int sys_get_dir_id(const char *name) { return syscall(29, (int)name, 0, 0); }
/**
 * @brief Reads directory entries into buffer.
 * @param dir_id Directory ID to read.
 * @param buf Buffer to store null-separated filenames.
 * @param buf_size Buffer size.
 * @return Total bytes written.
 */
int sys_readdir(int dir_id, char *buf, int buf_size) { return syscall(44, dir_id, (int)buf, buf_size); }
/**
 * @brief Creates a pipe.
 * @param pipefd Array to store the read and write file descriptors.
 * @return System call status.
 */
int pipe(int pipefd[2]) { return syscall(SYSCALL_PIPE, (int)pipefd, 0, 0); }
/**
 * @brief Duplicates a file descriptor.
 * @param oldfd Old file descriptor.
 * @param newfd New file descriptor.
 * @return System call status.
 */
int dup2(int oldfd, int newfd) { return syscall(SYSCALL_DUP2, oldfd, newfd, 0); }
/**
 * @brief Closes a file descriptor.
 * @param fd File descriptor to close.
 * @return System call status.
 */
int sys_close(int fd) { return syscall(SYSCALL_CLOSE, fd, 0, 0); }
/**
 * @brief Reads a slice of the kernel ring buffer into a buffer.
 *
 * The kernel used to print the log itself, which meant it went to the screen and
 * nowhere else. Reading it here and writing it below sends it through descriptor
 * 1, so it can be piped and redirected like anything else - and keeps the
 * position in this loop rather than in a kernel dump that cannot block partway.
 *
 * @param buf Destination buffer.
 * @param size Capacity of buf.
 * @param index Record position, counted from the oldest still held. It was a
 *              byte offset; the log holds records now, and a position in bytes
 *              cannot survive one being dropped between two reads.
 * @return Bytes read, 0 past the last record, or a negative errno.
 */
int sys_dmesg_read(char *buf, int size, int index) { return syscall(SYSCALL_DMESG, (int)buf, size, index); }
/**
 * @brief Inspects or changes the kernel log.
 *
 * @param op One of the KLOG_CTL_* operations.
 * @param arg Argument for the operation, where one applies.
 * @return The requested value, 0, or a negative errno.
 */
int sys_klog_ctl(int op, int arg) { return syscall(59, op, arg, 0); }
/**
 * @brief Opens a file.
 * @param name File name.
 * @return File descriptor.
 */
int sys_open(const char *name) { return syscall(SYSCALL_OPEN, (int)name, 0, 0); }

/**
 * @brief Opens a file for writing, which truncates it.
 *
 * The second syscall argument is the mode; it was ignored by the kernel until
 * v0.4.3, which is why nothing could write to a file. 1 is write.
 */
int sys_open_write(const char *name) { return syscall(SYSCALL_OPEN, (int)name, 1, 0); }
/**
 * @brief Changes the working directory. The kernel resolves the path.
 * @param path Absolute or relative path.
 * @return E_OK, or a negative errno.
 */
int sys_chdir(const char *path) { return syscall(SYSCALL_CHDIR, (int)path, 0, 0); }
/**
 * @brief Reads the working directory into buf.
 * @return Path length on success, or a negative errno.
 */
int sys_getcwd(char *buf, int size) { return syscall(SYSCALL_GETCWD, (int)buf, size, 0); }
/* --- 4. ENVIRONMENT VARIABLES (ENV) --- */
char env_keys[20][32];
char env_vals[20][64];
int env_count = 0;
int last_exit_status = 0; 
char current_path[64] = "/";
int current_uid = -1;
char current_username[32];

/*
 * The prompt used to have this in a string literal, twice - once where the
 * prompt is printed and once where tab completion redraws it. It is read from
 * /etc/hostname now, and the literal survives only as the fallback for a disk
 * that does not have the file.
 */
char current_hostname[32] = "esdumanOS";

/**
 * @brief Sets an environment variable.
 * 
 * @param key Variable name.
 * @param val Variable value.
 */
void set_env(const char *key, const char *val) {
    for(int i = 0; i < env_count; i++) {
        if(ft_strcmp(env_keys[i], key) == 0) { ft_strncpy(env_vals[i], val, 64); return; }
    }
    if(env_count < 20) {
        ft_strncpy(env_keys[env_count], key, 32); 
        ft_strncpy(env_vals[env_count], val, 64); 
        env_count++;
    }
}

/**
 * @brief Gets the value of an environment variable.
 * 
 * @param key Variable name.
 * @return The value of the variable, or empty string if not found.
 */
char* get_env(const char *key) {
    for(int i = 0; i < env_count; i++) {
        if(ft_strcmp(env_keys[i], key) == 0) return env_vals[i];
    }
    return ""; 
}

/**
 * @brief Custom signal handler for the shell.
 */
void my_custom_handler(void) {
    printk("\n[!!!] esdumanOS CAUGHT USER SIGNAL! [!!!]\n");
    sys_sigreturn();
}

/* Defined with the line editor further down, and needed here for the one thing
 * this prompt has to do about escape sequences: swallow them. */
static int read_escape_key(void);

/* Also from the line editor. Tab completion has to put the prompt back after it
 * lists candidates, and it used to do that with its own copy of these lines -
 * which meant the editor's idea of how wide the prompt is would have been left
 * behind by the one path that reprints it. */
static void print_prompt(void);

/**
 * @brief Reads a line from the user.
 *
 * Deliberately not the line editor the command prompt uses. There is nothing to
 * navigate in a password and nothing to recall, and moving a cursor through a
 * field displayed as asterisks would be a way to get it wrong without being able
 * to see it. Escape sequences are consumed and ignored - the arrow keys now send
 * three bytes each, and two of them are printable, so a shell that let them
 * through would put "[A" in the password.
 *
 * @param buf Buffer to store the line.
 * @param hide Whether to hide characters (e.g., for passwords).
 * @param max_len Maximum length of the buffer.
 */
int read_line(char *buf, int hide, int max_len) {
    int idx = 0;
    while (1) {
        char c = 0;
        int r = sys_read_char(&c);

        /*
         * Ctrl-C. The line is abandoned rather than kept, which is what a
         * terminal does: whatever was half-typed is gone and the caller starts
         * again. The kernel has already echoed the "^C", so there is nothing to
         * print here.
         */
        if (r == E_INTR) { buf[0] = '\0'; return -1; }

        /* Nothing readable this time round - end of input, or a read that
         * produced no byte. Unchanged from before. */
        if (r <= 0) continue;

        /* Swallowed whole. See the note above the function. */
        if (c == 27) { read_escape_key(); continue; }

        if (c == '\n' || c == '\r') { buf[idx] = '\0'; printk("\n"); return 0; }
        else if (c == '\b') { if (idx > 0) { idx--; printk("\b \b"); } }
        else if (c >= 32 && c <= 126 && idx < max_len - 1) {
            char str[2] = { hide ? '*' : c, '\0' };
            printk(str);
            buf[idx++] = c;
        }
    }
}

/**
 * @brief Displays the help menu showing available commands.
 */
void show_help(void) {
    printk("esdumanOS Shell — Available Commands:\n\n");
    printk("  File Operations:\n");
    printk("    ls               List directory contents\n");
    printk("    cat [-nbEsTA] [f] Read and display file contents\n");
    /* Not a builtin, and listed anyway - as echo already is. A user who types
     * `help` and never finds out the system has an editor has been failed by the
     * help rather than by the editor. */
    printk("    edit [file]       Edit a file. Modal: i inserts, ESC returns, :w :q\n");
    printk("                      u undoes, / searches, n and N repeat it\n");
    printk("    cat_raw [file]    Show raw (HEX) disk dump (bypasses decryption)\n");
    printk("    write [f] [text]  Create/write a file\n");
    printk("    rm [file]         Delete a file\n");
    printk("    mv [old] [new]    Rename a file\n");
    printk("    mkdir [dir]       Create a directory\n");
    printk("\n  Navigation:\n");
    printk("    cd [dir]          Change directory (supports ., .., ~, -)\n");
    printk("    pwd               Print working directory\n");
    printk("\n  Process & System:\n");
    printk("    exec [program]    Execute an ELF binary\n");
    printk("    kill [pid] [sig]  Send signal to a process\n");
    printk("    sleep [seconds]   Pause for a number of seconds\n");
    printk("    su                Switch to root user\n");
    printk("    reboot            Reboot the system\n");
    printk("    halt              Halt the processor\n");
    printk("    exit              Exit the shell\n");
    printk("\n  Information:\n");
    printk("    echo [-n] [text]  Print text (supports > redirect)\n");
    printk("    env               Show environment variables\n");
    printk("    export [K] [V]    Set environment variable\n");
    printk("    meminfo           Display RAM information\n");
    printk("    dmesg [-c|-n L|-l L]  Kernel log: show, clear, set level, filter\n");
    printk("    sync              Flush the disk cache and write the log to /var/log\n");
    printk("    jobs              List jobs this shell started and still tracks\n");
    printk("    fg [%n]           Bring a job to the foreground and wait for it\n");
    printk("    bg [%n]           Continue a stopped job in the background\n");
    printk("    wait              Block until every background job has finished\n");
    printk("    hexdump [addr]    Show memory dump at address\n");
    printk("    help              Show this help menu\n");
    printk("\n  Settings:\n");
    printk("    layout tr|us      Set keyboard layout\n");
    printk("    lockdown          Switch system to safe mode\n");
    printk("    clear             Clear the screen\n");
    printk("\n  Operators: | (pipe, two stages), > (redirect), && (AND), || (OR)\n");
    printk("  Note: > and | cannot be combined in one command.\n");
    printk("  Ctrl-D ends input for a program reading the keyboard (cat, grep, head, wc).\n");
}

/**
 * @brief Built-in cat command to display file contents.
 * 
 * @param args Array of command-line arguments.
 * @return Exit status of the command.
 */
int builtin_cat(char **args) {
    int flag_n = 0, flag_b = 0, flag_E = 0, flag_s = 0, flag_T = 0;
    int file_args_start = 1;
    
    for (int i = 1; args[i] != 0; i++) {
        if (args[i][0] == '-' && args[i][1] != '\0') {
            for (int j = 1; args[i][j] != '\0'; j++) {
                char c = args[i][j];
                if (c == 'n') flag_n = 1;
                else if (c == 'b') flag_b = 1; 
                else if (c == 'E') flag_E = 1;
                else if (c == 's') flag_s = 1;
                else if (c == 'T') flag_T = 1;
                else if (c == 'A') { flag_E = 1; flag_T = 1; }
                else {
                    printk("cat: Invalid option -- \'"); 
                    char err[2] = {c, '\0'}; printk(err); printk("'\n");
                    return 1;
                }
            }
            file_args_start++;
        } else break; 
    }

    /*
     * No file named means read standard input, which is what makes this the
     * consumer end of a pipeline.
     *
     * It used to be an error, and that left the shell with pipes and nothing
     * able to read one: grep and head both open a file by name, and so did this.
     * `a | b` could be parsed, forked and connected, and there was no b that
     * would take it.
     */
    int use_stdin = (args[file_args_start] == 0);

    for (int i = file_args_start; use_stdin || args[i] != 0; i++) {
        int fd;

        if (use_stdin) {
            fd = 0;
        } else {
            fd = sys_open(args[i]);
            if (fd < 0) {
                printk("cat: "); printk(args[i]); printk(": No such file or directory.\n");
                continue;
            }
        }
        
        char buf[256];          
        char out_buf[256];      
        int out_idx = 0;        
        int bytes_read;
        int line_num = 1;
        int is_new_line = 1;
        int consecutive_empty_lines = 0;

        // [FIX]: printk now only takes a single argument (out_buf). "%s" removed!
        #define FLUSH_OUT() do { \
            if (out_idx > 0) { \
                out_buf[out_idx] = '\0'; \
                printk(out_buf); \
                out_idx = 0; \
            } \
        } while(0)

        // Read loop from VFS
        while ((bytes_read = syscall(3 /* SYSCALL_READ */, fd, (int)buf, 256)) > 0) {
            for (int k = 0; k < bytes_read; k++) {
                char c = buf[k];
                if (c == '\r' || c == '\b' || c == '\0') continue; 

                int is_empty_line = (c == '\n');
                
                if (flag_s && is_empty_line && is_new_line) {
                    consecutive_empty_lines++;
                    if (consecutive_empty_lines > 1) continue; 
                } else if (!is_empty_line) {
                    consecutive_empty_lines = 0;
                }

                if (out_idx > 240) { FLUSH_OUT(); }

                // [FIX]: Replaced "%s" usage in line numbering with separate prints.
                if (is_new_line) {
                    if (flag_b) {
                        if (!is_empty_line) {
                            FLUSH_OUT(); 
                            char num_str[16]; ft_itoa(line_num++, num_str);
                            printk("    "); printk(num_str); printk("  ");
                        }
                    } else if (flag_n) {
                        FLUSH_OUT();
                        char num_str[16]; ft_itoa(line_num++, num_str);
                        printk("    "); printk(num_str); printk("  ");
                    }
                    is_new_line = 0;
                }

                if (c == '\n') {
                    if (flag_E) out_buf[out_idx++] = '$'; 
                    out_buf[out_idx++] = '\n';
                    is_new_line = 1;
                } 
                else if (c == '\t' && flag_T) {
                    out_buf[out_idx++] = '^'; 
                    out_buf[out_idx++] = 'I';
                } 
                else {
                    out_buf[out_idx++] = c;
                }
            }
            FLUSH_OUT(); 
        }
        FLUSH_OUT(); 
        
        #undef FLUSH_OUT 

        /* Never close descriptor 0: it belongs to whoever started this process,
         * and a builtin closing it would leave the shell without input. */
        if (use_stdin) break;

        sys_close(fd); 
    }
    return 0;
}


/**
 * @brief Built-in ls: lists a directory through this process's standard output.
 *
 * Two things were wrong with what this replaces, and they had different causes.
 *
 * The listing came from SYSCALL_LS_DIR, which prints it inside the kernel with
 * terminal_putchar() - so it went to the screen whatever this process's
 * descriptor 1 pointed at. "ls | grep bin" read an empty pipe and "ls > names"
 * created an empty file. SYSCALL_READDIR hands the same entries back instead,
 * and printing them here means they travel the ordinary way.
 *
 * The argument was also simply never read: the old call passed the id of "."
 * whatever was typed after it, so "ls /bin" listed the working directory and
 * quietly looked like /bin had nothing in it.
 *
 * The format is unchanged apart from colour, which the kernel applied per entry
 * and this cannot - there is no syscall for it, and colour codes written into a
 * pipe or a file would be wrong anyway. The [DIR] and [FILE] markers carry the
 * same distinction in text.
 *
 * @param args Array of command-line arguments; args[1] is an optional directory.
 * @return Exit status of the command.
 */
/**
 * @brief Parses a log level from an argument.
 *
 * A digit or a name, because "dmesg -n 3" and "dmesg -n error" are both things a
 * person types and neither is more correct than the other.
 *
 * @param s Argument text.
 * @return Level 0..4, or -1 when it is neither.
 */
static int dmesg_parse_level(const char *s) {
    if (!s || !s[0]) return -1;

    if (s[0] >= '0' && s[0] <= '4' && s[1] == '\0') return s[0] - '0';

    if (ft_strcmp(s, "debug") == 0) return 0;
    if (ft_strcmp(s, "info") == 0)  return 1;
    if (ft_strcmp(s, "warn") == 0)  return 2;
    if (ft_strcmp(s, "error") == 0) return 3;
    if (ft_strcmp(s, "fatal") == 0) return 4;
    return -1;
}

/**
 * @brief Reads the severity out of a rendered log line.
 *
 * The level tag is the second bracketed field - "[   12.34] [WARN ] MM: ..." -
 * and is found by scanning rather than at a fixed offset, because the timestamp
 * grows a character wider every time the uptime gains a digit.
 *
 * The first letters happen to be unique across the five levels, which is what
 * lets this be a switch instead of five comparisons.
 *
 * @param line Rendered record.
 * @param len Bytes in @p line.
 * @return Level 0..4, or -1 when there is no recognisable tag.
 */
static int dmesg_line_level(const char *line, int len) {
    int i = 0;

    while (i < len && line[i] != ']') i++;
    while (i < len && line[i] != '[') i++;
    if (i + 1 >= len) return -1;

    switch (line[i + 1]) {
        case 'D': return 0;
        case 'I': return 1;
        case 'W': return 2;
        case 'E': return 3;
        case 'F': return 4;
        default:  return -1;
    }
}

/**
 * @brief Prints the kernel log, and the three things you do with it.
 *
 * `-n` sets the kernel's threshold and prints nothing. That is deliberately not
 * the same as `-l`: one changes what gets recorded from here on, the other
 * chooses what to show out of what already was, and running them together would
 * make "dmesg -n debug" look as though it had lost the log.
 *
 * `-l` filters here rather than in the kernel. The records carry their level as
 * a field, so the syscall surface stays as small as it was and the shell reads
 * the field it was given - which is how Linux does it too.
 *
 * @param args Argument vector; args[1] is an optional flag.
 * @return Exit status.
 */
int builtin_dmesg(char **args) {
    int min_level = -1;
    int clear_after = 0;

    if (args[1] && ft_strcmp(args[1], "-n") == 0) {
        int level = dmesg_parse_level(args[2]);
        if (level < 0) {
            printk("dmesg: -n needs a level: 0-4, or debug/info/warn/error/fatal\n");
            return 1;
        }
        if (sys_klog_ctl(KLOG_CTL_SET_LEVEL, level) < 0) {
            printk("dmesg: permission denied\n");
            return 1;
        }
        return 0;
    }

    if (args[1] && ft_strcmp(args[1], "-l") == 0) {
        min_level = dmesg_parse_level(args[2]);
        if (min_level < 0) {
            printk("dmesg: -l needs a level: 0-4, or debug/info/warn/error/fatal\n");
            return 1;
        }
    } else if (args[1] && ft_strcmp(args[1], "-c") == 0) {
        clear_after = 1;
    } else if (args[1]) {
        printk("dmesg: unknown option; try -c, -n LEVEL or -l LEVEL\n");
        return 1;
    }

    /*
     * One record per read, written from here so it goes through this process's
     * descriptor 1 and can be piped or redirected. The loop belongs in the shell
     * because a write into a full pipe blocks, and a blocked syscall resumes by
     * re-running from the trap - a kernel-side dump that blocked halfway would
     * start over and emit everything twice.
     *
     * The index counts records. It counted bytes until the log started holding
     * records, and a byte position cannot survive one of them being dropped
     * between two reads.
     */
    char line[256];
    int index = 0;
    int got;

    while ((got = sys_dmesg_read(line, sizeof(line), index)) > 0) {
        index++;
        if (min_level >= 0 && dmesg_line_level(line, got) < min_level) continue;
        syscall(SYSCALL_WRITE, 1, (int)line, got);
    }

    if (got < 0) {
        printk("dmesg: permission denied\n");
        return 1;
    }

    if (clear_after && sys_klog_ctl(KLOG_CTL_CLEAR, 0) < 0) {
        printk("dmesg: permission denied\n");
        return 1;
    }

    /*
     * How many records went when the ring wrapped. Nothing could ask this
     * before, so a gap in the log looked like a quiet period.
     *
     * On descriptor 2, not 1. It is a note about the log rather than part of it,
     * and putting it on stdout would drop it into the middle of "dmesg > boot.log"
     * and every pipeline that reads the log.
     */
    int dropped = sys_klog_ctl(KLOG_CTL_DROPPED, 0);
    if (dropped > 0) {
        char num[16];
        const char *pre = "[dmesg] records dropped when the ring wrapped: ";

        ft_itoa(dropped, num);
        syscall(SYSCALL_WRITE, 2, (int)pre, ft_strlen(pre));
        syscall(SYSCALL_WRITE, 2, (int)num, ft_strlen(num));
        syscall(SYSCALL_WRITE, 2, (int)"\n", 1);
    }

    return 0;
}

int builtin_ls(char **args) {
    int dir_id;

    if (args[1]) {
        dir_id = sys_get_dir_id(args[1]);
        if (dir_id < 0) {
            printk("ls: "); printk(args[1]); printk(": No such directory\n");
            return 1;
        }
    } else {
        dir_id = sys_get_dir_id(".");
        if (dir_id < 0) { printk("ls: cannot resolve the working directory\n"); return 1; }
    }

    /* One call, one buffer. A directory holding more entries than fit is
     * truncated by the kernel at an entry boundary rather than split across
     * calls; readdir has no cursor to resume from.
     *
     * Off the stack and onto the heap, which is the point: 4 KB is what the
     * kernel is willing to return and there was no room for it here. main()'s
     * frame carries the line buffer, the argument vector and the pipeline stage
     * tables, and the stack is 32 pages for the whole program. */
    char *dir_buf = (char *)umalloc(LS_BUF_SIZE);
    if (!dir_buf) { printk("ls: out of memory\n"); return 1; }

    int bytes = sys_readdir(dir_id, dir_buf, LS_BUF_SIZE);
    if (bytes < 0) { ufree(dir_buf); printk("ls: cannot read directory\n"); return 1; }

    printk("Contents:\n----------------------------------------\n");

    int off = 0;
    int found = 0;
    while (off < bytes) {
        char *name = &dir_buf[off];
        int nlen = 0;
        /* "name\x01" for a directory, "name\x02" for a file, then a null. */
        while (name[nlen] != 1 && name[nlen] != 2 && name[nlen] != '\0') nlen++;

        int is_dir = (name[nlen] == 1);
        name[nlen] = '\0';

        printk(is_dir ? "[DIR]  " : "[FILE] ");
        printk(name);
        printk("\n");

        found = 1;
        off += nlen + 2;
    }

    if (!found) printk("(Empty)\n");
    printk("----------------------------------------\n");

    ufree(dir_buf);
    return 0;
}

/**
 * @brief Executes a built-in or external command.
 *
 * @param args Array of command-line arguments.
 * @param redirect_file File to redirect output to (if any).
 */
/*
 * Redirection is set up by the caller rather than in here.
 *
 * This function took a redirect_file parameter and never used it - the whole
 * reason "cmd > file" printed to the terminal and created nothing. Wiring it up
 * inside the body would have been fragile: there are three early returns, and
 * any of them would skip the restore and leave the shell's stdout pointing at a
 * closed file for the rest of the session. run_with_redirect() below wraps the
 * call instead, so the teardown cannot be bypassed.
 */
/**
 * @brief Whether a command word names a path rather than a command.
 *
 * A word containing a slash is a path and is never matched against the builtin
 * table, which is the rule every real shell uses and the reason "/bin/rm" now
 * reaches the ELF of that name. Three programs in /bin - rm, mv and kill - share
 * their names with builtins that were checked first, so they had been shipped in
 * the image and were unreachable by any spelling.
 *
 * @param word The first word of a command.
 * @return 1 when the word contains a slash.
 */
void run_external(char **args);

static int word_is_path(const char *word) {
    for (int i = 0; word[i] != '\0'; i++) {
        if (word[i] == '/') return 1;
    }
    return 0;
}

void execute_command(char **args) {
    if (!args[0]) return;

    if (word_is_path(args[0])) {
        run_external(args);
        return;
    }

    if (ft_strcmp(args[0], "cat") == 0) {
        last_exit_status = builtin_cat(args);
    }
    else if (ft_strcmp(args[0], "pwd") == 0) { printk(current_path); printk("\n"); last_exit_status = 0; }
    else if (ft_strcmp(args[0], "env") == 0) {
        for(int i = 0; i < env_count; i++) { printk(env_keys[i]); printk("="); printk(env_vals[i]); printk("\n"); }
        last_exit_status = 0;
    }
    else if (ft_strcmp(args[0], "export") == 0) {
        if (args[1] && args[2]) { set_env(args[1], args[2]); last_exit_status = 0; } 
        else { printk("Error. Example: export LANG EN\n"); last_exit_status = 1; }
    }
    else if (ft_strcmp(args[0], "help") == 0) { show_help(); last_exit_status = 0; }
    else if (ft_strcmp(args[0], "ls") == 0) { last_exit_status = builtin_ls(args); }
    else if (ft_strcmp(args[0], "mkdir") == 0) {
        if (args[1]) last_exit_status = (sys_mkdir(args[1]) == E_OK) ? 0 : 1;
        else { printk("Usage: mkdir <directory>\n"); last_exit_status = 1; }
    }
    else if (ft_strcmp(args[0], "cd") == 0) {
        static char old_path[256] = {0};
        char *target = args[1];
        
        // Handle cd without arguments or cd ~
        if (!target || ft_strcmp(target, "~") == 0) {
            target = get_env("HOME");
            if (!target) target = "/";
        }
        // Handle cd -
        else if (ft_strcmp(target, "-") == 0) {
            if (old_path[0] == '\0') {
                printk("sh: cd: OLDPWD not set\n");
                last_exit_status = 1;
                return;
            }
            target = old_path;
            printk(target); printk("\n");
        }

        /*
         * The kernel owns the working directory now, so it also owns resolving
         * the path. This used to build an absolute path by hand and then
         * canonicalize it here - splitting on '/', pushing and popping tokens to
         * fold "." and ".." - all of which vfs_resolve_path() already does, and
         * did even then. The shell's copy could disagree with the kernel's view;
         * now there is only one.
         */
        int rc = sys_chdir(target);
        if (rc == E_OK) {
            ft_strcpy(old_path, current_path);   /* OLDPWD, for "cd -" */

            /* Re-read rather than predict: the prompt should show where the
             * kernel actually put us, not where we asked to go. */
            char resolved[64];
            int len = sys_getcwd(resolved, sizeof(resolved));
            if (len > 0) ft_strcpy(current_path, resolved);

            last_exit_status = 0;
        } else {
            if (rc == E_ACCES) {
                printk("sh: cd: "); printk(args[1] ? args[1] : "~"); printk(": Permission denied\n");
            } else if (rc == E_NOTDIR) {
                printk("sh: cd: "); printk(args[1] ? args[1] : "~"); printk(": Not a directory\n");
            } else {
                printk("sh: cd: "); printk(args[1] ? args[1] : "~"); printk(": No such file or directory\n");
            }
            last_exit_status = 1;
        }
    }
    else if (ft_strcmp(args[0], "write") == 0) {
        if (args[1] && args[2]) {
            char *content = args[2];
            for(int i = 2; args[i] != 0; i++) { if (args[i+1] != 0) args[i][ft_strlen(args[i])] = ' '; }
            int res = sys_create_file(args[1], content);
            if (res == E_OK) printk("File written successfully!\n");
            else if (res == E_ACCES) printk("write: Permission denied\n");
            else { printk("write: Failed to create file\n"); }
            last_exit_status = (res == E_OK) ? 0 : 1;
        } else { printk("Usage: write <file> <content>\n"); last_exit_status = 1; }
    }
    else if (ft_strcmp(args[0], "rm") == 0) {
        /* The return value was being discarded, so "rm /nope && echo GONE"
         * printed GONE. Every builtin below now reports what it did. */
        if (args[1]) last_exit_status = (sys_delete_file(args[1]) == E_OK) ? 0 : 1;
        else { printk("Usage: rm <file>\n"); last_exit_status = 1; }
    }
    else if (ft_strcmp(args[0], "mv") == 0) {
        if (args[1] && args[2]) last_exit_status = (sys_rename_file(args[1], args[2]) == E_OK) ? 0 : 1;
        else { printk("Usage: mv <old> <new>\n"); last_exit_status = 1; }
    }
    else if (ft_strcmp(args[0], "layout") == 0) {
        if (args[1] && ft_strcmp(args[1], "tr") == 0) { syscall(12, 1, 0, 0); last_exit_status = 0; }
        else if (args[1] && ft_strcmp(args[1], "us") == 0) { syscall(12, 0, 0, 0); last_exit_status = 0; }
        else { printk("Usage: layout tr|us\n"); last_exit_status = 1; }
    }
    else if (ft_strcmp(args[0], "sync") == 0) {
        /*
         * SYSCALL_SYNC has existed since v0.4.x and nothing in user space had
         * ever called it - it was in the syscall table and reachable from
         * nowhere. That did not matter while it only flushed the block cache;
         * it does now that it is also when the kernel log is written to
         * /var/log/kern.log, because the other two moments that write it are
         * halt and reboot, and neither leaves a session to look at the result in.
         */
        last_exit_status = syscall(SYSCALL_SYNC, 0, 0, 0) == E_OK ? 0 : 1;
    }
    else if (ft_strcmp(args[0], "lockdown") == 0) { last_exit_status = syscall(13, 0, 0, 0) < 0 ? 1 : 0; }
    else if (ft_strcmp(args[0], "stack") == 0) { last_exit_status = syscall(14, 0, 0, 0) < 0 ? 1 : 0; }
    else if (ft_strcmp(args[0], "meminfo") == 0) { last_exit_status = syscall(15, 0, 0, 0) < 0 ? 1 : 0; }
    else if (ft_strcmp(args[0], "testmalloc") == 0) { last_exit_status = syscall(16, 0, 0, 0) < 0 ? 1 : 0; }
    else if (ft_strcmp(args[0], "hexdump") == 0) {
        if (args[1]) {
            syscall(17, hex_to_int(args[1]), 0, 0); 
        } 
        else {
            char chunk[16];
            int bytes_read;
            int total_offset = 0;
            printk("[INFO] Keyboard read mode. Press ESC to exit...\n");

            while ((bytes_read = syscall(3, 0, (int)chunk, 16)) > 0) {
                if (chunk[0] == 27 || chunk[0] == 4) {
                    printk("\n");
                    break;
                }

                char offset_str[16];
                ft_itoa(total_offset, offset_str);
                printk(offset_str); printk("  ");

                for (int i = 0; i < 16; i++) {
                    if (i < bytes_read) {
                        static const char hex_chars[] = "0123456789ABCDEF";
                        char hex_out[3];
                        hex_out[0] = hex_chars[(chunk[i] >> 4) & 0x0F];
                        hex_out[1] = hex_chars[chunk[i] & 0x0F];
                        hex_out[2] = '\0';
                        printk(hex_out); printk(" ");
                    } else {
                        printk("   "); 
                    }
                    if (i == 7) printk(" "); 
                }

                printk(" |");
                for (int i = 0; i < bytes_read; i++) {
                    if (chunk[i] >= 32 && chunk[i] <= 126) {
                        char ascii_out[2] = { chunk[i], '\0' };
                        printk(ascii_out);
                    } else {
                        printk("."); 
                    }
                }
                printk("|\n");

                total_offset += bytes_read;
            }
        }
        last_exit_status = 0;
    }
    else if (ft_strcmp(args[0], "alarm") == 0) { last_exit_status = syscall(18, 0, 0, 0) < 0 ? 1 : 0; }
    else if (ft_strcmp(args[0], "panic") == 0) { last_exit_status = syscall(19, 0, 0, 0) < 0 ? 1 : 0; }
    else if (ft_strcmp(args[0], "reboot") == 0) { last_exit_status = syscall(20, 0, 0, 0) < 0 ? 1 : 0; }
    else if (ft_strcmp(args[0], "halt") == 0) { last_exit_status = syscall(21, 0, 0, 0) < 0 ? 1 : 0; }
    else if (ft_strcmp(args[0], "exec") == 0) {
        /*
         * The same path an ordinary command takes, rather than a bare exec()
         * call of its own. It used to make that call directly and blocking,
         * which left it as the one way to start a program the shell could not
         * stop, could not name and did not put in a group - and it reported
         * success for a program that had failed, because it only tested for a
         * negative return.
         */
        if (args[1]) run_external(&args[1]);
        else { printk("Usage: exec <program>\n"); last_exit_status = 1; }
    }
    else if (ft_strcmp(args[0], "exit") == 0) { printk("exit\n"); syscall(1, 0, 0, 0); while(1); }
    else if (ft_strcmp(args[0], "cat_raw") == 0) {
        if (args[1]) last_exit_status = (sys_cat_raw_file(args[1]) == E_OK) ? 0 : 1;
        else { printk("Usage: cat_raw <file>\n"); last_exit_status = 1; }
    }
    else if (ft_strcmp(args[0], "kill") == 0) {
        /*
         * Decimal, not hexadecimal. hex_to_int() read "10" as sixteen, so
         * "kill 10 9" signalled PID 16 - and it returns 0 for anything
         * non-hex, so "kill abc 9" quietly targeted PID 0. Neither missing
         * arguments nor a junk one produced any message at all.
         */
        if (args[1] && args[2]) {
            int pid = dec_to_int(args[1]);
            int sig = dec_to_int(args[2]);
            if (pid <= 0 || sig <= 0) {
                printk("kill: pid and signal must be positive numbers\n");
                last_exit_status = 1;
            } else {
                last_exit_status = (sys_kill(pid, sig) < 0) ? 1 : 0;
            }
        } else { printk("Usage: kill <pid> <signal>\n"); last_exit_status = 1; }
    }
    else if (ft_strcmp(args[0], "su") == 0) {
        printk("Password for root: ");
        char su_pass[64];

        /* Ctrl-C abandons the password prompt without attempting an
         * authentication, which also keeps the failed-attempt rate limit from
         * counting an entry the user never made. */
        if (read_line(su_pass, 1, 64) < 0) {
            last_exit_status = 130;
        }
        else if (sys_setuid(0, su_pass) == 0) {
            set_env("USER", "root");
            current_uid = 0;
            ft_strcpy(current_username, "root");
            printk("\n[SYSTEM] Privileges elevated to ROOT!\n");
            last_exit_status = 0;
        } else {
            /* Failing silently left the user staring at a fresh prompt with no
             * idea whether the password had been accepted. */
            printk("\nsu: Authentication failed\n");
            last_exit_status = 1;
        }
    }
    else if (ft_strcmp(args[0], "dmesg") == 0) {
        /*
         * Read a slice, write a slice. The kernel used to print the whole log
         * itself, straight to the screen, so "dmesg | head" fed an empty pipe and
         * "dmesg > boot.log" wrote nothing.
         *
         * The loop belongs here rather than in the kernel because the write can
         * block: an 8 KB log does not fit a 4 KB pipe, and a blocked syscall
         * resumes by re-running from the start. A kernel-side dump would emit
         * everything twice; this keeps the offset in a variable no restart
         * touches, and each write blocks and restarts on its own.
         */
        last_exit_status = builtin_dmesg(args);
    }
    else if (ft_strcmp(args[0], "wait") == 0) {
        /*
         * Block until every background job this shell started has reported.
         *
         * The loop ends on its own: wait() returns E_CHILD once there is
         * nothing left to wait for, which is a negative value and not a pid. A
         * shell that tested for "no more jobs" by counting its own table instead
         * would deadlock the first time a job it had lost track of was still
         * running - the kernel's answer is the one that matters.
         */
        int status = 0;
        int pid;
        int gave_up = 0;
        char num[16];

        /*
         * Refuse before blocking if a job is already stopped.
         *
         * A stopped job never finishes, so waiting for one is waiting for the
         * rest of the boot - and this wait cannot be escaped. The shell ignores
         * Ctrl-C, and a task blocked in wait() is not woken by a signal in any
         * case, so the session would simply be over.
         *
         * The check is against the shell's own table rather than against the
         * kernel's answer, which is the one place that is right: the kernel says
         * only that a child exists, and a stop it has already reported is not
         * reported twice - so by the time the user types this, there is nothing
         * left for wait() to tell us and it would block on a child that has
         * nothing more to say.
         */
        int stopped_slot = -1;
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].stopped) { stopped_slot = i; break; }
        }

        if (stopped_slot >= 0) {
            printk("sh: job [");
            ft_itoa(jobs[stopped_slot].id, num); printk(num);
            printk("] is stopped and will never finish; use fg or bg first\n");
            last_exit_status = 1;
        } else {
            while ((pid = sys_wait_flags(&status, WUNTRACED)) > 0) {
                /* And one that stops while this is waiting, which is the same
                 * dead end reached from the other direction. */
                if (status & WSTATUS_STOPPED) {
                    job_report_stopped(pid);
                    printk("sh: a stopped job cannot be waited for; use fg or bg\n");
                    gave_up = 1;
                    break;
                }
                job_report_done(pid, status);
            }
            last_exit_status = gave_up ? 1 : 0;
        }
    }
    else if (ft_strcmp(args[0], "jobs") == 0) {
        /*
         * Only what this shell started and has not yet collected. It is not a
         * process list: the kernel has no call that enumerates tasks, and a
         * shell has no business reading one if it did.
         */
        if (job_count == 0) {
            printk("sh: no jobs\n");
        } else {
            char num[16];
            for (int i = 0; i < job_count; i++) {
                printk("[");
                ft_itoa(jobs[i].id, num); printk(num);
                printk("] ");
                printk(jobs[i].stopped ? "Stopped" : "Running");
                printk("  pgid ");
                ft_itoa(jobs[i].pgid, num); printk(num);
                printk("  pid");
                for (int k = 0; k < jobs[i].npid; k++) {
                    printk(" ");
                    ft_itoa(jobs[i].pids[k], num); printk(num);
                }
                printk("\n");
            }
        }
        last_exit_status = 0;
    }
    else if (ft_strcmp(args[0], "fg") == 0) {
        /*
         * Bring a job back to the foreground: give it the terminal, continue it,
         * and wait for it as though it had just been typed.
         *
         * The terminal is handed over before the signal rather than after. A job
         * that resumes and immediately reads the keyboard would otherwise do so
         * while the shell still held the terminal, and the first thing the user
         * typed would go to whichever of them the kernel woke first.
         */
        int slot = job_resolve(args[1]);
        if (slot < 0 || jobs[slot].npid <= 0) {
            printk("sh: fg: no such job\n");
            last_exit_status = 1;
        } else {
            int pids[MAX_STAGES];
            int npid = jobs[slot].npid;
            int pgid = jobs[slot].pgid;
            int id = jobs[slot].id;
            char num[16];

            for (int k = 0; k < npid; k++) pids[k] = jobs[slot].pids[k];
            jobs[slot].stopped = 0;

            printk("[");
            ft_itoa(id, num); printk(num);
            printk("] continued\n");

            sys_tcsetpgrp(pgid);
            sys_kill(-pgid, SIG_CONT);

            int status = wait_foreground(pids, npid, pgid, pids[npid - 1], id);

            sys_tcsetpgrp(sys_getpgid(0));
            last_exit_status = (status == JOB_STOPPED) ? 148 : status;
        }
    }
    else if (ft_strcmp(args[0], "bg") == 0) {
        /*
         * Continue a job without giving it the terminal, which is the whole of
         * the difference from fg. It keeps running while the prompt comes back,
         * and it is collected by the sweep before some later prompt.
         *
         * The signal goes to the group rather than to the pid the shell forked.
         * That process may itself have started the program the user is looking
         * at, and Ctrl-Z stopped both of them; continuing only the one the shell
         * knows about would leave it blocked waiting for a task still stopped.
         */
        int slot = job_resolve(args[1]);
        if (slot < 0) {
            printk("sh: bg: no such job\n");
            last_exit_status = 1;
        } else if (!jobs[slot].stopped) {
            printk("sh: bg: job is already running\n");
            last_exit_status = 1;
        } else {
            char num[16];
            jobs[slot].stopped = 0;

            printk("[");
            ft_itoa(jobs[slot].id, num); printk(num);
            printk("] continued in the background\n");

            last_exit_status = (sys_kill(-jobs[slot].pgid, SIG_CONT) < 0) ? 1 : 0;
        }
    }
    else if (ft_strcmp(args[0], "sleep") == 0) {
        /*
         * Seconds here, milliseconds at the syscall. The kernel takes the finer
         * unit because TIMER_HZ gives it 10 ms of resolution and nothing else
         * could reach it; the shell keeps the unit people expect from sleep(1).
         */
        int seconds = args[1] ? dec_to_int(args[1]) : -1;
        if (seconds < 0) {
            printk("Usage: sleep <seconds>\n");
            last_exit_status = 1;
        } else {
            last_exit_status = syscall(SYSCALL_SLEEP, seconds * 1000, 0, 0) == 0 ? 0 : 1;
        }
    }
    else {
        run_external(args);
    }
}

/**
 * @brief Runs a command as an ELF, either by name from /bin or by path.
 *
 * Split out of execute_command() so that a word containing a slash can reach it
 * without walking the builtin table first - see word_is_path().
 *
 * @param args Array of command-line arguments.
 */
void run_external(char **args) {
        if (ft_strlen(args[0]) > 58) {
            printk("sh: command name too long (max 58 characters)\n");
            last_exit_status = 127;
            return;
        }

        /*
         * A word with a slash in it is used as written; anything else is looked
         * for in /bin. There is no PATH variable to search, and one directory is
         * the whole of the search this shell does.
         */
        char exec_path[64];
        if (word_is_path(args[0])) {
            ft_strcpy(exec_path, args[0]);
        } else {
            ft_strcpy(exec_path, "/bin/");
            ft_strcpy(&exec_path[5], args[0]);
        }

        char arg_str[256];
        for(int k=0; k<256; k++) arg_str[k] = '\0';
        
        /*
         * Only the user's arguments.
         *
         * The current directory used to be pasted on the front as an implicit
         * first token, from when the kernel had no idea where a process was and
         * every tool had to be told. It was joined with a space rather than a
         * '/', so "touch notes.txt" in /home produced the argument string
         * "/home notes.txt" - and touch, which treats the whole string as one
         * filename, created a file literally called that. Every tool except
         * echo, which alone skipped the leading token, mis-parsed its arguments
         * because of it.
         *
         * The kernel tracks the working directory now, so there is nothing to
         * pass: a bare "notes.txt" resolves where the process actually stands.
         */
        /*
         * Bounded join.
         *
         * The tokens come straight from the input line, which is capped at 254
         * characters - so an unbounded join into 256 bytes looked safe. It was
         * not: the expansion pass replaces short tokens with longer ones before
         * this runs, so "$A" becomes up to 63 characters and "~" becomes HOME.
         * A line of repeated "$A" therefore produced kilobytes out of 254 input
         * characters and smashed this function's return address.
         *
         * Truncation is silent here only because the kernel truncates too:
         * cmd_args in the PCB is 128 bytes and sys_get_args() copies at most
         * 127. That is a separate limitation, recorded rather than fixed here.
         */
        uint32_t arg_len = 0;
        for (int i = 1; args[i] != 0; i++) {
            if (arg_len > 0 && arg_len < sizeof(arg_str) - 1) {
                arg_str[arg_len++] = ' ';
            }
            for (int k = 0; args[i][k] != '\0' && arg_len < sizeof(arg_str) - 1; k++) {
                arg_str[arg_len++] = args[i][k];
            }
        }
        arg_str[arg_len] = '\0';

        /*
         * Started, not run.
         *
         * exec() used to block here and hand back the exit status, and that is
         * still what it does for a caller that asks - but a shell cannot own a
         * job it never learns the pid of. It could not put the program in a group
         * of its own, so a foreground command lived in the shell's own group; it
         * could not hand it the terminal, so which group was in the foreground
         * was decided by whatever the kernel happened to do; and once a program
         * can be stopped rather than finished, it had no way to name the thing
         * the user would want back.
         *
         * With the pid in hand a foreground command is exactly what a pipeline
         * already was: a group the shell placed, gave the terminal to, and waits
         * for with wait().
         */
        int pid = syscall(5, (int)exec_path, EXEC_NOWAIT, (int)arg_str); // SYSCALL_EXEC
        if (pid < 0) {
            printk("sh: command not found: "); printk(args[0]); printk("\n");
            last_exit_status = 127;
            return;
        }

        /*
         * A group of its own, and the terminal with it - but only when this is
         * the shell at the prompt. A pipeline stage runs this same code after
         * fork() and its program belongs to the pipeline's group, not to a new
         * one, or Ctrl-C would reach half of what the user typed.
         */
        int job_pgid = pid;
        if (shell_owns_terminal) {
            sys_setpgid(pid, pid);
            sys_tcsetpgrp(pid);
        } else {
            job_pgid = sys_getpgid(0);
        }

        int status = wait_foreground(&pid, 1, job_pgid, pid, 0);

        /*
         * Take the terminal back. The kernel already hands it to a woken parent
         * when the foreground group empties or stops, so this is belt and braces
         * - but the prompt has to be the shell's either way.
         */
        if (shell_owns_terminal) sys_tcsetpgrp(sys_getpgid(0));

        /*
         * The child's own exit status. This used to be a hardcoded 0, so every
         * program that started at all counted as having succeeded and the && and
         * || below could not tell one outcome from the other: "stat
         * /no_such_file && echo CHAINED" printed CHAINED.
         *
         * 148 is 128 + SIG_TSTP, which is what a shell reports for a command the
         * user stopped - it did not finish, and calling that success would let an
         * && chain run on as though it had.
         */
        last_exit_status = (status == JOB_STOPPED) ? 148 : status;
}

/**
 * @brief Runs a command with its standard output sent to a file.
 *
 * Opening for writing truncates, so the file only has to exist first; if it does
 * not, it is created empty. The kernel buffers what is written and commits the
 * whole thing when the last descriptor closes, which is why both descriptors
 * below have to be closed for the file to appear on disk.
 *
 * fd 12 holds the saved stdout. The pipe path uses 10 and 11 for the same
 * purpose, so this stays clear of both.
 *
 * @param args Tokenised command.
 * @param redirect_file Target path.
 */
void run_with_redirect(char **args, char *redirect_file) {
    int fd = sys_open_write(redirect_file);

    if (fd < 0) {
        /* Does not exist yet - open() does not create. */
        if (sys_create_file(redirect_file, "") != E_OK) {
            printk("sh: cannot create "); printk(redirect_file); printk("\n");
            last_exit_status = 1;
            return;
        }
        fd = sys_open_write(redirect_file);
    }

    if (fd < 0) {
        printk("sh: cannot open "); printk(redirect_file); printk("\n");
        last_exit_status = 1;
        return;
    }

    dup2(1, 12);
    dup2(fd, 1);

    execute_command(args);

    dup2(12, 1);
    sys_close(12);

    /*
     * The commit happens on the last close, and the status it reports is the
     * only signal that the write reached the disk - a full disk or a destroyed
     * master key surfaces here and nowhere else.
     */
    if (sys_close(fd) != E_OK) {
        printk("sh: write to "); printk(redirect_file); printk(" failed\n");
        last_exit_status = 1;
    }
}

/* ================== TAB COMPLETION ================== */

/** Built-in command names for tab completion */
static const char *builtin_commands[] = {
    "cat", "cat_raw", "cd", "clear", "dmesg", "echo", "env", "exec",
    "bg", "fg", "jobs", "wait",
    "exit", "export", "halt", "help", "hexdump", "kill", "layout",
    "lockdown", "ls", "meminfo", "mkdir", "mv", "pwd", "reboot",
    "rm", "sleep", "su", "sync", "write",
    0  // sentinel
};

/**
 * @brief Performs tab completion on the current input buffer.
 *
 * If cursor is at first word position, completes command names.
 * Otherwise, completes file/directory names in the current directory.
 * Single match: auto-completes. Multiple matches: lists them.
 *
 * @param buf Current input buffer.
 * @param idx Pointer to current cursor position in buffer.
 */
static void handle_tab_completion(char *buf, int *idx) {
    // Find the start of the current word
    int word_start = *idx;
    while (word_start > 0 && buf[word_start - 1] != ' ') word_start--;
    
    // Extract the partial word (prefix to match)
    /*
     * The copy was already clamped to 127; the terminator was not. A word
     * longer than the buffer - the input line allows 254 characters - stored
     * a NUL that far past the end of a 128-byte stack array. Clamp the length
     * once and use it for both.
     */
    char prefix[128];
    int prefix_len = *idx - word_start;
    if (prefix_len > 127) prefix_len = 127;
    if (prefix_len < 0) prefix_len = 0;
    for (int i = 0; i < prefix_len; i++) prefix[i] = buf[word_start + i];
    prefix[prefix_len] = '\0';
    
    // Determine if we're completing a command (first word) or a filename
    int is_command = (word_start == 0);
    
    // Collect matches
    char matches[32][64];  // max 32 matches, 64 chars each
    int match_count = 0;
    int match_is_dir[32];  // track which matches are directories
    
    if (is_command) {
        // Match built-in commands
        for (int i = 0; builtin_commands[i] != 0; i++) {
            if (prefix_len == 0 || ft_strncmp(builtin_commands[i], prefix, prefix_len) == 0) {
                if (match_count < 32) {
                    ft_strncpy(matches[match_count], builtin_commands[i], 64);
                    match_is_dir[match_count] = 0;
                    match_count++;
                }
            }
        }
        // Also match /bin/ executables
        int bin_id = sys_get_dir_id("/bin");
        if (bin_id >= 0) {
            char dir_buf[2048];
            int bytes = sys_readdir(bin_id, dir_buf, sizeof(dir_buf));
            int off = 0;
            while (off < bytes) {
                char *name = &dir_buf[off];
                int nlen = 0;
                while (name[nlen] != 1 && name[nlen] != 2 && name[nlen] != '\0') nlen++;
                // Skip the type marker byte
                /* nlen keeps its true value - the buffer walk below advances by
                 * it - so the clamp lives in a second variable used for both
                 * the copy and the terminator. Storing at entry_name[nlen] wrote
                 * past the array for any name longer than 63 bytes, and
                 * readdir() emits up to 255. */
                char entry_name[64];
                int name_len = (nlen > 63) ? 63 : nlen;
                for (int j = 0; j < name_len; j++) entry_name[j] = name[j];
                entry_name[name_len] = '\0';
                
                if (prefix_len == 0 || ft_strncmp(entry_name, prefix, prefix_len) == 0) {
                    // Check it's not already a builtin
                    int is_dup = 0;
                    for (int m = 0; m < match_count; m++) {
                        if (ft_strcmp(matches[m], entry_name) == 0) { is_dup = 1; break; }
                    }
                    if (!is_dup && match_count < 32) {
                        ft_strncpy(matches[match_count], entry_name, 64);
                        match_is_dir[match_count] = 0;
                        match_count++;
                    }
                }
                // Skip past: name + type_byte + null
                off += nlen + 2;
            }
        }
    } else {
        // File/directory completion in current directory
        // Check if prefix contains a path
        int dir_id = sys_get_dir_id(".");
        char name_prefix[128];
        ft_strcpy(name_prefix, prefix);
        
        // If prefix contains '/', resolve the directory part
        int last_slash = -1;
        for (int i = 0; name_prefix[i]; i++) {
            if (name_prefix[i] == '/') last_slash = i;
        }
        if (last_slash >= 0) {
            // Split into dir path and name prefix
            char dir_path[128];
            for (int i = 0; i <= last_slash; i++) dir_path[i] = name_prefix[i];
            dir_path[last_slash + 1] = '\0';
            
            int new_dir = sys_get_dir_id(dir_path);
            if (new_dir >= 0) {
                dir_id = new_dir;
                // Shift name_prefix to after the last slash
                int j = 0;
                for (int i = last_slash + 1; name_prefix[i]; i++) name_prefix[j++] = name_prefix[i];
                name_prefix[j] = '\0';
                prefix_len = j;
            }
        }
        
        char dir_buf[2048];
        int bytes = sys_readdir(dir_id, dir_buf, sizeof(dir_buf));
        int off = 0;
        while (off < bytes) {
            char *name = &dir_buf[off];
            int nlen = 0;
            while (name[nlen] != 1 && name[nlen] != 2 && name[nlen] != '\0') nlen++;
            int is_dir = (name[nlen] == 1);
            /* Same clamp as above; nlen stays intact for the buffer walk. */
            char entry_name[64];
            int name_len = (nlen > 63) ? 63 : nlen;
            for (int j = 0; j < name_len; j++) entry_name[j] = name[j];
            entry_name[name_len] = '\0';
            
            int nplen = ft_strlen(name_prefix);
            if (nplen == 0 || ft_strncmp(entry_name, name_prefix, nplen) == 0) {
                if (match_count < 32) {
                    ft_strncpy(matches[match_count], entry_name, 64);
                    match_is_dir[match_count] = is_dir;
                    match_count++;
                }
            }
            off += nlen + 2;
        }
    }
    
    if (match_count == 0) return;  // No matches
    
    if (match_count == 1) {
        // Single match: auto-complete
        char *match = matches[0];
        int match_len = ft_strlen(match);
        // Erase the current prefix from display
        // Then write the full match
        for (int i = prefix_len; i < match_len && *idx < 254; i++) {
            char ch[2] = { match[i], '\0' };
            printk(ch);
            buf[(*idx)++] = match[i];
        }
        // Add trailing / for directories or space for files/commands
        if (match_is_dir[0]) {
            if (*idx < 254) {
                printk("/");
                buf[(*idx)++] = '/';
            }
        } else {
            if (*idx < 254) {
                printk(" ");
                buf[(*idx)++] = ' ';
            }
        }
    } else {
        // Multiple matches: find common prefix first
        int common_len = ft_strlen(matches[0]);
        for (int m = 1; m < match_count; m++) {
            int k = 0;
            while (k < common_len && matches[0][k] == matches[m][k] && matches[m][k] != '\0') k++;
            common_len = k;
        }
        
        // If common prefix is longer than what's typed, complete to it
        if (common_len > prefix_len) {
            for (int i = prefix_len; i < common_len && *idx < 254; i++) {
                char ch[2] = { matches[0][i], '\0' };
                printk(ch);
                buf[(*idx)++] = matches[0][i];
            }
        } else {
            // Show all matches
            printk("\n");
            for (int m = 0; m < match_count; m++) {
                printk(matches[m]);
                if (match_is_dir[m]) printk("/");
                printk("  ");
            }
            
            // Redraw prompt and current input
            printk("\n");
            print_prompt();
            buf[*idx] = '\0';
            printk(buf);
        }
    }
}

/**
 * @brief Copies a whole small file into a buffer, null-terminated.
 *
 * Shared by the three startup files under /etc. A file that is not there is not
 * an error - the caller falls back to what it compiled in - which is what keeps
 * a disk image made before these existed from failing to boot a shell.
 *
 * @param path File to read.
 * @param buf Destination.
 * @param size Capacity of buf, including the terminator.
 * @return Bytes read, or a negative errno.
 */
static int read_small_file(const char *path, char *buf, int size) {
    int fd = sys_open(path);
    if (fd < 0) return fd;

    int total = 0;
    int n;
    while (total < size - 1 &&
           (n = syscall(SYSCALL_READ, fd, (int)&buf[total], size - 1 - total)) > 0) {
        total += n;
    }

    buf[total] = '\0';
    sys_close(fd);
    return total;
}

/**
 * @brief Applies one line of /etc/profile.
 *
 * Only "export KEY VALUE" is recognised, and the file says so in its own first
 * two lines. This is a settings file rather than a script: running arbitrary
 * commands from it would mean forking and exec'ing before the first prompt, and
 * a syntax error in it would be a shell that will not start.
 *
 * @param line One line, modified in place while it is split into words.
 */
static void apply_profile_line(char *line) {
    if (line[0] == '#' || line[0] == '\0') return;

    char *word[3] = { 0, 0, 0 };
    int count = 0;
    int in_word = 0;

    for (int i = 0; line[i] != '\0'; i++) {
        if (line[i] == ' ' || line[i] == '\t') { line[i] = '\0'; in_word = 0; }
        else if (!in_word) {
            if (count >= 3) break;
            word[count++] = &line[i];
            in_word = 1;
        }
    }

    if (count == 3 && ft_strcmp(word[0], "export") == 0) {
        set_env(word[1], word[2]);
    }
}

/**
 * @brief Reads the three startup files under /etc, if they are there.
 *
 * The hostname was a string literal in two places, and there was nothing a user
 * could change without rebuilding the image. None of these is required: each
 * falls back to what the shell already had.
 */
static void read_startup_files(void) {
    char buf[512];

    if (read_small_file("/etc/hostname", buf, sizeof(buf)) > 0) {
        int i = 0;
        while (buf[i] != '\0' && buf[i] != '\n' && buf[i] != '\r' &&
               i < (int)sizeof(current_hostname) - 1) {
            current_hostname[i] = buf[i];
            i++;
        }
        /* An empty or blank-first-line file leaves the built-in name alone
         * rather than producing a prompt with nothing between @ and the path. */
        if (i > 0) current_hostname[i] = '\0';
    }

    if (read_small_file("/etc/profile", buf, sizeof(buf)) > 0) {
        int start = 0;
        for (int i = 0; ; i++) {
            if (buf[i] == '\n' || buf[i] == '\0') {
                char end = buf[i];
                buf[i] = '\0';
                apply_profile_line(&buf[start]);
                start = i + 1;
                if (end == '\0') break;
            }
        }
    }

    if (read_small_file("/etc/motd", buf, sizeof(buf)) > 0) {
        printk(buf);
    }
}

/*
 * The line editor.
 *
 * Until v0.8.2 the arrow keys reached no program at all, so a typed line could
 * only ever grow at its end and the only correction was backspace. The keyboard
 * sends the sequences a terminal sends now, and this is the first thing in the
 * system to read them.
 *
 * It used to be one screen row. The redraw moved the cursor with CUB, which
 * cannot cross a row boundary, so a line that wrapped past column 80 could be
 * typed and run but was redrawn only on its last row - the rest of it stayed on
 * screen as whatever it had been. The wrap is tracked by hand now, and what made
 * that cheap is that the geometry is not something to discover: the prompt is
 * always printed after a newline, so it starts at column 0, and the terminal
 * wraps the moment column 80 is written rather than deferring it. Prompt and
 * line are therefore one run of cells, and cell n is at row n/80, column n%80.
 *
 * Nothing here asks which screen row it is on, and nothing needs to. Every move
 * is relative, and when output scrolls the screen the whole run moves up with
 * the cursor, so a relative move stays right across a scroll.
 */

/** @brief Not a key this editor acts on: a lone Escape, or a sequence it ignores. */
#define KEY_NONE   0
#define KEY_UP     1
#define KEY_DOWN   2
#define KEY_RIGHT  3
#define KEY_LEFT   4
#define KEY_HOME   5
#define KEY_END    6
#define KEY_DELETE 7

/*
 * One byte of pushback.
 *
 * An escape sequence is read one byte at a time and there is no way to tell the
 * Escape key from the start of one without waiting for the next byte. Waiting is
 * fine; throwing that byte away is not, because a user who presses Escape and
 * then types a letter would lose the letter. It is held here and taken by the
 * next read instead.
 */
static int shell_pushback = -1;

/**
 * @brief Reads one byte, taking the pushed-back one first.
 * @param c Receives the byte.
 * @return 1 on a byte, or what the read reported.
 */
static int shell_read_char(char *c) {
    if (shell_pushback >= 0) {
        *c = (char)shell_pushback;
        shell_pushback = -1;
        return 1;
    }
    return sys_read_char(c);
}

/**
 * @brief Reads the rest of an escape sequence and names the key it stood for.
 *
 * Called with the Escape already consumed. Anything not understood is swallowed
 * rather than printed - a sequence this shell has no use for must leave no mark,
 * which is the same rule the terminal's own parser follows on the way out.
 *
 * @return One of the KEY_ constants.
 */
static int read_escape_key(void) {
    char c = 0;

    if (shell_read_char(&c) <= 0) return KEY_NONE;

    /* Not a control sequence at all: the Escape key, followed by whatever the
     * user typed next. That byte is theirs and goes back. */
    if (c != '[') { shell_pushback = (unsigned char)c; return KEY_NONE; }

    if (shell_read_char(&c) <= 0) return KEY_NONE;

    switch (c) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        default:  break;
    }

    /* The numbered keys: a decimal parameter closed by '~'. */
    if (c >= '0' && c <= '9') {
        int n = 0;
        while (c >= '0' && c <= '9') {
            n = n * 10 + (c - '0');
            if (shell_read_char(&c) <= 0) return KEY_NONE;
        }
        if (c != '~') return KEY_NONE;
        if (n == 3) return KEY_DELETE;
        /* 2 is Insert and 5 and 6 are the Page keys; nothing here acts on them. */
    }

    return KEY_NONE;
}

/**
 * @brief Moves the cursor sideways by a column count.
 * @param n Columns; zero or fewer does nothing.
 * @param dir 'C' for right, 'D' for left.
 */
static void ansi_move(int n, char dir) {
    if (n <= 0) return;

    char seq[16];
    char num[16];
    int k = 0;

    ft_itoa(n, num);
    seq[k++] = 27;
    seq[k++] = '[';
    for (int i = 0; num[i] != '\0' && k < 13; i++) seq[k++] = num[i];
    seq[k++] = dir;
    seq[k] = '\0';

    printk(seq);
}

/** @brief Screen width. The terminal wraps here, and the arithmetic counts on it. */
#define SHELL_COLS 80

/**
 * Where the cursor is, in cells from the first character of the prompt.
 *
 * One number rather than a row and a column, because a row on its own means
 * nothing here: the run starts at column 0 of whatever row the prompt landed on,
 * and every position in it follows from the count. Everything that prints or
 * moves keeps this in step, which is what lets a move be worked out from where
 * the cursor is to where it should be without asking the terminal anything.
 */
static int line_cell = 0;

/** @brief Width of the prompt, so cell prompt_w is the first character typed. */
static int prompt_w = 0;

/**
 * @brief Prints one piece of the prompt and counts it into the prompt's width.
 *
 * Printing and measuring in the same call, so that a piece cannot be added to
 * the prompt without being added to its width. The prompt holds no escape
 * sequences, so its bytes and its columns are the same number.
 *
 * @param s The piece.
 */
static void prompt_put(const char *s) {
    printk(s);
    prompt_w += ft_strlen(s);
}

/**
 * @brief Prints the shell prompt and records where it leaves the cursor.
 *
 * Split out because the history has to put it back: recalling a line rewrites
 * everything from the start of the prompt, and the prompt is part of what was
 * there. Tab completion reprints it too, and used to do so from a copy of these
 * lines - which is now gone, because a second copy is a second width.
 */
static void print_prompt(void) {
    prompt_w = 0;

    prompt_put(current_username);
    prompt_put("@");
    prompt_put(current_hostname);
    prompt_put(" ");
    prompt_put(current_path);
    prompt_put(current_uid == 0 ? " # " : " $ ");

    line_cell = prompt_w;
}

/**
 * @brief Moves the cursor to a cell of the prompt-and-line run.
 *
 * The rows are covered with CUU and CUD and the column with a CUB clamped to
 * zero followed by a CUF, which is the one combination that can cross a row
 * boundary - CUB alone stops at column 0 of the row it is already on, and that
 * was the whole of the old limitation.
 *
 * @param to Destination cell, counted from the start of the prompt.
 */
static void cursor_to_cell(int to) {
    int rows = (to / SHELL_COLS) - (line_cell / SHELL_COLS);

    if (rows < 0)      ansi_move(-rows, 'A');
    else if (rows > 0) ansi_move(rows, 'B');

    /* SHELL_COLS is past any column the cursor can be in, so this lands on 0. */
    ansi_move(SHELL_COLS, 'D');
    ansi_move(to % SHELL_COLS, 'C');

    line_cell = to;
}

/**
 * @brief Redraws the line from the cursor rightwards and returns to it.
 *
 * Used after anything that changed the text under or after the cursor. The erase
 * matters as much as the reprint: a deletion leaves the old last character on
 * screen with nothing to overwrite it.
 *
 * ED rather than EL, and that is the wrap. A line that shrinks from three rows
 * to two leaves its third row behind, and erasing to the end of the current row
 * cannot reach it. Nothing below the line is ever anything but blank - the
 * prompt is the last thing printed - so erasing to the bottom of the screen
 * costs nothing and takes the stale rows with it.
 *
 * @param buf Line contents.
 * @param len Characters in the line.
 * @param pos Cursor position within it.
 */
static void line_redraw_tail(char *buf, int len, int pos) {
    buf[len] = '\0';
    printk(&buf[pos]);
    line_cell = prompt_w + len;

    printk("\033[J");
    cursor_to_cell(prompt_w + pos);
}

/**
 * @brief Redraws the prompt and the whole line, cursor included.
 *
 * @param buf Line contents, null-terminated at len.
 * @param len Characters in the line.
 * @param pos Cursor position within it, 0 to len.
 */
static void line_refresh(char *buf, int len, int pos) {
    buf[len] = '\0';

    cursor_to_cell(0);
    print_prompt();
    printk(buf);
    line_cell = prompt_w + len;

    printk("\033[J");          /* whatever the line used to be longer by */
    cursor_to_cell(prompt_w + pos);
}

/*
 * Command history.
 *
 * A fixed ring of the last few lines, which is the same shape and the same size
 * as the job table for the same reason: a shell that grew either without bound
 * would be a shell that could be made to run out of memory by holding a key
 * down.
 *
 * hist_view is how far back the user has walked: 0 is the line being typed, and
 * anything else is an entry. The line being typed is saved when they first walk
 * away from it, so that walking back returns it rather than an empty line.
 */
#define MAX_HISTORY 8

static char history[MAX_HISTORY][256];
static int history_count = 0;
static int history_next = 0;

/**
 * @brief Records a line, unless it is empty or repeats the last one.
 * @param line The line as entered.
 */
static void history_add(const char *line) {
    if (line[0] == '\0') return;

    if (history_count > 0) {
        int last = (history_next + MAX_HISTORY - 1) % MAX_HISTORY;
        if (ft_strcmp(history[last], line) == 0) return;
    }

    ft_strncpy(history[history_next], line, 255);
    history[history_next][255] = '\0';
    history_next = (history_next + 1) % MAX_HISTORY;
    if (history_count < MAX_HISTORY) history_count++;
}

/**
 * @brief Fetches an entry counted backwards from the most recent.
 * @param back 1 is the most recent, history_count the oldest.
 * @return The line, or 0 when there is no such entry.
 */
static const char *history_at(int back) {
    if (back < 1 || back > history_count) return 0;
    return history[(history_next + MAX_HISTORY - back) % MAX_HISTORY];
}

/**
 * @brief Main entry point for the shell process.
 *
 * Sets up environment, registers signals, and runs the command loop.
 */
void main(void) {
    char cmd_buf[256];
    char *args[MAX_ARGS];

    current_uid = syscall(SYSCALL_GETUID, 0, 0, 0);
    current_username[0] = '\0';
    syscall(SYSCALL_GET_ARGS, (int)current_username, 0, 0);

    if (current_username[0] == '\0') {
        if (current_uid == 0) ft_strcpy(current_username, "root");
        else ft_strcpy(current_username, "esduman");
    }

    if (current_uid == 0 || ft_strcmp(current_username, "root") == 0) {
        ft_strcpy(current_path, "/root");
        set_env("HOME", "/root");
    } else {
        ft_strcpy(current_path, "/home/");
        ft_strcpy(&current_path[ft_strlen(current_path)], current_username);
        set_env("HOME", current_path);
    }
    
    /*
     * Move the process itself into the home directory rather than just recording
     * a string. Everything the shell runs from here - including the /bin tools,
     * which never took a directory argument of their own - resolves relative
     * paths against this.
     */
    if (sys_chdir(current_path) != E_OK) {
        sys_chdir("/");
        ft_strcpy(current_path, "/");
    }

    set_env("USER", current_username);
    set_env("OS", "esdumanOS");
    sys_register_signal(5, my_custom_handler);

    /*
     * The shell declines SIG_PIPE for itself.
     *
     * It has no business dying because something it wrote to stopped reading;
     * losing the shell would take the session with it, and there is no way to
     * get another one. This is what every real shell does.
     *
     * The catch is that a disposition survives fork(), so every stage the shell
     * forks starts out ignoring it too - and a stage that ignores SIG_PIPE is
     * exactly the runaway this release exists to stop. Each fork below puts it
     * back before running anything. External commands are not affected either
     * way: they are started through exec(), which gives the new image a clean
     * set of handlers.
     */
    sys_register_signal(SIG_PIPE, (void *)SIG_IGN);

    /*
     * And SIG_INT, for a different reason.
     *
     * Ctrl-C goes to every process in the foreground group, and between commands
     * that group is the shell's own - there is nothing else to hand the terminal
     * to. A shell that took the default action would end the session the first
     * time somebody pressed Ctrl-C at an idle prompt, which is to say it would
     * end the session.
     *
     * Every stage forked below puts it back to the default, exactly as it does
     * with SIG_PIPE: a job that ignored the interrupt would be a job the user
     * cannot stop.
     */
    sys_register_signal(SIG_INT, (void *)SIG_IGN);

    /*
     * And SIG_TSTP, for the same reason again.
     *
     * Between commands the foreground group is the shell's own, and the shell
     * shares that group with init - so a Ctrl-Z at an idle prompt reaches both.
     * A shell that took the default action would park the session and leave
     * nothing running that could ever continue it, which is worse than the
     * interrupt case: there would not even be a login screen to come back to.
     *
     * Every job forked below puts it back to the default, exactly as it does
     * with SIG_PIPE and SIG_INT. A job that could not be stopped is a job the
     * user cannot get out of the way.
     */
    sys_register_signal(SIG_TSTP, (void *)SIG_IGN);

    /*
     * Last in the setup, so that a value exported from /etc/profile wins over
     * the ones set above rather than being overwritten by them.
     */
    read_startup_files();

    while (1) {
        /*
         * Collect finished background jobs here rather than the moment they
         * report. A job ending in the middle of a line being typed would
         * otherwise print over it, and the status has nowhere to go until there
         * is a prompt to print it above.
         */
        jobs_reap();

        printk("\n");
        print_prompt();

        int len = 0;              /* characters in the line */
        int pos = 0;              /* where the cursor sits within them */
        int interrupted = 0;
        int hist_view = 0;        /* 0 is the line being typed */
        char hist_saved[256];

        hist_saved[0] = '\0';
        cmd_buf[0] = '\0';

        while (1) {
            char c = 0;
            int r = shell_read_char(&c);

            /*
             * Ctrl-C at the prompt. The half-typed line is thrown away and the
             * loop starts over with a fresh prompt, which is what a terminal
             * does - the alternative, going quietly back to sleep with the line
             * still there, is what this looked like before the read could report
             * being cut short. The kernel has already echoed the "^C".
             */
            if (r == E_INTR) { interrupted = 1; break; }
            if (r <= 0) continue;

            if (c == 27) {
                int key = read_escape_key();

                /* Every one of these moves by naming the cell it wants rather
                 * than a number of columns, which is what makes stepping off the
                 * end of a row land on the start of the next one. */
                if (key == KEY_LEFT) {
                    if (pos > 0) { pos--; cursor_to_cell(prompt_w + pos); }
                } else if (key == KEY_RIGHT) {
                    if (pos < len) { pos++; cursor_to_cell(prompt_w + pos); }
                } else if (key == KEY_HOME) {
                    pos = 0;
                    cursor_to_cell(prompt_w);
                } else if (key == KEY_END) {
                    pos = len;
                    cursor_to_cell(prompt_w + pos);
                } else if (key == KEY_DELETE) {
                    if (pos < len) {
                        for (int i = pos; i < len - 1; i++) cmd_buf[i] = cmd_buf[i + 1];
                        len--;
                        line_redraw_tail(cmd_buf, len, pos);
                    }
                } else if (key == KEY_UP || key == KEY_DOWN) {
                    /*
                     * The line being typed is saved on the way out and restored
                     * on the way back, so that walking through the history and
                     * returning does not cost the user what they had started.
                     */
                    int moved = 0;

                    if (key == KEY_UP && hist_view < history_count) {
                        if (hist_view == 0) {
                            cmd_buf[len] = '\0';
                            ft_strncpy(hist_saved, cmd_buf, 255);
                            hist_saved[255] = '\0';
                        }
                        hist_view++;
                        moved = 1;
                    } else if (key == KEY_DOWN && hist_view > 0) {
                        hist_view--;
                        moved = 1;
                    }

                    if (moved) {
                        const char *line = (hist_view == 0) ? hist_saved
                                                            : history_at(hist_view);
                        if (line == 0) line = "";

                        ft_strncpy(cmd_buf, line, 255);
                        cmd_buf[255] = '\0';
                        len = ft_strlen(cmd_buf);
                        if (len > 254) len = 254;
                        pos = len;

                        line_refresh(cmd_buf, len, pos);
                    }
                }
                continue;
            }

            if (c == '\n' || c == '\r') { cmd_buf[len] = '\0'; printk("\n"); break; }

            if (c == '\b') {
                if (pos > 0) {
                    for (int i = pos - 1; i < len - 1; i++) cmd_buf[i] = cmd_buf[i + 1];
                    len--;
                    pos--;
                    /* Not the backspace character. It does step back over a wrap
                     * and blank the cell it lands on, but the redraw below both
                     * reprints from here and erases past the end anyway, so
                     * moving by cell keeps one rule instead of two. */
                    cursor_to_cell(prompt_w + pos);
                    line_redraw_tail(cmd_buf, len, pos);
                }
                continue;
            }

            if (c == '\t') {
                /* Completion appends to the line, so it means nothing anywhere
                 * but at the end of it. */
                if (pos == len) {
                    cmd_buf[len] = '\0';
                    handle_tab_completion(cmd_buf, &len);
                    pos = len;
                    /* Completion prints what it appended, and the branch that
                     * lists candidates reprints the prompt through the same
                     * function this uses - either way the cursor ends up after
                     * the line. */
                    line_cell = prompt_w + len;
                }
                continue;
            }

            if (c >= 32 && c <= 126 && len < 254) {
                for (int i = len; i > pos; i--) cmd_buf[i] = cmd_buf[i - 1];
                cmd_buf[pos] = c;
                len++;

                char str[2] = { c, '\0' };
                printk(str);
                pos++;
                line_cell++;      /* the terminal wrapped it if it had to */

                line_redraw_tail(cmd_buf, len, pos);
            }
        }

        if (interrupted) {
            /* 130 is 128 + SIG_INT, which is what a shell reports for a command
             * the user interrupted - and an interrupted prompt is the same
             * answer to "how did that go". */
            last_exit_status = 130;
            continue;
        }
        if (len == 0) continue;

        /* Recorded before the line is taken apart: the parser writes null bytes
         * into cmd_buf at every && and ||, so by the time anything runs there is
         * no whole line left to remember. */
        history_add(cmd_buf);

        char *current_cmd = cmd_buf;
        int skip_execution = 0;

        while (current_cmd && *current_cmd) {
            char *next_cmd = 0;
            int op_type = 0;

            char *and_p = ft_strstr(current_cmd, "&&");
            char *or_p = ft_strstr(current_cmd, "||");

            if (and_p && (!or_p || and_p < or_p)) {
                *and_p = '\0';
                next_cmd = and_p + 2;
                op_type = 1;
            } else if (or_p) {
                *or_p = '\0';
                next_cmd = or_p + 2;
                op_type = 2;
            }

            if (!skip_execution) {
                for (int i = 0; i < MAX_ARGS; i++) { args[i] = 0; }
                int arg_count = 0; int in_word = 0;
                char *stage_args[MAX_STAGES][MAX_ARGS];
                char *stage_redirect[MAX_STAGES];
                int stage_count = 0;
                int background = 0;
                int too_many_args = 0;
                int parse_error = 0;

                for (int s = 0; s < MAX_STAGES; s++) { stage_args[s][0] = 0; stage_redirect[s] = 0; }

                for (int i = 0; current_cmd[i] != '\0'; i++) {
                    if (current_cmd[i] == ' ') { current_cmd[i] = '\0'; in_word = 0; }
                    else if (!in_word) {
                        /* One slot is reserved for the NULL terminator. */
                        if (arg_count >= MAX_ARGS - 1) { too_many_args = 1; break; }
                        args[arg_count++] = &current_cmd[i];
                        in_word = 1;
                    }
                }

                if (too_many_args) {
                    printk("sh: too many arguments\n");
                    last_exit_status = 1;
                    arg_count = 0;
                    args[0] = 0;
                }

                /*
                 * A trailing '&' means "do not wait for this". Only recognised as
                 * a token of its own and only at the end, which is the whole of
                 * the syntax this shell supports - "a & b" as two commands is a
                 * different feature and is not one of them.
                 */
                if (arg_count > 0 && ft_strcmp(args[arg_count - 1], "&") == 0) {
                    args[arg_count - 1] = 0;
                    arg_count--;
                    background = 1;
                }

                /*
                 * Expansion runs before the line is split into stages, and over
                 * arg_count tokens rather than up to the first null.
                 *
                 * It used to run afterwards, and the split had already written a
                 * null over the '|' - so the loop stopped there and no token in
                 * the second stage was ever expanded. "echo $HOME | cat" passed
                 * the literal text to the first stage's expansion and nothing to
                 * the second's, because there was no second pass.
                 */
                tilde_used = 0;
                int tilde_full = 0;

                for (int i = 0; i < arg_count; i++) {
                    if (args[i][0] == '$') {
                        if (args[i][1] == '?') {
                            static char status_str[16]; ft_itoa(last_exit_status, status_str); args[i] = status_str;
                        } else args[i] = get_env(&args[i][1]);
                    }
                    else if (args[i][0] == '~') {
                        /*
                         * Each ~ gets its own storage, taken from an arena that
                         * is reset per command.
                         *
                         * A single shared buffer used to serve every ~ in a line,
                         * so each expansion overwrote the last and every token
                         * ended up pointing at the same string: "cp ~/a ~/b"
                         * passed ~/b twice and copied a file onto itself.
                         *
                         * The append is bounded, as it has been since the tail
                         * came straight from the input line with no length check
                         * and "~/AAAA..." wrote past the buffer into the
                         * environment table beside it. Running out of arena is
                         * reported rather than absorbed - falling back to sharing
                         * is the bug this replaces.
                         */
                        const char *home = get_env("HOME");
                        uint32_t start = tilde_used;
                        uint32_t p = start;

                        for (int k = 0; home && home[k] != '\0' && p < sizeof(tilde_arena) - 1; k++) {
                            tilde_arena[p++] = home[k];
                        }
                        if (args[i][1] == '/') {
                            for (int k = 1; args[i][k] != '\0' && p < sizeof(tilde_arena) - 1; k++) {
                                tilde_arena[p++] = args[i][k];
                            }
                        }

                        if (p >= sizeof(tilde_arena) - 1) { tilde_full = 1; break; }

                        tilde_arena[p++] = '\0';
                        args[i] = &tilde_arena[start];
                        tilde_used = p;
                    }
                }

                if (tilde_full) {
                    printk("sh: too many ~ expansions in one command\n");
                    last_exit_status = 1;
                    parse_error = 1;
                }

                /*
                 * Split into pipeline stages on '|', with '>' belonging to the
                 * stage it appears in.
                 *
                 * The parser used to take whichever of the two it met first and
                 * stop, so everything after it was handed on as ordinary
                 * arguments: "a | b | c" ran "a" against the literal tokens
                 * "b | c", and "a | b > f" passed "> f" to b as two words. Both
                 * were silent - the wrong thing ran and reported success.
                 */
                int slot = 0;
                for (int i = 0; !parse_error && i < arg_count; i++) {
                    if (ft_strcmp(args[i], "|") == 0) {
                        if (stage_count + 1 >= MAX_STAGES) {
                            printk("sh: too many pipeline stages (max 4)\n");
                            last_exit_status = 1;
                            parse_error = 1;
                            break;
                        }
                        stage_args[stage_count][slot] = 0;
                        stage_count++;
                        slot = 0;
                    }
                    else if (ft_strcmp(args[i], ">") == 0) {
                        if (i + 1 >= arg_count || args[i + 1] == 0) {
                            printk("sh: syntax error: > without a file\n");
                            last_exit_status = 1;
                            parse_error = 1;
                            break;
                        }
                        stage_redirect[stage_count] = args[i + 1];
                        i++;   /* the filename is consumed, not an argument */
                    }
                    else if (slot < MAX_ARGS - 1) {
                        stage_args[stage_count][slot++] = args[i];
                    }
                }

                stage_args[stage_count][slot] = 0;
                stage_count++;

                /* An empty stage means a bare or doubled '|', which would
                 * otherwise fork a child with nothing to run. */
                for (int s = 0; !parse_error && s < stage_count; s++) {
                    if (stage_args[s][0] == 0 && stage_count > 1) {
                        printk("sh: syntax error: empty pipeline stage\n");
                        last_exit_status = 1;
                        parse_error = 1;
                    }
                }

                /*
                 * A backgrounded pipeline is refused rather than run in the
                 * foreground with the '&' quietly dropped, which is what would
                 * happen otherwise - the pipeline branch below is chosen first
                 * and never looks at the flag. Backgrounding one properly needs
                 * a process to own the whole pipeline, and this shell tracks
                 * jobs by pid.
                 */
                if (!parse_error && background && stage_count > 1) {
                    printk("sh: cannot background a pipeline\n");
                    last_exit_status = 1;
                    parse_error = 1;
                }

                if (parse_error) { stage_count = 0; }

                if (stage_count > 0 && stage_args[0][0] != 0) {
                    if (stage_count > 1) {
                        /*
                         * Every stage runs at once, each in its own process,
                         * joined by one pipe per gap.
                         *
                         * They used to run one after the other in this shell: the
                         * first stage was executed to completion with stdout
                         * pointing at the pipe, and only then was the second
                         * started to drain it. Nothing was reading while the
                         * first wrote, so a first stage producing more than the
                         * 4 KB the pipe holds blocked with no reader and never
                         * resumed - the shell along with it. That is what fork()
                         * was added for, and v0.5.2 fixed it for two stages;
                         * this is the same thing for any number up to MAX_STAGES.
                         */
                        int pfd[MAX_STAGES - 1][2];
                        int pipes_made = 0;
                        int pipes_needed = stage_count - 1;

                        while (pipes_made < pipes_needed && pipe(pfd[pipes_made]) >= 0) {
                            pipes_made++;
                        }

                        if (pipes_made < pipes_needed) {
                            printk("sh: cannot create pipe\n");
                            last_exit_status = 1;
                            for (int k = 0; k < pipes_made; k++) {
                                sys_close(pfd[k][0]); sys_close(pfd[k][1]);
                            }
                        } else {
                            int forked = 0;
                            int last_pid = -1;
                            int leader_pid = -1;
                            /* Every stage, so that a pipeline the user stops can
                             * be listed and brought back as the one thing they
                             * typed rather than as three processes. */
                            int stage_pids[MAX_STAGES];

                            for (int s = 0; s < stage_count; s++) {
                                int pid = sys_fork();

                                if (pid == 0) {
                                    /*
                                     * Join the pipeline's group before anything
                                     * else. The parent makes the same call on the
                                     * line below, with the same arguments,
                                     * because neither side can know which of them
                                     * runs first - and a stage that is signalled
                                     * before it has a group is not in the one the
                                     * user is looking at.
                                     *
                                     * The first stage founds the group and the
                                     * rest join it, so "ls | grep etc" is three
                                     * tasks the terminal can address as the one
                                     * thing that was typed.
                                     */
                                    sys_setpgid(0, (leader_pid > 0) ? leader_pid : 0);

                                    /* Undo the shell's own SIG_IGN, inherited a
                                     * line ago through fork(). A writing stage is
                                     * the one process that has to die when its
                                     * reader does, and a builtin stage runs right
                                     * here in the shell's image with the shell's
                                     * dispositions. The same goes for SIG_INT: a
                                     * stage the user cannot interrupt is worse
                                     * than one that dies. */
                                    sys_register_signal(SIG_PIPE, (void *)SIG_DFL);
                                    sys_register_signal(SIG_INT, (void *)SIG_DFL);
                                    sys_register_signal(SIG_TSTP, (void *)SIG_DFL);

                                    /* And it is not the shell any more: a program
                                     * this stage starts belongs to the pipeline's
                                     * group, and the terminal is not this
                                     * process's to hand out. */
                                    shell_owns_terminal = 0;

                                    if (s > 0) dup2(pfd[s - 1][0], 0);
                                    if (s < pipes_needed) dup2(pfd[s][1], 1);

                                    /* Every pipe end, not just this stage's. A
                                     * descriptor left open anywhere in the
                                     * pipeline keeps a reader or a writer alive
                                     * that has no business being one, and the
                                     * stage downstream never sees end-of-file. */
                                    for (int k = 0; k < pipes_needed; k++) {
                                        sys_close(pfd[k][0]); sys_close(pfd[k][1]);
                                    }

                                    if (stage_redirect[s]) run_with_redirect(stage_args[s], stage_redirect[s]);
                                    else execute_command(stage_args[s]);

                                    sys_exit_status(last_exit_status);
                                }

                                if (pid < 0) break;

                                if (leader_pid < 0) leader_pid = pid;
                                sys_setpgid(pid, leader_pid);

                                stage_pids[forked] = pid;
                                last_pid = pid;
                                forked++;
                            }

                            /* The pipeline holds the terminal while it runs, so
                             * Ctrl-C reaches its stages and not this shell. */
                            if (leader_pid > 0) sys_tcsetpgrp(leader_pid);

                            /*
                             * The shell closes every end before waiting, and this
                             * is load-bearing rather than tidiness: a reader sees
                             * end-of-file only when every write end is shut, and
                             * the shell holds one of each.
                             */
                            for (int k = 0; k < pipes_needed; k++) {
                                sys_close(pfd[k][0]); sys_close(pfd[k][1]);
                            }

                            if (forked < stage_count) {
                                printk("sh: cannot fork for pipeline\n");
                                last_exit_status = 1;
                            }

                            /*
                             * Collect them all, and report the last stage's
                             * status as the pipeline's - which is why wait() has
                             * to say which child it is talking about. They finish
                             * in whatever order they finish, and if the user
                             * stops them the whole pipeline becomes one job.
                             */
                            if (forked > 0) {
                                int st = wait_foreground(stage_pids, forked,
                                                         leader_pid, last_pid, 0);
                                last_exit_status = (st == JOB_STOPPED) ? 148 : st;
                            }

                            /*
                             * Take the terminal back. The kernel already hands it
                             * to a woken parent when the last member of the
                             * foreground group dies, so this is belt and braces -
                             * but a pipeline that failed to fork every stage never
                             * had a group to lose, and the prompt has to be
                             * interruptible-by-nobody either way.
                             */
                            sys_tcsetpgrp(sys_getpgid(0));
                        }
                    } else if (background) {
                        /*
                         * Run it in a child and carry on. The status is collected
                         * later by jobs_reap(), so $? is not set here: the command
                         * has not finished, and reporting a status for something
                         * still running would be a lie the '&&' chain would then
                         * act on.
                         */
                        int pid = sys_fork();
                        if (pid == 0) {
                            /* Its own group, set from both sides for the same
                             * reason the pipeline sets it from both. A background
                             * job is deliberately never given the terminal, which
                             * is what keeps Ctrl-C from reaching it. */
                            sys_setpgid(0, 0);

                            /* Same reason as the pipeline stages: only the shell
                             * itself gets to ignore these, and only the shell
                             * hands out the terminal. */
                            sys_register_signal(SIG_PIPE, (void *)SIG_DFL);
                            sys_register_signal(SIG_INT, (void *)SIG_DFL);
                            sys_register_signal(SIG_TSTP, (void *)SIG_DFL);
                            shell_owns_terminal = 0;

                            if (stage_redirect[0]) run_with_redirect(stage_args[0], stage_redirect[0]);
                            else execute_command(stage_args[0]);
                            sys_exit_status(last_exit_status);
                        } else if (pid < 0) {
                            printk("sh: cannot fork\n");
                            last_exit_status = 1;
                        } else {
                            sys_setpgid(pid, pid);

                            char num[16];
                            int id = job_add(pid, &pid, 1, 0);

                            printk("[");
                            ft_itoa(id, num); printk(num);
                            printk("] pid ");
                            ft_itoa(pid, num); printk(num);
                            printk("\n");
                            last_exit_status = 0;
                        }
                    } else {
                        if (stage_redirect[0]) run_with_redirect(stage_args[0], stage_redirect[0]);
                        else execute_command(stage_args[0]);
                    }
                }
            }

            if (!skip_execution) {
                if (op_type == 1) { skip_execution = (last_exit_status != 0); }
                else if (op_type == 2) { skip_execution = (last_exit_status == 0); }
            } else {
                if (op_type == 1) { skip_execution = 1; }
                else if (op_type == 2) { skip_execution = 0; }
            }

            current_cmd = next_cmd;
        }
    }
}

/**
 * @brief Initialization routine for the shell.
 */
void _start(void) {
    main();
    sys_exit();
}