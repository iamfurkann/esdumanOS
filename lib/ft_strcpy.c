#include "libft.h"

char *ft_strcpy(char *dest, const char *src)
{
    char *start = dest;

    while (*src != '\0')
    {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = '\0';

    return start;
}