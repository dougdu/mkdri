#pragma once
/*
	Copyright 1983
	Alcyon Corporation
	8716 Production Ave.
	San Diego, CA  92121

	@(#) ar68.h - Jul 26, 1983  REGULUS 4.1
*/

#pragma pack(push)
#pragma pack(2)

#define LIBMAGIC	0177545		// 0xFF65
#define LIBNSIZE	14
#define LIBHDSIZE	(sizeof(LIB2HDR))

#define LIB1MAGIC	0177555		// 0xFF6D
#define LIB1NSIZE	8
#define LIB1HDSIZE	(sizeof(LIB1HDR))

#define LIB2MAGIC	0xffffff65	// 0xFF65
#define LIB2HDSIZE	(sizeof(LIB2HDR))
#define LIB2NSIZE	14

#define LIB3MAGIC	0x3c21
#define LIB3HDSIZE	(sizeof(LIB3HDR))
#define LIB3NSIZE	16


typedef struct libhdr {
	char			 l1fname[LIB1NSIZE];
	unsigned __int32 l1modti;
	char			 l1userid;
	char			 l1fimode;
	unsigned __int16 l1fsize;
} LIB1HDR;


typedef struct lib2hdr {
	char			 l2fname[LIBNSIZE];
	unsigned __int32 l2modti;
	char			 l2userid;
	char			 l2gid;
	unsigned __int16 l2fimode;
	unsigned __int32 l2fsize;
	unsigned __int16 l2reserved;
} LIB2HDR;


typedef struct ar_hdr {
	char	ar_name[LIB3NSIZE];
	char	ar_date[12];
	char	ar_uid[6];
	char	ar_gid[6];
	char	ar_mode[8];
	char	ar_size[10];
	char	ar_fmag[2];
} LIB3HDR;

#pragma pack(pop)

