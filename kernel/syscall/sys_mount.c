/* Volume administration. The boot root and its open files stay live when
 * another device is attached at /mnt. */
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

static int render_device_list(char *out, int cap) {
    blockdev_t *root = fs_mounted ? fs_root_volume.device : 0;
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
                        (d == root) ? "  [mounted] /" :
                        (d == fs_secondary_device()) ? "  [mounted] /mnt" : "");
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
    if (copy_string_from_user(name, uname, sizeof(name)) != E_OK) {
        regs->eax = E_FAULT;
        return;
    }
    blockdev_t *target = blockdev_find(name);
    if (!target) { regs->eax = E_NODEV; return; }

    char pass[KEYSLOT_MAX_PASSPHRASE] = {0};
    if (regs->edx == MOUNT_UNLOCK_V1 && regs->ecx &&
        copy_string_from_user(pass, (const char *)regs->ecx, sizeof(pass)) != E_OK)
        regs->eax = E_FAULT;
    else regs->eax = fs_attach(target, pass);
    volatile char *wipe = pass;
    for (unsigned i = 0; i < sizeof(pass); i++) wipe[i] = 0;
}

void sys_umount(arch_regs_t *regs) {
    if (mount_permitted(regs)) regs->eax = fs_detach();
}

void sys_mkfs(arch_regs_t *regs) {
    if (!mount_permitted(regs)) return;
    char name[MOUNT_NAME_MAX], pass[KEYSLOT_MAX_PASSPHRASE] = {0};
    if (copy_string_from_user(name, (const char *)regs->ebx, sizeof(name)) != E_OK ||
        copy_string_from_user(pass, (const char *)regs->ecx, sizeof(pass)) != E_OK)
        regs->eax = E_FAULT;
    else {
        blockdev_t *device = blockdev_find(name);
        regs->eax = device ? fs_format_data_device(device, regs->edx, pass) : E_NODEV;
    }
    volatile char *wipe = pass;
    for (unsigned i = 0; i < sizeof(pass); i++) wipe[i] = 0;
}
