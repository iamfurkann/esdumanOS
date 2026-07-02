#include "syscall.h"
#include "errno.h"

typedef unsigned int uint32_t;

static inline int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" 
                 : "=a" (ret) 
                 : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) 
                 : "memory");
    return ret;
}

int ft_strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return -1;
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

void ft_strcpy(char *dest, const char *src) {
    while(*src) *dest++ = *src++;
    *dest = '\0';
}

int ft_strlen(const char *s) {
    int i = 0; while(s[i]) i++; return i;
}

void ft_itoa(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char temp[16]; int i = 0;
    while(n > 0) { temp[i++] = (n % 10) + '0'; n /= 10; }
    int j = 0;
    while(i > 0) { buf[j++] = temp[--i]; }
    buf[j] = '\0';
}

uint32_t hex_to_int(const char *hex_str) {
    uint32_t val = 0;
    if (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X')) hex_str += 2;
    while (*hex_str) {
        char c = *hex_str++; val = val * 16;
        if (c >= '0' && c <= '9') val += (c - '0');
        else if (c >= 'a' && c <= 'f') val += (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val += (c - 'A' + 10);
        else return 0;
    }
    return val;
}

uint32_t hash_djb2_salted(const char *str) {
    uint32_t hash = 5381;
    while (*str) {
        hash = ((hash << 5) + hash) + *str++;
    }
    hash = ((hash << 5) + hash) + '4';
    hash = ((hash << 5) + hash) + '2';
    return hash;
}

void print(const char *str) {
    syscall(SYSCALL_WRITE, 1, (int)str, ft_strlen(str)); 
}

void set_defcon(int level) {
    syscall(SYSCALL_SET_SEC_LEVEL, level, 0, 0);
    print("\n[!] Sistem Guvenlik Seviyesi Degistirildi!\n");
}


/* KERNEL */
void printk(const char *str) { syscall(SYSCALL_WRITE, 1, (int)str, ft_strlen(str)); }
char get_keyboard_char(void) { char c = 0; syscall(SYSCALL_READ, 0, (int)&c, 1); return c;}
int sys_create_file(const char *name, const char *content, int parent_id) { return syscall(8, (int)name, (int)content, parent_id); }
int sys_delete_file(const char *name, int parent_id) { return syscall(22, (int)name, parent_id, 0); }
int sys_cat_file(const char *name, int parent_id) { return syscall(11, (int)name, parent_id, 0); }
int sys_cat_raw_file(const char *name, int parent_id) { return syscall(34, (int)name, parent_id, 0); } // 34 = SYSCALL_CAT_RAW 
int sys_rename_file(const char *old_name, const char *new_name, int parent_id) { return syscall(23, (int)old_name, (int)new_name, parent_id); }
int sys_receive_message(uint32_t *sender, uint32_t *payload) { return syscall(SYSCALL_IPC_RECEIVE, (int)sender, (int)payload, 0); }
void sys_set_priority(int pid, int priority) { syscall(SYSCALL_SET_PRIORITY, pid, priority, 0); }
void sys_exit(void) { syscall(SYSCALL_EXIT, 0, 0, 0); while(1); }
void sys_register_signal(int sig_num, void *handler) { syscall(SYSCALL_SIGNAL_REG, sig_num, (int)handler, 0); }
void sys_kill(int pid, int sig_num) { syscall(SYSCALL_KILL, pid, sig_num, 0); }
void sys_sigreturn(void) { syscall(SYSCALL_SIGRETURN, 0, 0, 0); }
int sys_setuid(int uid, const char *password) { return syscall(SYSCALL_SETUID, uid, (int)password, 0); }
int sys_mkdir(const char *name, int parent_id) { return syscall(26, (int)name, parent_id, 0); }
void sys_ls_dir(int parent_id) { syscall(28, parent_id, 0, 0); }
int sys_get_dir_id(const char *name, int parent_id) { return syscall(29, (int)name, parent_id, 0); }
int pipe(int pipefd[2]) { return syscall(SYSCALL_PIPE, (int)pipefd, 0, 0); }
int dup2(int oldfd, int newfd) { return syscall(SYSCALL_DUP2, oldfd, newfd, 0); }
int sys_close(int fd) { return syscall(SYSCALL_CLOSE, fd, 0, 0); }
/* --- 4. ORTAM DEĞİŞKENLERİ (ENV) --- */
char env_keys[20][32];
char env_vals[20][64];
int env_count = 0;
int last_exit_status = 0; 
int current_dir_id = 0; 
char current_path[64] = "/";
int current_uid = -1;
char current_username[32];

void set_env(const char *key, const char *val) {
    for(int i = 0; i < env_count; i++) {
        if(ft_strcmp(env_keys[i], key) == 0) { ft_strcpy(env_vals[i], val); return; }
    }
    if(env_count < 20) {
        ft_strcpy(env_keys[env_count], key); ft_strcpy(env_vals[env_count], val); env_count++;
    }
}

char* get_env(const char *key) {
    for(int i = 0; i < env_count; i++) {
        if(ft_strcmp(env_keys[i], key) == 0) return env_vals[i];
    }
    return ""; 
}

void my_custom_handler(void) {
    printk("\n[!!!] MINISHELL KULLANICI SINYALI YAKALADI! [!!!]\n");
    sys_sigreturn();
}

void read_line(char *buf, int hide) {
    int idx = 0;
    while (1) {
        char c = get_keyboard_char();
        if (c == '\n' || c == '\r') { buf[idx] = '\0'; printk("\n"); break; } 
        else if (c == '\b') { if (idx > 0) { idx--; printk("\b \b"); } } 
        else if (c >= 32 && c <= 126 && idx < 254) {
            // Eğer 'hide' 1 ise ekrana '*' bas (Parola gizleme)
            char str[2] = { hide ? '*' : c, '\0' }; 
            printk(str); 
            buf[idx++] = c;
        }
    }
}

void show_help(void) {
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
    printk("  halt       : Islemciyi durdurur\n");
    printk("  exec [elf] : Disaridan program calistirir\n");
    printk("  kill [pid] [sig]: Belirtilen surece sinyal gonderir\n");
    printk("  --- 42 Minishell Built-in ---\n");
    printk("  echo [-n]  : Metni ekrana basar ('>' destekli)\n");
    printk("  pwd        : Gecerli dizini gosterir\n");
    printk("  env        : Cevresel degiskenleri gosterir\n");
    printk("  export     : Yeni degisken tanimlar (Orn: export DEG DEGER)\n");
}

void execute_command(char **args, char *redirect_file) {
    if (!args[0]) return;

    if (ft_strcmp(args[0], "echo") == 0) {
        char output_buffer[512]; output_buffer[0] = '\0';
        int i = 1, newline = 1;
        if (args[1] && ft_strcmp(args[1], "-n") == 0) { newline = 0; i++; }
        while (args[i]) {
            ft_strcpy(&output_buffer[ft_strlen(output_buffer)], args[i]);
            if (args[i+1]) ft_strcpy(&output_buffer[ft_strlen(output_buffer)], " ");
            i++;
        }
        if (newline) ft_strcpy(&output_buffer[ft_strlen(output_buffer)], "\n");

        if (redirect_file) {
            sys_create_file(redirect_file, output_buffer, current_dir_id);
            printk("[OK] Echo ciktisi diske yazildi!\n");
        } else {
            printk(output_buffer);
        }
        last_exit_status = 0;
    }
    else if (ft_strcmp(args[0], "cat") == 0) {
        if (args[1]) {
            if (sys_get_dir_id(args[1], current_dir_id) != -1) {
                printk("cat: "); printk(args[1]); printk(": Bu bir dizin (Is a directory)\n");
            } else {
                sys_cat_file(args[1], current_dir_id); 
            }
        } else {
            char c;
            printk("[BILGI] Klavye okuma modu. Cikmak icin ESC'ye basin...\n");
            while (syscall(3, 0, (int)&c, 1) > 0) {
                if (c == 27 || c == 4) { printk("\n"); break; }
                char str[2] = {c, '\0'}; printk(str); 
            }
        }
        last_exit_status = 0;
    }
    else if (ft_strcmp(args[0], "pwd") == 0) { printk(current_path); printk("\n"); last_exit_status = 0; }
    else if (ft_strcmp(args[0], "env") == 0) {
        for(int i = 0; i < env_count; i++) { printk(env_keys[i]); printk("="); printk(env_vals[i]); printk("\n"); }
        last_exit_status = 0;
    }
    else if (ft_strcmp(args[0], "export") == 0) {
        if (args[1] && args[2]) { set_env(args[1], args[2]); last_exit_status = 0; } 
        else { printk("Hata. Ornek: export DIL TR\n"); last_exit_status = 1; }
    }
    else if (ft_strcmp(args[0], "help") == 0) { show_help(); last_exit_status = 0; }
    else if (ft_strcmp(args[0], "clear") == 0) { syscall(10, 0, 0, 0); last_exit_status = 0; }
    else if (ft_strcmp(args[0], "ls") == 0) { sys_ls_dir(current_dir_id); last_exit_status = 0; }
    else if (ft_strcmp(args[0], "mkdir") == 0) {
        if (args[1]) sys_mkdir(args[1], current_dir_id); else printk("Kullanim: mkdir <dizin>\n");
    }
    else if (ft_strcmp(args[0], "cd") == 0) {
        if (!args[1] || ft_strcmp(args[1], "/") == 0) { 
            current_dir_id = 0; 
            ft_strcpy(current_path, "/"); 
        } 
        else if (ft_strcmp(args[1], "..") == 0) {
            current_dir_id = 0; 
            ft_strcpy(current_path, "/"); 
        }
        else {
            int new_id = sys_get_dir_id(args[1], current_dir_id);
            if (new_id != -1) { 
                current_dir_id = new_id; 
                if (ft_strcmp(current_path, "/") != 0) {
                    ft_strcpy(&current_path[ft_strlen(current_path)], "/");
                }
                ft_strcpy(&current_path[ft_strlen(current_path)], args[1]); 
            } 
            else { printk("cd: Boyle bir dizin yok\n"); }
        }
    }
    else if (ft_strcmp(args[0], "write") == 0) {
        if (args[1] && args[2]) {
            char *content = args[2];
            for(int i = 2; args[i] != 0; i++) { if (args[i+1] != 0) args[i][ft_strlen(args[i])] = ' '; }
            int res = sys_create_file(args[1], content, current_dir_id);
            if (res == E_OK) printk("Dosya yazildi!\n");
        } else { printk("Kullanim: write <dosya> <icerik>\n"); }
    }
    else if (ft_strcmp(args[0], "rm") == 0) {
        if (args[1]) sys_delete_file(args[1], current_dir_id); else printk("Kullanim: rm <dosya>\n");
    }
    else if (ft_strcmp(args[0], "mv") == 0) {
        if (args[1] && args[2]) sys_rename_file(args[1], args[2], current_dir_id); else printk("Kullanim: mv <eski> <yeni>\n");
    }
    else if (ft_strcmp(args[0], "layout") == 0) {
        if (args[1] && ft_strcmp(args[1], "tr") == 0) syscall(12, 1, 0, 0);
        else if (args[1] && ft_strcmp(args[1], "us") == 0) syscall(12, 0, 0, 0);
    }
    else if (ft_strcmp(args[0], "lockdown") == 0) { syscall(13, 0, 0, 0); }
    else if (ft_strcmp(args[0], "stack") == 0) { syscall(14, 0, 0, 0); }
    else if (ft_strcmp(args[0], "meminfo") == 0) { syscall(15, 0, 0, 0); }
    else if (ft_strcmp(args[0], "testmalloc") == 0) { syscall(16, 0, 0, 0); }
    else if (ft_strcmp(args[0], "hexdump") == 0) {
        if (args[1]) {
            syscall(17, hex_to_int(args[1]), 0, 0); 
        } 
        else {
            char chunk[16];
            int bytes_read;
            int total_offset = 0;
            printk("[BILGI] Klavye okuma modu. Cikmak icin ESC'ye basin...\n");

            while ((bytes_read = syscall(3, 0, (int)chunk, 16)) > 0) {
                if (chunk[0] == 27 || chunk[0] == 4) {
                    printk("\n");
                    break;
                }

                char offset_str[16];
                ft_itoa(total_offset, offset_str);
                printk(offset_str); printk("  ");

                for (int i = 0; i < 16; i++) {
                    if (i < bytes_read) {
                        static const char hex_chars[] = "0123456789ABCDEF";
                        char hex_out[3];
                        hex_out[0] = hex_chars[(chunk[i] >> 4) & 0x0F];
                        hex_out[1] = hex_chars[chunk[i] & 0x0F];
                        hex_out[2] = '\0';
                        printk(hex_out); printk(" ");
                    } else {
                        printk("   "); 
                    }
                    if (i == 7) printk(" "); 
                }

                printk(" |");
                for (int i = 0; i < bytes_read; i++) {
                    if (chunk[i] >= 32 && chunk[i] <= 126) {
                        char ascii_out[2] = { chunk[i], '\0' };
                        printk(ascii_out);
                    } else {
                        printk("."); 
                    }
                }
                printk("|\n");

                total_offset += bytes_read;
            }
        }
        last_exit_status = 0;
    }
    else if (ft_strcmp(args[0], "alarm") == 0) { syscall(18, 0, 0, 0); }
    else if (ft_strcmp(args[0], "panic") == 0) { syscall(19, 0, 0, 0); }
    else if (ft_strcmp(args[0], "reboot") == 0) { syscall(20, 0, 0, 0); }
    else if (ft_strcmp(args[0], "halt") == 0) { syscall(21, 0, 0, 0); }
    else if (ft_strcmp(args[0], "exec") == 0) { if (args[1]) syscall(5, (int)args[1], 0, 0); }
    else if (ft_strcmp(args[0], "exit") == 0) { printk("exit\n"); syscall(1, 0, 0, 0); while(1); }
    else if (ft_strcmp(args[0], "cat_raw") == 0) {
        if (args[1]) sys_cat_raw_file(args[1], current_dir_id); else printk("Kullanim: cat_raw <dosya>\n");
    }
    else if (ft_strcmp(args[0], "kill") == 0) {
        if (args[1] && args[2]) sys_kill(hex_to_int(args[1]), hex_to_int(args[2]));
    }
    else if (ft_strcmp(args[0], "su") == 0) {
        printk("Password for root: "); char su_pass[32]; read_line(su_pass, 1);
        if (sys_setuid(0, su_pass) == 0) { 
            set_env("USER", "root"); 
            current_uid = 0;
            ft_strcpy(current_username, "root");
            printk("\n[SISTEM] Yetkiler ROOT olarak yukseltildi!\n");
        }
    }
    else {
        printk("minishell: command not found: "); printk(args[0]); printk("\n");
        last_exit_status = 127;
    }
}

void main(void) {
    set_env("USER", "esduman");
    set_env("OS", "esdumanOS");
    sys_register_signal(5, my_custom_handler);
    
    // LOGIN
    char user_buf[32];
    char pass_buf[32];
    int current_uid = -1;
    char current_username[32];

    syscall(10, 0, 0, 0);
    printk("======================================\n");
    printk("       esdumanOS Login Ekranina       \n");
    printk("             Hos Geldiniz!            \n");
    printk("======================================\n\n");

    int failed_attempts = 0;
    int penalty_seconds = 1;

    while (1) {
        printk("login: ");
        read_line(user_buf, 0); 

        printk("password: ");
        read_line(pass_buf, 1); 

        if (ft_strcmp(user_buf, "root") == 0 && hash_djb2_salted(pass_buf) == 0x19E28ECF) {
            current_uid = 0;
            ft_strcpy(current_username, "root");
            break;
        } 
        else if (ft_strcmp(user_buf, "esduman") == 0 && hash_djb2_salted(pass_buf) == 0x7DD17035) {
            current_uid = 1000;
            ft_strcpy(current_username, "esduman");
            break;
        } 
        else {
            failed_attempts++;
            
            printk("\n[HATA] Yanlis kullanici adi veya sifre!\n");
            if (failed_attempts >= 3) {
                printk("[GUVENLIK] Cok fazla hatali deneme! ");
                
                char sec_str[16];
                ft_itoa(penalty_seconds, sec_str);
                printk(sec_str);
                printk(" saniye bekleniyor...\n");

                for (volatile int sec = 0; sec < penalty_seconds; sec++) {
                    for (volatile int delay = 0; delay < 50000000; delay++) {
                        asm volatile("nop"); // İşlemciyi oyalıyoruz
                    }
                }

                penalty_seconds *= 2; 
            }
            printk("\n");
        }
    }
    if (current_uid == 0) {
        sys_setuid(0, pass_buf); 
    } else {
        sys_setuid(current_uid, ""); 
    }
    
    set_env("USER", current_username);

    printk("\n[esdumanOS] Basariyla giris yapildi. Yetki (UID): ");
    char uid_str[16]; ft_itoa(current_uid, uid_str); printk(uid_str); printk("\n");

    char cmd_buf[256];
    char *args[32];

    while (1) {
        printk("\n");
        printk(current_username);
        if (current_uid == 0) printk("@minishell ");
        else printk("@minishell ");
        
        printk(current_path);
        
        if (current_uid == 0) printk(" # ");
        else printk(" $ ");
        int idx = 0;

        while (1) {
            char c = get_keyboard_char();
            if (c == '\n' || c == '\r') { cmd_buf[idx] = '\0'; printk("\n"); break; } 
            else if (c == '\b') { if (idx > 0) { idx--; printk("\b \b"); } } 
            else if (c >= 32 && c <= 126 && idx < 254) {
                char str[2] = {c, '\0'}; printk(str); cmd_buf[idx++] = c;
            }
        }
        if (idx == 0) continue;

        for (int i = 0; i < 32; i++) { args[i] = 0; }
        
        int arg_count = 0; 
        int in_word = 0; 
        char *redirect_file = 0;

        char *pipe_args[32]; 
        for (int i = 0; i < 32; i++) { pipe_args[i] = 0; }
        int has_pipe = 0;
        int pipe_arg_count = 0;

        for (int i = 0; cmd_buf[i] != '\0'; i++) {
            if (cmd_buf[i] == ' ') { cmd_buf[i] = '\0'; in_word = 0; } 
            else if (!in_word) { args[arg_count++] = &cmd_buf[i]; in_word = 1; }
        }

        for (int i = 0; i < arg_count; i++) {
            if (ft_strcmp(args[i], ">") == 0) {
                args[i] = 0; 
                if (args[i+1]) redirect_file = args[i+1]; 
                break;
            }

            else if (ft_strcmp(args[i], "|") == 0) {
                args[i] = 0;
                has_pipe = 1;
                
                for (int j = i + 1; j < arg_count; j++) {
                    pipe_args[pipe_arg_count++] = args[j];
                }
                break;
            }
        }

        /* ENV VARIABLE EXPANSION */
        for (int i = 0; args[i] != 0; i++) {
            if (args[i][0] == '$') {
                if (args[i][1] == '?') {
                    static char status_str[16]; ft_itoa(last_exit_status, status_str); args[i] = status_str;
                } else args[i] = get_env(&args[i][1]);
            }
        }

        if (args[0] == 0) continue;
         
        if (has_pipe) {
            int pfd[2];
            if (pipe(pfd) < 0) { printk("Hata: Pipe olusturulamadi!\n"); continue; }
            
            dup2(1, 10);
            dup2(0, 11);
            
            dup2(pfd[1], 1);
            execute_command(args, redirect_file);
            dup2(10, 1);

            sys_close(pfd[1]);
            
            dup2(pfd[0], 0);
            execute_command(pipe_args, 0); 
            
            dup2(11, 0);
            sys_close(pfd[0]);
            
            last_exit_status = 0;
            continue; 
        }
        execute_command(args, redirect_file);
    }
}

void _start(void) {
    main();
    sys_exit();
}