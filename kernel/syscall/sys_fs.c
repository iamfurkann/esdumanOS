/*
 * File: sys_fs.c
 * Purpose: Contains system calls and related utilities.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscalls_internal.h"
#include "types.h"
#include "arch.h"
#include "registers.h"
#include "stdio.h"
#include "tty.h"
#include "io.h"
#include "fs.h"
#include "process.h"
#include "pipe.h"
#include "devfs.h"
#include "errno.h"
#include "klog.h"
#include "isr.h"
#include "keyboard.h"
#include "kheap.h"
#include "libft.h"
#include "uaccess.h"
#include "stat.h"
#include "security.h"

/**
 * @brief Upper bound on the kernel staging buffer used by sys_readdir().
 *
 * The requested size comes from user space, so the allocation is capped rather
 * than trusted. 4 KB holds the whole directory table at realistic name lengths.
 */
#define READDIR_STAGE_MAX 4096

/**
 * @brief Bounds on sys_getcwd()'s parent-chain walk.
 *
 * DEPTH exists because the walk follows parent_id links: a table whose links
 * formed a cycle would otherwise spin forever inside a syscall, with interrupts
 * enabled but the task never returning. Bounding it turns a corrupted table into
 * E_NAMETOOLONG instead of a hang - the same reasoning as the depth limit added
 * to check_vfs_access().
 *
 * PATH is what the rendered path is built into, on the kernel stack. MAX_FILENAME
 * is 256, so a deeply nested path with long names can exceed this; that case
 * reports E_NAMETOOLONG rather than truncating to something that looks valid.
 */
#define GETCWD_MAX_DEPTH 16
#define GETCWD_MAX_PATH  512

static int copy_user_string(char *destination, const char *source, size_t max_len) {
    if (!validate_string_pointer(source, max_len)) return 0;
    return copy_string_from_user(destination, source, max_len) == E_OK;
}

/**
 * @brief Splits a path into its parent directory and final component.
 *
 * Relative paths resolve against the calling process's working directory, which
 * lives in the PCB. Before this, each syscall took the base directory from a
 * register the caller filled in - so a process chose where its own relative
 * lookups started, and every /bin tool passed a hardcoded 0, which is why they
 * all operated on the root directory no matter where the shell had cd'd to.
 *
 * @param path     Path already copied into kernel memory.
 * @param basename Receives the final component; MAX_FILENAME bytes.
 * @return Parent directory entry id, or a negative errno.
 */
static int split_from_cwd(const char *path, char *basename) {
    uint8_t base = current_task ? current_task->cwd_id : 0;
    return vfs_resolve_path(path, base, basename);
}
/**
 * @brief Function sys_read
 */
void sys_read(arch_regs_t *regs) {
    int fd = (int)regs->ebx;
    char *buf = (char *)regs->ecx;
    int size = (int)regs->edx;

    if (!validate_fd(fd) || size < 0) {
        regs->eax = E_INVAL; 
        return;
    }
    if (size == 0) { regs->eax = 0; return; }
    if (!validate_user_writable_pointer(buf, (size_t)size)) {
        regs->eax = E_FAULT;
        return;
    }
    
    file_descriptor_t *desc = &current_task->fd_table[fd];

    if (desc->type == FD_TYPE_CONSOLE) {
        char c = get_keyboard_char();
        if (c == 0) {
            // Nothing typed yet: block, and re-run the whole read once a key
            // arrives. keyboard_interrupt_handler() wakes WAIT_KBD.
            if (!syscall_block_and_restart(regs, WAIT_KBD)) {
                regs->eax = E_AGAIN;
            }
            return;
        }
        regs->eax = copy_to_user(buf, &c, 1) == E_OK ? 1 : E_FAULT;
    }
    else {
        uint8_t *kernel_buf = (uint8_t *)kmalloc((uint32_t)size);
        if (!kernel_buf) { regs->eax = E_NOMEM; return; }

        int ret;
        if (desc->type == FD_TYPE_FILE) {
            ret = fs_read((vfs_file_t *)desc->ptr, kernel_buf, (uint32_t)size);
        }
        else if (desc->type == FD_TYPE_PIPE) {
            ret = pipe_read((pipe_t *)desc->ptr, kernel_buf, size);
        }
        else if (desc->type == FD_TYPE_DEVICE) {
            int d_idx = (int)desc->ptr;
            if (!dev_index_is_valid(d_idx)) {
                klog_int(LOG_LEVEL_ERROR, "DEVFS", "Rejected read through out-of-range device index", d_idx);
                kfree(kernel_buf);
                regs->eax = E_BADF;
                return;
            }
            ret = dev_table[d_idx].read ? dev_table[d_idx].read(kernel_buf, size) : E_NOENT;
        }
        else {
            kfree(kernel_buf);
            regs->eax = E_BADF;
            return;
        }

        if (ret == E_AGAIN) {
            // Pipe is empty with the writer still open: block and re-run.
            kfree(kernel_buf);
            if (!syscall_block_and_restart(regs, WAIT_IPC)) {
                regs->eax = E_AGAIN;
            }
            return;
        }
        if (ret > 0 && copy_to_user(buf, kernel_buf, (size_t)ret) != E_OK) {
            ret = E_FAULT;
        }
        kfree(kernel_buf);
        regs->eax = ret;
    }
}

