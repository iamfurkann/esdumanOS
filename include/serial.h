#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"
#include "io.h"

// Standart COM1 Port Adresi
#define PORT_COM1 0x3f8

// Seri Port Başlatma Fonksiyonu
void init_serial(void);

// Seri Porta Tek Bir Karakter Yazma
void serial_write_char(char c);

// Seri Porta String (Metin) Yazma
void serial_print(const char *str);

#endif // SERIAL_H