/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, CA  92121

	@(#) libput.c - Sep 12, 1983  REGULUS 4.1
*/

/*
	I/O independent mapping routine.  Machine specific.  Independent
	of structure padding.  Buffer must contain at least as many
	characters as is required for structure.
 */

#include <stdlib.h>
#include <stdio.h>
#include "order.h"

int lputl(unsigned __int32* lp, FILE* f)			/* returns 0 for success, -1 for failure */
{
	unsigned __int32 uValue = _byteswap_ulong(*lp);

	if (*lp == 0xcdcdcdcd) {
		printf("Uninitialized data\n");
	}

	if (fwrite(&uValue, sizeof(uValue), 1, f) == 1) {
		return SUCCESS;
	}

	return ERROR;
}

int lputw(unsigned __int16* lp, FILE* f)			/* returns 0 for success, -1 for failure */
{
	unsigned __int16 uValue = _byteswap_ushort(*lp);

	if (*lp == 0xcdcd) {
		printf("Uninitialized data\n");
	}

	if (fwrite(&uValue, sizeof(uValue), 1, f) == 1) {
		return SUCCESS;
	}

	return ERROR;
}

int lputb(unsigned __int8* lp, FILE* f)			/* returns 0 for success, -1 for failure */
{
	if (fwrite(lp, 1, 1, f) == 1) {
		return SUCCESS;
	}

	return ERROR;
}

int _lputl(unsigned __int32 uValue, FILE* fp)
{
	return lputl(&uValue, fp);
}

int _lputw(unsigned __int16 uValue, FILE* fp)
{
	return lputw(&uValue, fp);
}

int _lputb(unsigned __int8 uValue, FILE* fp)
{
	return lputb(&uValue, fp);
}
