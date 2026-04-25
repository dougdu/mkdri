#if !defined(_MSC_VER)
#include <alclib.h>
#include <string.h>
#include <errno.h>

errno_t strcpy_s(char *strDestination,  size_t numberOfElements,  const char *strSource)
{
	strcpy(strDestination, strSource);
	return 0;
}

#endif