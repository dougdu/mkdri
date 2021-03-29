/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, CA  92121

	@(#) getarhd.c - Sep 12, 1983  REGULUS 4.1
*/

/*
	I/O independent mapping routine.  Machine specific.  Independent
	of structure padding.  Buffer must contain at least as many
	characters as is required for structure.
 */

#include <alclib.h>
#include <string.h>


/*
 * getarhd - fills the archive header structure from the buffer int
 *		the manner which will be understood on the current machine.
 */
int getarhd(FILE* fp, struct lib2hdr* arptr)				/* returns -1 for failure, 0 for success */
{
	int i;
	char *lp;

	for (i = 0, lp = arptr->l2fname; i < LIBNSIZE; i++) {
		if ((*lp++ = getc(fp)) == EOF) {
			return(ERROR);
		}
	}
	if ((lgetl(&arptr->l2modti, fp)) == ERROR) {
		return(ERROR);
	}
	if ((arptr->l2userid = getc(fp)) == EOF) {
		return(ERROR);
	}
	if ((arptr->l2gid = getc(fp)) == EOF) {
		return(ERROR);
	}
	if ((lgetw(&arptr->l2fimode, fp)) == ERROR) {
		return(ERROR);
	}
	if ((lgetl(&arptr->l2fsize, fp)) == ERROR) {
		return(ERROR);
	}
	if ((lgetw(&arptr->l2reserved, fp)) == ERROR) {
		return(ERROR);
	}

	return(SUCCESS);
}

