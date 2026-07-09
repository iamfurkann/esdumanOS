int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void main(void) {
    syscall(10, 0, 0, 0); 
    syscall(1, 0, 0, 0); 
    while(1);
}