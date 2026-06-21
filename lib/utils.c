#include "stdio.h"

int ft_kputchar(int c)
{
    terminal_putchar((char)c);
    return (1);
}

int ft_kputstr(char *str)
{
    int i = 0;
    if (!str)
        str = "(null)";
    while (str[i])
    {
        ft_kputchar(str[i]);
        i++;
    }
    return (i);
}

int	ft_kputnbr(int c)
{
	int		i;
	long	nb;

	i = 0;
	nb = c;
	if (nb < 0)
	{
		i += ft_kputchar('-');
		nb = -nb;
	}
	if (nb >= 10)
		i += ft_kputnbr(nb / 10);
	i += ft_kputchar((nb % 10) + '0');
	return (i);
}

int	ft_kputnbru(unsigned int c)
{
	int	i;

	i = 0;
	if (c >= 10)
		i += ft_kputnbru(c / 10);
	i += ft_kputchar((c % 10) + '0');
	return (i);
}