#include "keyboard.h"
#include "io.h"
#include "stdio.h"
#include "tty.h"
#include "libft.h"
#include "stack.h"
#include "rtc.h"
#include "pmm.h"
#include "kheap.h"
#include "signal.h"
#include "fs.h"

//extern section
extern void load_and_exec_elf(const char *filename);

volatile char kbd_buffer_char = 0; 

char get_keyboard_char(void) {
  char temp = kbd_buffer_char;
  kbd_buffer_char = 0;
  return temp;
}

int current_layout = 0; 

const char kbd_US[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', /* 0x00 - 0x0E */
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',     /* 0x0F - 0x1C */
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',          /* 0x1D - 0x29 */
    0,  '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',  0,            /* 0x2A - 0x36 */
    '*', 0, ' ',  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,          /* 0x37 - 0x46 (BOŞLUK BURADA!) */
    0,  0,  0, '-',  0,  0,  0, '+',  0,  0,  0,  0,  0,  0,  0, '<'          /* 0x47 - 0x56 */
};

const char kbd_US_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', /* 0x00 - 0x0E */
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',     /* 0x0F - 0x1C */
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',           /* 0x1D - 0x29 */
    0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',  0,            /* 0x2A - 0x36 */
    '*', 0, ' ',  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,          /* 0x37 - 0x46 */
    0,  0,  0, '-',  0,  0,  0, '+',  0,  0,  0,  0,  0,  0,  0, '>'          /* 0x47 - 0x56 */
};


/* --- TÜRKÇE (MAC QWERTY) DİZİLİMİ --- */
const char kbd_TR[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '*', '-', '\b', /* 0x00 - 0x0E */
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 'g', 'u', '\n',     /* 0x0F - 0x1C */
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 's', 'i', '"',           /* 0x1D - 0x29 */
    0,  ',', 'z', 'x', 'c', 'v', 'b', 'n', 'm', 'o', 'c', '.',  0,            /* 0x2A - 0x36 */
    '*', 0, ' ',  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,          /* 0x37 - 0x46 */
    0,  0,  0, '-',  0,  0,  0, '+',  0,  0,  0,  0,  0,  0,  0, '<'          /* 0x47 - 0x56 */
};

const char kbd_TR_shift[128] = {
    0,  27, '!', '\'', '^', '+', '%', '&', '/', '(', ')', '=', '?', '_', '\b',/* 0x00 - 0x0E */
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 'G', 'U', '\n',     /* 0x0F - 0x1C */
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'S', 'I', 'e',           /* 0x1D - 0x29 */
    0,  ';', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', 'O', 'C', ':',  0,            /* 0x2A - 0x36 */
    '*', 0, ' ',  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,          /* 0x37 - 0x46 */
    0,  0,  0, '-',  0,  0,  0, '+',  0,  0,  0,  0,  0,  0,  0, '>'          /* 0x47 - 0x56 */
};

