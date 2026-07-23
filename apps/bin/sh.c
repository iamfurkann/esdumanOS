#include "syscall.h"
#include "errno.h"

typedef unsigned int uint32_t;

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
 * @param parent_id ID of the parent directory.
 * @return System call status.
 */
int sys_create_file(const char *name, const char *content, int parent_id) { return syscall(8, (int)name, (int)content, parent_id); }
/**
 * @brief Deletes a file via system call.
 * @param name File name.
 * @param parent_id ID of the parent directory.
 * @return System call status.
 */
int sys_delete_file(const char *name, int parent_id) { return syscall(22, (int)name, parent_id, 0); }
/**
 * @brief Reads a file content via system call.
 * @param name File name.
 * @param parent_id ID of the parent directory.
 * @return System call status.
 */
int sys_cat_file(const char *name, int parent_id) { return syscall(11, (int)name, parent_id, 0); }
/**
 * @brief Reads raw file content via system call.
 * @param name File name.
 * @param parent_id ID of the parent directory.
 * @return System call status.
 */
int sys_cat_raw_file(const char *name, int parent_id) { return syscall(34, (int)name, parent_id, 0); } // 34 = SYSCALL_CAT_RAW 
/**
 * @brief Renames a file via system call.
 * @param old_name Current name.
 * @param new_name New name.
 * @param parent_id ID of the parent directory.
 * @return System call status.
 */
int sys_rename_file(const char *old_name, const char *new_name, int parent_id) { return syscall(23, (int)old_name, (int)new_name, parent_id); }
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
void sys_kill(int pid, int sig_num) { syscall(SYSCALL_KILL, pid, sig_num, 0); }
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
 * @param parent_id ID of the parent directory.
 * @return System call status.
 */
int sys_mkdir(const char *name, int parent_id) { return syscall(26, (int)name, parent_id, 0); }
/**
 * @brief Lists directory contents.
 * @param parent_id ID of the directory to list.
 */
void sys_ls_dir(int parent_id) { syscall(28, parent_id, 0, 0); }
/**
 * @brief Gets the directory ID.
 * @param name Directory name.
 * @param parent_id ID of the parent directory.
 * @return Directory ID.
 */
int sys_get_dir_id(const char *name, int parent_id) { return syscall(29, (int)name, parent_id, 0); }
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
 * @param parent_id ID of the parent directory.
 * @return File descriptor.
 */
int sys_open(const char *name, int parent_id) { return syscall(SYSCALL_OPEN, (int)name, parent_id, 0); }
/* --- 4. ENVIRONMENT VARIABLES (ENV) --- */
char env_keys[20][32];
char env_vals[20][64];
int env_count = 0;
int last_exit_status = 0; 
int current_dir_id = 0; 
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
    printk("\n[!!!] MINISHELL CAUGHT USER SIGNAL! [!!!]\n");
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
    printk("Available commands:\n");
    printk("  help       : Shows this menu\n");
    printk("  ls         : Lists disk contents\n");
    printk("  cat [name] : Reads file content\n");
    printk("  cat_raw [name]   : Bypasses VFS decryption, shows raw (HEX) disk dump\n");
    printk("  write [name]: Writes new file to disk\n");
    printk("  rm [name]  : Permanently deletes file from disk\n");
    printk("  mv [old] [new] : Renames a file\n");
    printk("  clear      : Clears the screen\n");
    printk("  layout tr  : Sets keyboard to Turkish (QWERTY)\n");
    printk("  layout us  : Sets keyboard to English (QWERTY)\n");
    printk("  lockdown   : Switches system to SAFE MODE\n");
    printk("  stack      : Shows kernel stack dump\n");
    printk("  meminfo    : Provides RAM info\n");
    printk("  testmalloc : Starts heap test\n");
    printk("  hexdump    : Shows data dump at address\n");
    printk("  alarm      : CALLBACK test\n");
    printk("  panic      : ISR test\n");
    printk("  reboot     : Reboots the system\n");
    printk("  halt       : Halts the processor\n");
    printk("  exec [elf] : Executes an external program\n");
    printk("  kill [pid] [sig]: Sends signal to specified process\n");
    printk("  --- 42 Minishell Built-in ---\n");
    printk("  echo [-n]  : Prints text to screen (supports \'>\')\n");
    printk("  pwd        : Shows current directory\n");
    printk("  env        : Shows environment variables\n");
    printk("  export     : Defines new variable (e.g., export VAR VALUE)\n");
}

/**
 * @brief Built-in cat command to display file contents.
 * 
 * @param args Array of command-line arguments.
 * @param current_dir_id ID of the current directory.
 * @return Exit status of the command.
 */
