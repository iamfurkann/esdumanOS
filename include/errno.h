#ifndef ERRNO_H
#define ERRNO_H

// POSIX / LINUX Standart Hata Kodları (Negatif dönüşler için tasarlanmıştır)
#define E_OK             0    // Hata yok, başarılı
#define E_PERM          -1    // Operation not permitted (İzin reddedildi)
#define E_NOENT         -2    // No such file or directory (Dosya/Dizin yok)
#define E_SRCH          -3    // No such process (Süreç bulunamadı)
#define E_INTR          -4    // Interrupted system call (Kesintiye uğradı)
#define E_IO            -5    // I/O error (Donanım/Disk hatası)
#define E_NXIO          -6    // No such device or address (Aygıt bulunamadı)
#define E_2BIG          -7    // Argument list too long (Argümanlar çok uzun)
#define E_NOEXEC        -8    // Exec format error (ELF format hatası)
#define E_BADF          -9    // Bad file number (Geçersiz FD)
#define E_CHILD         -10   // No child processes (Alt süreç yok)
#define E_AGAIN         -11   // Try again (Non-blocking IO, Pipe vb.)
#define E_NOMEM         -12   // Out of memory (RAM/Heap doldu)
#define E_ACCES         -13   // Permission denied (Erişim engellendi)
#define E_FAULT         -14   // Bad address (Geçersiz bellek adresi)
#define E_BUSY          -16   // Device or resource busy (Kaynak meşgul)
#define E_EXIST         -17   // File exists (Dosya zaten var)
#define E_XDEV          -18   // Cross-device link
#define E_NODEV         -19   // No such device
#define E_NOTDIR        -20   // Not a directory
#define E_ISDIR         -21   // Is a directory
#define E_INVAL         -22   // Invalid argument (Geçersiz argüman)
#define E_NFILE         -23   // File table overflow (Sistem FD sınırı)
#define E_MFILE         -24   // Too many open files (Süreç FD sınırı)
#define E_NOTTY         -25   // Not a typewriter
#define E_FBIG          -27   // File too large (Dosya çok büyük)
#define E_NOSPC         -28   // No space left on device (Disk doldu)
#define E_SPIPE         -29   // Illegal seek
#define E_ROFS          -30   // Read-only file system (Salt okunur VFS)
#define E_MLINK         -31   // Too many links
#define E_PIPE          -32   // Broken pipe (Kırık boru hattı)
#define E_NAMETOOLONG   -36   // File name too long

extern int errno;

#endif // ERRNO_H