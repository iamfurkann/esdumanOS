/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_calloc.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: esadfurkanduman <esadfurkanduman@studen    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/06/23 13:38:12 by esduman           #+#    #+#             */
/*   Updated: 2026/05/26 15:20:16 by esadfurkand      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

void	*ft_calloc(size_t nmemb, size_t size)
{
	void	*new_value;

	new_value = kmalloc(size * nmemb);
	if (!new_value)
		return (NULL);
	ft_memset(new_value, 0, nmemb * size);
	return (new_value);
}
