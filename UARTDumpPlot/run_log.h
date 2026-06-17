#ifndef _RUN_LOG_H__
#define _RUN_LOG_H__

typedef enum{
    LOG_INFO=0,
    LOG_ERR=1,
}LogType_t;
extern int run_log_init(const char *filename);
extern void run_log_add(LogType_t type, const char *log, int len);
extern void run_log_flush();

#endif //_RUN_LOG_H__