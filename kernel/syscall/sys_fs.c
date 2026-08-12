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

/**
 * @brief Upper bound on the kernel staging buffer used by sys_readdir().
 *
 * The requested size comes from user space, so the allocation is capped rather
 * than trusted. 4 KB holds the whole directory table at realistic name lengths.
 */
#define READDIR_STAGE_MAX 4096

static int copy_user_string(char *destination, const char *source, size_t max_len) {
    if (!validate_string_pointer(source, max_len)) return 0;
    return copy_string_from_user(destination, source, max_len) == E_OK;
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
    int parent_id = vfs_resolve_path(path, (uint8_t)regs->ecx, basename);
    
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
    uint8_t parent_id = (uint8_t)regs->edx;

    if (!check_vfs_access(parent_id, 1)) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: No write access to this location!");
        kfree(content);
        regs->eax = E_ACCES; return;
    }
    regs->eax = fs_create_file(filename, (uint8_t *)content, ft_strlen(content), parent_id);
    kfree(content);
}

/**
 * @brief Function sys_rm_file
 */
void sys_rm_file(arch_regs_t *regs) {
    char filename[MAX_FILENAME];
    if (!copy_user_string(filename, (const char *)regs->ebx, sizeof(filename))) { regs->eax = E_FAULT; return; }
    uint8_t parent_id = (uint8_t)regs->ecx; 

    if (!check_vfs_access(parent_id, 1)) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "rm: Permission denied. Cannot delete this file!");
        regs->eax = E_ACCES; return;
    }
    regs->eax = fs_delete(filename, parent_id);
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
    uint8_t parent_id = (uint8_t)regs->edx; 

    if (!check_vfs_access(parent_id, 1)) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "mv: Permission denied. Cannot rename this file!");
        regs->eax = E_ACCES; return;
    }
    regs->eax = fs_rename(old_name, new_name, parent_id);
}

/**
 * @brief Function sys_mkdir
 */
void sys_mkdir(arch_regs_t *regs) {
    char name[MAX_FILENAME];
    if (!copy_user_string(name, (const char *)regs->ebx, sizeof(name))) { regs->eax = E_FAULT; return; }
    uint8_t parent_id = (uint8_t)regs->ecx;
    
    if (!check_vfs_access(parent_id, 1)) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "mkdir: Permission denied. No directory creation access!");
        regs->eax = E_ACCES; return;
    }
    regs->eax = fs_mkdir(name, parent_id);
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
 * @brief Function sys_get_dir_id
 */
void sys_get_dir_id(arch_regs_t *regs) {
    char path[MAX_FILENAME];
    if (!copy_user_string(path, (const char *)regs->ebx, sizeof(path))) { regs->eax = E_FAULT; return; }
    
    char basename[MAX_FILENAME];
    int parent_dir_id = vfs_resolve_path(path, (uint8_t)regs->ecx, basename);
    if (parent_dir_id < 0) { regs->eax = E_NOENT; return; }

    if (basename[0] == '\0' || (basename[0] == '.' && basename[1] == '\0')) {
        regs->eax = parent_dir_id; 
    }
    else if (basename[0] == '.' && basename[1] == '.' && basename[2] == '\0') {
        int final_id = 0; 
        for (int k = 0; k < MAX_FILES_IN_DIR; k++) {
            if (dir_table[k].entry_id == parent_dir_id && dir_table[k].file_type == 1 && dir_table[k].is_used == 1) {
                final_id = dir_table[k].parent_id;
                break;
            }
        }
        regs->eax = final_id;
    }
    else {
        int idx = fs_get_entry_idx(basename, parent_dir_id);
        if (idx != -1) {
            if (dir_table[idx].file_type == 1) regs->eax = dir_table[idx].entry_id;
            else regs->eax = E_NOTDIR;
        } else {
            regs->eax = E_NOENT;
        }
    }
    if (regs->eax != (uint32_t)E_NOENT && regs->eax != (uint32_t)E_NOTDIR && !check_vfs_access(regs->eax, 0)) {
        regs->eax = E_ACCES;
    }
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
    uint8_t parent_id = (uint8_t)regs->ecx;
    vfs_file_t file;
    
    if (fs_open(target_file, parent_id, &file) == 0) {
        int file_idx = fs_get_entry_idx(target_file, parent_id);
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
    uint8_t parent_id = (uint8_t)regs->ecx;
    vfs_file_t file;
    
    if (fs_open(target_file, parent_id, &file) == 0) {
        int file_idx = fs_get_entry_idx(target_file, parent_id);
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
