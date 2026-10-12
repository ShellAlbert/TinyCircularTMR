#include "ZLog.h"
#include <stdio.h>
int zlog_init(ZLog_t *t)
{
    if(t==NULL)
    {
        return -1;
    }
    t->fp=fopen(t->file_name,"w");
    if(NULL==t->fp)
    {
        perror("fopen() failed");
        return -1;
    }
    return 0;
}
int zlog_append(ZLog_t *t, const char *message, int size)
{
    if(NULL==t)
    {
        return -1;
    }
    if(t->fp==NULL)
    {
        return -1;
    }
    if(1!=fwrite(message,size,1,t->fp))
    {
        perror("fwrite() failed\n");
        return -1;
    }
    return 0;
}
int zlog_append_flush(ZLog_t *t, const char *message, int size)
{
    int rc=zlog_append(t,message,size);
    if(rc<0)
    {
        return rc;
    }
    fflush(t->fp);
    return 0;
}
int zlog_uninit(ZLog_t *t)
{
    if(NULL==t)
    {
        return 0;
    }
    if(t->fp==NULL)
    {
        return 0;
    }
    fflush(t->fp);
    fclose(t->fp);
    t->fp=NULL;
    return 0;
}