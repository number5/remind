/***************************************************************/
/*                                                             */
/*  DYNBUF.H                                                   */
/*                                                             */
/*  Declaration of functions for manipulating dynamic buffers  */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2024 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#ifndef DYNBUF_H
#define DYNBUF_H

#include <stdio.h>  /* For FILE */

#define DBUF_STATIC_SIZE 128
typedef struct {
    char *buffer;
    size_t len;
    size_t allocatedLen;
    char staticBuf[DBUF_STATIC_SIZE];
} DynamicBuffer;

void DBufInit(DynamicBuffer *dbuf);
int DBufPutcFN(DynamicBuffer *dbuf, char c);
int DBufPuts(DynamicBuffer *dbuf, char const *str);
void DBufFree(DynamicBuffer *dbuf);
int DBufGets(DynamicBuffer *dbuf, FILE *fp);

#define DBufValue(bufPtr) ((bufPtr)->buffer)
#define DBufLen(bufPtr) ((bufPtr)->len)

#define DBufPutc(dbuf, c) ( ( (dbuf)->allocatedLen <= (dbuf)->len+1 ) ?   \
    DBufPutcFN( (dbuf), c) : \
    ( (dbuf)->buffer[(dbuf)->len++] = c, (dbuf)->buffer[(dbuf)->len] = 0, OK) )

#endif /* DYNBUF_H */
