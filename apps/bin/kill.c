
int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void print(const char *str) {
    int len = 0;
    while(str[len]) len++;
    syscall(4, 1, (int)str, len);
}

void print_newline() {
    syscall(4, 1, (int)"\n", 1);
}

void main(void) {
    char args_buf[128];
    for (int k = 0; k < 128; k++) args_buf[k] = '\0';
    syscall(42, (int)args_buf, 0, 0); // SYSCALL_GET_ARGS

    // The shell will pass the canonical absolute path as the argument string.
    // E.g. for "touch a.txt", the shell passes "/current/path/a.txt"
    
    
    if (args_buf[0] == '\0') {
        print("Usage: kill <pid>"); print_newline();
    } else {
        int pid = 0;
        int i = 0;
        while(args_buf[i] >= '0' && args_buf[i] <= '9') {
            pid = pid * 10 + (args_buf[i] - '0');
            i++;
        }
        int res = syscall(25, pid, 9, 0); // SYSCALL_KILL (SIGKILL=9)
        if (res < 0) { print("kill: Failed"); print_newline(); }
        else { print("kill: Sent signal"); print_newline(); }
    }
    

    syscall(1, 0, 0, 0); // EXIT
    while(1);
}
