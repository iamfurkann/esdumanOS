/*
 * File: test_vfs.c
 * Purpose: VFS (Virtual File System) unit and security tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "syscall.h"
#include "fs.h"
#include "errno.h"
#include "libft.h"
#include "security.h"
#include "bcache.h"
static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

static inline int sys_create_file(const char *name, const char *content, int parent_id) { 
    return ktest_syscall(8, (int)name, (int)content, parent_id); 
}
static inline int sys_delete_file(const char *name, int parent_id) { 
    return ktest_syscall(22, (int)name, parent_id, 0); 
}
static inline int sys_mkdir(const char *name, int parent_id) { 
    return ktest_syscall(26, (int)name, parent_id, 0); 
}
static inline int sys_get_dir_id(const char *name, int parent_id) {
    return ktest_syscall(29, (int)name, parent_id, 0);
}
/*
 * Every string handed to a syscall has to sit in the simulated user window.
 * validate_string_pointer() rejects kernel addresses, so a literal or a stack
 * local produces E_FAULT rather than exercising anything - which is how the
 * depth test in this file came to pass while doing nothing at all.
 */
#define U_SCRATCH_PATH ((char *)0x500C00)

static inline int sys_chdir(const char *path) {
    ft_strcpy(U_SCRATCH_PATH, path);
    return ktest_syscall(SYSCALL_CHDIR, (int)U_SCRATCH_PATH, 0, 0);
}
static inline int sys_mkdir_u(const char *name, int parent_id) {
    ft_strcpy(U_SCRATCH_PATH, name);
    return ktest_syscall(26, (int)U_SCRATCH_PATH, parent_id, 0);
}
static inline int sys_get_dir_id_u(const char *name) {
    ft_strcpy(U_SCRATCH_PATH, name);
    return ktest_syscall(29, (int)U_SCRATCH_PATH, 0, 0);
}

/**
 * @brief Tests that fs_size() reports readable bytes rather than on-disk bytes.
 *
 * The directory table records what a file occupies on the disk. Under
 * SEC_LEVEL_CRYPTO_ENFORCED, which security.c makes the default, that is an IV,
 * a header and the plaintext padded up to an AES block - so it is larger than
 * what read() returns, always, for every regular file in the system. stat() and
 * lseek(SEEK_END) are built on fs_size() precisely so they do not inherit that
 * discrepancy.
 *
 * The gap itself is asserted rather than assumed: if encryption were ever to
 * stop padding, or fs_size() were quietly changed to return dir_table's value,
 * a test that only checked "size == strlen(content)" would still pass while the
 * reason the function exists had evaporated.
 *
 * Expected Behavior:
 * - fs_size() returns exactly the number of bytes written.
 * - fs_read() hands back that same number, so the two agree.
 * - With encryption on, the on-disk size is strictly larger.
 *
 * Edge Cases Covered:
 * - A file too short to hold the header reports 0 rather than a negative or a
 *   wrapped length.
 * - Null arguments are rejected instead of dereferenced.
 */
