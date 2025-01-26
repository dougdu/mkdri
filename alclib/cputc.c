
/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, CA  92121

	@(#) cputc.c - Sep 12, 1983  REGULUS 4.1
*/

# include	"alclib.h"



/**
 **	put a single character
 **/


int cputc(char c, int fn)
{
	int retval;

	if (fn == 2) {
		/* Standard Error */
		retval = fwrite(&c, 1, 1, stderr);
	}
	else {
		retval = putchar(c);
	}

	return retval;
}