/**
 * @brief Function sys_write
 */
void sys_write(arch_regs_t *regs) {
    int fd = (int)regs->ebx;
    char *buf = (char *)regs->ecx;
    int size = (int)regs->edx;

    if (!validate_fd(fd) || size < 0) {
        regs->eax = E_INVAL; 
        return;
    }
    if (size == 0) { regs->eax = 0; return; }
    if (!validate_user_pointer(buf, (size_t)size)) {
        regs->eax = E_FAULT;
        return;
    }
    file_descriptor_t *desc = &current_task->fd_table[fd];

    uint8_t *kernel_buf = (uint8_t *)kmalloc((uint32_t)size);
    if (!kernel_buf) { regs->eax = E_NOMEM; return; }
    if (copy_from_user(kernel_buf, buf, (size_t)size) != E_OK) {
        kfree(kernel_buf);
        regs->eax = E_FAULT;
        return;
    }

    if (desc->type == FD_TYPE_CONSOLE) {
        for(int i=0; i<size; i++) terminal_putchar(kernel_buf[i]);
        regs->eax = size;
    } 
    else if (desc->type == FD_TYPE_PIPE) {
        int ret = pipe_write((pipe_t *)desc->ptr, kernel_buf, size);
        if (ret == E_AGAIN) {
            // Pipe is full with the reader still open: block and re-run.
            kfree(kernel_buf);
            if (!syscall_block_and_restart(regs, WAIT_IPC)) {
                regs->eax = E_AGAIN;
            }
            return;
        } else {
            regs->eax = ret;
        }
    }
    else if (desc->type == FD_TYPE_DEVICE) {
        int d_idx = (int)desc->ptr;
        if (!dev_index_is_valid(d_idx)) {
            klog_int(LOG_LEVEL_ERROR, "DEVFS", "Rejected write through out-of-range device index", d_idx);
            regs->eax = E_BADF;
        } else if (dev_table[d_idx].write) {
            regs->eax = dev_table[d_idx].write(kernel_buf, size);
        } else { regs->eax = E_NOENT; }
    }
    else { regs->eax = E_BADF; }
    kfree(kernel_buf);
}

/**
 * @brief Function sys_pipe
 */
void sys_pipe(arch_regs_t *regs) {
    uint32_t *fds = (uint32_t *)regs->ebx;
    if (!validate_user_writable_pointer((const void *)fds, 8)) { regs->eax = E_FAULT; return; }
    
    pipe_t *p = create_pipe();
    if (!p) { regs->eax = E_NOMEM; return; }

    int fd1 = -1, fd2 = -1;
    for(uint32_t i=3; i<current_task->fd_table_size; i++) {
        if (current_task->fd_table[i].type == FD_TYPE_NONE) {
            if (fd1 == -1) fd1 = i;
            else if (fd2 == -1) { fd2 = i; break; }
        }
    }
    if (fd2 == -1) { destroy_pipe(p); regs->eax = E_MFILE; return; }

    // READ END
    current_task->fd_table[fd1].type = FD_TYPE_PIPE;
    current_task->fd_table[fd1].ptr = (uint32_t)p;
    current_task->fd_table[fd1].mode = 0; 

    // WRITE END
    current_task->fd_table[fd2].type = FD_TYPE_PIPE;
    current_task->fd_table[fd2].ptr = (uint32_t)p;
    current_task->fd_table[fd2].mode = 1; 

    uint32_t new_fds[2] = { (uint32_t)fd1, (uint32_t)fd2 };
    regs->eax = copy_to_user(fds, new_fds, sizeof(new_fds));
}

/**
 * @brief Function sys_dup2
 */
void sys_dup2(arch_regs_t *regs) {
    int oldfd = (int)regs->ebx;
    int newfd = (int)regs->ecx;
    if (!validate_fd(oldfd) || !validate_fd(newfd)) { regs->eax = E_BADF; return; }
    if (current_task->fd_table[oldfd].type == FD_TYPE_NONE) { regs->eax = E_BADF; return; }

    uint8_t old_type = current_task->fd_table[newfd].type;
    if (old_type == FD_TYPE_PIPE) {
        pipe_t *p = (pipe_t *)current_task->fd_table[newfd].ptr;
        if (p != 0) { 
            if (current_task->fd_table[newfd].mode == 1) p->write_refs--; 
            else p->read_refs--;
            
            if (p->read_refs <= 0 && p->write_refs <= 0) {
                destroy_pipe(p);
            }
        }
    }
    current_task->fd_table[newfd] = current_task->fd_table[oldfd];
    if (current_task->fd_table[oldfd].type == FD_TYPE_PIPE) {
        pipe_t *p = (pipe_t *)current_task->fd_table[oldfd].ptr;
        if (p != 0) {
            if (current_task->fd_table[oldfd].mode == 1) p->write_refs++; 
            else p->read_refs++;
        }
    }
    regs->eax = newfd;
}