static void test_vfs_size_reporting(void) {
    static const char payload[] = "esdumanOS fs_size probe: the length here is what read() must return.";
    const uint32_t payload_len = (uint32_t)(sizeof(payload) - 1);

    int created = fs_create_file("sizeprobe.txt", (const uint8_t *)payload, payload_len, 0);
    KTEST_ASSERT(created >= 0, "[VFS] fs_size probe file created");

    vfs_file_t f;
    int opened = fs_open("sizeprobe.txt", 0, &f);
    KTEST_ASSERT(opened == E_OK, "[VFS] fs_size probe file opened");

    if (opened == E_OK) {
        uint32_t reported = 0xFFFFFFFFu;
        int rc = fs_size(&f, &reported);

        KTEST_ASSERT(rc == E_OK, "[VFS] fs_size() succeeds on an open file");
        KTEST_ASSERT(reported == payload_len,
                     "[STRICT] [VFS] fs_size() reports the bytes written, not the bytes on disk");

        if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
            /*
             * 16 bytes of IV plus a 40-byte header, then padding. The exact
             * total is not the point; that it differs at all is.
             */
            KTEST_ASSERT(f.file_size > reported + 40,
                         "[STRICT] [VFS] the on-disk size is larger, which is why fs_size() exists");
        }

        /* And the number fs_size() gave is the number a read actually yields. */
        uint8_t readback[128];
        for (uint32_t i = 0; i < sizeof(readback); i++) readback[i] = 0;

        f.current_offset = 0;
        int got = fs_read(&f, readback, sizeof(readback));
        KTEST_ASSERT(got == (int)reported,
                     "[STRICT] [VFS] fs_read() returns exactly the count fs_size() predicted");

        int same = 1;
        for (uint32_t i = 0; i < payload_len; i++) {
            if (readback[i] != (uint8_t)payload[i]) same = 0;
        }
        KTEST_ASSERT(same, "[VFS] the bytes read back match what was written");
    }

    KTEST_ASSERT(fs_size(0, 0) < 0, "[VFS] fs_size() rejects null arguments");

    /*
     * A file with no room for the IV and header. fs_read_encrypted() already
     * treats this as empty, and fs_size() has to agree with it rather than
     * underflow the subtraction that derives the maximum plausible length.
     */
    uint32_t tiny_size = 0xFFFFFFFFu;
    vfs_file_t tiny;
    for (uint32_t i = 0; i < sizeof(tiny.filename); i++) tiny.filename[i] = '\0';
    tiny.file_size = 8;
    tiny.current_offset = 0;
    tiny.start_sector = FAT_EOF;
    tiny.ref_count = 1;
    tiny.inode_idx = -1;

    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        KTEST_ASSERT(fs_size(&tiny, &tiny_size) == E_OK && tiny_size == 0,
                     "[STRICT] [VFS] a file too short to hold the header reports size 0");
    } else {
        KTEST_ASSERT(fs_size(&tiny, &tiny_size) == E_OK && tiny_size == 8,
                     "[VFS] unencrypted, fs_size() is the on-disk size");
    }

    fs_delete("sizeprobe.txt", 0);
}

/**
 * @brief Tests the bounds on buffered writes.
 *
 * The Ring 3 half covers the semantics - commit on the last close, truncation on
 * open, the dup2 case. What it cannot cover cheaply is the size cap: reaching it
 * from user space takes 256 syscalls and then commits a 64 KB file. Called
 * directly, the refusal costs nothing, because fs_write_buffered() checks the
 * size before it allocates anything.
 *
 * Expected Behavior:
 * - A single write larger than the cap is refused with E_FBIG.
 * - A write that would carry an already-partly-filled buffer past the cap is
 *   refused too, rather than only the first oversized one.
 * - Null arguments are rejected instead of dereferenced.
 * - Committing a file that was never opened for writing does nothing and
 *   reports success.
 *
 * Edge Cases Covered:
 * - A zero-length write, which must succeed without allocating.
 */
