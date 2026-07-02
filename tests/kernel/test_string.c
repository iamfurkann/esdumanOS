#include "ktest.h"

extern int ft_strlen(const char *s);
extern int ft_strcmp(const char *s1, const char *s2);
extern void ft_strcpy(char *dest, const char *src);

void run_string_tests(void) {
    printk("\n--- String (libc) Testleri ---\n");
    
    KTEST_ASSERT(ft_strlen("esduman") == 7, "ft_strlen dogru uzunluk donuyor");
    KTEST_ASSERT(ft_strlen("") == 0, "ft_strlen bos stringde 0 donuyor");
    
    KTEST_ASSERT(ft_strcmp("alfa", "alfa") == 0, "ft_strcmp esit kelimeleri buluyor");
    KTEST_ASSERT(ft_strcmp("alfa", "beta") != 0, "ft_strcmp farkli kelimeleri ayiriyor");
    KTEST_ASSERT(ft_strcmp(0, "alfa") == -1, "ft_strcmp NULL pointer korumasi calisiyor");

    char buf[32];
    ft_strcpy(buf, "42KFS");
    KTEST_ASSERT(ft_strcmp(buf, "42KFS") == 0, "ft_strcpy basariyla kopyalama yapiyor");
}