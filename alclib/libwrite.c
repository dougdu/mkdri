
/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, Ca.  92121

	@(#) libwrite.c - Sep 12, 1983  REGULUS 4.1
*/

#include <alclib.h>



int lwritel(int fd, unsigned __int32* str)
{
	unsigned __int32 junk = _byteswap_ulong(*str);

	if (_write(fd, &junk, sizeof(junk)) == sizeof(junk)) {
		return(SUCCESS);
	}

	return(ERROR);
}

int lwritew(int fd, unsigned __int16* str)
{
	unsigned __int16 junk = _byteswap_ushort(*str);

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