static void test_vfs_write_bounds(void) {
    vfs_file_t probe;
    for (uint32_t i = 0; i < sizeof(probe.filename); i++) probe.filename[i] = '\0';
    probe.file_size = 0;
    probe.current_offset = 0;
    probe.start_sector = FAT_EOF;
    probe.ref_count = 1;
    probe.inode_idx = -1;
    probe.write_buf = 0;
    probe.write_len = 0;
    probe.write_cap = 0;
    probe.dirty = 0;

    static const uint8_t chunk[64] = { 'x' };

    KTEST_ASSERT(fs_write_buffered(&probe, chunk, 0) == 0,
                 "[WRITE] a zero-length write succeeds and allocates nothing");
    KTEST_ASSERT(probe.write_buf == 0,
                 "[STRICT] [WRITE] and really did not allocate");

    KTEST_ASSERT(fs_write_buffered(&probe, chunk, MAX_FILE_WRITE_BYTES + 1) == E_FBIG,
                 "[STRICT] [WRITE] a single write past the cap is refused");

    KTEST_ASSERT(fs_write_buffered(&probe, chunk, 64) == 64,
                 "[WRITE] a normal write is accepted");
    KTEST_ASSERT(probe.write_len == 64 && probe.dirty == 1,
                 "[STRICT] [WRITE] and marks the file dirty");

    /* Already holding 64 bytes, so a write of the full cap must not fit. */
    KTEST_ASSERT(fs_write_buffered(&probe, chunk, MAX_FILE_WRITE_BYTES) == E_FBIG,
                 "[STRICT] [WRITE] the cap accounts for what is already buffered");

    KTEST_ASSERT(fs_write_buffered(0, chunk, 8) < 0 && fs_write_buffered(&probe, 0, 8) < 0,
                 "[WRITE] null arguments are rejected");

    /* inode_idx is -1, so a commit cannot resolve a parent; it must fail rather
     * than index dir_table out of range, and must still release the buffer. */
    KTEST_ASSERT(fs_commit_writes(&probe) < 0,
                 "[STRICT] [WRITE] committing a file with no directory entry is refused");
    KTEST_ASSERT(probe.write_buf == 0 && probe.dirty == 0,
                 "[STRICT] [WRITE] and the buffer is released even when the commit fails");

    KTEST_ASSERT(fs_commit_writes(&probe) == E_OK,
                 "[WRITE] committing a file with nothing buffered is a no-op");
}

/**
 * @brief Deleting a directory that still holds something.
 *
 * A child records its parent's entry id, and fs_delete() used to clear the
 * parent's slot and stop there. The children stayed behind pointing at an id
 * that no longer described anything: unreachable through any path, and visible
 * again as somebody else's contents the moment the slot was reused. Nothing
 * tested it because nothing had reason to delete a directory.
 *
 * An empty one still goes, which is what makes this rmdir(2) rather than a
 * refusal to remove directories at all - there is no separate rmdir call here,
 * so refusing outright would leave a directory with no way to remove it.
 */
static void test_vfs_rmdir_semantics(void) {
    printk("\n--- VFS: directory deletion ---\n");

    KTEST_ASSERT(fs_mkdir("rmdirprobe", 0) == E_OK,
                 "[VFS] a directory was created for the deletion checks");

    int dir_id = fs_get_entry_idx("rmdirprobe", 0);
    KTEST_ASSERT(dir_id > 0, "[VFS] and it resolves to an entry id");

    if (dir_id > 0) {
        const char *payload = "occupied";
        KTEST_ASSERT(fs_create_file("inside.txt", (const uint8_t *)payload, 8, (fs_id_t)dir_id) >= 0,
                     "[VFS] a file was created inside it");

        KTEST_ASSERT(fs_delete("rmdirprobe", 0) == E_NOTEMPTY,
                     "[STRICT] [VFS] deleting a directory that still holds a file is refused");
        KTEST_ASSERT(fs_get_entry_idx("rmdirprobe", 0) == dir_id,
                     "[STRICT] [VFS] the refused delete left the directory in place");
        KTEST_ASSERT(fs_get_entry_idx("inside.txt", (fs_id_t)dir_id) > 0,
                     "[STRICT] [VFS] and left its contents reachable rather than orphaned");

        KTEST_ASSERT(fs_delete("inside.txt", (fs_id_t)dir_id) == E_OK,
                     "[VFS] the file inside deletes normally");
        KTEST_ASSERT(fs_delete("rmdirprobe", 0) == E_OK,
                     "[STRICT] [VFS] an empty directory deletes");
        KTEST_ASSERT(fs_get_entry_idx("rmdirprobe", 0) < 0,
                     "[VFS] and is gone afterwards");
    }
}


