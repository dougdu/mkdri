/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, CA  92121

	@(#) putarhd.c - Sep 12, 1983  REGULUS 4.1
*/

/*
	I/O independent mapping routine.  Machine specific.  Independent
	of structure padding.  Buffer must contain at least as many
	characters as is required for structure.
 */

#include <alclib.h>



/*
 * putarhd - fills the buffer from the archive header structure in
 *		the byte orientation of the target machine (68000).
 */
int putarhd(FILE* fp, struct lib2hdr* arptr)		/* returns 0 for success, -1 for error */
{
	int i;
	char* lp;

	arptr->l2reserved = 0;

	for (i = 0, lp = arptr->l2fname; i < LIBNSIZE; i++, lp++) {
		if ((putc(*lp, fp)) == EOF) {
			return(ERROR);
		}
	}
	if (lputl(&arptr->l2modti, fp) == ERROR) {
		return(ERROR);
	}
	if ((putc(arptr->l2userid, fp)) == EOF) {
		return(ERROR);
	}
	if ((putc(arptr->l2gid, fp)) == EOF) {
		return(ERROR);
	}
	if (lputw(&arptr->l2fimode, fp) == ERROR) {
		return(ERROR);
	}
	if (lputl(&arptr->l2fsize, fp) == ERROR) {
		return(ERROR);
	}
	if (lputw(&arptr->l2reserved, fp) == ERROR) {
		return(ERROR);
	}

	return(SUCCESS);
}