/**
 * @brief Function sys_close
 */
void sys_close(arch_regs_t *regs) {
    int fd = (int)regs->ebx;
    if (!validate_fd(fd)) { regs->eax = E_BADF; return; }
    file_descriptor_t *desc = &current_task->fd_table[fd];
    
    if (desc->type == FD_TYPE_PIPE && desc->ptr != 0) {
        pipe_t *p = (pipe_t *)desc->ptr;
        if (desc->mode == 1) p->write_refs--; 
        else p->read_refs--;
        
        if (p->read_refs <= 0 && p->write_refs <= 0) {
            destroy_pipe(p); 
        }
    }
    if (desc->type == FD_TYPE_FILE && desc->ptr != 0) { 
        vfs_file_t *f = (vfs_file_t *)desc->ptr;
        f->ref_count--;
        if (f->ref_count <= 0) {
            kfree((void *)desc->ptr);
        }
    }
    
    desc->type = FD_TYPE_NONE;
    desc->ptr = 0;
    desc->mode = 0;
    regs->eax = 0;
}


/* ── VIRTUAL FILE SYSTEM (VFS) DISK OPERATIONS ────────────────────── */

/**
 * @brief Function sys_open
 */
void sys_open(arch_regs_t *regs) {
    char path[MAX_FILENAME];
    if (!copy_user_string(path, (const char *)regs->ebx, sizeof(path))) { regs->eax = E_FAULT; return; }
    char basename[MAX_FILENAME];
    for (int k = 0; k < MAX_FILENAME; k++) basename[k] = '\0';
    int parent_id = split_from_cwd(path, basename);

    if (parent_id < 0 || basename[0] == '\0') { regs->eax = E_NOENT; return; }

    int dev_idx_vfs = fs_get_entry_idx("dev", 0);
    int dev_id = (dev_idx_vfs != -1) ? dir_table[dev_idx_vfs].entry_id : -1;

    if (parent_id == dev_id) {
        /* get_device_idx() reports failure as a negative errno (E_NOENT is -2),
         * so this must not be compared against -1: doing so handed out a
         * descriptor carrying ptr == (uint32_t)-2, and the first read()/write()
         * on it evaluated dev_table[-2].read and called through it. */
        int d_idx = get_device_idx(basename);
        if (d_idx >= 0) {
            int fd = -1;
            for (uint32_t i = 3; i < current_task->fd_table_size; i++) {
                if (current_task->fd_table[i].type == FD_TYPE_NONE) { fd = i; break; }
            }
            if (fd != -1) {
                current_task->fd_table[fd].type = FD_TYPE_DEVICE;
                current_task->fd_table[fd].ptr = d_idx;
                current_task->fd_table[fd].mode = 0;
                regs->eax = fd;
            } else { regs->eax = E_MFILE; }
        } else { regs->eax = E_NOENT; }
        return;
    }

    if (!check_vfs_access(parent_id, 0)) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("cat: Permission denied\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        regs->eax = E_ACCES; return;
    }

    int fd = -1;
    for (uint32_t i = 3; i < current_task->fd_table_size; i++) {
        if (current_task->fd_table[i].type == 0) { fd = i; break; }
    }
    if (fd == -1) { regs->eax = E_MFILE; return; }

    vfs_file_t *new_file = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
    
    if (fs_open(basename, parent_id, new_file) == 0) { 
        new_file->current_offset = 0;
        current_task->fd_table[fd].type = FD_TYPE_FILE;
        current_task->fd_table[fd].ptr = (uint32_t)new_file;
        current_task->fd_table[fd].mode = 0;
        regs->eax = fd;
    } else {
        kfree(new_file);
        regs->eax = E_NOENT;
    }
}

/**
 * @brief Function sys_create_file
 */
void sys_create_file(arch_regs_t *regs) {
    char filename[MAX_FILENAME];
    char *content = (char *)kmalloc(4096);
    if (!content) { regs->eax = E_NOMEM; return; }
    if (!copy_user_string(filename, (const char *)regs->ebx, sizeof(filename)) ||
        !copy_user_string(content, (const char *)regs->ecx, 4096)) {
        kfree(content);
        regs->eax = E_FAULT; return; 
    }
    char basename[MAX_FILENAME];
    int parent_id = split_from_cwd(filename, basename);
    if (parent_id < 0 || basename[0] == '\0') { kfree(content); regs->eax = E_NOENT; return; }

    if (!check_vfs_access((uint8_t)parent_id, 1)) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: No write access to this location!");
        kfree(content);
        regs->eax = E_ACCES; return;
    }
    regs->eax = fs_create_file(basename, (uint8_t *)content, ft_strlen(content), (uint8_t)parent_id);
    kfree(content);
}

