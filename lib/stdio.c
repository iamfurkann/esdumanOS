#include "stdio.h"
#include "tty.h"
#include "process.h"

extern void klog_write_char(char c);
extern void serial_write_char(char c);

mutex_t vga_mutex;
int vga_mutex_initialized = 0;
int kernel_panic_mode = 0;

// =========================================================================
// YARDIMCI: TAMPON (BUFFER) YAZICI
// =========================================================================
static void buf_putc(char *buf, uint32_t *offset, uint32_t max, char c) {
    if (*offset < max - 1) {
        buf[*offset] = c;
        (*offset)++;
    }
}

// =========================================================================
// ÇEKİRDEK: EVRENSEL SAYI BİÇİMLENDİRİCİ
// =========================================================================
static void print_number(char *buf, uint32_t *offset, uint32_t max,
                         unsigned long num, int base, int is_signed,
                         int width, int pad_zero, int uppercase, int left_justify) 
{
    char temp[64];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    int is_negative = 0;

    if (is_signed && (long)num < 0) {
        is_negative = 1;
        num = (unsigned long)(-(long)num);
    }

    if (num == 0) {
        temp[i++] = '0';
    } else {
        while (num != 0) {
            temp[i++] = digits[num % base];
            num /= base;
        }
    }

    int pad_len = width - i - is_negative;
    char pad_char = (pad_zero && !left_justify) ? '0' : ' ';

    if (pad_zero && is_negative && !left_justify) {
        buf_putc(buf, offset, max, '-');
        is_negative = 0; 
    }

    // Sağa yaslama (Normal): Sayıdan önce boşluk/sıfır bas
    if (!left_justify) {
        while (pad_len > 0) { buf_putc(buf, offset, max, pad_char); pad_len--; }
    }

    if (is_negative) buf_putc(buf, offset, max, '-');

    // Sayıyı ters çevirerek bas
    while (i > 0) {
        i--;
        buf_putc(buf, offset, max, temp[i]);
    }

    // Sola yaslama (-): Sayıdan sonra boşluk bas
    if (left_justify) {
        while (pad_len > 0) { buf_putc(buf, offset, max, ' '); pad_len--; }
    }
}

// =========================================================================
// LİBC STANDARTLARINDA KVSNPRINTF (TAM ÖZELLİKLİ)
// =========================================================================
int kvsnprintf(char *buf, uint32_t size, const char *format, va_list args) {
    uint32_t offset = 0;
    int i = 0;

    if (!buf || size == 0) return 0;

    while (format[i] && offset < size - 1) {
        if (format[i] == '%') {
            i++;
            int left_justify = 0;
            int pad_zero = 0;
            int width = 0;
            int is_long = 0, is_long_long = 0, is_size_t = 0;

            // 1. ADIM: Bayraklar (Flags) -> '-' veya '0'
            while (format[i] == '-' || format[i] == '0') {
                if (format[i] == '-') left_justify = 1;
                if (format[i] == '0') pad_zero = 1;
                i++;
            }

            // 2. ADIM: Genişlik (Width) -> '5', '10' vb.
            while (format[i] >= '0' && format[i] <= '9') {
                width = width * 10 + (format[i] - '0');
                i++;
            }

            // 3. ADIM: Uzunluk Belirteçleri -> 'l', 'll', 'z', 'h' (VARARG DESYNC KORUMASI)
            while (format[i] == 'l' || format[i] == 'h' || format[i] == 'z') {
                if (format[i] == 'l') {
                    if (is_long) is_long_long = 1;
                    else is_long = 1;
                } else if (format[i] == 'z') {
                    is_size_t = 1;
                }
                i++;
            }

            // 4. ADIM: Tip Belirleyici (Specifier)
            switch (format[i]) {
                case 'c':
                    if (!left_justify) while(width-- > 1) buf_putc(buf, &offset, size, ' ');
                    buf_putc(buf, &offset, size, (char)va_arg(args, int));
                    if (left_justify) while(width-- > 1) buf_putc(buf, &offset, size, ' ');
                    break;
                    
                case 's': {
                    char *s = va_arg(args, char *);
                    if (!s) s = "(null)";
                    int slen = 0;
                    while(s[slen]) slen++;
                    if (!left_justify) while(width-- > slen) buf_putc(buf, &offset, size, ' ');
                    while (*s) buf_putc(buf, &offset, size, *s++);
                    if (left_justify) while(width-- > slen) buf_putc(buf, &offset, size, ' ');
                    break;
                }
                
                case 'd':
                case 'i': {
                    long val;
                    if (is_long_long) {
                        // Vararg diziliminin kaymasını önlemek için 64-bit (8 byte) tüket
                        long long llval = va_arg(args, long long);
                        val = (long)llval; // x86-32bit udivdi3 hatasını önlemek için basarken 32-bit'e düşür
                    } else {
                        val = va_arg(args, int);
                    }
                    print_number(buf, &offset, size, val, 10, 1, width, pad_zero, 0, left_justify);
                    break;
                }
                    
                case 'u':
                case 'x':
                case 'X': {
                    unsigned long val;
                    if (is_long_long) {
                        unsigned long long ullval = va_arg(args, unsigned long long);
                        val = (unsigned long)ullval;
                    } else if (is_size_t) {
                        val = va_arg(args, uint32_t);
                    } else {
                        val = va_arg(args, unsigned int);
                    }
                    int base = (format[i] == 'u') ? 10 : 16;
                    int uppercase = (format[i] == 'X') ? 1 : 0;
                    print_number(buf, &offset, size, val, base, 0, width, pad_zero, uppercase, left_justify);
                    break;
                }
                    
                case 'p':
                    buf_putc(buf, &offset, size, '0');
                    buf_putc(buf, &offset, size, 'x');
                    print_number(buf, &offset, size, (unsigned long)va_arg(args, void *), 16, 0, width > 2 ? width - 2 : 0, pad_zero, 0, left_justify);
                    break;
                    
                case '%':
                    buf_putc(buf, &offset, size, '%');
                    break;
                    
                default:
                    buf_putc(buf, &offset, size, '%');
                    buf_putc(buf, &offset, size, format[i]);
                    break;
            }
        } else {
            buf_putc(buf, &offset, size, format[i]);
        }
        i++;
    }

    buf[offset] = '\0'; 
    return offset;
}

// =========================================================================
// KERNEL LOG FONKSİYONU
// =========================================================================
#define PRINTK_BUF_SIZE 256 // Stack koruması: 1024 -> 256 Byte'a indirildi!

int printk(const char *format, ...) {
    // 256 Byte'lık tampon 4KB'lık yığının (Stack) sadece %6'sını kullanır.
    // Kesme (Interrupt) anlarında stack taşmasını büyük ölçüde engeller.
    char print_buffer[PRINTK_BUF_SIZE]; 
    va_list args;

    if (!vga_mutex_initialized) {
        mutex_init(&vga_mutex);
        vga_mutex_initialized = 1;
    }

    va_start(args, format);
    int len = kvsnprintf(print_buffer, sizeof(print_buffer), format, args);
    va_end(args);

    if (!kernel_panic_mode) mutex_lock(&vga_mutex, 0);

    for (int i = 0; i < len; i++) {
        klog_write_char(print_buffer[i]); // Dmesg Kaydı (Açmak istersen kullanabilirsin)
        terminal_putchar(print_buffer[i]); 
        serial_write_char(print_buffer[i]);
    }

    if (!kernel_panic_mode) mutex_unlock(&vga_mutex);

    return len;
}