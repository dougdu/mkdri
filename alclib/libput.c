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
#include <stdint.h>
#include <stdio.h>
#include "order.h"

int lputl(uint32_t* lp, FILE* f)			/* returns 0 for success, -1 for failure */
{
#if defined(__msvc__)
	uint32_t uValue = _byteswap_ulong(*lp);
#else
	uint32_t uValue = __builtin_bswap32(*lp);
#endif

	if (*lp == 0xcdcdcdcd) {
		printf("Uninitialized data\n");
	}

	if (fwrite(&uValue, sizeof(uValue), 1, f) == 1) {
		return SUCCESS;
	}

	return ERROR;
}

int lputw(uint16_t* lp, FILE* f)			/* returns 0 for success, -1 for failure */
{
#if defined(__msvc__)
	uint16_t uValue = _byteswap_ushort(*lp);
#else
	uint16_t uValue = __builtin_bswap16(*lp);
#endif

	if (*lp == 0xcdcd) {
		printf("Uninitialized data\n");
	}

	if (fwrite(&uValue, sizeof(uValue), 1, f) == 1) {
		return SUCCESS;
	}

	return ERROR;
}

int lputb(uint8_t* lp, FILE* f)			/* returns 0 for success, -1 for failure */
{
	if (fwrite(lp, 1, 1, f) == 1) {
		return SUCCESS;
	}

	return ERROR;
}

int _lputl(uint32_t uValue, FILE* fp)
{
	return lputl(&uValue, fp);
}

int _lputw(uint16_t uValue, FILE* fp)
{
	return lputw(&uValue, fp);
}

int _lputb(uint8_t uValue, FILE* fp)
{
	return lputb(&uValue, fp);
}