/**
 * @brief Tests VFS directory boundaries, depth limits, and prevents path traversal (OOB).
 *
 * This test evaluates the robustness of the Virtual File System (VFS) against
 * out-of-bounds inputs, excessive directory nesting, circular references, and
 * malicious path traversal attempts.
 * 
 * Expected Behavior:
 * - Nested directory creation up to defined limits should succeed, beyond which bounds are checked.
 * - 'cd ..' traversal out of deep directories must safely resolve back to the root without violating memory bounds.
 * - Requests pointing to invalid identifiers (e.g., 35, 255) must be rejected.
 * - Path traversal attempts like "../../etc/shadow" must be intercepted and denied.
 *
 * Edge Cases Covered:
 * - Infinite loop paths (a directory pointing to itself as a parent).
 * - Broken chain paths (a directory whose parent does not exist).
 * - Buffer boundary overflows.
 */
void test_vfs_boundary_and_depth(void) {
    printk("\n--- VFS Boundary and Depth (OOB) Tests ---\n");

    /*
     * The nesting is driven by chdir() now. mkdir() no longer takes a parent
     * directory from the caller - it creates relative to the process's working
     * directory - so descending has to be done by actually moving there.
     *
     * Written this way deliberately rather than passing each new id along: with
     * the parent argument ignored, the old loop still created 15 directories and
     * still passed, but every one of them landed flat in root. The assertions
     * below would have stayed green while testing nothing.
     */
    /*
     * Names must live in the simulated user window, not on the kernel stack.
     * validate_string_pointer() rejects kernel addresses since the test-mode
     * relaxation was removed, so the "char dir_name[8]" local this loop used to
     * pass made every sys_mkdir() return E_FAULT. The loop then broke on its
     * first iteration with depth_reached == 0, current_parent == 0, and both
     * assertions below passed vacuously - the backtrack has nothing to walk when
     * it starts at root. The depth assertion added here is what exposed it.
     */
    char *u_dir_name = (char *)0x500B00;

    sys_chdir("/");

    int current_parent = 0; // Root ID (0)
    int depth_reached = 0;

    for (int i = 0; i < 15; i++) {
        u_dir_name[0] = 'd';
        u_dir_name[1] = '0' + (i % 10);
        u_dir_name[2] = '\0';

        int res = sys_mkdir(u_dir_name, 0);
        if (res != 0) break; // Reached maximum directory depth or table limit.

        int new_id = sys_get_dir_id(u_dir_name, 0);
        if (new_id <= 0 || new_id >= 255) break; // Validate ID boundaries.

        if (sys_chdir(u_dir_name) != E_OK) break;

        current_parent = new_id;
        depth_reached++;
    }

    KTEST_ASSERT(depth_reached >= 10,
                 "[STRICT] chdir-driven nesting really descended (flat creation would not)");

    sys_chdir("/");

    int backtrack_id = current_parent;
    int steps_back = 0;
    int is_corrupted = 0;

    while (backtrack_id != 0) {
        int next_parent = -1;
        /*
         * The whole table, not a hardcoded 32. MAX_FILES_IN_DIR is 256; the
         * comment that used to sit here claiming otherwise was stale, and the
         * short scan missed every entry past index 32 - which is where a boot
         * has already put the directories this test creates. It never showed
         * because the nesting loop above was failing before it produced any.
         */
        for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
            if (dir_table[i].is_used && dir_table[i].entry_id == backtrack_id) {
                next_parent = dir_table[i].parent_id;
                break;
            }
        }

        if (next_parent == -1) {
            is_corrupted = 1; break; // Parent not found (Broken Chain)
        }
        if (next_parent == backtrack_id && backtrack_id != 0) {
            is_corrupted = 2; break; // Infinite Loop (Points to itself)
        }
        
        backtrack_id = next_parent;
        steps_back++;
        // Ensure we do not step back further than we stepped in, which would indicate a cycle.
        if (steps_back > depth_reached + 1) {
            is_corrupted = 3; break; // Too many steps (Out of bounds)
        }
    }

    KTEST_ASSERT(is_corrupted == 0, "[STRICT] 'cd ..' in deep directories does not violate memory");
    KTEST_ASSERT(backtrack_id == 0, "[STRICT] 'cd ..' chain successfully reached Root (0)");

    /*
     * These used to pass 35 and 255 as parent directory ids and assert that the
     * VFS rejected them, because an unvalidated id from user space could create
     * an entry under a parent that did not exist (K-10).
     *
     * The argument is no longer read: mkdir() resolves against the working
     * directory. The rejection they checked for cannot happen because the input
     * that caused it is gone, so asserting a rejection would now fail. What is
     * worth asserting instead is that the stale argument has no influence -
     * both calls must land in the same directory, the one we are standing in.
     */
    sys_chdir("/");
    int oob_res1 = sys_mkdir_u("oob_test1", 35);
    int oob_res2 = sys_mkdir_u("oob_test2", 255);

    KTEST_ASSERT(oob_res1 == 0 && oob_res2 == 0,
                 "[SECURITY] VFS ignores the obsolete parent-id argument");
    KTEST_ASSERT(sys_get_dir_id_u("/oob_test1") > 0 && sys_get_dir_id_u("/oob_test2") > 0,
                 "[STRICT] [SECURITY] both landed in the working directory, not under a bogus id");

    char *trav_path = (char *)0x500A00;
    ft_strcpy(trav_path, "../../etc/shadow");
    // Attempt to open the traversed path with read-only permissions to test bounds checking.
    int trav_fd = ktest_syscall(37, (int)trav_path, 0, 0); // O_RDONLY (0)
    KTEST_ASSERT(trav_fd < 0, "[SECURITY] VFS rejected Path Traversal (../../etc/shadow) request");
}

