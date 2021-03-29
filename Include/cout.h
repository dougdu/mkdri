#pragma once
/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, CA  92121

	@(#) sys/cout.h - Jul 26, 1983  REGULUS 4.1
*/


#pragma pack(push)
#pragma pack(2)

typedef struct hdr {
	unsigned __int16 ch_magic;
	unsigned __int32 ch_tsize;		/* text size */
	unsigned __int32 ch_dsize;		/* data size */
	unsigned __int32 ch_bsize;		/* bss size */
	unsigned __int32 ch_ssize;		/* symbol table size */
	unsigned __int32 ch_stksize;	/* stack size */
	unsigned __int32 ch_entry;		/* location of entry point */
	unsigned __int16 ch_rlbflg;	    /* relocation bits present flag, must be signed */
} HDR;

typedef struct hdr_cout {
	unsigned __int16 ftype;
	unsigned __int32 ftext;		/* text size */
	unsigned __int32 fdata;		/* data size */
	unsigned __int32 fbss;		/* bss size */
	unsigned __int32 fsym;		/* symbol table size */
	unsigned __int32 fssize;	/* stack size */
	unsigned __int32 fentry;	/* location of entry point */
	short			 fflag;	    /* relocation bits present flag, must be signed */
} HDR_COUT;

typedef struct hdr2 {
	unsigned __int16 ch_magic;
	unsigned __int32 ch_tsize;		/* # bytes in program text segment */
	unsigned __int32 ch_dsize;		/* # bytes in program data segment */
	unsigned __int32 ch_bsize;		/* # bytes in program bss  segment */
	unsigned __int32 ch_ssize;		/* # bytes in symbol table */
	unsigned __int32 ch_stksize;	/* initial stack size */
	unsigned __int32 ch_entry;		/* entry point--address of text segment */
	unsigned __int16 ch_rlbflg;		/* relocation bits suppressed flag, must be signed */
	unsigned __int32 ch_dstart;		/* address of data segment */
	unsigned __int32 ch_bstart;		/* address of bss segment */
 } HDR2;

/* structure of the symbol table */

typedef struct nlist {
	char             n_name[8];
	unsigned __int16 n_type;
	unsigned __int32 n_value;
} NLIST;

/* HDSIZE should be 28 bytes, HDSIZ2 should be 36 bytes */
#define HDSIZE		(sizeof (struct hdr))
#define HDSIZ2		(sizeof (struct hdr2))

#define MAGIC		0x601a	/*  bra .+26 instruction */
#define MAGIC1		0x601b	/* data & bss base defined */
#define MAGICST2 	0x601c	/* shared text 2K boundary */
#define MAGICID 	0x601d	/* I & D split */
#define MAGICST		0x601e	/* shared test 4k boundary (-n default) */

#define EX_MAGIC	0x601a
#define EX_ABMAGIC	0x601b
#define EX_2KSTXT	0x601c
#define EX_IDMAGIC	0x601d
#define EX_4KSTXT	0x601e

#define SHT2KBOUND	2048
#define SHT2KFIX(x)	(((x+SHT2KBOUND-1)/SHT2KBOUND)*SHT2KBOUND)
#define SHT4KBOUND	4096
#define SHT4KFIX(x)	(((x+SHT4KBOUND-1)/SHT4KBOUND)*SHT4KBOUND)

#pragma pack(pop)