/**
 * @brief Function sys_rm_file
 */
void sys_rm_file(arch_regs_t *regs) {
    char filename[MAX_FILENAME];
    if (!copy_user_string(filename, (const char *)regs->ebx, sizeof(filename))) { regs->eax = E_FAULT; return; }
    char basename[MAX_FILENAME];
    int parent_id = split_from_cwd(filename, basename);
    if (parent_id < 0 || basename[0] == '\0') { regs->eax = E_NOENT; return; }

    if (!check_vfs_access((uint8_t)parent_id, 1)) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "rm: Permission denied. Cannot delete this file!");
        regs->eax = E_ACCES; return;
    }
    regs->eax = fs_delete(basename, (uint8_t)parent_id);
}

/**
 * @brief Function sys_mv_file
 */
void sys_mv_file(arch_regs_t *regs) {
    char old_name[MAX_FILENAME];
    char new_name[MAX_FILENAME];
    if (!copy_user_string(old_name, (const char *)regs->ebx, sizeof(old_name)) ||
        !copy_user_string(new_name, (const char *)regs->ecx, sizeof(new_name))) {
        regs->eax = E_FAULT; return; 
    }
    char old_base[MAX_FILENAME];
    char new_base[MAX_FILENAME];
    int old_parent = split_from_cwd(old_name, old_base);
    int new_parent = split_from_cwd(new_name, new_base);

    if (old_parent < 0 || new_parent < 0 || old_base[0] == '\0' || new_base[0] == '\0') {
        regs->eax = E_NOENT; return;
    }

    /*
     * fs_rename() renames within a single directory - it has no notion of moving
     * an entry between parents. Refuse the cross-directory case rather than
     * silently dropping the destination directory and renaming in place, which
     * is what ignoring new_parent would do.
     */
    if (old_parent != new_parent) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "mv: moving between directories is not supported.");
        regs->eax = E_INVAL; return;
    }

    if (!check_vfs_access((uint8_t)old_parent, 1)) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "mv: Permission denied. Cannot rename this file!");
        regs->eax = E_ACCES; return;
    }
    regs->eax = fs_rename(old_base, new_base, (uint8_t)old_parent);
}

/**
 * @brief Function sys_mkdir
 */
void sys_mkdir(arch_regs_t *regs) {
    char name[MAX_FILENAME];
    if (!copy_user_string(name, (const char *)regs->ebx, sizeof(name))) { regs->eax = E_FAULT; return; }
    char basename[MAX_FILENAME];
    int parent_id = split_from_cwd(name, basename);
    if (parent_id < 0 || basename[0] == '\0') { regs->eax = E_NOENT; return; }

    if (!check_vfs_access((uint8_t)parent_id, 1)) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "mkdir: Permission denied. No directory creation access!");
        regs->eax = E_ACCES; return;
    }
    regs->eax = fs_mkdir(basename, (uint8_t)parent_id);
}

/**
 * @brief Function sys_ls_dir
 */
void sys_ls_dir(arch_regs_t *regs) {
    if (!check_vfs_access((uint8_t)regs->ebx, 0)) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "ls: Permission denied");
        regs->eax = E_ACCES; return;
    }
    fs_list_dir((uint8_t)regs->ebx);
    regs->eax = E_OK;
}

/**
 * @brief Resolves a path to a directory entry id, relative to a base directory.
 *
 * Shared by sys_get_dir_id() and sys_chdir(), which need exactly the same
 * resolution - the "." and ".." cases and the access check included.
 *
 * Relative paths resolve against the calling process's working directory; see
 * split_from_cwd() for why the base is no longer something the caller chooses.
 *
 * @param path User-supplied path, already copied into kernel memory.
 * @return Directory entry id on success, or a negative errno.
 */
static int resolve_dir_from_cwd(const char *path) {
    char basename[MAX_FILENAME];

    int parent = split_from_cwd(path, basename);
    if (parent < 0) return E_NOENT;

    int target;

    if (basename[0] == '\0' || (basename[0] == '.' && basename[1] == '\0')) {
        target = parent;
    }
    else if (basename[0] == '.' && basename[1] == '.' && basename[2] == '\0') {
        /* Root is its own parent; anything unfound falls back to root. */
        target = 0;
        for (int k = 0; k < MAX_FILES_IN_DIR; k++) {
            if (dir_table[k].entry_id == parent && dir_table[k].file_type == FT_DIR &&
                dir_table[k].is_used == 1) {
                target = dir_table[k].parent_id;
                break;
            }
        }
    }
    else {
        int idx = fs_get_entry_idx(basename, parent);
        if (idx == -1) return E_NOENT;
        if (dir_table[idx].file_type != FT_DIR) return E_NOTDIR;
        target = dir_table[idx].entry_id;
    }

    if (!check_vfs_access((uint8_t)target, 0)) return E_ACCES;
    return target;
}

