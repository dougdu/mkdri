#if !defined(_MSC_VER)
#include <alclib.h>
#include <errno.h>

errno_t fopen_s( FILE *restrict *restrict streamptr, const char *restrict filename, const char *restrict mode )
{
	if (NULL == (*streamptr = fopen(filename, mode)))
	{
		return errno;
	}

	return 0;
}

#endif