#ifndef _ZLOG_H__
#define _ZLOG_H__

#include <stdio.h>
typedef struct{
    const char *file_name;
    FILE *fp;
}ZLog_t;

extern int zlog_init(ZLog_t *t);
extern int zlog_append(ZLog_t *t, const char *message, int size);
extern int zlog_append_flush(ZLog_t *t, const char *message, int size);
extern int zlog_uninit(ZLog_t *t);

#endif //_ZLOG_H__