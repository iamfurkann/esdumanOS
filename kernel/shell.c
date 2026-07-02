#include "shell.h"
#include "stdio.h"
#include "libft.h"
#include "fs.h"
#include "pmm.h"
#include "kheap.h"
#include "tty.h"
#include "io.h"
#include "signal.h"
#include "stack.h"
#include "keyboard.h"
#include "security.h"

extern security_level_t current_sec_level;
extern void set_security_level(security_level_t level);
extern void load_and_exec_elf(const char *filename);
extern int current_layout; // Klavye düzeni (0: US, 1: TR)

uint32_t hex_to_int(const char *hex_str) {
  uint32_t val = 0;
  if (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X')) 
    hex_str += 2;
  while (*hex_str) {
    char c = *hex_str++;
    val = val * 16;
    if (c >= '0' && c <= '9') val += (c - '0');
    else if (c >= 'a' && c <= 'f') val += (c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') val += (c - 'A' + 10);
    else return 0;
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
      } else {
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

void execute_command(char *cmd) {
  while (*cmd == ' ') cmd++;

  if (ft_strcmp(cmd, "help") == 0) {
    printk("Mevcut komutlar:\n");
    printk("  help       : Bu menuyu gosterir\n");
    printk("  ls         : Disktekileri listeler\n");
    printk("  cat [isim] : Dosya icerigini okur\n");
    printk("  cat_raw [isim]   : VFS sifre cozucusunu atlar, diskin ham (HEX) dokumunu gosterir\n");
    printk("  write [isim]: Diske yeni dosya yazar\n");
    printk("  rm [isim]  : Dosyayi diskten kalici olarak siler\n");
    printk("  mv [eski] [yeni] : Dosyanin adini degistirir\n");
    printk("  clear      : Ekrani temizler\n");
    printk("  layout tr  : Klavyeyi Turkce (QWERTY) yapar\n");
    printk("  layout us  : Klavyeyi Ingilizce (QWERTY) yapar\n");
    printk("  lockdown   : Sistemi GUVENLI MODA gecirir\n");
    printk("  stack      : Kernel stack dokumunu (dump) gosterir\n");
    printk("  meminfo    : RAM bilgisi verir\n");
    printk("  testmalloc : Heap testi baslatir\n");
    printk("  hexdump    : Adresdeki verileri dokumunu gosterir\n");
    printk("  alarm      : CALLBACK testi\n");
    printk("  panic      : ISR testi\n");
    printk("  reboot     : Sistemi yeniden baslatir\n");
    printk("  halt       : islemciyi durdurur\n");
    printk("  exec [elf] : Disaridan program calistirir\n");
    printk("  kill [pid] [sig]: Belirtilen surece sinyal gonderir\n");
    printk("  --- 42 Minishell Built-in ---\n");
    printk("  echo [-n]  : Metni ekrana basar ('>' destekli)\n");
    printk("  pwd        : Gecerli dizini gosterir\n");
    printk("  env        : Cevresel degiskenleri gosterir\n");
    printk("  export     : Yeni degisken tanimlar (Orn: export DEG DEGER)\n");
  }
  else if (cmd[0] == 'w' && cmd[1] == 'r' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == 'e' && cmd[5] == ' ') {
      char *args = &cmd[6]; char filename[32]; int i = 0;
      while (*args != ' ' && *args != '\0' && i < 31) { filename[i++] = *args++; }
      filename[i] = '\0';
      if (*args == ' ') args++;

      if (fs_create_file(filename, (uint8_t *)args, ft_strlen(args), 0) == 0) {
          terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
          printk(">>> Dosya '%s' basariyla Hard Disk'e yazildi!\n", filename);
          terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
      } else printk("Hata: Dosya olusturulamadi.\n");
  }
  else if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'e' && cmd[3] == 'c' && cmd[4] == ' ') {
      if (current_sec_level >= SEC_LEVEL_LOCKDOWN) printk("ERISIM ENGELLENDI.\n");
      else load_and_exec_elf(&cmd[5]);
  }
  else if (ft_strcmp(cmd, "ls") == 0) fs_list_files();
  else if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && cmd[3] == ' ') {
    char *target_file = &cmd[4]; vfs_file_t file;
    if (fs_open(target_file, 0, &file) == 0) {
      printk("Dosya Icerigi (%s):\n", file.filename);
      uint8_t chunk[256]; uint32_t bytes_read; file.current_offset = 0;
      while ((bytes_read = fs_read(&file, chunk, 256)) > 0) {
          for (uint32_t i = 0; i < bytes_read; i++) {
              if (chunk[i] == '\r' || chunk[i] == '\b' || chunk[i] == '\0') continue; 
              char temp[2] = { (char)chunk[i], '\0' }; printk("%s", temp);
          }
      }
      printk("\n");
    } else printk("Hata: '%s' bulunamadi!\n", target_file);
  }
  else if (ft_strcmp(cmd, "lockdown") == 0) {
      if (current_sec_level >= SEC_LEVEL_LOCKDOWN) {
          printk("Sistem zaten SECURE MODE altinda calisiyor!\n");
      } else {
          set_security_level(SEC_LEVEL_LOCKDOWN);
          
          terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
          printk("\n[DIKKAT] KERNEL KILITLENDI (SECURE MODE AKTIF)!\n\n");
          terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
      }
  }
  else if (cmd[0] == 'l' && cmd[1] == 'a' && cmd[2] == 'y' && cmd[3] == 'o' && cmd[4] == 'u' && cmd[5] == 't') {
      if (cmd[7] == 't' && cmd[8] == 'r') { current_layout = 1; printk("Duzeni: TR\n"); } 
      else if (cmd[7] == 'u' && cmd[8] == 's') { current_layout = 0; printk("Duzeni: US\n"); } 
      else printk("Bilinmeyen duzen!\n");
  }
  else if (ft_strcmp(cmd, "clear") == 0) terminal_initialize();
  else if (ft_strcmp(cmd, "stack") == 0) {
    if (current_sec_level >= SEC_LEVEL_LOCKDOWN) printk("ERISIM ENGELLENDI.\n"); else print_kernel_stack();
  }
  else if (ft_strcmp(cmd, "meminfo") == 0) {
    if (current_sec_level >= SEC_LEVEL_LOCKDOWN) printk("ERISIM ENGELLENDI.\n");
    else {
        printk("RAM: Toplam %d MB | Bos %d MB\n", pmm_get_total_memory()/(1024*1024), pmm_get_free_memory()/(1024*1024));
    }
  }
  else if(ft_strcmp(cmd, "testmalloc") == 0) {
    if (current_sec_level >= SEC_LEVEL_LOCKDOWN) printk("ERISIM ENGELLENDI.\n");
    else {
        char *word = (char *)kmalloc(50);
        if (word) {
          word[0] = '4'; word[1] = '2'; word[2] = '\0';
          printk("Ayrilan: 0x%x, Boyut: %d byte\n", (uint32_t)word, kmalloc_size(word));
          kfree(word);
        }
    }
  }
  else if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'x' && cmd[3] == 'd' && cmd[4] == 'u' && cmd[5] == 'm' && cmd[6] == 'p') {
    if (current_sec_level >= SEC_LEVEL_LOCKDOWN) printk("ERISIM ENGELLENDI.\n");
    else {
        if (cmd[7] == ' ' && cmd[8] != '\0') print_hexdump(hex_to_int(&cmd[8]), 64);
        else printk("Kullanim hatasi.\n");
    }
  }
  else if (ft_strcmp(cmd, "syscall") == 0) printk("Shell komutu calistirildi!\n");
  else if (ft_strcmp(cmd, "alarm") == 0) { printk("Alarm kuruldu!\n"); schedule_signal(1, 55); }
  else if (ft_strcmp(cmd, "panic") == 0) {
      if (current_sec_level >= SEC_LEVEL_LOCKDOWN) printk("ERISIM ENGELLENDI.\n"); else asm volatile("int $0x0"); 
  }
  else if (ft_strcmp(cmd, "reboot") == 0) { printk("Reboot...\n"); outb(0x64, 0xFE); } 
  else if (ft_strcmp(cmd, "halt") == 0) { printk("Durduruldu.\n"); asm volatile("cli; hlt"); } 
  else if (cmd[0] != '\0') printk("Bilinmeyen komut: '%s'\n", cmd);
}