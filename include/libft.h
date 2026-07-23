#ifndef LIBFT_H
# define LIBFT_H

# include "types.h"
# include "kheap.h"

/**
 * @brief Writes data to a file descriptor.
 * 
 * @param fd The file descriptor.
 * @param buf The buffer containing data to write.
 * @param count The number of bytes to write.
 * @return int The number of bytes written, or a negative error code.
 */
int write(int fd, const void *buf, unsigned long count);

#ifndef NULL
/**
 * @brief Null pointer definition.
 */
# define NULL ((void *)0)
#endif

/**
 * @brief Linked list node structure.
 * 
 * Used for standard singly linked list operations throughout the kernel.
 */
typedef struct s_list
{
	void			*content;
	struct s_list	*next;
}					t_list;

/** @brief Creates a new list node. */
t_list	*ft_lstnew(void *content);
/** @brief Adds a new node at the beginning of the list. */
void	ft_lstadd_front(t_list **lst, t_list *new);
/** @brief Counts the number of nodes in a list. */
int		ft_lstsize(t_list *lst);
/** @brief Returns the last node of the list. */
t_list	*ft_lstlast(t_list *lst);
/** @brief Adds a new node at the end of the list. */
void	ft_lstadd_back(t_list **lst, t_list *new);
/** @brief Deletes a single node from the list using a given function. */
void	ft_lstdelone(t_list *lst, void (*del)(void *));
/** @brief Clears and deletes the entire list. */
void	ft_lstclear(t_list **lst, void (*del)(void *));
/** @brief Converts a string to an integer. */
int		ft_atoi(const char *nptr);
/** @brief Writes zeroed bytes to a memory block. */
void	ft_bzero(void *s, size_t n);
/** @brief Allocates zero-initialized memory. */
void	*ft_calloc(size_t number, size_t size);
/** @brief Checks if a character is alphanumeric. */
int		ft_isalnum(int c);
/** @brief Checks if a character is alphabetic. */
int		ft_isalpha(int c);
/** @brief Checks if a character is 7-bit ASCII. */
int		ft_isascii(int c);
/** @brief Checks if a character is a digit. */
int		ft_isdigit(int c);
/** @brief Checks if a character is printable. */
int		ft_isprint(int c);
/** @brief Converts an uppercase letter to lowercase. */
int		ft_tolower(int c);
/** @brief Converts a lowercase letter to uppercase. */
int		ft_toupper(int c);
/** @brief Converts an integer to a dynamically allocated string. */
char	*ft_itoa(int n);
/** @brief Locates the first occurrence of a byte in memory. */
void	*ft_memchr(const void *s, int c, size_t n);
/** @brief Compares two memory blocks. */
int		ft_memcmp(const void *s1, const void *s2, size_t n);
/** @brief Copies memory area. */
void	*ft_memcpy(void *dest, const void *src, size_t n);
/** @brief Copies memory area, handling overlaps. */
void	*ft_memmove(void *dest, const void *src, size_t n);
/** @brief Fills memory with a constant byte. */
void	*ft_memset(void *s, int c, size_t n);
/** @brief Outputs a character to a file descriptor. */
void	ft_putchar_fd(char c, int fd);
/** @brief Outputs a string with a newline to a file descriptor. */
void	ft_putendl_fd(char *s, int fd);
/** @brief Outputs an integer to a file descriptor. */
void	ft_putnbr_fd(int n, int fd);
/** @brief Outputs a string to a file descriptor. */
void	ft_putstr_fd(char *s, int fd);
/** @brief Splits a string into an array of strings using a delimiter character. */
char	**ft_split(char const *s, char c);
/** @brief Locates the first occurrence of a character in a string. */
char	*ft_strchr(const char *s, int c);
/** @brief Duplicates a string using dynamic memory. */
char	*ft_strdup(const char *str);
/** @brief Applies a function to each character of a string, passing its index. */
void	ft_striteri(char *s, void (*f)(unsigned int, char *));
/** @brief Concatenates two strings into a new dynamically allocated string. */
char	*ft_strjoin(char const *s1, char const *s2);
/** @brief Appends a string to another with a size limit. */
size_t	ft_strlcat(char *dest, const char *src, size_t size);
/** @brief Copies a string with a size limit. */
size_t	ft_strlcpy(char *dest, const char *src, size_t size);
/** @brief Copies a string. */
char *ft_strcpy(char *dest, const char *src);
/** @brief Calculates the length of a string. */
size_t	ft_strlen(const char *s);
/** @brief Creates a new string by mapping a function to each character. */
char	*ft_strmapi(char const *s, char (*f)(unsigned int, char));
/** @brief Compares two strings up to a specific number of characters. */
int		ft_strncmp(const char *s1, const char *s2, size_t n);
/** @brief Compares two strings. */
int ft_strcmp(const char *s1, const char *s2);
/** @brief Locates a substring within a string, limited by length. */
char	*ft_strnstr(const char *big, const char *little, size_t len);
/** @brief Locates a substring within a string. */
char	*ft_strstr(char *str, char *to_find);
/** @brief Locates the last occurrence of a character in a string. */
char	*ft_strrchr(const char *s, int c);
/** @brief Trims characters from the beginning and end of a string. */
char	*ft_strtrim(char const *s1, char const *set);
/** @brief Extracts a substring from a string. */
char	*ft_substr(char const *s, unsigned int start, size_t len);

#endif