/**
 * @brief Function sys_get_dir_id
 */
void sys_get_dir_id(arch_regs_t *regs) {
    char path[MAX_FILENAME];
    if (!copy_user_string(path, (const char *)regs->ebx, sizeof(path))) { regs->eax = E_FAULT; return; }

    regs->eax = resolve_dir_from_cwd(path);
}

/**
 * @brief Changes the calling process's working directory.
 *
 * Refuses anything that is not a directory the caller may read, and only then
 * commits - so a failed chdir leaves the process exactly where it was rather
 * than somewhere half-resolved.
 */
void sys_chdir(arch_regs_t *regs) {
    char path[MAX_FILENAME];
    if (!copy_user_string(path, (const char *)regs->ebx, sizeof(path))) { regs->eax = E_FAULT; return; }
    if (current_task == 0) { regs->eax = E_FAULT; return; }

    int target = resolve_dir_from_cwd(path);
    if (target < 0) { regs->eax = target; return; }

    current_task->cwd_id = (uint8_t)target;
    regs->eax = E_OK;
}

/**
 * @brief Writes the calling process's working directory into a user buffer.
 *
 * Walks the parent chain up to root and then renders it forwards. The walk is
 * bounded rather than trusting the table to be acyclic: a corrupted parent_id
 * that pointed back into the chain would otherwise hang the kernel inside a
 * syscall, which is the same failure K-10 fixed in the VFS access check.
 */
void sys_getcwd(arch_regs_t *regs) {
    char *user_buf = (char *)regs->ebx;
    uint32_t user_size = regs->ecx;

    if (current_task == 0) { regs->eax = E_FAULT; return; }
    if (user_size == 0) { regs->eax = E_INVAL; return; }

    uint8_t chain[GETCWD_MAX_DEPTH];
    int depth = 0;
    uint8_t id = current_task->cwd_id;

    while (id != 0) {
        if (depth >= GETCWD_MAX_DEPTH) { regs->eax = E_NAMETOOLONG; return; }

        int idx = -1;
        for (int k = 0; k < MAX_FILES_IN_DIR; k++) {
            if (dir_table[k].entry_id == id && dir_table[k].file_type == FT_DIR &&
                dir_table[k].is_used == 1) {
                idx = k;
                break;
            }
        }
        if (idx == -1) { regs->eax = E_NOENT; return; }

        chain[depth++] = (uint8_t)idx;
        id = dir_table[idx].parent_id;
    }

    char path[GETCWD_MAX_PATH];
    uint32_t pos = 0;
    path[pos++] = '/';

    /* chain holds cwd-first, so render it in reverse to get root-first. */
    for (int d = depth - 1; d >= 0; d--) {
        const char *name = dir_table[chain[d]].filename;

        if (pos > 1) {
            if (pos + 1 >= sizeof(path)) { regs->eax = E_NAMETOOLONG; return; }
            path[pos++] = '/';
        }
        for (int c = 0; name[c] != '\0'; c++) {
            if (pos + 1 >= sizeof(path)) { regs->eax = E_NAMETOOLONG; return; }
            path[pos++] = name[c];
        }
    }
    path[pos] = '\0';

    if (pos + 1 > user_size) { regs->eax = E_NAMETOOLONG; return; }
    if (copy_to_user(user_buf, path, pos + 1) != E_OK) { regs->eax = E_FAULT; return; }

    regs->eax = (int)pos;
}

/**
 * @brief Finds the directory table slot holding a given entry id.
 *
 * Entry ids and table indices are not the same thing, and the table is not
 * indexed by id - so anything holding an id has to search. Unlike the walks in
 * resolve_dir_from_cwd() and sys_getcwd(), which are looking specifically for a
 * directory, this matches an entry of any type.
 *
 * @param id Entry id to find. 0 is the root, which has no slot of its own.
 * @return Index into dir_table, or -1 when no live entry carries that id.
 */
static int dir_index_of_entry_id(uint8_t id) {
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1 && dir_table[i].entry_id == id) return i;
    }
    return -1;
}

/**
 * @brief Describes the root directory, which owns no directory table entry.
 *
 * fs_dir_exists() treats id 0 as always present without looking it up, so a
 * stat of "/" has nothing to read fields out of and they are stated here
 * instead. Root is its own parent, which is the same convention the ".." walk
 * in resolve_dir_from_cwd() relies on.
 */
