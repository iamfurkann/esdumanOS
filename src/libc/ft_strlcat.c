/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_strlcat.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: esduman <esduman@student.42istanbul.com    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/06/23 13:41:09 by esduman           #+#    #+#             */
/*   Updated: 2025/06/23 13:41:09 by esduman          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

static void	f_findlen(char *dst, size_t *dlen, size_t size)
{
	while (*dlen < size && dst[*dlen])
		(*dlen)++;
}

size_t	ft_strlcat(char *dst, const char *src, size_t size)
{
	size_t	slen;
	size_t	dlen;
	size_t	i;

	slen = ft_strlen(src);
	dlen = 0;
	f_findlen(dst, &dlen, size);
	if (dlen >= size)
		return (size + slen);
	i = 0;
	while ((dlen + i + 1) < size && src[i])
	{
		dst[dlen + i] = src[i];
		i++;
	}
	if (dlen + i < size)
		dst[dlen + i] = '\0';
	return (dlen + slen);
}
