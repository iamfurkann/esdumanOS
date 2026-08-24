/*
 * File: sys_utils.c
 * Purpose: Contains system calls and related utilities.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscalls_internal.h"
#include "types.h"
#include "fs.h"
#include "process.h"
#include "stdio.h"
#include "klog.h"
#include "devfs.h"
#include "errno.h"
#include "kernel.h"
#include "uaccess.h"
#include "paging.h"
/**
 * @brief Function print_hexdump
 */
void print_hexdump(uint32_t addr, int length) {
  uint8_t *ptr = (uint8_t *)addr;
  const char hex_chars[] = "0123456789ABCDEF";

  for (int i = 0; i < length; i += 16) {
    printk("0x%x: ", (uint32_t)(ptr + i));
    for (int j = 0; j < 16; j++) {
      if (i + j < length) {
        uint8_t byte = ptr[i + j];
        printk("%c%c ", hex_chars[byte >> 4], hex_chars[byte & 0x0F]);
      } else {
        printk("  ");
      }
      if (j == 7) printk(" ");
    }
    printk(" |");
    for (int j = 0; j < 16; j++) {
      if (i + j < length) {
        uint8_t byte = ptr[i + j];
        if (byte >= 32 && byte <= 126) printk("%c", byte);
        else printk(".");
      }
    }
    printk("|\n");
  }
}

/**
 * @brief Function vfs_resolve_path
 */
int vfs_resolve_path(const char *path, int start_dir_id, char *basename) {
    if (!path || !path[0]) return E_INVAL;
    
    int current_id = start_dir_id;
    int i = 0;
    
    if (path[0] == '/') {
        current_id = 0; 
        i++;
    }
    
    char token[MAX_FILENAME];
    for (int k = 0; k < MAX_FILENAME; k++) { token[k] = '\0'; basename[k] = '\0'; }
    int t_idx = 0;
    
    while (1) {
        if (path[i] == '/' || path[i] == '\0') {
            token[t_idx] = '\0';
            
            if (path[i] == '\0') {
                int j = 0;
                while (token[j] && j < MAX_FILENAME - 1) { basename[j] = token[j]; j++; }
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
                    int idx = fs_get_entry_idx(token, current_id);
                    if (idx == -1) {
                        klog(LOG_LEVEL_DEBUG, "VFS", "vfs_resolve_path: Directory not found");
                        return E_NOENT;
                    }
                    
                    if (dir_table[idx].file_type != 1) {
                        klog(LOG_LEVEL_DEBUG, "VFS", "vfs_resolve_path: Path component is not a directory");
                        return E_NOTDIR;
                    }
                    current_id = dir_table[idx].entry_id;
                }
            }
            t_idx = 0;
        } else {
            /*
             * A component too long to hold is refused, not shortened.
             *
             * This used to drop the excess: t_idx simply stopped advancing. With
             * MAX_FILENAME at 256 that took a name nobody would type, and with it
             * at 64 it takes an ordinary long one - and the result is not an
             * error, it is a different file. The path would resolve, the create
             * would succeed under a name the user did not ask for, and two long
             * names sharing their first 63 characters would be the same file.
             */
            if (t_idx >= MAX_FILENAME - 1) {
                klog(LOG_LEVEL_DEBUG, "VFS", "vfs_resolve_path: Path component too long");
                return E_INVAL;
            }
            token[t_idx++] = path[i];
        }
        i++;
    }
    klog(LOG_LEVEL_DEBUG, "VFS", "vfs_resolve_path: Unknown error");
    return E_NOENT;
}

/**
 * @brief Finds the directory table slot an entry id occupies.
 *
 * @param entry_id Entry to find.
 * @return Slot index, or -1.
 */
static int slot_of_entry(int entry_id) {
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && dir_table[i].entry_id == entry_id) return i;
    }
    return -1;
}

