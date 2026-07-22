#include "serial.h"

void init_serial(void) {
    outb(PORT_COM1 + 1, 0x00);    // Bütün kesmeleri (Interrupts) kapat
    outb(PORT_COM1 + 3, 0x80);    // DLAB (Divisor Latch Access Bit) Aktif Et (Hız ayarı için)
    outb(PORT_COM1 + 0, 0x01);    // Hız bölücü Low Byte  (115200 / 1 = 115200 baud)
    outb(PORT_COM1 + 1, 0x00);    // Hız bölücü High Byte
    outb(PORT_COM1 + 3, 0x03);    // 8 bit, parite yok, 1 stop biti (8N1)
    outb(PORT_COM1 + 2, 0xC7);    // FIFO'yu aktif et, 14-byte eşiği kullan
    outb(PORT_COM1 + 4, 0x0B);    // IRQs aktif, RTS/DSR set
    outb(PORT_COM1 + 1, 0x01);    // Kesmeleri tekrar aktif et (Eğer kesme tabanlı okuma yapacaksak)
}

static int is_transmit_empty(void) {
    return inb(PORT_COM1 + 5) & 0x20;
}

void serial_write_char(char c) {
    int timeout = 100000;
    
    while (is_transmit_empty() == 0) {
        timeout--;
        if (timeout == 0) {
            return; 
        }
    }
    
    outb(PORT_COM1, c);
}

void serial_print(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        serial_write_char(str[i]);
    }
}