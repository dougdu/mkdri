#if !defined(_MSC_VER)
#include <alclib.h>
#include <strings.h>
#include <errno.h>
#include <sys/_types/_errno_t.h>

errno_t strcpy_s(char *strDestination,  size_t numberOfElements,  const char *strSource)
{
	strcpy(strDestination, strSource);
	return 0;
}

#endif