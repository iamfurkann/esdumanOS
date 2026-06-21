#include "stdio.h"
#include "tty.h"

/* YILDIZ (*) EKLENDİ! Artık kopyası değil, adresin kendisi ilerleyecek */
static int  get_format(const char c, va_list *args) 
{
    if (c == 'c') return (ft_kputchar(va_arg(*args, int)));
    else if (c == 's') return (ft_kputstr(va_arg(*args, char *)));
    else if (c == 'd' || c == 'i') return (ft_kputnbr(va_arg(*args, int)));
    else if (c == 'u') return (ft_kputnbru(va_arg(*args, unsigned int)));
    else if (c == 'x') return (ft_kputhex(va_arg(*args, unsigned int), 0));
    else if (c == 'X') return (ft_kputhex(va_arg(*args, unsigned int), 1));
    else if (c == 'p') return (ft_kputptr(va_arg(*args, void *)));
    else if (c == '%') return (ft_kputchar('%'));
    return (0);
}

int printk(const char *format, ...) {
    int     i = 0;
    int     count = 0;
    va_list args;

    va_start(args, format);
    while (format[i])
    {
        if (format[i] == '%' && format[i + 1])
        {
            /* ADRES (&) İŞARETİ EKLENDİ! */
            count += get_format(format[i + 1], &args); 
            i++;
        }
        else
            count += ft_kputchar(format[i]);
        i++;
    }
    va_end(args);
    return (count);
}