/**
 * @brief Main execution function for all Virtual File System (VFS) unit and security tests.
 *
 * This function orchestrates fundamental operations within the VFS, ensuring that
 * creation, identification, and deletion mechanisms function properly before
 * diving into the advanced boundary and depth tests.
 *
 * Expected Behavior:
 * - A valid directory is successfully created and correctly identified via sys_get_dir_id.
 * - Nonexistent directories appropriately return -1 indicating failure.
 * - Standard file creation and deletion processes execute correctly and return success indicators.
 * - Finally, triggers the advanced test_vfs_boundary_and_depth subroutine.
 */
/**
 * @brief Verifies the on-disk format: its shape, its geometry and its wide ids.
 *
 * Expected behavior:
 * - A directory entry is exactly the size the format says, on disk and in memory.
 * - The superblock describes a geometry the regions actually fit inside.
 * - An entry id past 255 survives being written and read back.
 *
 * Edge cases covered:
 * - A name one byte too long, which must be refused rather than shortened.
 * - A path component too long, which used to be silently truncated into a
 *   different name.
 *
 * Volume is deliberately not what this tests. Proving the table holds more than
 * 256 entries by creating 256 files would spend twenty thousand sector writes to
 * demonstrate something that can only fail one way - a field too narrow to carry
 * the number - and that is asserted directly instead.
 */
