#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include "syscall.h"
#include "tty.h"

int printk(const char *format, ...);
int	ft_kputchar(int c);
int	ft_kputstr(char *c);
int	ft_kputnbr(int c);
int	ft_kputnbru(unsigned int c);
int	ft_kputhex(unsigned int c, int mod);
int	ft_kputptr(void *ptr);

int pipe(int pipefd[2]);
int dup2(int oldfd, int newfd);

#endif // STDIO_H