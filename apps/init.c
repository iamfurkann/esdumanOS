#include "syscall.h"
#include "errno.h"

typedef unsigned int uint32_t;

/**
 * @brief Performs a system call.
 * 
 * This function triggers a software interrupt (0x80) to switch to kernel mode
 * and execute a system call.
 * 
 * @param num The system call number.
 * @param arg1 The first argument for the system call.
 * @param arg2 The second argument for the system call.
 * @param arg3 The third argument for the system call.
 * @return The result of the system call.
 */
static inline int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Compares two strings.
 * 
 * @param s1 The first string to compare.
 * @param s2 The second string to compare.
 * @return An integer less than, equal to, or greater than zero if s1 is found,
 *         respectively, to be less than, to match, or be greater than s2.
 */
int ft_strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * @brief Copies a string.
 * 
 * @param dest The destination buffer.
 * @param src The source string to copy.
 */
void ft_strcpy(char *dest, const char *src) {
    while(*src) *dest++ = *src++;
    *dest = '\0';
}

/**
 * @brief Calculates the length of a string.
 * 
 * @param s The string to measure.
 * @return The length of the string.
 */
int ft_strlen(const char *s) {
    int i = 0; 
    while(s[i]) i++; 
    return i;
}

/**
 * @brief Prints a string to the standard output.
 * 
 * @param str The string to print.
 */
void printk(const char *str) { 
    syscall(SYSCALL_WRITE, 1, (int)str, ft_strlen(str)); 
}

/**
 * @brief Reads a single character from the keyboard.
 * 
 * @return The character read.
 */
char get_keyboard_char(void) { 
    char c = 0; 
    syscall(SYSCALL_READ, 0, (int)&c, 1); 
    return c;
}

/**
 * @brief Exits the current process.
 */
void sys_exit(void) { 
    syscall(SYSCALL_EXIT, 0, 0, 0); 
    while(1); 
}

/**
 * @brief Sets the user ID.
 * 
 * @param uid The user ID to set.
 * @param password The password for authentication.
 * @return 0 on success, or a negative error code.
 */
int sys_setuid(int uid, const char *password) { 
    return syscall(SYSCALL_SETUID, uid, (int)password, 0); 
}

/**
 * @brief Reads a line of input from the user.
 * 
 * @param buf The buffer to store the read line.
 * @param hide If non-zero, characters are echoed as '*' (useful for passwords).
 */
void read_line(char *buf, int hide) {
    int idx = 0;
    while (1) {
        char c = get_keyboard_char();
        if (c == '\n' || c == '\r') { 
            buf[idx] = '\0'; 
            printk("\n"); 
            break; 
        } 
        else if (c == '\b') { 
            if (idx > 0) { 
                idx--; 
                printk("\b \b"); 
            } 
        } 
        else if (c >= 32 && c <= 126 && idx < 254) {
            char str[2] = { hide ? '*' : c, '\0' }; 
            printk(str); 
            buf[idx++] = c;
        }
    }
}

/**
 * @brief Main entry point for the init process.
 * 
 * This process is responsible for displaying the login screen, authenticating
 * the user, and launching the shell.
 */
void main(void) {
    char user_buf[32];
    char pass_buf[32];
    int current_uid = -1;
    char current_username[32];

    syscall(10, 0, 0, 0);
    printk("======================================\n");
    printk("         Welcome to esdumanOS         \n");
    printk("             Login Screen             \n");
    printk("======================================\n\n");

    int login_success = 0;
    while (!login_success) {
        printk("login: ");
        read_line(user_buf, 0); 

        printk("password: ");
        read_line(pass_buf, 1);

        int uid = syscall(SYSCALL_AUTH, (int)user_buf, (int)pass_buf, 0);

        if (uid >= 0) {
            current_uid = uid;
            ft_strcpy(current_username, user_buf);
            login_success = 1;
        } else {
            printk("\n[ERROR] Invalid username or password!\n\n");
        }
    }

    if (current_uid == 0) {
        sys_setuid(0, pass_buf); 
    } else {
        sys_setuid(current_uid, ""); 
    }

    // [CRITICAL]: If authentication is successful, start the Shell (sh)!
    // sys_get_dir_id and syscall(5) = EXEC to execute the /bin/sh.elf file
    int bin_id = syscall(29, (int)"bin", 0, 0); 
    
    // If the Shell is executable, this function will never return; the Shell takes over.
    int res = syscall(5, (int)"sh.elf", bin_id, (int)user_buf);
    
    if (res == -1) {
        printk("\n[CRITICAL ERROR] /bin/sh.elf could not be found or executed!\n");
        printk("System halted.\n");
    }
}

/**
 * @brief Initialization routine.
 * 
 * This is the very first function called when the init process starts.
 */
void _start(void) {
    main();
    sys_exit();
}