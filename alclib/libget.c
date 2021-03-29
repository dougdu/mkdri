/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, CA  92121

	@(#) libget.c - Sep 12, 1983  REGULUS 4.1
*/

/*
	I/O independent mapping routine.  Machine specific.  Independent
	of structure padding.  Buffer must contain at least as many
	characters as is required for structure.
 */

#include <stdlib.h>
#include <stdio.h>
#include "order.h"


int lgetl(unsigned __int32 *lp, FILE* f)		/* returns -1 for failure, 0 for success */
{
	if (fread(lp, sizeof(unsigned __int32), 1, f) != 1) {
		return (ERROR);
	}
	*lp = _byteswap_ulong(*lp);

	return(SUCCESS);
}

int lgetw(unsigned __int16* lp, FILE* f)		/* returns -1 for failure, 0 for success */
{
	if (fread(lp, sizeof(unsigned __int16), 1, f) != 1) {
		return (ERROR);
	}
	*lp = _byteswap_ushort(*lp);

	return(SUCCESS);
}

int lgetb(unsigned __int8* lp, FILE* f)		/* returns -1 for failure, 0 for success */
{
	if (fread(lp, 1, 1, f) != 1) {
		return (ERROR);
	}

	return(SUCCESS);
}

unsigned __int32 _lgetl(FILE* fp)
{
	unsigned __int32 uValue;
	if (fread(&uValue, sizeof(unsigned __int32), 1, fp) != 1) {
		return EOF;
	}

	return _byteswap_ulong(uValue);
}

unsigned __int16 _lgetw(FILE* fp)
{
	unsigned __int16 uValue;
	if (fread(&uValue, sizeof(unsigned __int16), 1, fp) != 1) {
		return EOF;
	}

	return _byteswap_ushort(uValue);
}

unsigned __int8  _lgetb(FILE* fp)
{
	unsigned __int8 uValue;
	if (fread(&uValue, sizeof(unsigned __int8), 1, fp) != 1) {
		return EOF;
	}

	return uValue;
}
