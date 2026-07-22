#include "syscalls_internal.h"
#include "types.h"
#include "fs.h"
#include "process.h"
#include "stdio.h"
#include "klog.h"
#include "devfs.h"

extern process_t tasks[];
extern disk_file_entry_t dir_table[];

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

int vfs_resolve_path(const char *path, int start_dir_id, char *basename) {
    if (!path || !path[0]) return -1;
    
    int current_id = start_dir_id;
    int i = 0;
    
    if (path[0] == '/') {
        current_id = 0; 
        i++;
    }
    
    char token[64];
    for (int k = 0; k < 64; k++) { token[k] = '\0'; basename[k] = '\0'; }
    int t_idx = 0;
    
    while (1) {
        if (path[i] == '/' || path[i] == '\0') {
            token[t_idx] = '\0';
            
            if (path[i] == '\0') {
                int j = 0;
                while (token[j] && j < 63) { basename[j] = token[j]; j++; }
                basename[j] = '\0';
                return current_id;
            }
            
            if (t_idx > 0) {
                if (token[0] == '.' && token[1] == '\0') {
                } 
                else if (token[0] == '.' && token[1] == '.' && token[2] == '\0') {
                    if (current_id != 0) {
                        for (int k = 0; k < MAX_FILES_IN_DIR; k++) {
                            if (dir_table[k].entry_id == current_id && dir_table[k].file_type == 1 && dir_table[k].is_used == 1) {
                                current_id = dir_table[k].parent_id;
                                break;
                            }
                        }
                    }
                }
                else {
                    extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
                    int idx = fs_get_entry_idx(token, current_id);
                    if (idx == -1) return -1; // Klasör yok!
                    
                    if (dir_table[idx].file_type != 1) return -1;
                    current_id = dir_table[idx].entry_id;
                }
            }
            t_idx = 0;
        } else {
            if (t_idx < 63) token[t_idx++] = path[i];
        }
        i++;
    }
    return -1;
}

int check_vfs_access(int entry_id, int needs_write) {
    if (current_task < 0) return 1; 
    uint32_t my_uid = tasks[current_task].uid;
    if (my_uid == 0) return 1;

    int curr = entry_id;
    int is_in_tmp = 0;

    while (curr != 0) {
        int found = 0;
        for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
            if (dir_table[i].entry_id == curr && dir_table[i].is_used) {
                char *name = dir_table[i].filename;
                
                if (dir_table[i].parent_id == 0 && name[0] == 't' && name[1] == 'm' && name[2] == 'p' && name[3] == '\0') {
                    is_in_tmp = 1;
                }
                
                if (dir_table[i].owner_uid == 0 && name[0]=='r' && name[1]=='o' && name[2]=='o' && name[3]=='t' && name[4]=='\0') {
                    return 0;
                }
                
                curr = dir_table[i].parent_id;
                found = 1;
                break;
            }
        }
        if (!found) break;
    }
    if (is_in_tmp) return 1;

    if (!needs_write) {
        for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
            if (dir_table[i].entry_id == entry_id && dir_table[i].is_used) {
                char *name = dir_table[i].filename;
                if (dir_table[i].owner_uid == 0 && name[0]=='s' && name[1]=='h' && name[2]=='a' && 
                    name[3]=='d' && name[4]=='o' && name[5]=='w' && name[6]=='\0') {
                    return 0;
                }
                break;
            }
        }
    }

    if (needs_write) {
        for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
            if (dir_table[i].entry_id == entry_id && dir_table[i].is_used) {
                if (dir_table[i].owner_uid != my_uid) return 0;
                break;
            }
        }
    }
    
    return 1;
}

int validate_user_pointer(const void *ptr, size_t size) {
    uint32_t start_addr = (uint32_t)ptr;
    uint32_t end_addr = start_addr + size;

    if (end_addr < start_addr) {
        return 0;
    }

    if (start_addr < 0x400000 || end_addr > 0xC0000000) {
        return 0;
    }

    uint32_t *pd_virt = (uint32_t *)0xFFFFF000;
    for (uint32_t page = (start_addr & 0xFFFFF000); page < end_addr; page += 4096) {
        uint32_t pd_index = page >> 22;
        uint32_t pt_index = (page >> 12) & 0x3FF;

        if (!(pd_virt[pd_index] & 1)) {
            return 0;
        }

        uint32_t *pt_virt = (uint32_t *)(0xFFC00000 + (pd_index * 0x1000));
        
        extern int is_test_mode;
        if ((pt_virt[pt_index] & 0x05) != 0x05) {
            if (!(is_test_mode && (pt_virt[pt_index] & 0x01))) {
                return 0;
            }
        }
    }

    return 1;
}

int validate_string_pointer(const char *str, size_t max_len) {
    if (!str) return 0;
    uint32_t curr_addr = (uint32_t)str;
    
    for (size_t i = 0; i < max_len; i++, curr_addr++) {
        if (curr_addr < 0x400000 || curr_addr >= 0xC0000000) return 0;
        
        if ((curr_addr & 0xFFF) == 0 || i == 0) {
            uint32_t pd_index = curr_addr >> 22;
            uint32_t pt_index = (curr_addr >> 12) & 0x3FF;
            uint32_t *pd_virt = (uint32_t *)0xFFFFF000;
            
            if (!(pd_virt[pd_index] & 1)) return 0;
            
            uint32_t *pt_virt = (uint32_t *)(0xFFC00000 + (pd_index * 0x1000));
            extern int is_test_mode;
            if ((pt_virt[pt_index] & 0x05) != 0x05) {
                if (!(is_test_mode && (pt_virt[pt_index] & 0x01))) {
                    return 0; 
                }
            }
        }
        if (*((char *)curr_addr) == '\0') {
            return 1;
        }
    }
    return 0;
}

int validate_fd(int fd) {
    if (fd < 0 || fd >= MAX_FD_PER_TASK) {
        return 0;
    }
    return 1;
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