/**
 * @brief Decides whether the calling task may use a directory.
 *
 * Until v0.9.1 this compared names. "tmp" at the root was writable by anybody,
 * an entry called "root" and owned by uid 0 was closed to everybody, "shadow"
 * could not be read, and otherwise you could write what you owned. It worked,
 * and it was the oldest compromise in the system: a file's permissions were a
 * property of what it was called, so renaming a file changed who could touch it
 * and creating one called "shadow" was a way to make something unreadable.
 *
 * v0.9.0 put a mode, an owner and a group on every entry. This reads them.
 *
 * The rule is the Unix one. Reaching an entry requires search permission on
 * every directory above it, and then the operation itself requires read or
 * write on the directory it happens in. fs_mode_allows() picks the class -
 * owner, group, other - and the first class that matches decides, so an owner
 * with no permission is refused rather than falling through to the group bits.
 *
 * One deliberate difference from Unix: the read case asks for r *and* x rather
 * than x alone. A directory that is searchable but not readable - 0711 - lets a
 * caller open a file whose name it already knows while refusing to list the
 * directory, and this function is not told which of the two its caller is about
 * to do. Asking for both is stricter than Unix, never looser, and no mode this
 * system sets for itself distinguishes them.
 *
 * The bounded walk survives from the old implementation and for the old reason.
 * fs_dir_exists() stops user space creating an entry whose parent does not
 * exist, but the directory table is read off the disk and a crafted image can
 * still contain a cycle. A chain longer than the table cannot be acyclic.
 *
 * @param entry_id Directory the operation happens in; 0 is the root.
 * @param needs_write Non-zero when the operation will modify the directory.
 * @return 1 when the task may proceed.
 */
int check_vfs_access(int entry_id, int needs_write) {
    if (current_task == 0) return 1;

    uint32_t uid = current_task->uid;
    uint32_t gid = current_task->gid;

    /* Root is not subject to the bits, as everywhere else. */
    if (uid == 0) return 1;

    int want = needs_write ? (FS_WANT_WRITE | FS_WANT_EXEC)
                           : (FS_WANT_READ | FS_WANT_EXEC);

    /*
     * The root directory owns no table entry, so it has no mode to read and its
     * permissions are stated here instead: 0755 owned by root, which is what
     * every Unix ships. A user who cannot write to / has /home and /tmp, which
     * is the arrangement this is copying rather than a restriction invented now.
     */
    if (entry_id == 0) return fs_mode_allows(0755, 0, 0, uid, gid, want);

    int idx = slot_of_entry(entry_id);
    if (idx < 0) return 0;

    if (!fs_mode_allows(dir_table[idx].mode, dir_table[idx].owner_uid,
                        dir_table[idx].owner_gid, uid, gid, want)) {
        return 0;
    }

    /* And every directory on the way down to it has to have been searchable. */
    int curr = dir_table[idx].parent_id;
    int hops = 0;

    while (curr != 0) {
        if (++hops > MAX_FILES_IN_DIR) {
            klog_int(LOG_LEVEL_ERROR, "VFS", "Cycle in the directory tree; denying access to entry", entry_id);
            return 0;
        }

        int up = slot_of_entry(curr);
        if (up < 0) break;

        if (!fs_mode_allows(dir_table[up].mode, dir_table[up].owner_uid,
                            dir_table[up].owner_gid, uid, gid, FS_WANT_EXEC)) {
            return 0;
        }
        curr = dir_table[up].parent_id;
    }

    return 1;
}

/**
 * @brief Function validate_user_pointer
 */
