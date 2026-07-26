
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
    
    
    int i = 0;
    while(args_buf[i] && args_buf[i] != ' ') i++;
    if (args_buf[i] == ' ') {
        args_buf[i] = '\0';
        char *src = args_buf;
        char *dst = &args_buf[i+1];
        
        int fd_in = syscall(40, (int)src, 0, 0); // SYSCALL_OPEN (O_RDONLY)
        if (fd_in < 0) { print("cp: Source not found"); print_newline(); }
        else {
            int res = syscall(8, (int)dst, (int)"", 0); // SYSCALL_CREATE_FILE
            if (res < 0) { print("cp: Error creating destination"); print_newline(); }
            else {
                int fd_out = syscall(40, (int)dst, 1, 0); // SYSCALL_OPEN (O_WRONLY)
                if (fd_out >= 0) {
                    char buf[64];
                    int bytes_read;
                    while ((bytes_read = syscall(3, fd_in, (int)buf, 64)) > 0) {
                        syscall(4, fd_out, (int)buf, bytes_read); // SYSCALL_WRITE
                    }
                    syscall(38, fd_out, 0, 0); // SYSCALL_CLOSE
                }
            }
            syscall(38, fd_in, 0, 0); // SYSCALL_CLOSE
        }
    } else {
        print("Usage: cp <src> <dst>"); print_newline();
    }
    

    syscall(1, 0, 0, 0); // EXIT
    while(1);
}
