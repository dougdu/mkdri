/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, CA  92121

	@(#) getchd.c - Sep 12, 1983  REGULUS 4.1
*/

/*
	I/O independent mapping routine.  Machine specific.  Independent
	of structure padding.  Buffer must contain at least as many
	characters as is required for structure.
 */

#include <alclib.h>


/*
 * getchd - fills the c.out header structure from the buffer in
 *		the manner which will be understood on the current machine.
 */
int getchd(FILE* fp, struct hdr2* arptr)		/* returns 0 for success, -1 for failure */
{
	if (lgetw(&arptr->ch_magic, fp) == ERROR) {
		return(ERROR);
	}
	if (lgetl(&arptr->ch_tsize, fp) == ERROR) {
		return(ERROR);
	}
	if (lgetl(&arptr->ch_dsize, fp) == ERROR) {
		return(ERROR);
	}
	if (lgetl(&arptr->ch_bsize, fp) == ERROR) {
		return(ERROR);
	}
	if (lgetl(&arptr->ch_ssize, fp) == ERROR) {
		return(ERROR);
	}
	if (lgetl(&arptr->ch_stksize, fp) == ERROR) {
		return(ERROR);
	}
	if (lgetl(&arptr->ch_entry, fp) == ERROR) {
		return(ERROR);
	}
	if (lgetw(&arptr->ch_rlbflg, fp) == ERROR) {
		return(ERROR);
	}
	if (arptr->ch_magic == EX_ABMAGIC) {
		if (lgetl(&arptr->ch_dstart, fp) == ERROR) {
			return(ERROR);
		}
		if (lgetl(&arptr->ch_bstart, fp) == ERROR) {
			return(ERROR);
		}
	}

	return(SUCCESS);
}

