/*
 * File: sys_mount.c
 * Purpose: Choosing which disk the file system is on.
 *
 * This file is part of the esdumanOS test suite.
 *
 * A file of its own rather than two more functions in sys_fs.c, which is 1392
 * lines and already the largest of these. The subject is different too: nothing
 * here reads or writes a file, it decides which device the ones that do will
 * reach.
 *
 * What this is not is POSIX mount. There is one file system at a time and this
 * changes which device carries it - which is the word include/blockdev.h used
 * back in v1.2.0 when it said the second device would arrive with "a caller that
 * needs to choose between them". Holding two at once means every directory id
 * needs a file system to belong to, and every one of them is a bare int that
 * crosses the frozen syscall boundary. That is a release of its own.
 *
 * Almost all the machinery this needs already existed and was already exercised:
 * init_fs() has been safe to call twice since v1.3.0 - test_vfs.c does it
 * deliberately - fs_tables_free() releases what a mount allocated, and
 * blockdev_set_root() has been saved and restored by three test modules for nine
 * releases. What was missing was a name to look a device up by, and that is what
 * v1.11.0 added to blockdev.h.
 */
#include "syscalls_internal.h"
#include "types.h"
#include "registers.h"
#include "stdio.h"
#include "fs.h"
#include "process.h"
#include "errno.h"
#include "klog.h"
#include "uaccess.h"
#include "blockdev.h"
#include "bcache.h"
#include "libft.h"
#include "syscall.h"

/** @brief Longest device name this will accept from user space. */
#define MOUNT_NAME_MAX 32

/** @brief Buffer the rendered device list needs, terminator included. */
#define MOUNT_LIST_BUF (BLOCKDEV_MAX * 64 + 64)

/**
 * @brief The check every entry point here shares.
 *
 * Root only, and for a reason narrower than "storage is dangerous": mounting a
 * disk hands its directory table to a file system that will believe it. The
 * table is validated - validate_directory_table() has checked its invariants
 * since v0.10.0 - but validation is a floor, not a guarantee, and choosing whose
 * disk the system reads is an administrative act.
 */
static int mount_permitted(arch_regs_t *regs) {
    if (current_task == 0 || current_task->uid != 0) {
        klog(LOG_LEVEL_WARN, "MOUNT", "Permission denied: mount is root only.");
        regs->eax = E_PERM;
        return 0;
    }
    return 1;
}

/**
 * @brief Whether any process is holding a file open.
 *
 * Asked across every task rather than the caller's own, because the caller is
 * not the only one that would be reading a device that went away. A descriptor
 * pointing into a file system that has been unmounted is a descriptor whose next
 * read reaches whatever is mounted next - which is somebody else's disk, and is
 * a worse outcome than refusing to unmount.
 */
static int any_file_open(void) {
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->fd_table == 0) continue;

        for (uint32_t i = 0; i < p->fd_table_size; i++) {
            if (p->fd_table[i].type == FD_TYPE_FILE) return 1;
        }
    }
    return 0;
}

/**
 * @brief Renders the registered devices, marking the one in use.
 *
 * The same division of labour lspci and lsusb use: the kernel renders and the
 * program writes it out, so `mount > devices.txt` produces a file with something
 * in it.
 */
static int render_device_list(char *out, int cap) {
    blockdev_t *root = blockdev_root();
    int used = 0;

    if (out == 0 || cap <= 0) return 0;
    out[0] = '\0';

    if (blockdev_count() == 0) {
        return kbprintf(out, (uint32_t)cap, 0, "No block devices.\n");
    }

    for (int i = 0; i < blockdev_count(); i++) {
        blockdev_t *d = blockdev_get(i);

        if (d == 0) continue;

        used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                        "%s %d sectors (%d KB)%s\n",
                        d->name, d->sector_count, d->sector_count / 2,
                        (d == root) ? "  [mounted]" : "");
    }

    if (root == 0) {
        used = kbprintf(out, (uint32_t)cap, (uint32_t)used,
                        "Nothing is mounted.\n");
    }

    return used;
}

