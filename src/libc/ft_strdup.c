/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_strdup.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: esadfurkanduman <esadfurkanduman@studen    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/05/31 05:52:17 by esduman           #+#    #+#             */
/*   Updated: 2026/05/26 15:19:27 by esadfurkand      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

char	*ft_strdup(const char *s)
{
	size_t	length;
	char	*cpy;

	length = ft_strlen(s) + 1;
	cpy = (char *)kmalloc(length);
	if (!cpy)
		return (NULL);
	ft_memmove(cpy, s, length);
	return (cpy);
}