static void stat_fill_root(esd_stat_t *out) {
    out->st_size = 0;
    out->st_disk_size = 0;
    out->st_ino = 0;
    out->st_uid = 0;
    out->st_parent = 0;
    out->st_type = FT_DIR;
    out->st_encrypted = 0;
    out->st_reserved = 0;
}

/**
 * @brief Fills an esd_stat_t from a directory table slot.
 *
 * st_size does not come from the table. Under SEC_LEVEL_CRYPTO_ENFORCED - the
 * default - dir_table records the encrypted form's length, which is an IV, a
 * header and padding larger than anything read() will hand back. fs_size()
 * resolves that, and taking the shortcut here would make stat() wrong for every
 * regular file on a normally configured system.
 *
 * @param out Destination, filled only on success.
 * @param idx Directory table slot to describe.
 * @param open_file An already-open handle on the same entry, or 0 to open a
 *                  temporary one. fstat() passes the caller's handle so the
 *                  entry is not looked up twice; fs_size() only reads from it.
 * @return E_OK on success, or a negative error code.
 */
static int stat_fill_from_index(esd_stat_t *out, int idx, vfs_file_t *open_file) {
    if (idx < 0 || idx >= MAX_FILES_IN_DIR || dir_table[idx].is_used != 1) return E_NOENT;

    out->st_ino = dir_table[idx].entry_id;
    out->st_uid = dir_table[idx].owner_uid;
    out->st_parent = dir_table[idx].parent_id;
    out->st_type = dir_table[idx].file_type;
    out->st_disk_size = dir_table[idx].file_size;
    out->st_reserved = 0;

    if (dir_table[idx].file_type == FT_DIR) {
        /* Directories hold no file content: fs_mkdir() stores size 0 and there
         * is nothing encrypted to measure. */
        out->st_encrypted = 0;
        out->st_size = 0;
        return E_OK;
    }

    out->st_encrypted = (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) ? 1 : 0;

    if (open_file != 0) return fs_size(open_file, &out->st_size);

    vfs_file_t probe;
    if (fs_open(dir_table[idx].filename, dir_table[idx].parent_id, &probe) != E_OK) return E_NOENT;
    return fs_size(&probe, &out->st_size);
}

/**
 * @brief Reports a path's metadata into a user-supplied esd_stat_t.
 *
 * Relative paths resolve against the process's working directory, like every
 * other path-taking syscall since v0.3.0 - see split_from_cwd().
 */
void sys_stat(arch_regs_t *regs) {
    char path[MAX_FILENAME];
    esd_stat_t *user_out = (esd_stat_t *)regs->ecx;

    if (!copy_user_string(path, (const char *)regs->ebx, sizeof(path))) { regs->eax = E_FAULT; return; }
    if (!validate_user_writable_pointer(user_out, sizeof(esd_stat_t))) { regs->eax = E_FAULT; return; }

    char basename[MAX_FILENAME];
    for (int k = 0; k < MAX_FILENAME; k++) basename[k] = '\0';

    /* Propagated rather than flattened to E_NOENT: a path whose middle component
     * is a regular file is E_NOTDIR, and saying so is more useful than "missing". */
    int parent_id = split_from_cwd(path, basename);
    if (parent_id < 0) { regs->eax = parent_id; return; }

    esd_stat_t st;
    int rc;

    int names_a_directory = (basename[0] == '\0') ||
                            (basename[0] == '.' && basename[1] == '\0') ||
                            (basename[0] == '.' && basename[1] == '.' && basename[2] == '\0');

    if (names_a_directory) {
        /*
         * "/", "." and ".." name a directory rather than an entry inside one.
         * resolve_dir_from_cwd() already handles all three, including the access
         * check, so the alternative would be a second implementation of the
         * same walk.
         */
        int target = resolve_dir_from_cwd(path);
        if (target < 0) { regs->eax = target; return; }

        if (target == 0) {
            stat_fill_root(&st);
            rc = E_OK;
        } else {
            rc = stat_fill_from_index(&st, dir_index_of_entry_id((uint8_t)target), 0);
        }
    } else {
        if (!check_vfs_access(parent_id, 0)) {
            klog(LOG_LEVEL_WARN, "SYSCALL", "stat: Permission denied");
            regs->eax = E_ACCES; return;
        }

        rc = stat_fill_from_index(&st, fs_get_entry_idx(basename, (uint8_t)parent_id), 0);
    }

    if (rc != E_OK) { regs->eax = rc; return; }

    regs->eax = (copy_to_user(user_out, &st, sizeof(st)) == E_OK) ? E_OK : E_FAULT;
}

