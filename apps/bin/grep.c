
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
        char *term = args_buf;
        char *file = &args_buf[i+1];
        
        int fd = syscall(40, (int)file, 0, 0); // SYSCALL_OPEN
        if (fd < 0) { print("grep: File not found"); print_newline(); }
        else {
            char buf[512];
            int read_bytes = syscall(3, fd, (int)buf, 511);
            if (read_bytes > 0) {
                buf[read_bytes] = '\0';
                // Very simple grep: just check lines
                int line_start = 0;
                int k = 0;
                while (k < read_bytes) {
                    if (buf[k] == '\n' || buf[k] == '\0') {
                        buf[k] = '\0';
                        // check if term is in buf+line_start
                        char *line = &buf[line_start];
                        int term_len = 0; while(term[term_len]) term_len++;
                        int line_len = 0; while(line[line_len]) line_len++;
                        int found = 0;
                        for (int m = 0; m <= line_len - term_len; m++) {
                            int match = 1;
                            for (int n = 0; n < term_len; n++) {
                                if (line[m+n] != term[n]) { match = 0; break; }
                            }
                            if (match) { found = 1; break; }
                        }
                        if (found) { print(line); print_newline(); }
                        line_start = k + 1;
                    }
                    k++;
                }
            }
            syscall(38, fd, 0, 0); // SYSCALL_CLOSE
        }
    } else {
        print("Usage: grep <term> <file>"); print_newline();
    }
    

    syscall(1, 0, 0, 0); // EXIT
    while(1);
}
