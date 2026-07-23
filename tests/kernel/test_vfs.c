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

extern void ft_strcpy(char *dest, const char *src);
extern int fs_mkdir(const char *name, uint8_t parent_id);
extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern disk_file_entry_t dir_table[];

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
    serial_print("\n--- VFS Boundary and Depth (OOB) Tests ---\n");

    int current_parent = 0; // Root ID (0)
    int depth_reached = 0;
    char dir_name[8] = "d0";

    for (int i = 0; i < 15; i++) {
        dir_name[1] = '0' + (i % 10); 
        
        // Attempt to create the nested directory.
        int res = sys_mkdir(dir_name, current_parent);
        if (res != 0) break; // Reached maximum directory depth or table limit.

        // Retrieve the newly created directory's ID.
        int new_id = sys_get_dir_id(dir_name, current_parent);
        if (new_id <= 0 || new_id >= 255) break; // Validate ID boundaries.

        current_parent = new_id;
        depth_reached++;
    }

    int backtrack_id = current_parent;
    int steps_back = 0;
    int is_corrupted = 0;

    while (backtrack_id != 0) {
        int next_parent = -1;
        // Search the directory table to find the parent of the current directory.
        for (int i = 0; i < 32; i++) { // MAX_FILES_IN_DIR = 32
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

    int oob_res1 = sys_mkdir("oob_test1", 35);
    int oob_res2 = sys_mkdir("oob_test2", 255);
    
    KTEST_ASSERT(oob_res1 != 0, "[SECURITY] VFS rejects out-of-bounds (35) requests");
    KTEST_ASSERT(oob_res2 != 0, "[SECURITY] VFS rejects out-of-bounds (255) requests");

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
void run_vfs_tests(void) {
    printk("\n--- VFS (Virtual File System) Tests ---\n");
    serial_print("\n--- VFS (Virtual File System) Tests ---\n");

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
}