/* C ve Terminal kodlaması için zorunlu AltGr (Mac Sağ Option) karakterleri */
const char kbd_TR_altgr[128] = {
    0,  27,  0,   0,  '#', '$',  0,   0,  '{', '[', ']', '}', '\\', '|', '\b',/* 0x00 - 0x0E */
  '\t', '@',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  '~', '\n',     /* 0x0F - 0x1C */
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  '<',           /* 0x1D - 0x29 */
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,            /* 0x2A - 0x36 */
    '*', 0, ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0,/* 0x37 - 0x46 */
    0,   0,   0, '-',  0,   0,   0, '+',  0,   0,   0,   0,   0,   0,   0, '|' /* 0x47 - 0x56 */
};
/* --- 3. SHELL VE YARDIMCI FONKSİYONLAR --- */
uint32_t hex_to_int(const char *hex_str) {
  uint32_t val = 0;
  if (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X')) 
    hex_str += 2;
  while (*hex_str) {
    char c = *hex_str++;
    val = val * 16;
    if (c >= '0' && c <= '9')
      val += (c - '0');
    else if (c >= 'a' && c <= 'f')
      val += (c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      val += (c - 'A' + 10);
    else
      return 0;
  }
  return val;
}

void print_hexdump(uint32_t addr, int lenght) {
  uint8_t *ptr = (uint8_t *)addr;
  const char hex_chars[] = "0123456789ABCDEF";

  for (int i = 0; i < lenght; i += 16) {
    printk("0x%x: ", (uint32_t)(ptr + i));
    for (int j = 0; j < 16; j++) {
      if (i + j < lenght) {
        uint8_t byte = ptr[i + j];
        printk("%c%c ", hex_chars[byte >> 4], hex_chars[byte & 0x0F]);
      }
      else {
        printk("  ");
      }
      if (j == 7) printk(" ");
    }
    printk(" |");
    for (int j = 0; j < 16; j++) {
      if (i + j < lenght) {
        uint8_t byte = ptr[i + j];
        if (byte >= 32 && byte <= 126) printk("%c", byte);
        else printk(".");
      }
    }
    printk("|\n");
  }
}

static int secure_mode = 0;

void execute_command(char *cmd) {
  while (*cmd == ' ') cmd++;

  if (ft_strcmp(cmd, "help") == 0) {
    printk("Mevcut komutlar:\n");
    printk("  help       : Bu menuyu gosterir\n");
    printk("  ls         : Disktekileri listeler\n");
    printk("  cat [isim] : Dosya icerigini okur\n");
    printk("  write [isim]: Diske yeni dosya yazar\n");
    printk("  clear      : Ekrani temizler\n");
    printk("  layout tr  : Klavyeyi Turkce (QWERTY) yapar\n");
    printk("  layout us  : Klavyeyi Ingilizce (QWERTY) yapar\n");
    printk("  lockdown   : Sistemi GUVENLI MODA gecirir (Geri donusu yoktur!)\n");
    printk("  stack      : Kernel stack dokumunu (dump) gosterir\n");
    printk("  meminfo    : RAM bilgisi verir\n");
    printk("  testmalloc : Heap testi baslatir\n");
    printk("  hexdump    : Adresdeki verileri dokumunu (dump) gosterir\n");
    printk("  syscall    : SYSCALL testi\n");
    printk("  alarm      : CALLBACK testi\n");
    printk("  panic      : ISR testi\n");
    printk("  reboot     : Sistemi yeniden baslatir\n");
    printk("  halt       : islemciyi durdurur (Sistemi kilitler)\n");
  }
  else if (cmd[0] == 'w' && cmd[1] == 'r' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == 'e' && cmd[5] == ' ') {
      char *args = &cmd[6];
      char filename[32];
      int i = 0;
      
      while (*args != ' ' && *args != '\0' && i < 31) {
          filename[i++] = *args++;
      }
      filename[i] = '\0';
      
      if (*args == ' ') args++;

      if (fs_create_file(filename, args) == 0) {
          terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
          printk(">>> Dosya '%s' basariyla Hard Disk'e yazildi!\n", filename);
          terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
      } else {
          printk("Hata: Dosya olusturulamadi (Disk dolu olabilir).\n");
      }
  }
  else if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'e' && cmd[3] == 'c' && cmd[4] == ' ') {
      if (secure_mode) {
          printk("ERISIM ENGELLENDI: Sistem Secure Mode'da. Disaridan program calistirilamaz.\n");
      } else {
          char *target_file = &cmd[5];
          load_and_exec_elf(target_file);
      }
  }
  else if (ft_strcmp(cmd, "ls") == 0)
    fs_list_files();
  else if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && cmd[3] == ' ') {
    char *target_file = &cmd[4];
    vfs_file_t file;

    if (fs_open(target_file, &file) == 0) {
      printk("Dosya Icerigi (%s):\n", file.filename);
      
      uint8_t chunk[256];
      uint32_t bytes_read;
      file.current_offset = 0;

      while ((bytes_read = fs_read(&file, chunk, 256)) > 0) {
          for (uint32_t i = 0; i < bytes_read; i++) {
              uint8_t c = chunk[i];
              
              if (c == '\r' || c == '\b' || c == '\0') {
                  continue; 
              }
              char temp[2] = { (char)c, '\0' };
              printk("%s", temp);
          }
      }
      printk("\n");
    }
    else {
      printk("Hata: '%s' adinda bir dosya bulunamadi!\n", target_file);
    }
  }
  else if (ft_strcmp(cmd, "lockdown") == 0) {
      if (secure_mode) {
          printk("Sistem zaten SECURE MODE altinda calisiyor!\n");
      } else {
          secure_mode = 1;
          terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
          printk("\n[DIKKAT] KERNEL KILITLENDI (SECURE MODE AKTIF)!\n");
          printk("Bellek okuma, yazma ve debug islemleri tamamen yasaklandi.\n\n");
          terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
      }
  }
  else if (cmd[0] == 'l' && cmd[1] == 'a' && cmd[2] == 'y' && cmd[3] == 'o' && cmd[4] == 'u' && cmd[5] == 't') {
      if (cmd[7] == 't' && cmd[8] == 'r') {
          current_layout = 1;
          printk("Klavye Duzeni: Turkce (TR)\n");
      } else if (cmd[7] == 'u' && cmd[8] == 's') {
          current_layout = 0;
          printk("Klavye Duzeni: Ingilizce (US)\n");
      } else {
          printk("Bilinmeyen duzen! (Kullanim: layout tr veya layout us)\n");
      }
  }
  else if (ft_strcmp(cmd, "clear") == 0) {
    terminal_initialize();
  } 
  else if (ft_strcmp(cmd, "stack") == 0) {
    if (secure_mode) printk("ERISIM ENGELLENDI: Sistem Secure Mode'da.\n");
    else print_kernel_stack();
  }
  else if (ft_strcmp(cmd, "meminfo") == 0) {
    if (secure_mode) printk("ERISIM ENGELLENDI: Sistem Secure Mode'da.\n");
    else {
        uint32_t total = pmm_get_total_memory() / (1024 * 1024);
        uint32_t free = pmm_get_free_memory() / (1024 * 1024);
        printk("Fiziksel Bellek (RAM) Bilgisi:\n");
        printk("Toplam: %d MB\n", total);
        printk("Bos:    %d MB\n", free);
    }
  }
  else if(ft_strcmp(cmd, "testmalloc") == 0) {
    if (secure_mode) printk("ERISIM ENGELLENDI: Sistem Secure Mode'da.\n");
    else {
        printk("Dinamik Bellek (Heap) Testi Basliyor...\n");
        char *word = (char *)kmalloc(50);
        if (word) {
          word[0] = '4'; word[1] = '2'; word[2] = 'K'; word[3] = 'F'; word[4] = 'S'; word[5] = '\0';
          printk("Ayrilan Adres: 0x%x\n", (uint32_t)word);
          printk("Icine Yazilan: %s\n", word);
          printk("Kapladigi Alan: %d byte\n", kmalloc_size(word));
          kfree(word);
          printk("Bellek basariyla serbest birakildi (kfree).\n");
        }
    }
  }
  else if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'x' && cmd[3] == 'd' && cmd[4] == 'u' && cmd[5] == 'm' && cmd[6] == 'p') {
    if (secure_mode) {
        printk("ERISIM ENGELLENDI: Sistem Secure Mode'da.\n");
    } else {
        if (cmd[7] == ' ' && cmd[8] != '\0') {
          uint32_t target_addr = hex_to_int(&cmd[8]);
          printk("Adres Dokumu: 0x%x\n", target_addr);
          print_hexdump(target_addr, 64);
        }
        else printk("Kullanim hatasi. Ornek: hexdump 0x200000\n");
    }
  }
  else if (ft_strcmp(cmd, "syscall") == 0) {
      printk("Shell komutu Kernel uzerinden basariyla calistirildi!\n");
  }
  else if (ft_strcmp(cmd, "alarm") == 0) {
      printk("Alarm kuruldu! Sen yazi yazmaya devam et, 3 saniye sonra calacak...\n");
      schedule_signal(1, 55); 
  }
  else if (ft_strcmp(cmd, "panic") == 0) {
      if (secure_mode) printk("ERISIM ENGELLENDI: Sistem Secure Mode'da.\n");
      else asm volatile("int $0x0"); 
  }
  else if (ft_strcmp(cmd, "reboot") == 0) {
    printk("Sistem yeniden baslatiliyor...\n");
    outb(0x64, 0xFE); 
  } 
  else if (ft_strcmp(cmd, "halt") == 0) {
    printk("Sistem durduruldu. Cikmak icin QEMU'yu kapatin.\n");
    asm volatile("cli; hlt"); 
  } 
  else if (cmd[0] != '\0') {
    printk("Bilinmeyen komut: '%s'. Komutlari gormek icin 'help' yazin.\n", cmd);
  }
}
/* --- 4. KLAVYE KESMESİ (IRQ1 HANDLER) --- */
static int shift_pressed = 0;
static int caps_lock = 0;
static int altgr_pressed = 0;
static int e0_mode = 0;

