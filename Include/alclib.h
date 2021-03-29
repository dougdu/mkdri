#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <sys\stat.h>
#include <stdtypes.h>
#include <cout.h>
#include <ar68.h>

// cputc.cpp
int cputc(char c, int fn);

// getarhd.cpp
int getarhd(FILE* fp, struct lib2hdr* arptr);

// getchd.cpp
int getchd(FILE* fp, struct hdr2* arptr);

// libget.cpp
int lgetl(unsigned __int32* lp, FILE* f);
int lgetw(unsigned __int16* lp, FILE* f);
int lgetb(unsigned __int8* lp, FILE* f);
unsigned __int32 _lgetl(FILE* fp);
unsigned __int16 _lgetw(FILE* fp);
unsigned __int8  _lgetb(FILE* fp);

// libput.cpp
int lputl(unsigned __int32* lp, FILE* f);
int lputw(unsigned __int16* lp, FILE* f);
int lputb(unsigned __int8* lp, FILE* f);
int _lputl(unsigned __int32 uValue, FILE* fp);
int _lputw(unsigned __int16 uValue, FILE* fp);
int _lputb(unsigned __int8 uValue, FILE* fp);

// libread.cpp
int lreadl(int fd, unsigned __int32* str);
int lreadw(int fd, unsigned __int16* str);
int lreadc(int fd, char* str);

// libwrite.cpp
int lwritel(int fd, unsigned __int32* str);
int lwritew(int fd, unsigned __int16* str);
int lwritec(int fd, char* str);

// putarhd.cpp
int putarhd(FILE* fp, struct lib2hdr* arptr);

// putchd.cpp
int putchd(FILE* fp, struct hdr2* arptr);