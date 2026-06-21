#include "stdio.h"

int	ft_kputhex(unsigned int c, int mod)
{
	char	*hex;
	int		i;

	i = 0;
	if (mod)
		hex = "0123456789ABCDEF";
	else
		hex = "0123456789abcdef";
	if (c >= 16)
		i += ft_kputhex(c / 16, mod);
	i += ft_kputchar(hex[c % 16]);
	return (i);
}

static int	ft_putptr_rec(unsigned long n)
{
	char	*hex;
	int		i;

	hex = "0123456789abcdef";
	i = 0;
	if (n >= 16)
		i += ft_putptr_rec(n / 16);
	i += ft_kputchar(hex[n % 16]);
	return (i);
}

int	ft_kputptr(void *ptr)
{
	int	i;

	if (!ptr)
		return (ft_kputstr("(nil)"));
	i = ft_kputstr("0x");
	return (i + ft_putptr_rec((unsigned long)ptr));
}