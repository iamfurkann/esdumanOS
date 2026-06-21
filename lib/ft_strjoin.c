/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_strjoin.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: esadfurkanduman <esadfurkanduman@studen    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/05/31 14:56:44 by esduman           #+#    #+#             */
/*   Updated: 2026/05/26 15:19:51 by esadfurkand      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

static void	f_add(char const *s, char *new_arr, size_t *i)
{
	size_t	j;

	j = 0;
	while (s[j])
	{
		new_arr[*i] = s[j];
		(*i)++;
		j++;
	}
}

char	*ft_strjoin(char const *s1, char const *s2)
{
	char	*new_arr;
	size_t	i;

	if (!s1 || !s2)
		return (NULL);
	new_arr = (char *)kmalloc(ft_strlen(s1) + ft_strlen(s2) + 1);
	if (!new_arr)
		return (NULL);
	i = 0;
	f_add(s1, new_arr, &i);
	f_add(s2, new_arr, &i);
	new_arr[i] = '\0';
	return (new_arr);
}