/**
 * @brief Reports an open descriptor's metadata into a user-supplied esd_stat_t.
 *
 * Only real files can answer. A pipe has no directory entry, and a device
 * descriptor stores a device table *index* in ptr rather than a pointer - the
 * same field overload that made a stale -1 comparison in sys_open() an indirect
 * call through dev_table[-2]. Refusing by type keeps that shape unreachable
 * here rather than relying on the value looking wrong.
 */
void sys_fstat(arch_regs_t *regs) {
    int fd = (int)regs->ebx;
    esd_stat_t *user_out = (esd_stat_t *)regs->ecx;

    if (!validate_fd(fd)) { regs->eax = E_BADF; return; }
    if (!validate_user_writable_pointer(user_out, sizeof(esd_stat_t))) { regs->eax = E_FAULT; return; }

    file_descriptor_t *desc = &current_task->fd_table[fd];
    if (desc->type != FD_TYPE_FILE || desc->ptr == 0) { regs->eax = E_BADF; return; }

    vfs_file_t *file = (vfs_file_t *)desc->ptr;

    esd_stat_t st;
    int rc = stat_fill_from_index(&st, file->inode_idx, file);
    if (rc != E_OK) { regs->eax = rc; return; }

    regs->eax = (copy_to_user(user_out, &st, sizeof(st)) == E_OK) ? E_OK : E_FAULT;
}

/**
 * @brief Repositions the read offset of an open file.
 *
 * vfs_file_t.current_offset is the real cursor: fs_read_raw() reads and advances
 * it, and fs_read_encrypted() honours it as an offset into the *plaintext*. So
 * seeking needs no special case for encrypted files - provided SEEK_END asks
 * fs_size() rather than the directory table, which records the longer encrypted
 * form.
 *
 * Seeking past the end is allowed, as POSIX has it. Nothing can be written
 * there - writes do not go through this cursor - and both read paths already
 * return 0 for an offset at or beyond the data.
 */
void sys_lseek(arch_regs_t *regs) {
    int fd = (int)regs->ebx;
    int offset = (int)regs->ecx;
    int whence = (int)regs->edx;

    if (!validate_fd(fd)) { regs->eax = E_BADF; return; }

    file_descriptor_t *desc = &current_task->fd_table[fd];
    if (desc->type == FD_TYPE_NONE) { regs->eax = E_BADF; return; }

    /*
     * Pipes, the console and /dev nodes are streams with no position to set.
     * E_SPIPE is exactly this case.
     */
    if (desc->type != FD_TYPE_FILE) { regs->eax = E_SPIPE; return; }
    if (desc->ptr == 0) { regs->eax = E_BADF; return; }

    vfs_file_t *file = (vfs_file_t *)desc->ptr;

    uint32_t base = 0;
    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = file->current_offset;
            break;
        case SEEK_END: {
            uint32_t end = 0;
            int rc = fs_size(file, &end);
            if (rc != E_OK) { regs->eax = rc; return; }
            base = end;
            break;
        }
        default:
            regs->eax = E_INVAL;
            return;
    }

    uint32_t target;

    if (offset < 0) {
        /*
         * Negated in two steps so INT_MIN does not overflow on the way. The
         * result is the magnitude of the move, as an unsigned value.
         */
        uint32_t back = (uint32_t)(-(offset + 1)) + 1u;
        if (back > base) { regs->eax = E_INVAL; return; }
        target = base - back;
    } else {
        /* Keep the result representable as a positive int, since that is what
         * the syscall returns and a negative one means failure. */
        if ((uint32_t)offset > 0x7FFFFFFFu - base) { regs->eax = E_INVAL; return; }
        target = base + (uint32_t)offset;
    }

    file->current_offset = target;
    regs->eax = (int)target;
}

/**
 * @brief Function sys_list_files
 */
void sys_list_files(arch_regs_t *regs) {
    fs_list_files();
    regs->eax = E_OK;
}

/**
 * @brief Function sys_cat_raw
 */
void sys_cat_raw(arch_regs_t *regs) {
    char target_file[MAX_FILENAME];
    if (!copy_user_string(target_file, (const char *)regs->ebx, sizeof(target_file))) { regs->eax = E_FAULT; return; }

    char basename[MAX_FILENAME];
    int resolved = split_from_cwd(target_file, basename);
    if (resolved < 0 || basename[0] == '\0') { regs->eax = E_NOENT; return; }
    uint8_t parent_id = (uint8_t)resolved;

    vfs_file_t file;

    if (fs_open(basename, parent_id, &file) == 0) {
        int file_idx = fs_get_entry_idx(basename, parent_id);
        if (file_idx != -1) {
            if (!check_vfs_access(dir_table[file_idx].entry_id, 0)) {
                printk("Error: No permission to read file '%s'!\n", target_file);
                regs->eax = E_ACCES;
                return;
            }
        }
        printk("--- %s [PHYSICAL DISK HEX DUMP] ---\n", file.filename);
        uint8_t chunk[256];
        // Signed: fs_read_raw() reports errors as a negative value, and storing
        // that in an unsigned made it read as ~4 billion - the loop below then
        // walked gigabytes past a 256-byte stack buffer.
        int bytes;
        file.current_offset = 0;

        while ((bytes = fs_read_raw(&file, chunk, 256)) > 0) {
            for (int i = 0; i < bytes; i++) {
                printk("%x ", chunk[i]);
            }
        }
        printk("\n----------------------------------\n");
    } else {
        printk("Error: '%s' not found!\n", target_file);
    }
}

