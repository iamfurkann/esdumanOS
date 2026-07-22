#ifndef SECURITY_H
#define SECURITY_H

#include "types.h"

typedef enum {
    SEC_LEVEL_NORMAL          = 0, // Standart operasyon (Şifreleme isteğe bağlı)
    SEC_LEVEL_CRYPTO_ENFORCED = 1, // VFS üzerindeki TÜM okuma/yazmalar şifrelenmek ZORUNDA
    SEC_LEVEL_LOCKDOWN        = 2, // Yeni görev başlatılamaz, RAM'deki Key silinir (Zeroize)
    SEC_LEVEL_IMMUTABLE       = 3  // Diske yazma tamamen kapatılır, Kernel "Read-Only" moduna geçer
} security_level_t;

extern security_level_t current_sec_level;

void set_security_level(security_level_t level);
void derive_master_key(const char *password);

#endif // SECURITY_H