static void test_disk_format(void) {
    /* ------------------------------------------------------------------
     * The shape. packed is a request, and a compiler that declined it would
     * produce a kernel that writes disks no other build can read.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(sizeof(disk_file_entry_t) == 96,
                 "[STRICT] [VFS] a directory entry is exactly 96 bytes");
    KTEST_ASSERT(sizeof(((disk_file_entry_t *)0)->entry_id) == 2 &&
                 sizeof(((disk_file_entry_t *)0)->parent_id) == 2,
                 "[STRICT] [VFS] and its ids are two bytes, not one");
    KTEST_ASSERT(sizeof(disk_superblock_t) <= 512,
                 "[STRICT] [VFS] the superblock fits in the sector it lives in");
    KTEST_ASSERT(MAX_FILENAME < MAX_PATH,
                 "[VFS] a path can hold more than one name's worth of characters");

    /* ------------------------------------------------------------------
     * The geometry, as the mounted superblock reports it.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(fs_mounted == 1, "[VFS] a file system is mounted");
    KTEST_ASSERT(fs_super.magic == FS_SUPER_MAGIC &&
                 fs_super.format_version == FS_FORMAT_VERSION,
                 "[STRICT] [VFS] the superblock says what it is and which version");
    KTEST_ASSERT(fs_super.entry_size == sizeof(disk_file_entry_t) &&
                 fs_super.max_entries <= MAX_FILES_IN_DIR,
                 "[STRICT] [VFS] and describes entries this build agrees with");
    KTEST_ASSERT(fs_super.dir_start < fs_super.fat_start &&
                 fs_super.fat_start < fs_super.data_start,
                 "[VFS] the regions are in order and do not overlap");

    /*
     * Each region is large enough for what it has to hold. Getting this wrong
     * does not fail loudly: the directory table would run over the start of the
     * FAT and each would quietly corrupt the other.
     */
    KTEST_ASSERT((uint32_t)fs_super.entry_size * fs_super.max_entries
                     <= fs_super.dir_sectors * 512u,
                 "[STRICT] [VFS] the directory table fits in the sectors reserved for it");
    KTEST_ASSERT(fs_super.dir_start + fs_super.dir_sectors <= fs_super.fat_start,
                 "[STRICT] [VFS] and stops before the allocation table starts");
    KTEST_ASSERT(fs_max_sectors * sizeof(uint32_t)
                     <= (fs_super.data_start - fs_super.fat_start) * 512u,
                 "[STRICT] [VFS] the allocation table fits before the data does");

    /* ------------------------------------------------------------------
     * An id past what a byte holds. This is the whole of what widening them
     * was for, and the way it fails is truncation - which is silent.
     * ------------------------------------------------------------------ */
    {
        int slot = -1;

        for (int i = 1; i < MAX_FILES_IN_DIR; i++) {
            if (dir_table[i].is_used == 0) { slot = i; break; }
        }
        KTEST_ASSERT(slot > 0, "[VFS] the directory table has room to test with");

        if (slot > 0) {
            uint8_t sec_buf[512];
            uint32_t byte_off = (uint32_t)slot * fs_super.entry_size;
            uint32_t sector = fs_super.dir_start + byte_off / 512u;
            uint32_t within = byte_off % 512u;
            disk_file_entry_t readback;

            ft_memset(&dir_table[slot], 0, sizeof(disk_file_entry_t));
            ft_strcpy(dir_table[slot].filename, "wide_id_dir");
            dir_table[slot].is_used = 1;
            dir_table[slot].file_type = FT_DIR;
            dir_table[slot].entry_id = 300;
            dir_table[slot].parent_id = 0;
            dir_table[slot].mode = FS_MODE_DEFAULT_DIR;

            KTEST_ASSERT(dir_table[slot].entry_id == 300,
                         "[STRICT] [VFS] an entry id of 300 is still 300 in memory");
            KTEST_ASSERT(fs_dir_exists(300) == 1,
                         "[STRICT] [VFS] and a lookup finds the directory it names");
            KTEST_ASSERT(fs_get_entry_idx("wide_id_dir", 0) == slot,
                         "[VFS] which is reachable by name from the root");

            /*
             * And it survives the disk. An entry only 96 bytes wide in memory
             * proves nothing about what lands in the sector; this reads the
             * bytes back out of the block the entry was written into.
             */
            fs_create_file_raw("wide_probe", (const uint8_t *)"x", 1, 300);
            bcache_read_sector(sector, sec_buf);
            ft_memcpy(&readback, &sec_buf[within], sizeof(disk_file_entry_t));

            KTEST_ASSERT(readback.entry_id == 300 && readback.parent_id == 0,
                         "[STRICT] [VFS] an id past 255 survives the trip to the sector and back");
            KTEST_ASSERT(fs_get_entry_idx("wide_probe", 300) >= 0,
                         "[STRICT] [VFS] and a file can be created inside a directory with one");

            /*
             * And it is still seen to hold that file. fs_delete()'s emptiness
             * check compared a child's parent_id against the parent's slot index
             * narrowed to a byte - two mistakes that agree below 256 and stop
             * agreeing above it. A directory that looked empty was deleted with
             * its children still in the table, pointing at an id nothing answered
             * to any more: the exact orphaning the check exists to prevent,
             * arrived at through the check itself.
             */
            KTEST_ASSERT(fs_delete("wide_id_dir", 0) == E_NOTEMPTY,
                         "[STRICT] [VFS] a directory with a wide id is not mistaken for an empty one");
            KTEST_ASSERT(fs_get_entry_idx("wide_probe", 300) >= 0,
                         "[STRICT] [VFS] and the refused delete left its contents where they were");

            /*
             * Cleared first, deleted second, and the order is the point: the
             * clear only touches memory, and fs_delete() is what writes the
             * table back. Doing it the other way round would leave the synthetic
             * directory on the disk for every module that runs after this one.
             */
            ft_memset(&dir_table[slot], 0, sizeof(disk_file_entry_t));
            fs_delete("wide_probe", 300);

            KTEST_ASSERT(fs_dir_exists(300) == 0 &&
                         fs_get_entry_idx("wide_probe", 300) == -1,
                         "[STRICT] [VFS] and the module leaves neither behind");
        }
    }

    /* ------------------------------------------------------------------
     * A name too long is refused. It used to be truncated, which at 256 bytes
     * was unreachable and at 64 is a name somebody might actually type - and
     * the result is not an error, it is a different file.
     * ------------------------------------------------------------------ */
    {
        char just_fits[MAX_FILENAME];
        char one_too_many[MAX_FILENAME + 2];
        int i;

        for (i = 0; i < MAX_FILENAME - 1; i++) just_fits[i] = 'a';
        just_fits[MAX_FILENAME - 1] = '\0';

        for (i = 0; i < MAX_FILENAME; i++) one_too_many[i] = 'b';
        one_too_many[MAX_FILENAME] = '\0';

        KTEST_ASSERT(fs_create_file_raw(just_fits, (const uint8_t *)"x", 1, 0) == E_OK,
                     "[VFS] a name of exactly the longest allowed length is accepted");
        KTEST_ASSERT(fs_create_file_raw(one_too_many, (const uint8_t *)"x", 1, 0) == E_INVAL,
                     "[STRICT] [VFS] and one byte more is refused rather than shortened");

        fs_delete(just_fits, 0);
    }
}