int builtin_cat(char **args, int current_dir_id) {
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
        int fd = sys_open(args[i], current_dir_id); 
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
void execute_command(char **args, char *redirect_file) {
    if (!args[0]) return;

    if (ft_strcmp(args[0], "cat") == 0) {
        last_exit_status = builtin_cat(args, current_dir_id);
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
    else if (ft_strcmp(args[0], "ls") == 0) { sys_ls_dir(current_dir_id); last_exit_status = 0; }
    else if (ft_strcmp(args[0], "mkdir") == 0) {
        if (args[1]) sys_mkdir(args[1], current_dir_id); else printk("Usage: mkdir <directory>\n");
    }
    else if (ft_strcmp(args[0], "cd") == 0) {
        char *target = args[1];
        if (!target) target = "~";

        int invalid_path = 0;
        char *ptr = target;
        while (*ptr) {
            if (*ptr == '/' && *(ptr + 1) == '/') {
                char *check = ptr;
                while (*check == '/') check++; 
                if (*check != '\0') {
                    invalid_path = 1;
                    break;
                }
            }
            ptr++;
        }

        if (invalid_path) {
            printk("minishell: cd: "); printk(target); printk(": No such file or directory\n");
            last_exit_status = 1;
            return;
        }

        int new_id = sys_get_dir_id(target, current_dir_id);
        if (new_id != -1) { 
            current_dir_id = new_id;
            
            if (target[0] == '/') {
                ft_strcpy(current_path, target);
            } else if (ft_strcmp(target, "~") == 0) {
                ft_strcpy(current_path, get_env("HOME"));
            } else if (ft_strcmp(target, "..") == 0) {
                int len = ft_strlen(current_path);
                while(len > 1 && current_path[len-1] != '/') len--;
                if (len > 1) current_path[len-1] = '\0';
                else current_path[1] = '\0'; // root
            } else if (ft_strcmp(target, ".") != 0) {
                if (ft_strcmp(current_path, "/") != 0) ft_strcpy(&current_path[ft_strlen(current_path)], "/");
                ft_strcpy(&current_path[ft_strlen(current_path)], target);
            }

            int r = 0, w = 0;
            while (current_path[r]) {
                if (current_path[r] == '/' && current_path[r+1] == '/') { r++; continue; }
                current_path[w++] = current_path[r++];
            }
            current_path[w] = '\0';
            if (w > 1 && current_path[w-1] == '/') current_path[w-1] = '\0';

            last_exit_status = 0;
        } 
        else { 
            printk("cd: No such directory: "); printk(target); printk("\n"); 
            last_exit_status = 1; 
        }
    }
    else if (ft_strcmp(args[0], "write") == 0) {
        if (args[1] && args[2]) {
            char *content = args[2];
            for(int i = 2; args[i] != 0; i++) { if (args[i+1] != 0) args[i][ft_strlen(args[i])] = ' '; }
            int res = sys_create_file(args[1], content, current_dir_id);
            if (res == E_OK) printk("File written successfully!\n");
        } else { printk("Usage: write <file> <content>\n"); }
    }
    else if (ft_strcmp(args[0], "rm") == 0) {
        if (args[1]) sys_delete_file(args[1], current_dir_id); else printk("Usage: rm <file>\n");
    }
    else if (ft_strcmp(args[0], "mv") == 0) {
        if (args[1] && args[2]) sys_rename_file(args[1], args[2], current_dir_id); else printk("Usage: mv <old> <new>\n");
    }
    else if (ft_strcmp(args[0], "layout") == 0) {
        if (args[1] && ft_strcmp(args[1], "tr") == 0) syscall(12, 1, 0, 0);
        else if (args[1] && ft_strcmp(args[1], "us") == 0) syscall(12, 0, 0, 0);
    }
    else if (ft_strcmp(args[0], "lockdown") == 0) { syscall(13, 0, 0, 0); }
    else if (ft_strcmp(args[0], "stack") == 0) { syscall(14, 0, 0, 0); }
    else if (ft_strcmp(args[0], "meminfo") == 0) { syscall(15, 0, 0, 0); }
    else if (ft_strcmp(args[0], "testmalloc") == 0) { syscall(16, 0, 0, 0); }
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
    else if (ft_strcmp(args[0], "alarm") == 0) { syscall(18, 0, 0, 0); }
    else if (ft_strcmp(args[0], "panic") == 0) { syscall(19, 0, 0, 0); }
    else if (ft_strcmp(args[0], "reboot") == 0) { syscall(20, 0, 0, 0); }
    else if (ft_strcmp(args[0], "halt") == 0) { syscall(21, 0, 0, 0); }
    else if (ft_strcmp(args[0], "exec") == 0) { if (args[1]) syscall(5, (int)args[1], current_dir_id, 0); }
    else if (ft_strcmp(args[0], "exit") == 0) { printk("exit\n"); syscall(1, 0, 0, 0); while(1); }
    else if (ft_strcmp(args[0], "cat_raw") == 0) {
        if (args[1]) sys_cat_raw_file(args[1], current_dir_id); else printk("Usage: cat_raw <file>\n");
    }
    else if (ft_strcmp(args[0], "kill") == 0) {
        if (args[1] && args[2]) sys_kill(hex_to_int(args[1]), hex_to_int(args[2]));
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
        }
    }
    else if (ft_strcmp(args[0], "dmesg") == 0) {
        sys_dmesg();
    }
    else {
        if (ft_strlen(args[0]) > 58) {
            printk("minishell: command name too long (max 58 characters)\n");
            last_exit_status = 127;
            return;
        }

        char bin_path[64] = "/bin/";
        ft_strcpy(&bin_path[5], args[0]);

        char full_cmd[128];
        int idx = 0;
        for (int i = 0; args[i] != 0; i++) {
            int j = 0;
            while (args[i][j]) { 
                if (idx < 126) full_cmd[idx++] = args[i][j++]; 
                else break;
            }
            if (args[i+1] != 0 && idx < 126) full_cmd[idx++] = ' ';
        }
        full_cmd[idx] = '\0';

        int fd = sys_open(bin_path, current_dir_id);
        if (fd >= 0) {
            sys_close(fd); 
            int res = syscall(5, (int)bin_path, current_dir_id, (int)full_cmd); 
            if (res == -1) last_exit_status = 126; 
            else last_exit_status = 0; 
        } 
        else {
            printk("minishell: command not found: "); printk(args[0]); printk("\n");
            last_exit_status = 127;
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
    char *args[32];

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
    
    current_dir_id = syscall(SYSCALL_GET_DIR_ID, (int)current_path, 0, 0);
    if (current_dir_id == -1) {
        current_dir_id = 0;
        ft_strcpy(current_path, "/");
    }

    set_env("USER", current_username);
    set_env("OS", "esdumanOS");
    sys_register_signal(5, my_custom_handler);

    while (1) {
        printk("\n");
        printk(current_username);
        printk("@minishell ");
        printk(current_path); 
        
        if (current_uid == 0) printk(" # ");
        else printk(" $ ");

        int idx = 0;
    while (1) {
            char c = get_keyboard_char();
            if (c == '\n' || c == '\r') { cmd_buf[idx] = '\0'; printk("\n"); break; } 
            else if (c == '\b') { if (idx > 0) { idx--; printk("\b \b"); } } 
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
                for (int i = 0; i < 32; i++) { args[i] = 0; }
                int arg_count = 0; int in_word = 0; char *redirect_file = 0;
                char *pipe_args[32]; for (int i = 0; i < 32; i++) { pipe_args[i] = 0; }
                int has_pipe = 0; int pipe_arg_count = 0;

                for (int i = 0; current_cmd[i] != '\0'; i++) {
                    if (current_cmd[i] == ' ') { current_cmd[i] = '\0'; in_word = 0; } 
                    else if (!in_word) { args[arg_count++] = &current_cmd[i]; in_word = 1; }
                }

                for (int i = 0; i < arg_count; i++) {
                    if (ft_strcmp(args[i], ">") == 0) {
                        args[i] = 0; if (args[i+1]) redirect_file = args[i+1]; break;
                    }
                    else if (ft_strcmp(args[i], "|") == 0) {
                        args[i] = 0; has_pipe = 1;
                        for (int j = i + 1; j < arg_count; j++) pipe_args[pipe_arg_count++] = args[j];
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
                        static char expanded_path[128];
                        ft_strcpy(expanded_path, get_env("HOME"));
                        if (args[i][1] == '/') ft_strcpy(&expanded_path[ft_strlen(expanded_path)], &args[i][1]);
                        args[i] = expanded_path;
                    }
                }

                if (args[0] != 0) {
                    if (has_pipe) {
                        int pfd[2];
                        if (pipe(pfd) >= 0) {
                            dup2(1, 10); dup2(0, 11);
                            dup2(pfd[1], 1); execute_command(args, redirect_file); dup2(10, 1);
                            sys_close(pfd[1]);
                            dup2(pfd[0], 0); execute_command(pipe_args, 0); dup2(11, 0);
                            sys_close(pfd[0]);
                        } else printk("Error: Failed to create pipe!\n");
                    } else {
                        execute_command(args, redirect_file);
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