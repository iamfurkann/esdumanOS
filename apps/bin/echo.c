int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void main(void) {
    char args_buf[256];
    for (int k = 0; k < 256; k++) {
        args_buf[k] = '\0';
    }

    syscall(42, (int)args_buf, 0, 0); 
    
    int i = 0;
    while (args_buf[i] != ' ' && args_buf[i] != '\0') i++; 
    if (args_buf[i] == ' ') i++;
    
    if (args_buf[i] == '\0') {
        syscall(4, 1, (int)"\n", 1);
    } else {
        int len = 0;
        
        while (args_buf[i + len] != '\0' && (i + len) < 255) {
            len++;
        }
        syscall(4, 1, (int)&args_buf[i], len);
        syscall(4, 1, (int)"\n", 1);
    }
    
    syscall(1, 0, 0, 0);
    while(1);
}