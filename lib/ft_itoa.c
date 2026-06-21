/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_itoa.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: esduman <esduman@student.42istanbul.com    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/06/23 13:38:48 by esduman           #+#    #+#             */
/*   Updated: 2025/06/23 13:38:48 by esduman          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

static int	f_getsize(int n, int *size)
{
	*size = 0;
	if (n <= 0)
		(*size)++;
	while (n)
	{
		n /= 10;
		(*size)++;
	}
	return (*size);
}

char	*ft_itoa(int n)
{
	int		size;
	long	num;
	char	*ptr;

	f_getsize(n, &size);
	num = n;
	ptr = (char *)kmalloc(size + 1);
	if (!ptr)
		return (NULL);
	ptr[size] = '\0';
	if (num == 0)
		ptr[0] = '0';
	if (num < 0)
	{
		ptr[0] = '-';
		num = -num;
	}
	while (num > 0)
	{
		ptr[--size] = (num % 10) + '0';
		num /= 10;
	}
	return (ptr);
}