void sys_mount(arch_regs_t *regs) {
    const char *uname = (const char *)regs->ebx;

    if (!mount_permitted(regs)) return;

    /*
     * A null name asks rather than sets. Two behaviours in one call because they
     * are two questions about one thing - which disks are there, and which one
     * is in use - and because a diagnostic does not earn a syscall number of its
     * own while these two are still spending the numbers this table promised
     * eleven releases ago.
     */
    if (uname == 0) {
        void *ubuf = (void *)regs->ecx;
        uint32_t ucap = (uint32_t)regs->edx;

        if (ucap == 0) { regs->eax = E_INVAL; return; }
        if (!validate_user_writable_pointer(ubuf, ucap)) { regs->eax = E_FAULT; return; }

        char text[MOUNT_LIST_BUF];
        int n = render_device_list(text, sizeof(text));

        if ((uint32_t)n >= ucap) n = (int)ucap - 1;
        if (copy_to_user(ubuf, text, (size_t)n + 1) != E_OK) {
            regs->eax = E_FAULT;
            return;
        }

        regs->eax = n;
        return;
    }

    char name[MOUNT_NAME_MAX];

    if (!validate_string_pointer(uname, sizeof(name))) { regs->eax = E_FAULT; return; }
    if (copy_string_from_user(name, uname, sizeof(name)) != E_OK) {
        regs->eax = E_FAULT;
        return;
    }

    blockdev_t *target = blockdev_find(name);

    if (target == 0) {
        klog(LOG_LEVEL_WARN, "MOUNT", "No block device answers to that name.");
        regs->eax = E_NODEV;
        return;
    }

    if (target == blockdev_root()) {
        /* Already the one in use. Not an error, and not a reason to tear the
         * file system down and build it again - which would be a formatting
         * decision taken over a request that asked for nothing. */
        regs->eax = E_OK;
        return;
    }

    if (any_file_open()) {
        klog(LOG_LEVEL_WARN, "MOUNT", "Refusing to change disks with a file open.");
        regs->eax = E_BUSY;
        return;
    }

    /*
     * The order is the whole of the correctness here. What is on the old device
     * has to reach it before anything points elsewhere, and the tables the old
     * mount allocated have to go before new ones are sized - init_fs() allocates
     * from the superblock it finds, and a second allocation over the first would
     * both leak and describe the wrong disk.
     */
    blockdev_t *previous = blockdev_root();

    if (previous != 0) {
        bcache_flush();
        bcache_flush_dev(previous);
    }

    blockdev_set_root(target);
    init_fs();

    if (!fs_mounted) {
        /*
         * The new device carried nothing this kernel can read. init_fs() has
         * already said why - it refuses rather than formats, which is v1.2.0's
         * decision - and the disk that was working is put back, because leaving
         * the system with no file system over a mount that failed would turn a
         * refused request into a broken machine.
         */
        klog(LOG_LEVEL_ERROR, "MOUNT", "The device holds no file system this kernel can read.");
        blockdev_set_root(previous);
        init_fs();
        regs->eax = E_INVAL;
        return;
    }

    klog(LOG_LEVEL_INFO, "MOUNT", "File system moved to another device.");
    regs->eax = E_OK;
}

void sys_umount(arch_regs_t *regs) {
    if (!mount_permitted(regs)) return;

    blockdev_t *dev = blockdev_root();

    if (dev == 0) { regs->eax = E_NODEV; return; }

    if (any_file_open()) {
        klog(LOG_LEVEL_WARN, "MOUNT", "Refusing to unmount with a file open.");
        regs->eax = E_BUSY;
        return;
    }

    /*
     * Flushed, then dropped, then detached. bcache_flush_dev() does the first two
     * together because they belong together: a device that has been unmounted
     * may be unplugged, and a cached sector of a disk that is no longer there is
     * one nothing can ever reconcile.
     */
    bcache_flush();

    int res = bcache_flush_dev(dev);

    if (res != E_OK) {
        klog(LOG_LEVEL_ERROR, "MOUNT", "Refusing to unmount: the disk would not take its data.");
        regs->eax = res;
        return;
    }

    blockdev_set_root(0);
    init_fs();          /* with no device, this tears the tables down and mounts nothing */

    klog(LOG_LEVEL_INFO, "MOUNT", "File system unmounted.");
    regs->eax = E_OK;
}
