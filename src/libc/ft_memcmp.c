/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_memcmp.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: esduman <esduman@student.42istanbul.com    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/06/23 13:39:50 by esduman           #+#    #+#             */
/*   Updated: 2025/06/23 13:39:51 by esduman          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

int	ft_memcmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char		*arr1;
	const unsigned char		*arr2;

	arr1 = (unsigned char *)s1;
	arr2 = (unsigned char *)s2;
	while (n--)
	{
		if (*arr1 != *arr2)
			return (*arr1 - *arr2);
		arr1++;
		arr2++;
	}
	return (0);
}
