
/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, Ca.  92121

	@(#) libwrite.c - Sep 12, 1983  REGULUS 4.1
*/

#include <alclib.h>



int lwritel(int fd, uint32_t* str)
{
#if defined(_MSC_VER)
	uint32_t junk = _byteswap_ulong(*str);
#else
	uint32_t junk = __builtin_bswap32(junk);
#endif

	if (_write(fd, &junk, sizeof(junk)) == sizeof(junk)) {
		return(SUCCESS);
	}

	return(ERROR);
}

int lwritew(int fd, uint16_t* str)
{
#if defined(_MSC_VER)
	uint32_t junk = _byteswap_ushort(*str);
#else
	uint32_t junk = __builtin_bswap16(junk);
#endif

	if (_write(fd, &junk, sizeof(junk)) == sizeof(junk)) {
		return(SUCCESS);
	}

	return(ERROR);
}

int lwritec(int fd, char* str)
{
	if (_write(fd, str, sizeof(char)) != sizeof(char)) {
		return(ERROR);
	}
	return(SUCCESS);
}
