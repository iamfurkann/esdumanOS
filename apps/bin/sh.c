#include "syscall.h"
#include "errno.h"

typedef unsigned int uint32_t;

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
 * @brief Waits for any child to finish.
 * @param status Receives the child's exit status; may be NULL.
 * @return The pid that reported, or E_CHILD when there are none left.
 */
int sys_wait(int *status) { return syscall(SYSCALL_WAIT, (int)status, 0, 0); }
/**
 * @brief Asks whether a child has finished, without blocking if none has.
 * @param status Receives the child's exit status; may be NULL.
 * @return The pid that reported, 0 when none is ready, E_CHILD when there are none.
 */
int sys_wait_nohang(int *status) { return syscall(SYSCALL_WAIT, (int)status, 1, 0); }

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
static int job_pids[MAX_JOBS];
static int job_count = 0;

/**
 * @brief Records a background job, if there is room to.
 * @param pid Process id of the job.
 */
static void job_add(int pid) {
    if (job_count < MAX_JOBS) {
        job_pids[job_count++] = pid;
    } else {
        /* Said out loud rather than dropped. The job still runs and is still
         * reaped by the sweep below - it just cannot be listed. */
        printk("sh: too many background jobs to track\n");
    }
}

/**
 * @brief Removes a job from the table once it has been collected.
 * @param pid Process id that reported.
 */
static void job_remove(int pid) {
    for (int i = 0; i < job_count; i++) {
        if (job_pids[i] == pid) {
            for (int j = i; j < job_count - 1; j++) job_pids[j] = job_pids[j + 1];
            job_count--;
            return;
        }
    }
}

/**
 * @brief Collects any background job that has finished, without waiting.
 *
 * Called before each prompt. Reporting the status here rather than the moment it
 * arrives is deliberate: a job finishing in the middle of a line being typed
 * would otherwise print over it.
 */
static void jobs_reap(void) {
    for (;;) {
        int status = 0;
        int pid = sys_wait_nohang(&status);
        if (pid <= 0) return;   /* 0: nothing ready. negative: no children. */

        job_remove(pid);

        char num[16];
        printk("[done] pid ");
        ft_itoa(pid, num);    printk(num);
        printk(" status ");
        ft_itoa(status, num); printk(num);
        printk("\n");
    }
}
/**
 * @brief Registers a signal handler.
 * @param sig_num Signal number.
 * @param handler Pointer to the handler function.
 */
void sys_register_signal(int sig_num, void *handler) { syscall(SYSCALL_SIGNAL_REG, sig_num, (int)handler, 0); }
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
/**
 * @brief Lists directory contents.
 * @param parent_id ID of the directory to list.
 */
void sys_ls_dir(int parent_id) { syscall(28, parent_id, 0, 0); }
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
 * @brief Prints the kernel ring buffer.
 */
void sys_dmesg(void) { syscall(SYSCALL_DMESG, 0, 0, 0); }
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

/**
 * @brief Reads a line from the user.
 * 
 * @param buf Buffer to store the line.
 * @param hide Whether to hide characters (e.g., for passwords).
 * @param max_len Maximum length of the buffer.
 */
