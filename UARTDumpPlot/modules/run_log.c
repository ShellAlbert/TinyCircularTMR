#include "run_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static FILE *fp = NULL;
int run_log_init(const char *filename) {
  fp = fopen(filename, "w");
  if(fp!=NULL)
  {
    char *log_init="TMR Log engine started.";
    run_log_add(LOG_INFO, log_init, strlen(log_init));
    return 0;
  }
  return -1;
}

void run_log_add(LogType_t type, const char *log, int len) {
  char buffer[256];
  // parameter check.
  if (fp == NULL || log == NULL || len <= 0) {
    return;
  }

  time_t current_time = time(NULL);
  struct tm *tm_info = localtime(&current_time);
  char current_time_buffer[128];
  strftime(current_time_buffer, sizeof(current_time_buffer),
           "%Y-%m-%d %H:%M:%S", tm_info);

  switch (type) {
  case LOG_INFO:
    snprintf(buffer, sizeof(buffer), "<INFO>,%s,%s\n", current_time_buffer,
             log);
    fwrite(buffer, strlen(buffer), 1, fp);
    break;
  case LOG_ERR:
    snprintf(buffer, sizeof(buffer), "<INFO>,%s,%s\n", current_time_buffer,
             log);
    fwrite(buffer, strlen(buffer), 1, fp);
    break;
  default:
    break;
  }
}
void run_log_flush() {
  if (fp != NULL) {
    fflush(fp);
  }
}
