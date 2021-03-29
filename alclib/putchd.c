/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, CA  92121

	@(#) putchd.c - Sep 12, 1983  REGULUS 4.1
*/

/*
	I/O independent mapping routine.  Machine specific.  Independent
	of structure padding.  Buffer must contain at least as many
	characters as is required for structure.
 */

#include <alclib.h>



/*
 * putchd - fills the buffer from the c.out header structure in
 *		the byte orientation of the target machine (68000).
 */
int putchd(FILE* fp, struct hdr2* arptr)		/* returns 0 for success, -1 for failure */
{
	if (lputw(&arptr->ch_magic, fp) == ERROR) {
		return(ERROR);
	}
	if (lputl(&arptr->ch_tsize, fp) == ERROR) {
		return(ERROR);
	}
	if (lputl(&arptr->ch_dsize, fp) == ERROR) {
		return(ERROR);
	}
	if (lputl(&arptr->ch_bsize, fp) == ERROR) {
		return(ERROR);
	}
	if (lputl(&arptr->ch_ssize, fp) == ERROR) {
		return(ERROR);
	}
	if (lputl(&arptr->ch_stksize, fp) == ERROR) {
		return(ERROR);
	}
	if (lputl(&arptr->ch_entry, fp) == ERROR) {
		return(ERROR);
	}
	if (lputw(&arptr->ch_rlbflg, fp) == ERROR) {
		return(ERROR);
	}
	if (arptr->ch_magic == EX_ABMAGIC) {
		if (lputl(&arptr->ch_dstart, fp) == ERROR) {
			return(ERROR);
		}
		if (lputl(&arptr->ch_bstart, fp) == ERROR) {
			return(ERROR);
		}
	}

	return(SUCCESS);
}

