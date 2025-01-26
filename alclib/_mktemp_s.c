#if !defined(_MSC_VER)
#include <alclib.h>
#include <errno.h>
#include <sys/_types/_errno_t.h>

errno_t _mktemp_s(char *nameTemplate, size_t sizeInChars)
{
	 return mkstemp(nameTemplate);
}

#endif