static int validate_user_pointer_access(const void *ptr, size_t size, int requires_write) {
    uint32_t start_addr = (uint32_t)ptr;
    uint32_t end_addr = start_addr + size;

    if (end_addr < start_addr) {
        return 0;
    }

    if (size == 0) return 1;

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
        /* Must be Present and User-accessible. No test-mode exemption: the
         * self-test suite now drives these paths from a real Ring 3 process,
         * so relaxing the check here would only hide boundary regressions. */
        if ((pt_virt[pt_index] & 0x05) != 0x05) {
            return 0;
        }
        /*
         * A page that is read-only *because it is shared* counts as writable
         * here. The write that follows faults, cow_handle_fault() hands the
         * caller a private copy, and the instruction retries - the pointer was
         * always valid and the copy always goes through.
         *
         * Rejecting it instead would break every syscall that writes into a user
         * buffer for as long as a page stayed shared, which after a fork is every
         * writable page either process owns: wait(), getcwd() and
         * receive_message() would refuse valid pointers with E_FAULT, and the
         * shell would break on the first command it ran. No kernel-side test
         * could have caught that - they reach the copy helpers directly, never
         * through this validator.
         */
        if (requires_write &&
            !(pt_virt[pt_index] & 0x02) &&
            !(pt_virt[pt_index] & PAGE_COW)) {
            return 0;
        }
    }

    return 1;
}

int validate_user_pointer(const void *ptr, size_t size) {
    return validate_user_pointer_access(ptr, size, 0);
}

int validate_user_writable_pointer(const void *ptr, size_t size) {
    return validate_user_pointer_access(ptr, size, 1);
}

/**
 * @brief Function validate_string_pointer
 */
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
            /* Present + User, with no test-mode exemption (see above). */
            if ((pt_virt[pt_index] & 0x05) != 0x05) {
                return 0;
            }
        }
        char ch;
        if (copy_from_user(&ch, (const void *)curr_addr, 1) != E_OK) {
            return 0;
        }
        if (ch == '\0') {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Function validate_fd
 */
int validate_fd(int fd) {
    if (current_task == 0 || fd < 0 || (uint32_t)fd >= current_task->fd_table_size) {
        return 0;
    }
    return 1;
}

/**
 * @brief Function sys_time
 *
 * Fills an esd_time_t with the current wall-clock time. Until now the clock was
 * readable from Ring 0 only - it drew the status bar and nothing else - so
 * date(1) printed a fixed string it carried in its own binary, the same one on
 * every boot.
 *
 * The fields are handed over broken down rather than as a count of seconds since
 * an epoch. The RTC reports them that way, nothing in this system agrees on an
 * epoch to count from, and both consumers in sight - date(1) and the timestamps
 * /var/log will want - need them broken down anyway.
 *
 * @param regs ebx is an esd_time_t* in user memory; a non-zero ecx asks for UTC
 *             rather than local time. On return eax is E_OK, or a negative errno.
 */
void sys_time(arch_regs_t *regs) {
    esd_time_t *user_out = (esd_time_t *)regs->ebx;
    int want_utc = (int)regs->ecx;

    if (!validate_user_writable_pointer(user_out, sizeof(esd_time_t))) {
        regs->eax = E_FAULT;
        return;
    }

    /*
     * Staged on the kernel stack and copied over in one go. esd_time_t is laid
     * out with no padding holes precisely so this cannot hand user space a byte
     * of whatever the stack was holding - see the comment on the struct.
     */
    esd_time_t now;

    /*
     * The choice is made here rather than left to the caller. Shifting a time
     * between zones means the calendar carry, and that lives in the kernel with
     * the leap-year rule beside it; a user program doing its own subtraction
     * would be a second copy of the arithmetic this release exists to get right.
     */
    if (want_utc) rtc_read_utc(&now);
    else rtc_read_local(&now);

    regs->eax = (copy_to_user(user_out, &now, sizeof(now)) == E_OK) ? E_OK : E_FAULT;
}

/**
 * @brief Function hash_djb2_salted
 */
uint32_t hash_djb2_salted(const char *str) {
    uint32_t hash = 5381;
    while (*str) {
        hash = ((hash << 5) + hash) + *str++;
    }
    hash = ((hash << 5) + hash) + '4';
    hash = ((hash << 5) + hash) + '2';
    return hash;
}
