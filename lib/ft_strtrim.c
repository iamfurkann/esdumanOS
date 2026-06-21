/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_strtrim.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: esadfurkanduman <esadfurkanduman@studen    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/05/31 17:27:37 by esduman           #+#    #+#             */
/*   Updated: 2026/05/26 15:20:02 by esadfurkand      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

static int	f_find(char c, char const *set)
{
	int	j;

	j = 0;
	while (set[j])
	{
		if (c == set[j])
			return (1);
		j++;
	}
	return (0);
}

static int	f_get_first(char const *s1, char const *set)
{
	int	first;

	first = 0;
	while (s1[first] && f_find(s1[first], set))
		first++;
	return (first);
}

static int	f_get_last(char const *s1, char const *set, int first)
{
	int	last;

	last = ft_strlen(s1);
	while (last > first && f_find(s1[last - 1], set))
		last--;
	return (last);
}

char	*ft_strtrim(char const *s1, char const *set)
{
	char		*new_arr;
	int			first;
	int			last;
	size_t		i;

	if (!s1 || !set)
		return (NULL);
	first = f_get_first(s1, set);
	last = f_get_last(s1, set, first);
	new_arr = (char *)kmalloc((last - first) + 1);
	if (!new_arr)
		return (NULL);
	i = 0;
	while (last > first)
	{
		new_arr[i] = s1[first];
		first++;
		i++;
	}
	new_arr[i] = '\0';
	return (new_arr);
}
