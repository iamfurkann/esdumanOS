/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_memchr.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: esduman <esduman@student.42istanbul.com    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/05/27 09:46:58 by esduman           #+#    #+#             */
/*   Updated: 2025/05/27 09:46:58 by esduman          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

void	*ft_memchr(const void *s, int c, size_t n)
{
	const unsigned char		*arr;

	arr = (const unsigned char *)s;
	while (n--)
	{
		if (*arr != (unsigned char)c)
			arr++;
		else
			return ((void *)arr);
	}
	return (NULL);
}
