/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_atoi.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: esduman <esduman@student.42istanbul.com    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/06/23 13:37:58 by esduman           #+#    #+#             */
/*   Updated: 2025/06/23 13:38:01 by esduman          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

static void	f_is_np(const char **nptr)
{
	while ((**nptr >= 9 && **nptr <= 13) || **nptr == 32)
		(*nptr)++;
}

static void	f_is_sign(const char **nptr, int *sign)
{
	if (**nptr == '-' || **nptr == '+')
	{
		if (**nptr == '-')
			*sign = -1;
		(*nptr)++;
	}
}

int	ft_atoi(const char *nptr)
{
	int	sign;
	int	sum;

	sign = 1;
	sum = 0;
	f_is_np(&nptr);
	f_is_sign(&nptr, &sign);
	while (*nptr >= '0' && *nptr <= '9')
	{
		sum = (sum * 10) + (*nptr - '0');
		nptr++;
	}
	return (sum * sign);
}
