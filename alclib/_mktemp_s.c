#if !defined(_MSC_VER)
#include <alclib.h>
#include <errno.h>

errno_t _mktemp_s(char *nameTemplate, size_t sizeInChars)
{
	 return mkstemp(nameTemplate);
}

#endif