/**
 * @brief Verifies the permissions the system puts on its own paths.
 *
 * Expected behavior:
 * - /etc/shadow is readable by root alone.
 * - /tmp is writable by everyone, /root by nobody else.
 * - The rest of the top level is readable and searchable, and not writable.
 *
 * These are applied at boot rather than only at creation, and this asserts the
 * result rather than the call. A disk written by v0.9.0 carries the default 0644
 * on everything it created, /etc/shadow included, because that release enforced
 * no modes at all - so a kernel that started enforcing them and only stamped new
 * entries would hand the password database to every user on the first mount. The
 * one thing worth checking is that after boot, the modes are right whatever the
 * disk arrived holding.
 */
static void test_system_modes(void) {
    int etc_id = fs_get_entry_idx("etc", 0);
    int idx;

    idx = fs_get_entry_idx("tmp", 0);
    KTEST_ASSERT(idx >= 0 && (dir_table[idx].mode & FS_MODE_PERM_MASK) == 0777,
                 "[STRICT] [VFS] /tmp is writable by everyone, which is what it is for");

    idx = fs_get_entry_idx("root", 0);
    KTEST_ASSERT(idx >= 0 && (dir_table[idx].mode & FS_MODE_PERM_MASK) == 0700,
                 "[STRICT] [VFS] /root is root's alone");

    idx = fs_get_entry_idx("bin", 0);
    KTEST_ASSERT(idx >= 0 && (dir_table[idx].mode & FS_MODE_PERM_MASK) == 0755,
                 "[VFS] /bin can be read and searched, and not written");

    if (etc_id >= 0) {
        int etc_entry = dir_table[etc_id].entry_id;

        idx = fs_get_entry_idx("shadow", (fs_id_t)etc_entry);
        KTEST_ASSERT(idx >= 0 && (dir_table[idx].mode & FS_MODE_PERM_MASK) == 0600,
                     "[STRICT] [VFS] /etc/shadow is readable by root alone");
        KTEST_ASSERT(idx < 0 || dir_table[idx].owner_uid == 0,
                     "[STRICT] [VFS] and owned by root, so 0600 means what it should");

        idx = fs_get_entry_idx("passwd", (fs_id_t)etc_entry);
        KTEST_ASSERT(idx < 0 || (dir_table[idx].mode & FS_MODE_PERM_MASK) == 0644,
                     "[VFS] /etc/passwd is readable by everyone, as it carries no secrets");
    }

    /* And the rule the modes exist to feed, asserted through the same function
     * the VFS decides with rather than by restating it. */
    idx = fs_get_entry_idx("root", 0);
    if (idx >= 0) {
        KTEST_ASSERT(fs_mode_allows(dir_table[idx].mode, dir_table[idx].owner_uid,
                                    dir_table[idx].owner_gid, 1000, 1000, FS_WANT_EXEC) == 0,
                     "[STRICT] [VFS] a user cannot enter /root");
    }
    idx = fs_get_entry_idx("tmp", 0);
    if (idx >= 0) {
        KTEST_ASSERT(fs_mode_allows(dir_table[idx].mode, dir_table[idx].owner_uid,
                                    dir_table[idx].owner_gid, 1000, 1000,
                                    FS_WANT_WRITE | FS_WANT_EXEC) == 1,
                     "[STRICT] [VFS] and can write in /tmp");
    }
}

