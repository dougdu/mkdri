
/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, Ca.  92121

	@(#) libread.c - Sep 12, 1983  REGULUS 4.1
*/

#include <alclib.h>



int lreadl(int fd, unsigned __int32* str)
{
	unsigned __int32 junk;

	if (_read(fd, &junk, sizeof(junk)) != sizeof(junk)) {
		return(ERROR);
	}
	*str = _byteswap_ulong(junk);

	return(SUCCESS);
}

int lreadw(int fd, unsigned __int16* str)
{
	unsigned __int16 junk;

	if (_read(fd, &junk, sizeof(junk)) != sizeof(junk)) {
		return(ERROR);
	}
	*str = _byteswap_ushort(junk);

	return(SUCCESS);
}

int lreadc(int fd, char* str)
{
	if (_read(fd, str, sizeof(char)) != sizeof(char)) {
		return(ERROR);
	}

	return(SUCCESS);
}
