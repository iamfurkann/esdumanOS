#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define RESET "\033[0m"

// Dosyanın içinde belirli bir metin (syscall kuralı) geçiyor mu diye bakar.
int check_pattern(const char *filename, const char *pattern) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1; // Dosya bulunamadı

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Yorum satırlarındaki (//) aldatmacaları atla
        char *comment = strstr(line, "//");
        char *match = strstr(line, pattern);
        
        if (match != NULL) {
            // Eğer eşleşme varsa ama yorum satırından sonraysa sayma
            if (comment != NULL && comment < match) continue;
            
            fclose(f);
            return 1; // Başarılı, kurala uyuyor
        }
    }
    fclose(f);
    return 0; // Başarısız, kuralı ihlal etti
}

void run_static_test(const char *filename, const char *pattern, const char *desc, int *fail_count) {
    int res = check_pattern(filename, pattern);
    if (res == 1) {
        printf("  %s[PASS]%s %s\n", GREEN, RESET, desc);
    } else if (res == 0) {
        printf("  %s[FAIL]%s %s (Eksik: '%s')\n", RED, RESET, desc, pattern);
        (*fail_count)++;
    } else {
        printf("  %s[HATA]%s %s dosyasi bulunamadi!\n", RED, RESET, filename);
        (*fail_count)++;
    }
}

int main() {
    int fail_count = 0;
    
    printf("\n======================================================\n");
    printf("       esdumanOS - ELF Static Analyzer (SAST)         \n");
    printf("======================================================\n");

    // ---------------------------------------------------------
    // 1. ECHO.C STATİK DAVRANIŞ TESTLERİ
    // ---------------------------------------------------------
    run_static_test("apps/bin/echo.c", "syscall(42", 
                    "echo.c -> Argumanlari okumak icin SYSCALL_GET_ARGS (42) kullaniyor", &fail_count);
                    
    run_static_test("apps/bin/echo.c", "syscall(4", 
                    "echo.c -> Ekrana metin basmak icin SYSCALL_WRITE (4) kullaniyor", &fail_count);
                    
    run_static_test("apps/bin/echo.c", "syscall(1", 
                    "echo.c -> Guvenli kapanis icin SYSCALL_EXIT (1) kullaniyor", &fail_count);

    // ---------------------------------------------------------
    // 2. CLEAR.C STATİK DAVRANIŞ TESTLERİ
    // ---------------------------------------------------------
    run_static_test("apps/bin/clear.c", "syscall(10", 
                    "clear.c -> Ekrani temizlemek icin SYSCALL_CLEAR_SCREEN (10) kullaniyor", &fail_count);
                    
    run_static_test("apps/bin/clear.c", "syscall(1", 
                    "clear.c -> Guvenli kapanis icin SYSCALL_EXIT (1) kullaniyor", &fail_count);

    printf("======================================================\n");
    if (fail_count == 0) {
        printf("SONUC: %sTUM STATIK TESTLER GECTI.%s Yeni ELF programlari standartlara uygun.\n\n", GREEN, RESET);
        return 0;
    } else {
        printf("SONUC: %s%d STATIK TEST BASARISIZ!%s ELF programlari kural ihlali yapiyor.\n\n", RED, fail_count, RESET);
        return 1;
    }
}