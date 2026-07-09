// apps/hello.c
int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

// [DÜZELTME]: _start yerine main yapıldı!
void main(void) {
    char *msg = "Merhaba! Ben /bin altindan calisan bagimsiz bir ELF programiyim!\n";
    
    int len = 0;
    while(msg[len]) len++;
    
    // Ekrana yaz (SYSCALL_WRITE)
    syscall(4, 1, (int)msg, len);
    
    // Programi guvenlice kapat (SYSCALL_EXIT)
    syscall(1, 0, 0, 0);
    
    while(1);
}