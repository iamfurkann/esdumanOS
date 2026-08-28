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
int format_hexdump(char *buf, uint32_t cap, uint32_t addr, int length) {
  uint8_t *ptr = (uint8_t *)addr;
  const char hex_chars[] = "0123456789ABCDEF";
  int n = 0;

  for (int i = 0; i < length; i += 16) {
    n = kbprintf(buf, cap, (uint32_t)n, "0x%x: ", (uint32_t)(ptr + i));
    for (int j = 0; j < 16; j++) {
      if (i + j < length) {
        uint8_t byte = ptr[i + j];
        n = kbprintf(buf, cap, (uint32_t)n, "%c%c ", hex_chars[byte >> 4], hex_chars[byte & 0x0F]);
      } else {
        n = kbprintf(buf, cap, (uint32_t)n, "   ");
      }
      if (j == 7) n = kbprintf(buf, cap, (uint32_t)n, " ");
    }
    n = kbprintf(buf, cap, (uint32_t)n, " |");
    for (int j = 0; j < 16; j++) {
      if (i + j < length) {
        uint8_t byte = ptr[i + j];
        if (byte >= 32 && byte <= 126) n = kbprintf(buf, cap, (uint32_t)n, "%c", byte);
        else n = kbprintf(buf, cap, (uint32_t)n, ".");
      }
    }
    n = kbprintf(buf, cap, (uint32_t)n, "|\n");
  }

  return n;
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
                        for (int k = 0; k < fs_max_entries; k++) {
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
    for (int i = 0; i < fs_max_entries; i++) {
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
        if (++hops > fs_max_entries) {
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
 * @brief Decides whether the calling task may use an entry's own permission bits.
 *
 * check_vfs_access() asks about the directory an operation happens in. Nothing
 * asked about the file, ever, which meant a mode on a file was recorded and
 * reported and stood between nobody and anything: `/etc/shadow` carried 0600
 * inside an `/etc` that carried 0755, and any user on the system could read it.
 *
 * Until v0.9.1 a name comparison refused reads of "shadow" specifically. v0.9.1
 * retired the comparison - correctly, a file's permissions should not be a
 * property of what it is called - and replaced it with a check that never looked
 * at the file. This is the half that was missing, and until it existed the
 * retirement was a loosening for that one file rather than a tightening.
 *
 * Asked *in addition to* check_vfs_access(), never instead of it. Reaching an
 * entry still requires getting through every directory above it; this is the
 * second question, about the entry itself.
 *
 * A name that does not exist answers 1. The caller's own lookup is about to
 * report E_NOENT with better information than this function has, and inventing
 * an errno here would mean two places deciding what a missing file is.
 *
 * @param parent_id Directory the entry lives in.
 * @param name Entry name, already in kernel memory.
 * @param want FS_WANT_READ, FS_WANT_WRITE, FS_WANT_EXEC, or a combination.
 * @return 1 when the task may proceed.
 */
int check_file_access(fs_id_t parent_id, const char *name, int want) {
    if (current_task == 0) return 1;

    /* Root is not subject to the bits, as everywhere else. */
    if (current_task->uid == 0) return 1;

    int idx = fs_get_entry_idx(name, parent_id);
    if (idx < 0) return 1;

    return fs_mode_allows(dir_table[idx].mode, dir_table[idx].owner_uid,
                          dir_table[idx].owner_gid,
                          current_task->uid, current_task->gid, want);
}

/**
 * @brief Decides whether the calling task may remove or rename a named entry.
 *
 * Removal is a write to the *directory*, which check_vfs_access() has already
 * granted by the time this is asked. The sticky bit is the second rule, and it
 * exists because those two questions are otherwise the same one: `/tmp` has to
 * be writable by everybody, and that alone would let anybody delete anybody
 * else's file in it.
 *
 * The entry's own mode is deliberately not consulted. Removing a file has never
 * required permission on the file in Unix - a read-only file in a directory you
 * can write is yours to delete - and copying that is what keeps `rm` on your own
 * 0444 file working.
 *
 * @param parent_id Directory the entry lives in.
 * @param name Entry name, already in kernel memory.
 * @return 1 when the removal may proceed.
 */
int check_removal_access(fs_id_t parent_id, const char *name) {
    if (current_task == 0) return 1;
    if (current_task->uid == 0) return 1;

    int entry_idx = fs_get_entry_idx(name, parent_id);
    if (entry_idx < 0) return 1;

    /*
     * The root directory owns no table entry, so its mode is stated rather than
     * read - 0755 owned by root, as in check_vfs_access(). It is not sticky, so
     * this resolves to "allowed" and the directory's write bit, which the caller
     * already checked, is what actually decided.
     */
    uint16_t dir_mode = 0755;
    uint32_t dir_owner = 0;

    if (parent_id != 0) {
        int dir_idx = slot_of_entry(parent_id);
        if (dir_idx < 0) return 0;
        dir_mode  = dir_table[dir_idx].mode;
        dir_owner = dir_table[dir_idx].owner_uid;
    }

    return fs_sticky_allows_removal(dir_mode, dir_owner,
                                    dir_table[entry_idx].owner_uid,
                                    current_task->uid);
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
 * @brief Sets the wall clock.
 *
 * The clock could be read and never written until v0.9.2, so `date` reported
 * whatever the machine had come up with and nothing could correct it. That
 * mattered more once files started carrying timestamps: a wrong clock does not
 * announce itself, it just stamps every file written from then on.
 *
 * The caller hands over the time in the shape it reads one - fields plus the
 * offset they are adjusted by - and the offset is taken back out here. The
 * conversion is esdtime.h's, so the calendar carry and the leap-year rule are
 * the same ones every other part of this system uses rather than a second copy
 * that gets to disagree.
 *
 * Root only. A clock a user can move is a clock that says nothing about when a
 * file was written.
 *
 * @param regs ebx is an esd_time_t* in user memory. On return eax is E_OK, or a
 *             negative errno.
 */
void sys_settime(arch_regs_t *regs) {
    const esd_time_t *user_in = (const esd_time_t *)regs->ebx;
    esd_time_t given, utc;

    if (current_task != 0 && current_task->uid != 0) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "settime: root only");
        regs->eax = E_PERM;
        return;
    }

    if (copy_from_user(&given, user_in, sizeof(given)) != E_OK) {
        regs->eax = E_FAULT;
        return;
    }

    /* Round-tripped through the epoch, which is where the offset comes off and
     * the calendar carry is already right. A date before 1970 converts to zero
     * and would silently become the epoch, so it is refused instead. */
    uint32_t epoch = esd_time_to_epoch(&given);
    if (epoch == 0) { regs->eax = E_INVAL; return; }

    esd_time_from_epoch(epoch, &utc);

    regs->eax = rtc_set_utc(&utc);
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