void read_line(char *buf, int hide, int max_len) {
    int idx = 0;
    while (1) {
        char c = get_keyboard_char();
        if (c == '\n' || c == '\r') { buf[idx] = '\0'; printk("\n"); break; } 
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
    printk("    dmesg             Show kernel log buffer\n");
    printk("    jobs              List background jobs started with &\n");
    printk("    wait              Block until every background job has finished\n");
    printk("    hexdump [addr]    Show memory dump at address\n");
    printk("    help              Show this help menu\n");
    printk("\n  Settings:\n");
    printk("    layout tr|us      Set keyboard layout\n");
    printk("    lockdown          Switch system to safe mode\n");
    printk("    clear             Clear the screen\n");
    printk("\n  Operators: | (pipe, two stages), > (redirect), && (AND), || (OR)\n");
    printk("  Note: > and | cannot be combined in one command.\n");
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

    if (args[file_args_start] == 0) {
        printk("cat: Please specify a file to read.\n");
        return 1;
    }

    for (int i = file_args_start; args[i] != 0; i++) {
        int fd = sys_open(args[i]); 
        if (fd < 0) { 
            printk("cat: "); printk(args[i]); printk(": No such file or directory.\n"); 
            continue; 
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

        sys_close(fd); 
    }
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
void execute_command(char **args) {
    if (!args[0]) return;

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
    else if (ft_strcmp(args[0], "ls") == 0) { sys_ls_dir(sys_get_dir_id(".")); last_exit_status = 0; }
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
        if (args[1]) last_exit_status = syscall(5, (int)args[1], 0, 0) < 0 ? 127 : 0;
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
        read_line(su_pass, 1, 64);
        if (sys_setuid(0, su_pass) == 0) {
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
        sys_dmesg();
        last_exit_status = 0;
    }
    else if (ft_strcmp(args[0], "wait") == 0) {
        /*
         * Block until every background job this shell started has reported.
         *
         * The loop ends on its own: sys_wait() returns E_CHILD once there is
         * nothing left to wait for, which is a negative value and not a pid. A
         * shell that tested for "no more jobs" by counting its own table instead
         * would deadlock the first time a job it had lost track of was still
         * running - the kernel's answer is the one that matters.
         */
        int status = 0;
        int pid;
        char num[16];

        while ((pid = sys_wait(&status)) > 0) {
            job_remove(pid);
            printk("[done] pid ");
            ft_itoa(pid, num);    printk(num);
            printk(" status ");
            ft_itoa(status, num); printk(num);
            printk("\n");
        }
        last_exit_status = 0;
    }
    else if (ft_strcmp(args[0], "jobs") == 0) {
        /*
         * Only what this shell started with '&' and has not yet collected. It is
         * not a process list: the kernel has no call that enumerates tasks, and
         * a shell has no business reading one if it did.
         */
        if (job_count == 0) {
            printk("sh: no background jobs\n");
        } else {
            char num[16];
            for (int i = 0; i < job_count; i++) {
                printk("[");
                ft_itoa(i + 1, num); printk(num);
                printk("] pid ");
                ft_itoa(job_pids[i], num); printk(num);
                printk("\n");
            }
        }
        last_exit_status = 0;
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
        if (ft_strlen(args[0]) > 58) {
            printk("sh: command name too long (max 58 characters)\n");
            last_exit_status = 127;
            return;
        }

        // Try to execute it as an ELF from /bin/
        char exec_path[64];
        ft_strcpy(exec_path, "/bin/");
        ft_strcpy(&exec_path[5], args[0]);

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

        int exec_res = syscall(5, (int)exec_path, 0, (int)arg_str); // SYSCALL_EXEC
        if (exec_res < 0) {
            printk("sh: command not found: "); printk(args[0]); printk("\n");
            last_exit_status = 127;
        } else {
            /*
             * The child's own exit status, which exec() now returns once the
             * child has finished. This used to be a hardcoded 0, so every
             * program that started at all counted as having succeeded and the
             * && and || below could not tell one outcome from the other:
             * "stat /no_such_file && echo CHAINED" printed CHAINED.
             *
             * Negative already means the program could not be started, and an
             * exit status is 0-255, so the two never collide.
             */
            last_exit_status = exec_res;
        }
    }
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
    "jobs", "wait",
    "exit", "export", "halt", "help", "hexdump", "kill", "layout",
    "lockdown", "ls", "meminfo", "mkdir", "mv", "pwd", "reboot",
    "rm", "sleep", "su", "write",
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
            printk(current_username);
            printk("@esdumanOS ");
            printk(current_path);
            if (current_uid == 0) printk(" # ");
            else printk(" $ ");
            buf[*idx] = '\0';
            printk(buf);
        }
    }
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

    while (1) {
        /*
         * Collect finished background jobs here rather than the moment they
         * report. A job ending in the middle of a line being typed would
         * otherwise print over it, and the status has nowhere to go until there
         * is a prompt to print it above.
         */
        jobs_reap();

        printk("\n");
        printk(current_username);
        printk("@esdumanOS ");
        printk(current_path); 
        
        if (current_uid == 0) printk(" # ");
        else printk(" $ ");

        int idx = 0;
    while (1) {
            char c = get_keyboard_char();
            if (c == '\n' || c == '\r') { cmd_buf[idx] = '\0'; printk("\n"); break; } 
            else if (c == '\b') { if (idx > 0) { idx--; printk("\b \b"); } } 
            else if (c == '\t') { cmd_buf[idx] = '\0'; handle_tab_completion(cmd_buf, &idx); }
            else if (c >= 32 && c <= 126 && idx < 254) {
                char str[2] = {c, '\0'}; printk(str); cmd_buf[idx++] = c;
            }
        }
        if (idx == 0) continue;

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
                int arg_count = 0; int in_word = 0; char *redirect_file = 0;
                char *pipe_args[MAX_ARGS]; for (int i = 0; i < MAX_ARGS; i++) { pipe_args[i] = 0; }
                int has_pipe = 0; int pipe_arg_count = 0;
                int background = 0;
                int too_many_args = 0;

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

                for (int i = 0; i < arg_count; i++) {
                    if (ft_strcmp(args[i], ">") == 0) {
                        /* i + 1 can be arg_count, which is a slot inside the
                         * array only because the count is now bounded. */
                        args[i] = 0;
                        if (i + 1 < arg_count && args[i + 1]) redirect_file = args[i + 1];
                        break;
                    }
                    else if (ft_strcmp(args[i], "|") == 0) {
                        args[i] = 0; has_pipe = 1;
                        for (int j = i + 1; j < arg_count && pipe_arg_count < MAX_ARGS - 1; j++) {
                            pipe_args[pipe_arg_count++] = args[j];
                        }
                        break;
                    }
                }

                for (int i = 0; args[i] != 0; i++) {
                    if (args[i][0] == '$') {
                        if (args[i][1] == '?') {
                            static char status_str[16]; ft_itoa(last_exit_status, status_str); args[i] = status_str;
                        } else args[i] = get_env(&args[i][1]);
                    }
                    else if (args[i][0] == '~') {
                        /*
                         * Bounded append. The tail came from the input line with
                         * no length check, so "~/AAAA..." wrote past a 128-byte
                         * buffer in .bss - straight into env_keys, env_vals,
                         * current_path and current_username, which sit beside it.
                         *
                         * KNOWN LIMITATION, not fixed here: this buffer is
                         * static and shared, so every ~ token in one command
                         * ends up pointing at the same string. "cp ~/a ~/b"
                         * passes ~/b twice. Fixing that needs per-token storage.
                         */
                        static char expanded_path[128];
                        uint32_t p = 0;
                        const char *home = get_env("HOME");
                        for (int k = 0; home && home[k] != '\0' && p < sizeof(expanded_path) - 1; k++) {
                            expanded_path[p++] = home[k];
                        }
                        if (args[i][1] == '/') {
                            for (int k = 1; args[i][k] != '\0' && p < sizeof(expanded_path) - 1; k++) {
                                expanded_path[p++] = args[i][k];
                            }
                        }
                        expanded_path[p] = '\0';
                        args[i] = expanded_path;
                    }
                }

                if (args[0] != 0) {
                    if (has_pipe) {
                        int pfd[2];
                        if (pipe(pfd) >= 0) {
                            /*
                             * Both stages run at once, each in its own process.
                             *
                             * They used to run one after the other in this shell:
                             * the first stage was executed to completion with
                             * stdout pointing at the pipe, and only then was the
                             * second started to drain it. Nothing was reading
                             * while the first stage wrote, so a first stage that
                             * produced more than the 4 KB the pipe holds blocked
                             * with no reader and never resumed - the shell along
                             * with it. That is what fork() was added for.
                             *
                             * Redirection combined with a pipe is still not
                             * supported: the parser takes whichever it meets
                             * first, so the other is passed through as a literal
                             * argument.
                             */
                            int left = sys_fork();
                            if (left == 0) {
                                dup2(pfd[1], 1);
                                sys_close(pfd[0]);
                                sys_close(pfd[1]);
                                execute_command(args);
                                sys_exit_status(last_exit_status);
                            }

                            int right = (left < 0) ? -1 : sys_fork();
                            if (right == 0) {
                                dup2(pfd[0], 0);
                                sys_close(pfd[0]);
                                sys_close(pfd[1]);
                                execute_command(pipe_args);
                                sys_exit_status(last_exit_status);
                            }

                            /*
                             * The shell closes both ends before waiting, and this
                             * is load-bearing rather than tidiness: the reader
                             * sees end-of-file only when every write end is shut,
                             * and the shell holds one. Leaving it open hangs the
                             * second stage on a pipe nobody will ever write to.
                             */
                            sys_close(pfd[0]);
                            sys_close(pfd[1]);

                            if (left < 0 || right < 0) {
                                printk("sh: cannot fork for pipeline\n");
                                last_exit_status = 1;
                            }

                            /*
                             * Collect both, and report the last stage's status as
                             * the pipeline's - which is why wait() has to say
                             * which child it is talking about. They finish in
                             * whatever order they finish.
                             */
                            int reaped = 0;
                            while (reaped < 2) {
                                int st = 0;
                                int who = sys_wait(&st);
                                if (who < 0) break;
                                if (who == right) last_exit_status = st;
                                reaped++;
                            }
                        } else printk("Error: Failed to create pipe!\n");
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
                            if (redirect_file) run_with_redirect(args, redirect_file);
                            else execute_command(args);
                            sys_exit_status(last_exit_status);
                        } else if (pid < 0) {
                            printk("sh: cannot fork\n");
                            last_exit_status = 1;
                        } else {
                            job_add(pid);

                            char num[16];
                            printk("[bg] pid ");
                            ft_itoa(pid, num); printk(num);
                            printk("\n");
                            last_exit_status = 0;
                        }
                    } else {
                        if (redirect_file) run_with_redirect(args, redirect_file);
                        else execute_command(args);
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