void keyboard_interrupt_handler(void) {
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
      e0_mode = 1;
      return;
    }

    if (e0_mode) {
      e0_mode = 0;
      if (scancode == 0x38) {
        altgr_pressed = 1;
        return; 
      }
      else if (scancode == (0x38 | 0x80)) {
        altgr_pressed = 0; 
        return;
      }
    }

    if (scancode & 0x80) {
        uint8_t released_key = scancode & 0x7F;
        if (released_key == 0x2A || released_key == 0x36) shift_pressed = 0;
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; return; }
    if (scancode == 0x3A) { caps_lock = !caps_lock; return; }
    
    // F tuşları ve Scrollback
    if (scancode == 0x3B) { terminal_switch(0); return; }
    if (scancode == 0x3C) { terminal_switch(1); return; }
    if (scancode == 0x3D) { terminal_switch(2); return; }
    if (scancode == 0x48) { terminal_scroll_up(); return; }
    if (scancode == 0x50) { terminal_scroll_down(); return; }

    char c = 0;
    if (current_layout == 1) {
        if (altgr_pressed) {
            c = kbd_TR_altgr[scancode];
        } else if (shift_pressed) {
            c = kbd_TR_shift[scancode];
        } else {
            c = kbd_TR[scancode];
        }
    } else {
        if (shift_pressed) {
            c = kbd_US_shift[scancode];
        } else {
            c = kbd_US[scancode];
        }
    }
    if (caps_lock) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32;
    }

    if (c != 0) {
        kbd_buffer_char = c;
    }
}