#pragma once
/************************************************/
/*                                              */
/*      Definitions for use with C programs     */
/*      according to CP/M-68K (tm) standard     */
/*      coding practices                        */
/*                                              */
/************************************************/

/*
 * Standard type definitions
 */
                                    /***************************/
#define BYTE    char                /* Signed byte             */
#define UBYTE   unsigned char       /* Unsigned byte           */
#define BOOLEAN char                /* 2 valued (true/false)   */
#define WORD    short               /* Signed word (16 bits)   */
#define UWORD   unsigned int        /* unsigned word           */
#define LONG    long                /* signed long (32 bits)   */
#define ULONG   unsigned long       /* Unsigned long           */
#define REG     register            /* register variable       */
#define LOCAL   auto                /* Local var on 68000      */
#define EXTERN  extern              /* External variable       */
#define MLOCAL  static              /* Local to module         */
#define GLOBAL  extern              /* Global variable         */
#define VOID    void                /* Void function return    */
                                    /***************************/



/****************************************************************************/
/*                     Miscellaneous Definitions:                           */
/****************************************************************************/
#define ERROR   (-1)                /* Function failure return val */
#define FAILURE (-1)                /* Function failure return val */
#define SUCCESS (0)                 /* Function success return val */
#define YES     1                   /* "TRUE"                      */
#define NO      0                   /* "FALSE"                     */
#define FOREVER for(;;)             /* Infinite loop declaration   */
#define TRUE    1                   /* Function TRUE  value        */
#define FALSE   0                   /* Function FALSE value        */


/************************************************/