/**
 * @brief Function sys_cat_file
 */
void sys_cat_file(arch_regs_t *regs) {
    char target_file[MAX_FILENAME];
    if (!copy_user_string(target_file, (const char *)regs->ebx, sizeof(target_file))) { regs->eax = E_FAULT; return; }

    char basename[MAX_FILENAME];
    int resolved = split_from_cwd(target_file, basename);
    if (resolved < 0 || basename[0] == '\0') { regs->eax = E_NOENT; return; }
    uint8_t parent_id = (uint8_t)resolved;

    vfs_file_t file;

    if (fs_open(basename, parent_id, &file) == 0) {
        int file_idx = fs_get_entry_idx(basename, parent_id);
        if (file_idx != -1) {
            if (!check_vfs_access(dir_table[file_idx].entry_id, 0)) {
                printk("Error: Permission denied!\n");
                regs->eax = E_ACCES;
                return;
            }
        }
        printk("--- %s ---\n", file.filename);
        uint8_t chunk[256];
        // Signed for the same reason as sys_cat_raw(); fs_read() can now return
        // E_ACCES when the master key has been destroyed.
        int bytes;
        file.current_offset = 0;
        while ((bytes = fs_read(&file, chunk, 256)) > 0) {
            for (int i = 0; i < bytes; i++) {
                if (chunk[i] != '\r' && chunk[i] != '\b' && chunk[i] != '\0') {
                    char temp[2] = { chunk[i], '\0' }; 
                    printk("%s", temp);
                }
            }
        }
        printk("\n");
    } else {
        printk("Error: '%s' not found!\n", target_file);
    }
}

/**
 * @brief Reads directory entries into a user-space buffer.
 * 
 * Copies null-separated filenames of all entries in the given directory
 * into the user buffer. Returns total bytes written.
 *
 * Args:
 *   ebx = parent directory ID
 *   ecx = user buffer pointer
 *   edx = buffer size
 * Returns:
 *   eax = total bytes written, or negative error code
 */
void sys_readdir(arch_regs_t *regs) {
    uint8_t parent_id = (uint8_t)regs->ebx;
    char *user_buf = (char *)regs->ecx;
    uint32_t buf_size = regs->edx;

    if (buf_size == 0) { regs->eax = 0; return; }

    // Writable, not merely readable: this call fills the caller's buffer.
    if (!validate_user_writable_pointer(user_buf, buf_size)) {
        regs->eax = E_FAULT;
        return;
    }
    if (!check_vfs_access(parent_id, 0)) {
        regs->eax = E_ACCES;
        return;
    }

    /*
     * Stage the listing in kernel memory and hand it over with one
     * copy_to_user(). The entries used to be stored straight through the user
     * pointer, which skipped the uaccess path entirely: with SMAP active every
     * such store faults with no fixup installed and lands in kernel_panic().
     *
     * buf_size arrives from user space, so the staging buffer is capped -
     * kmalloc()ing whatever the caller asks for is a one-line way to exhaust
     * the kernel heap. A caller with a larger buffer simply gets a short read.
     */
    uint32_t cap = (buf_size > READDIR_STAGE_MAX) ? READDIR_STAGE_MAX : buf_size;

    char *kbuf = (char *)kmalloc(cap);
    if (!kbuf) { regs->eax = E_NOMEM; return; }

    uint32_t offset = 0;
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && dir_table[i].parent_id == parent_id) {
            uint32_t name_len = 0;
            while (dir_table[i].filename[name_len] && name_len < MAX_FILENAME - 1) name_len++;

            // Need space for: name + type byte + null separator.
            if (offset + name_len + 2 > cap) break;

            for (uint32_t j = 0; j < name_len; j++) {
                kbuf[offset++] = dir_table[i].filename[j];
            }
            // Type marker sits right after the name, before the separator:
            // "filename\x01" for directories, "filename\x02" for files.
            kbuf[offset++] = (dir_table[i].file_type == FT_DIR) ? 1 : 2;
            kbuf[offset++] = '\0';
        }
    }

    if (offset > 0 && copy_to_user(user_buf, kbuf, offset) != E_OK) {
        kfree(kbuf);
        regs->eax = E_FAULT;
        return;
    }

    kfree(kbuf);
    regs->eax = offset;
}
