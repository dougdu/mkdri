
/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, Ca.  92121

	@(#) libread.c - Sep 12, 1983  REGULUS 4.1
*/

#include <alclib.h>
#include <stdint.h>
#include <unistd.h>



int lreadl(int fd, uint32_t* str)
{
	uint32_t junk;

	if (read(fd, &junk, sizeof(junk)) != sizeof(junk)) {
		return(ERROR);
	}
#if defined(__msvc__)
	*str = _byteswap_ulong(junk);
#else
	*str = __builtin_bswap32(junk);
#endif

	return(SUCCESS);
}

int lreadw(int fd, uint16_t* str)
{
	uint16_t junk;

	if (_read(fd, &junk, sizeof(junk)) != sizeof(junk)) {
		return(ERROR);
	}
#if defined(__msvc__)
	*str = _byteswap_ushort(junk);
#else
	*str = __builtin_bswap16(junk);
#endif	

	return(SUCCESS);
}

int lreadc(int fd, char* str)
{
	if (_read(fd, str, sizeof(char)) != sizeof(char)) {
		return(ERROR);
	}

	return(SUCCESS);
}