void run_vfs_tests(void) {
    printk("\n--- VFS (Virtual File System) Tests ---\n");

    char *u_dir = (char *)0x500000;
    char *u_fake = (char *)0x500100;
    char *u_file = (char *)0x500200;
    char *u_content = (char *)0x500300;
    
    // Copy the test data into the allocated memory areas.
    ft_strcpy(u_dir, "test_dir");
    ft_strcpy(u_fake, "nonexistent_folder");
    ft_strcpy(u_file, "test.txt");
    ft_strcpy(u_content, "Hello Test");

    int mkdir_res = sys_mkdir(u_dir, 0);
    KTEST_ASSERT(mkdir_res >= 0, "[STRICT] sys_mkdir successfully created new folder");

    int dir_id = sys_get_dir_id(u_dir, 0);
    KTEST_ASSERT(dir_id >= 0, "[STRICT] sys_get_dir_id returned valid ID");

    int fake_dir = sys_get_dir_id(u_fake, 0);
    KTEST_ASSERT(fake_dir == E_NOENT, "[STRICT] sys_get_dir_id returns E_NOENT for nonexistent folder");

    int file_res = sys_create_file(u_file, u_content, 0);
    KTEST_ASSERT(file_res >= 0, "[STRICT] sys_create_file successfully wrote virtual file");

    int del_res = sys_delete_file(u_file, 0);
    KTEST_ASSERT(del_res >= 0, "[STRICT] sys_delete_file successfully deleted the file");

    test_vfs_boundary_and_depth();
    test_vfs_size_reporting();
    test_vfs_write_bounds();
    test_vfs_rmdir_semantics();
    test_disk_format();
    test_system_modes();
}