#ifndef SHELL_H
#define SHELL_H

#include "types.h"

// Yardimci fonksiyonlar
uint32_t hex_to_int(const char *hex_str);
void print_hexdump(uint32_t addr, int lenght);

// Ana yorumlayici fonksiyon
void execute_command(char *cmd);

#endif