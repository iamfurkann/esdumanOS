/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_substr.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: esadfurkanduman <esadfurkanduman@studen    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/05/31 14:05:20 by esduman           #+#    #+#             */
/*   Updated: 2026/05/26 15:20:09 by esadfurkand      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

static void	f_isbig(size_t *len, size_t length, unsigned int start)
{
	if (*len > length - start)
		*len = length - start;
}

static char	*f_get_null(void)
{
	char	*new_value;

	new_value = (char *)ft_calloc(1, 1);
	if (!new_value)
		return (NULL);
	return (new_value);
}

char	*ft_substr(char const *s, unsigned int start, size_t len)
{
	char	*new_value;

	if (!s)
		return (NULL);
	if (start >= ft_strlen(s))
		return (f_get_null());
	f_isbig(&len, ft_strlen(s), start);
	new_value = (char *)kmalloc(len + 1);
	if (!new_value)
		return (NULL);
	ft_memmove(new_value, s + start, len);
	new_value[len] = '\0';
	return (new_value);
}
