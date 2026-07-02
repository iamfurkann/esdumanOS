#ifndef ERRNO_H
#define ERRNO_H

/* ==================================================================
 * STANDART HATA KODLARI (POSIX Uyumlu)
 * Tüm Kernel ve User-Space fonksiyonları bu kodları döndürmelidir.
 * ================================================================== */

#define E_OK         0
#define E_ERROR     -1
#define E_NOENT     -2  // No such file or directory
#define E_EXISTS    -3  // File exists
#define E_NOMEM     -4  // Out of memory / Disk full
#define E_ACCESS    -5  // Permission denied
#define E_INVAL     -6  // Invalid argument
#define E_BUSY      -7  //Device or resource busy

#endif