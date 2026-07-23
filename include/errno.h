#ifndef ERRNO_H
#define ERRNO_H

/**
 * @brief POSIX / LINUX Standard Error Codes.
 * 
 * Designed for negative return values indicating specific system or 
 * library level errors.
 */
#define E_OK             0    /**< No error, operation successful */
#define E_PERM          -1    /**< Operation not permitted */
#define E_NOENT         -2    /**< No such file or directory */
#define E_SRCH          -3    /**< No such process */
#define E_INTR          -4    /**< Interrupted system call */
#define E_IO            -5    /**< I/O error (Hardware/Disk error) */
#define E_NXIO          -6    /**< No such device or address */
#define E_2BIG          -7    /**< Argument list too long */
#define E_NOEXEC        -8    /**< Exec format error (ELF format error) */
#define E_BADF          -9    /**< Bad file number (Invalid FD) */
#define E_CHILD         -10   /**< No child processes */
#define E_AGAIN         -11   /**< Try again (Non-blocking IO, Pipe etc.) */
#define E_NOMEM         -12   /**< Out of memory (RAM/Heap full) */
#define E_ACCES         -13   /**< Permission denied */
#define E_FAULT         -14   /**< Bad address (Invalid memory address) */
#define E_BUSY          -16   /**< Device or resource busy */
#define E_EXIST         -17   /**< File exists */
#define E_XDEV          -18   /**< Cross-device link */
#define E_NODEV         -19   /**< No such device */
#define E_NOTDIR        -20   /**< Not a directory */
#define E_ISDIR         -21   /**< Is a directory */
#define E_INVAL         -22   /**< Invalid argument */
#define E_NFILE         -23   /**< File table overflow (System FD limit) */
#define E_MFILE         -24   /**< Too many open files (Process FD limit) */
#define E_NOTTY         -25   /**< Not a typewriter */
#define E_FBIG          -27   /**< File too large */
#define E_NOSPC         -28   /**< No space left on device (Disk full) */
#define E_SPIPE         -29   /**< Illegal seek */
#define E_ROFS          -30   /**< Read-only file system */
#define E_MLINK         -31   /**< Too many links */
#define E_PIPE          -32   /**< Broken pipe */
#define E_NAMETOOLONG   -36   /**< File name too long */

/**
 * @brief Global error number variable.
 * 
 * Stores the last error code encountered by system calls and library functions.
 */
extern int errno;

#endif // ERRNO_H