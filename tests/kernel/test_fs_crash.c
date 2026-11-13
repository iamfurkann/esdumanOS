/* Opt-in power-cut probe. The host harness supplies a new disposable image for
 * each case and kills QEMU after the selected driver write completes. This
 * module is never in the ordinary module list or in the production kernel. */
#include "ktest.h"
#include "fs.h"
#include "bcache.h"
#include "blockdev.h"
#include "errno.h"
#include "libft.h"
#include "stdio.h"

static blockdev_t *backing;
static blockdev_t observed;
static uint32_t cut_lba;
static const char *cut_name;

static void stopped(const char *name) {
    printk("[CRASH] STOP %s\n", name);
    for (;;) asm volatile("cli; hlt");
}

static int observed_read(void *ctx, uint32_t lba, uint8_t *buf) {
    (void)ctx;
    return blockdev_read(backing, lba, buf);
}

static int observed_write(void *ctx, uint32_t lba, const uint8_t *buf) {
    (void)ctx;
    int rc = blockdev_write(backing, lba, buf);
    if (rc == E_OK && lba == cut_lba) stopped(cut_name);
    return rc;
}

static void verify_reopen(int require_new) {
    int readable = 0, valid = 1, saw_new = 0;
    const char *names[] = {"crash-target", "crash-renamed"};
    for (unsigned n = 0; n < 2; n++) {
        vfs_file_t file;
        int rc = fs_open(names[n], 0, &file);
        if (rc == E_NOENT) continue;
        uint8_t bytes[4] = {0};
        if (rc != E_OK || fs_read(&file, bytes, sizeof(bytes)) != 4) { valid = 0; continue; }
        if (ft_memcmp(bytes, "new!", 4) == 0) saw_new = 1;
        else if (ft_memcmp(bytes, "old!", 4) != 0) valid = 0;
        readable++;
    }
    KTEST_ASSERT(valid && (!require_new || (readable == 1 && saw_new)),
                 "[CRASH] reopened named payloads authenticate and contain a complete version");
    printk("[CRASH] REOPEN readable=%d new=%d valid=%d\n", readable, saw_new, valid);
    ktest_finish();
}

void run_fs_crash_probe(const char *scenario) {
    if (ft_strcmp(scenario, "verify") == 0) { verify_reopen(0); return; }
    if (ft_strcmp(scenario, "verify-synced") == 0) { verify_reopen(1); return; }
    int create = ft_strcmp(scenario, "create-fat") == 0 ||
                 ft_strcmp(scenario, "create-dir") == 0;
    int update = ft_strcmp(scenario, "update-dir") == 0;
    int remove = ft_strcmp(scenario, "delete-dir") == 0;
    int rename = ft_strcmp(scenario, "rename-dir") == 0;
    int synced = ft_strcmp(scenario, "synced") == 0;
    if ((!create && !update && !remove && !rename && !synced) || !fs_mounted) {
        KTEST_ASSERT(0, "[CRASH] known scenario and mounted disposable root required");
        ktest_finish();
        return;
    }
    if (fs_get_entry_idx("crash-target", 0) >= 0 || fs_get_entry_idx("crash-renamed", 0) >= 0) {
        KTEST_ASSERT(0, "[CRASH] fixture must be freshly provisioned");
        ktest_finish();
        return;
    }
    if (!create && fs_create_file("crash-target", (const uint8_t *)"old!", 4, 0) != E_OK) {
        KTEST_ASSERT(0, "[CRASH] baseline file created");
        ktest_finish();
        return;
    }
    if (fs_sync_all() != E_OK || bcache_flush_dev(fs_root_volume.device) != E_OK) {
        KTEST_ASSERT(0, "[CRASH] baseline reached the backing driver");
        ktest_finish();
        return;
    }
    int slot = fs_get_entry_idx("crash-target", 0);
    if (slot < 0) {
        for (int i = 1; i < fs_max_entries; i++) {
            if (!dir_table[i].is_used) { slot = i; break; }
        }
    }
    if (slot < 0) { KTEST_ASSERT(0, "[CRASH] a file slot is available"); ktest_finish(); return; }

    backing = fs_root_volume.device;
    observed = *backing;
    observed.name = "crash-observer";
    observed.read = observed_read;
    observed.write = observed_write;
    cut_name = scenario;
    cut_lba = ft_strcmp(scenario, "create-fat") == 0 ? fs_part_start + fs_super.fat_start :
              fs_part_start + fs_super.dir_start + ((uint32_t)slot * sizeof(disk_file_entry_t) +
                                                   sizeof(disk_file_entry_t) - 1) / 512;
    if (synced) cut_lba = 0xffffffffu;
    fs_root_volume.device = &observed;
    int rc;
    if (create) rc = fs_create_file("crash-target", (const uint8_t *)"new!", 4, 0);
    else if (remove) rc = fs_delete("crash-target", 0);
    else if (rename) rc = fs_rename("crash-target", "crash-renamed", 0);
    else rc = fs_atomic_update("crash-target", (const uint8_t *)"new!", 4, 0);
    if (rc == E_OK) rc = fs_sync_all();
    if (synced && rc == E_OK) stopped("synced");
    KTEST_ASSERT(0, "[CRASH] requested physical write boundary was reached");
    ktest_finish();
}
