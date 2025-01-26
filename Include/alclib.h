#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#if defined(_MSC_VER)
#include <io.h>
#include <sys\stat.h>
#else
#include <unistd.h>
#define _read read
#define _write write
#endif
#include <fcntl.h>
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
int lgetl(uint32_t* lp, FILE* f);
int lgetw(uint16_t* lp, FILE* f);
int lgetb(uint8_t* lp, FILE* f);
uint32_t _lgetl(FILE* fp);
uint16_t _lgetw(FILE* fp);
uint8_t  _lgetb(FILE* fp);

// libput.cpp
int lputl(uint32_t* lp, FILE* f);
int lputw(uint16_t* lp, FILE* f);
int lputb(uint8_t* lp, FILE* f);
int _lputl(uint32_t uValue, FILE* fp);
int _lputw(uint16_t uValue, FILE* fp);
int _lputb(uint8_t uValue, FILE* fp);

// libread.cpp
int lreadl(int fd, uint32_t* str);
int lreadw(int fd, uint16_t* str);
int lreadc(int fd, char* str);

// libwrite.cpp
int lwritel(int fd, uint32_t* str);
int lwritew(int fd, uint16_t* str);
int lwritec(int fd, char* str);

// putarhd.cpp
int putarhd(FILE* fp, struct lib2hdr* arptr);

// putchd.cpp
int putchd(FILE* fp, struct hdr2* arptr);