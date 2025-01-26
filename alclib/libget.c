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
#include <stdint.h>
#include <stdio.h>
#include "order.h"


int lgetl(uint32_t *lp, FILE* f)		/* returns -1 for failure, 0 for success */
{
	if (fread(lp, sizeof(uint32_t), 1, f) != 1) {
		return (ERROR);
	}

#if defined(_MSC_VER)	
	*lp = _byteswap_ulong(*lp);
#else
	*lp = __builtin_bswap32(*lp);
#endif

	return(SUCCESS);
}

int lgetw(uint16_t* lp, FILE* f)		/* returns -1 for failure, 0 for success */
{
	if (fread(lp, sizeof(uint16_t), 1, f) != 1) {
		return (ERROR);
	}

#if defined(_MSC_VER)	
	*lp = _byteswap_ushort(*lp);
#else
	*lp = __builtin_bswap16(*lp);
#endif

	return(SUCCESS);
}

int lgetb(uint8_t* lp, FILE* f)		/* returns -1 for failure, 0 for success */
{
	if (fread(lp, 1, 1, f) != 1) {
		return (ERROR);
	}

	return(SUCCESS);
}

uint32_t _lgetl(FILE* fp)
{
	uint32_t uValue;
	if (fread(&uValue, sizeof(uint32_t), 1, fp) != 1) {
		return EOF;
	}

#if defined(_MSC_VER)	
	return _byteswap_ulong(uValue);
#else
	return __builtin_bswap32(uValue);
#endif
}

uint16_t _lgetw(FILE* fp)
{
	uint16_t uValue;
	if (fread(&uValue, sizeof(uint16_t), 1, fp) != 1) {
		return EOF;
	}

#if defined(_MSC_VER)	
	return _byteswap_ushort(uValue);
#else
	return __builtin_bswap16(uValue);
#endif
}

uint8_t  _lgetb(FILE* fp)
{
	uint8_t uValue;
	if (fread(&uValue, sizeof(uint8_t), 1, fp) != 1) {
		return EOF;
	}

	return uValue;
}
