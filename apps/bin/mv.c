
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
    
    
    // mv gets args like "/path/src /path/dst"
    int i = 0;
    while(args_buf[i] && args_buf[i] != ' ') i++;
    if (args_buf[i] == ' ') {
        args_buf[i] = '\0';
        char *src = args_buf;
        char *dst = &args_buf[i+1];
        int res = syscall(23, (int)src, (int)dst, 0); // SYSCALL_MV_FILE
        if (res < 0) { print("mv: Error moving file"); print_newline(); }
    } else {
        print("Usage: mv <src> <dst>"); print_newline();
    }
    

    syscall(1, 0, 0, 0); // EXIT
    while(1);
}
