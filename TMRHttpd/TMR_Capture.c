// filename: TMR_Capture.c
// function: dump 3 uarts data to different named FIFO.
// date: May 25, 2025.
// author: anonymous.

// quick command for only gnuplot testing
//./uart_dump.bin -s -p -v

#define _GNU_SOURCE 1 // necessary for F_SETPIPE_SZ
#include "ZLog.h"
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define APP_NAME "TMR_Capture"
#define APP_VERSION "1.0.0" // May 25, 2026.
#define MY_PID_FILE "/tmp/TMR_Capture.pid"
#define MY_LOG_FILE "/tmp/TMR_Capture.log"
#define MAX_LOG_FILE_SIZE 1 * 1024 * 1024 // 10Mb

// global exit flag.
volatile int g_Exit = 0;

int verbose = 0;
int massive_verbose = 0;
int simulation_mode = 0;
int tcp_dump = 0;
int plot_dump = 0;
int log_enabled = 0;

#define UART_BUF_SIZE 4096
#define SYNC_HEADER 0x55

// log file.
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *fp_log = NULL;
void log_file_rotate(void);
void log_file_append(const char *message);

// Circular Buffer Structure
typedef struct {
  unsigned char buffer[UART_BUF_SIZE];
  int head;  // Write index
  int tail;  // Read index
  int count; // Number of bytes in buffer
} RingBuffer_t;
// Thread-safe or Interrupt-safe add (simplified for single-thread example)
void rb_put(RingBuffer_t *rb, unsigned char data) {
  if (rb->count >= UART_BUF_SIZE) {
    // Buffer overflow: Decide strategy.
    // For high speed, usually overwrite oldest or drop new.
    // Here we drop new to keep consistency, or you could advance tail.
    return;
  }
  rb->buffer[rb->head] = data;
  rb->head = (rb->head + 1) % UART_BUF_SIZE;
  rb->count++;
}
void rb_mput(RingBuffer_t *rb, unsigned char *data, unsigned int size) {
  if (rb == NULL || data == NULL || size == 0) {
    return;
  }

  for (unsigned int i = 0; i < size; i++) {
    // Check if buffer is full
    if (rb->count >= UART_BUF_SIZE) {
      // Buffer overflow: Drop the remaining new data to keep consistency.
      // Alternatively, you could break here to stop writing further.
      return;
    }

    // Write single byte
    rb->buffer[rb->head] = data[i];

    // Advance head index with wrap-around
    rb->head = (rb->head + 1) % UART_BUF_SIZE;

    // Increment count
    rb->count++;
  }
}
// Get a byte from circular buffer without removing it (peek)
// Returns -1 if empty
int rb_peek(RingBuffer_t *rb, int offset) {
  if (offset >= rb->count) {
    return -1;
  }
  int index = (rb->tail + offset) % UART_BUF_SIZE;
  return rb->buffer[index];
}

// Remove n bytes from the buffer
void rb_consume(RingBuffer_t *rb, int n) {
  if (n > rb->count) {
    n = rb->count;
  }
  rb->tail = (rb->tail + n) % UART_BUF_SIZE;
  rb->count -= n;
}

// using rename strategy to avoid TMR_httpd.bin read empty file.
// rename and mv is atomic operation in linux.
typedef struct {
  const char *device_path;
  const char *http_file_tmp; // temporary file for http service.
  const char *http_file;     // for http service.
  const char *plot_fifo;     // for local gnuplot.
  const char *log_file;      // each ddevice has its own log file.

  FILE *fp_http;
  int fd_plot; // NON_BLOCK only valid in open()/write()/close() API layer.
  int fd_uart;
} uart_config_t;

// batch data calculation.
typedef struct {
  float sumValue; // the sum value.
  float maxValue; // Maximum Value.
  float minValue; // Minimum Value.
  float avgValue; // Average Value.

  float sum_square;
  float mean_square;
  float root_square; // Root Mean Square.
} BatchData_t;

// signal handler.
void signal_handler(int signo) {
  if (signo == SIGINT || signo == SIGKILL) {
    printf("Caught SIGINT....\n");
    g_Exit = 1;
  }
}

int setup_uart(int fd, speed_t baud_rate) {
  struct termios tty;
  memset(&tty, 0, sizeof(tty));

  // get-modify-set.
  if (tcgetattr(fd, &tty) != 0) {
    // perror(), short format for fprintf(stderr, "%s: %s\n", str,
    // strerror(errno));
    perror("Error from tcgetattr");
    return -1;
  }

  // baudrate setting.
  cfsetospeed(&tty, baud_rate);
  cfsetispeed(&tty, baud_rate);

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit chars
  tty.c_cflag &= ~PARENB;                     // No parity
  tty.c_cflag &= ~CSTOPB;                     // 1 stop bit
  tty.c_cflag &= ~CRTSCTS;                    // No hardware flow control
  tty.c_cflag |= CREAD | CLOCAL; // Enable receiver, ignore modem lines

  tty.c_iflag &=
      ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
  tty.c_oflag &= ~OPOST;
  tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 1; // timeout 100ms.

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    perror("Error from tcsetattr");
    return -1;
  }
  return 0;
}

// simulation thread function.
void *simulation_thread_func(void *arg) {
  time_t time_start, time_end;
  volatile int capture_in_process = 0;
  volatile int captured_samples = 0;

  uart_config_t *config = (uart_config_t *)arg;
  printf("[Thread] Starting for device %s, dump to %s and %s.\n", ///<
         config->device_path, config->http_file, config->plot_fifo);

  // create the FIFO if it doesn't exist
  // mkfifo returns -1 if it already exists (EEXIST), which is fine.
  if (plot_dump) {
    if (mkfifo(config->plot_fifo, 0666) < 0) {
      if (errno != EEXIST) {
        perror("mkfifo plot_fifo failed");
        // If it already exists, we can still proceed, so don't exit
        // immediately. Check if it's actually a FIFO or just a regular file
        // error.
        pthread_exit(NULL);
      }
    }
  }

  srand((unsigned)time(NULL));

  time(&time_start);
  while (!g_Exit) {
    float phase_value;
    switch (config->device_path[strlen(config->device_path) - 1]) {
    case '0':
      phase_value = (double)rand() / RAND_MAX * 2.0;
      break;
    case '1':
      phase_value = (double)rand() / RAND_MAX * 5.0;
      break;
    case '2':
      phase_value = (double)rand() / RAND_MAX * 10.0;
      break;
    default:
      phase_value = (double)rand() / RAND_MAX;
      break;
    }
    if (verbose) {
      printf("New phase value: %f\n", phase_value);
    }

    // write to fifo for real-time plotting.
    if (plot_dump) {
      if (config->fd_plot < 0) {
        config->fd_plot = open(config->plot_fifo, O_WRONLY | O_NONBLOCK);
      } else {
        size_t ret = write(config->fd_plot, &phase_value, sizeof(phase_value));
        if (ret < 0) {
          printf("write fifo failed, force to reopen.\n");
          config->fd_plot = -1;
        }
      }
    }

    // fetch data every 10 seconds.
    if (!capture_in_process) {
      time(&time_end);
      double second_escaped = difftime(time_end, time_start);
      if (second_escaped > 10.0) {
        capture_in_process = 1;
        config->fp_http = fopen(config->http_file_tmp, "wb");
        printf("10s escaped, start to write %s\n", config->http_file_tmp);
      }
    } else {
      if (captured_samples >= 1024) {
        fflush(config->fp_http);
        fclose(config->fp_http);
        printf("write %d samples to file %s\n", captured_samples,
               config->http_file_tmp);
        if (rename(config->http_file_tmp, config->http_file) != 0) {
          remove(config->http_file_tmp);
          printf("rename() failed.\n");
        }
        captured_samples = 0;
        capture_in_process = 0;
        time(&time_start);
      } else {
        fwrite(&phase_value, sizeof(float), 1, config->fp_http);
        captured_samples++;
      }
    }

    // sleep to prevent heavy CPU load.
    usleep(1000 * 50);
  }
  fclose(config->fp_http);
  close(config->fd_plot);
  printf("[Thread] Finished for device: %s\n", config->device_path);
  pthread_exit(0);
}
//////////////////////////////////////////////////////////////////////////////
// hardware thread function.
void *hw_thread_func(void *arg) {
  time_t time_start, time_end;
  volatile int capture_in_process = 0;
  volatile int captured_samples = 0;

  FILE *fp_tcp, *fp_plot;
  uart_config_t *myDev = (uart_config_t *)arg;

  char msgFormat[128];
  // firstly, create log file for this thread.
  ZLog_t myLog;
  myLog.file_name = myDev->log_file;
  if (zlog_init(&myLog) < 0) {
    pthread_exit((void *)0);
    return ((void *)0);
  }

  snprintf(msgFormat, sizeof(msgFormat), ///<
           "[Thread] Starting for device: %s, dump to %s and %s.\n",
           myDev->device_path, myDev->http_file, myDev->plot_fifo);
  zlog_append_flush(&myLog, msgFormat, strlen(msgFormat));

  // open UART device file.
  if ((myDev->fd_uart = open(myDev->device_path, O_RDONLY | O_NOCTTY)) < 0) {
    snprintf(msgFormat, sizeof(msgFormat), ///<
             "open() failed %s:%s\n", myDev->device_path, strerror(errno));
    zlog_append_flush(&myLog, msgFormat, strlen(msgFormat));
    pthread_exit((void *)0);
  }

  // configure parameters.
  if (setup_uart(myDev->fd_uart, B4000000) != 0) {
    snprintf(msgFormat, sizeof(msgFormat), ///<
             "failed to set 4000000 baudrate %s, %s.\n", myDev->device_path,
             strerror(errno));
    zlog_append_flush(&myLog, msgFormat, strlen(msgFormat));
    close(myDev->fd_uart);
    pthread_exit((void *)0);
  }

  // open TCP temporary FIFO file.
  fp_tcp = fopen(myDev->http_file_tmp, "wb");
  if (!fp_tcp) {
    // perror("Error opening output file");
    snprintf(msgFormat, sizeof(msgFormat), ///<
             "Error opening file %s, %s.\n", myDev->http_file_tmp,
             strerror(errno));
    zlog_append_flush(&myLog, msgFormat, strlen(msgFormat));
    close(myDev->fd_uart);
    pthread_exit(NULL);
  }

  // create the FIFO if it doesn't exist
  // mkfifo returns -1 if it already exists (EEXIST), which is fine.
  if (plot_dump) {
    if (mkfifo(myDev->plot_fifo, 0666) < 0) {
      if (errno != EEXIST) {
        perror("mkfifo plot_fifo failed");
        // If it already exists, we can still proceed, so don't exit
        // immediately. Check if it's actually a FIFO or just a regular file
        // error.
        pthread_exit(NULL);
      }
    }
  }

  char buffer[4096];
  int buffer_len = 0;
  // calculate Root Mean Square.
  float sum_squares = 0.0f;
  BatchData_t batch = {.sumValue = 0,
                       .maxValue = 0,
                       .minValue = 65536.0f,
                       .avgValue = 0,
                       .sum_square = 0,
                       .mean_square = 0,
                       .root_square = 0};

  // loop to read data.
  time(&time_start);
  while (!g_Exit) {
    // read maximum bytes as possible as we can.
    int bytes_available = sizeof(buffer) - buffer_len;
    // try to read device.
    int bytes_read = read(myDev->fd_uart, buffer + buffer_len, bytes_available);
    // printf("bytes_read:%d\n",bytes_read);
    if (bytes_read > 0) {
      buffer_len += bytes_read;

      // start to parse buffer.
      if (buffer_len < (4096 - 10)) {
        continue; // more data is needed.
      }

      // seek valid sync header position.
      int valid_index = -1;
      for (int i = 0; i < 4; i++) {
        if (buffer[i] == 0x55 && buffer[i + 4] == 0x55) {
          valid_index = i;
          break;
        }
      }
      if (valid_index < 0) // seek sync header failed, reset.
      {
        buffer_len = 0;
        continue;
      }
      // seek sync header success, continue to process.
      // 55 XX XX XX : 4 bytes of each frame.
      int loop_times = (buffer_len - valid_index) / 4;
      int remain_bytes = (buffer_len - valid_index) % 4;
      if (massive_verbose && log_enabled) {
        snprintf(msgFormat, sizeof(msgFormat),
                 "loop times:%d, remain bytes:%d\n", loop_times, remain_bytes);
        zlog_append(&myLog, msgFormat, strlen(msgFormat));
      }
      unsigned char *pData = (unsigned char *)(buffer + valid_index);
      for (int i = 0; i < loop_times; i++, pData += 4) {
        unsigned short high_bytes = ((unsigned short)pData[1] & 0xFF) << 8;
        unsigned short low_bytes = (unsigned short)pData[2] & 0x00FF;
        unsigned short ADC_Value_16bits = high_bytes | low_bytes;

        if (massive_verbose && log_enabled) {
          snprintf(msgFormat, sizeof(msgFormat), "%s, %04x %02x %02x %02x %02x\n",
                   myDev->plot_fifo, ADC_Value_16bits, pData[0], pData[1],
                   pData[2], pData[3]);
          zlog_append(&myLog, msgFormat, strlen(msgFormat));
        }

        // linear mapping ADC value to realistic current.
        // y=kx+b.
        float k_coefficient = 1.0f;
        float b_offset = 0.0f;
        float TMR_Current = ADC_Value_16bits * k_coefficient + b_offset;

        // write to fifo for real-time plotting.
        if (plot_dump) {
          if (myDev->fd_plot < 0) {
            myDev->fd_plot = open(myDev->plot_fifo, O_WRONLY | O_NONBLOCK | O_NOCTTY);
            // cat /proc/sys/fs/pipe-max-size  1048576
            // echo 65536 | sudo tee  /proc/sys/fs/pipe-max-size
            // must run with root to set fifo size.
            if (myDev->fd_plot > 0) {
              printf("fd_plot:%d\n", myDev->fd_plot);
              if (fcntl(myDev->fd_plot, F_SETPIPE_SZ, 4096) == -1) {
                perror("fcntl F_SETPIPE_SZ");
                printf("errno=%d\n", errno);
              }
            }
          } else {
            float tmr_current = ADC_Value_16bits;
            size_t ret =
                write(myDev->fd_plot, &tmr_current, sizeof(tmr_current));
            if (ret < 0) {
              printf("write fifo failed, force to reopen.\n");
              myDev->fd_plot = -1;
            }
          }
        }

        // write to tmp file for http showing.
        // fetch data every 10 seconds.
        if (!capture_in_process) {
          time(&time_end);
          double second_escaped = difftime(time_end, time_start);
          if (second_escaped > 2.0) {
            capture_in_process = 1;
            myDev->fp_http = fopen(myDev->http_file_tmp, "wb");
            if (massive_verbose && log_enabled) {
              snprintf(msgFormat, sizeof(msgFormat),
                       "2s escaped, start to write %s\n",
                       myDev->http_file_tmp);
              zlog_append(&myLog, msgFormat, strlen(msgFormat));
            }
          }
        } else {
          if (captured_samples >= 1024) { // only sample 1024 times.
            fflush(myDev->fp_http);
            fclose(myDev->fp_http);
            if (massive_verbose && log_enabled) {
              snprintf(msgFormat, sizeof(msgFormat),
                       "write %d samples to file %s\n", captured_samples,
                       myDev->http_file_tmp);
              zlog_append(&myLog, msgFormat, strlen(msgFormat));
            }
            // using rename strategy to prevend http server reads empty file.
            // rename operation is atomic.
            if (rename(myDev->http_file_tmp, myDev->http_file) != 0) {
              remove(myDev->http_file_tmp);
              printf("rename() failed.\n");
            }
            captured_samples = 0;
            capture_in_process = 0;
            time(&time_start);

            // calculate the average value.
            batch.avgValue = batch.sumValue / 1024.0f;

            // 2.calculate mean.
            batch.mean_square = batch.sum_square / 1024.0;
            // 3.calculate root.
            batch.root_square = sqrt(batch.mean_square);
            if (massive_verbose && log_enabled) {
              snprintf(msgFormat, sizeof(msgFormat),
                       "sum=%.2f,min=%.2f,max=%.2f,avg=%.2f,rms=%.2f\n",
                       batch.sumValue, batch.minValue, batch.maxValue,
                       batch.avgValue, batch.root_square);
              zlog_append(&myLog, msgFormat, strlen(msgFormat));
            }

            // write results into file.
            char file_statistic[128];
            char file_statistic_tmp[128];
            snprintf(file_statistic, sizeof(file_statistic), "%s.statistic",
                     myDev->http_file);
            snprintf(file_statistic_tmp, sizeof(file_statistic_tmp),
                     "%s.statistic.tmp", myDev->http_file);
            FILE *fp_rms = fopen(file_statistic_tmp, "w");
            if (fp_rms) {
              char buffer[128];
              snprintf(buffer, sizeof(buffer),
                       "sum=%.2f,min=%.2f,max=%.2f,avg=%.2f,rms=%.2f\n",
                       batch.sumValue, batch.minValue, batch.maxValue,
                       batch.avgValue, batch.root_square);
              fwrite(buffer, strlen(buffer), 1, fp_rms);
              fflush(fp_rms);
              fclose(fp_rms);
              // using rename strategy to prevend http server reads empty file.
              // rename operation is atomic.
              if (rename(file_statistic_tmp, file_statistic) != 0) {
                remove(file_statistic_tmp);
                printf("rename() failed.\n");
              }
            }
            // reset counters.
            batch.sumValue = 0;
            batch.maxValue = 0;
            batch.minValue = 65535.0f;
            batch.avgValue = 0;
            batch.sum_square = 0;
            batch.mean_square = 0;
            batch.root_square = 0;
          } else {
            fwrite(&TMR_Current, sizeof(float), 1, myDev->fp_http);
            captured_samples++;

            // sorting algorithm.
            batch.maxValue = (TMR_Current > batch.maxValue) ? (TMR_Current)
                                                            : (batch.maxValue);
            batch.minValue = (TMR_Current < batch.minValue) ? (TMR_Current)
                                                            : (batch.minValue);

            batch.sumValue += TMR_Current;
            // 1.calculate squares.
            batch.sum_square += pow(TMR_Current, 2);
          }
        }
      } // for (int i = 0; i < loop_times; i++, pData += 4) {
      // after processing. move remain bytes to top.
      if (remain_bytes > 0) {
        memmove(buffer, buffer + valid_index + loop_times * 4, remain_bytes);
        buffer_len = remain_bytes;
      } else {
        buffer_len = 0;
      }

      // write data to tcp FIFO.
      // int wr_bytes1 = fwrite(buffer, 1, bytes_read, fp_tcp);
      // printf("wr_bytes1=%d\n", wr_bytes1);
      // if (wr_bytes1 != bytes_read) {
      //   perror("Error writing to TCP FIFO file");
      //   break;
      // }
      // fflush(fp_tcp);

      // // write data to plot FIFO.
      // size_t wr_bytes2 = fwrite(buffer, 1, bytes_read, fp_plot);
      // if (wr_bytes2 != (size_t)bytes_read) {
      //   perror("Error writing to Plot FIFO file");
      //   break;
      // }
      // fflush(fp_plot);
    } else if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // data is unavailable, call sleep() to prevent heavy CPU load.
        usleep(1000);
        continue;
      } else {
        perror("Error reading from UART");
        break;
      }
    } else { // bytes_read == 0, not happen normally.
      usleep(1000);
    }
  }

  fclose(fp_tcp);
  close(myDev->fd_plot);
  close(myDev->fd_uart);
  printf("[Thread] Finished for device: %s\n", myDev->device_path);
  pthread_exit(NULL);
  // set exit flag to cause other threads also to exit.
  g_Exit = 1;
}

void print_usage(char *app_name) {
  printf("TMR Capture Program %s\n", APP_VERSION);
  printf("%s <options>\n", app_name);
  printf("<options> list:\n");
  printf("-s Simulation mode enabled, bypass the hardware.\n");
  printf("-t TCP dump enabled.\n");
  printf("-p Plot dump enabled.\n");
  printf("-v Verbose enabled, printf more messages.\n");
  printf("-m Massive Verbose enabled, printf massive messages.\n");
  printf("-l Log file enabled, log messages to file.\n");
  printf("-h Output help message\n");
  printf("common used combination parameters for debug -tpml\n");
  printf("long time running parameters -tp\n");
  printf("Compiled on %s %s", __DATE__, __TIME__);
}
// write my pid to file.
void write_pid2file(void) {
  FILE *fp_pid = fopen(MY_PID_FILE, "w");
  if (fp_pid) {
    pid_t mypid = getpid();
    printf("my pid is %d\n",mypid);
    fprintf(fp_pid, "%d\n", mypid);
    fclose(fp_pid);
    fp_pid = NULL;
  }else{
    perror("fopen() failed\n");
  }
}

int main(int argc, char **argv) {
  int opt;
  char *file_pid = NULL;

  // thread ID.
  pthread_t threads[3];
  uart_config_t configs[3] = {
      {"/dev/ttyUSB0", "/tmp/TMR_Phase_A.dat.tmp", "/tmp/TMR_Phase_A.dat",
       "/tmp/TMR_Phase_A.fifo", "/tmp/TMR_Phase_A.log", 0, -1,-1},
      {"/dev/ttyUSB1", "/tmp/TMR_Phase_B.dat.tmp", "/tmp/TMR_Phase_B.dat",
       "/tmp/TMR_Phase_B.fifo", "/tmp/TMR_Phase_B.log", 0, -1,-1},
      {"/dev/ttyUSB2", "/tmp/TMR_Phase_C.dat.tmp", "/tmp/TMR_Phase_C.dat",
       "/tmp/TMR_Phase_C.fifo", "/tmp/TMR_Phase_C.log", 0, -1,-1}};

  printf("Welcome to use %s Version %s\n",APP_NAME,APP_VERSION);

  // p: means parameter is necessary.
  // v: means no parameter.
  while ((opt = getopt(argc, argv, "stpvmlh")) > 0) {
    switch (opt) {
    case 's':
      simulation_mode = 1;
      log_file_append("Simulation mode enabled.\n");
      break;
    case 't': // TCP dump mode.
      tcp_dump = 1;
      log_file_append("TCP dump mode enabled.\n");
      break;
    case 'p': // Plot dump mode.
      plot_dump = 1;
      log_file_append("Plot dump mode enabled.\n");
      break;
    case 'v':
      verbose = 1;
      log_file_append("Verbose mode enabled.\n");
      break;
    case 'm':
      massive_verbose = 1;
      log_file_append("Massive verbose mode enabled.\n");
      break;
    case 'l':
      log_enabled = 1;
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    case '?':
      if (optopt == 'p') {
        fprintf(stderr, "Option -p requires an argument.");
      } else {
        fprintf(stderr, "Unknown option %c\n", optopt);
      }
      return -1;
    default:
      break;
    }
  }

  // install signal handler.
  signal(SIGINT, signal_handler);
  // for a named FIFO, when reader exits, it emits SIG_PIPE will cause writer to
  // exit. here we ignore SIGPIPE.
  signal(SIGPIPE, SIG_IGN);

  // write my pid to file.
  write_pid2file();

  if (simulation_mode) {
    // simulation mode enabled, bypass the hardware.
    for (int i = 0; i < 3; i++) {
      int rc = pthread_create(&threads[i], NULL, simulation_thread_func,
                              (void *)&configs[i]);
      if (rc) {
        fprintf(stderr, "Error: unable to create thread %d, rc=%d\n", i, rc);
        exit(-1);
      }
    }

    for (int i = 0; i < 3; i++) {
      pthread_join(threads[i], NULL);
    }
  } else {
    // create threads.
    for (int i = 0; i < 3; i++) {
      int rc = pthread_create(&threads[i], NULL, hw_thread_func,
                              (void *)&configs[i]);
      if (rc) {
        fprintf(stderr, "Error: unable to create thread %d, rc=%d\n", i, rc);
        exit(-1);
      }
    }

    printf("running in background, press Ctrl+C to quit...\n");
    printf("  ^_^   ...   ^_^  ...   \n");
    for (int i = 0; i < 3; i++) {
      pthread_join(threads[i], NULL);
    }
  }
  printf("All threads exited normally.  ^_^ \n");
  return 0;
}

// running log support section.
void log_file_rotate(void) {
  // check if the log file exceeds 10Mb.
  struct stat st;
  pthread_mutex_lock(&log_mutex);
  if (stat(MY_LOG_FILE, &st) < 0) {
    perror("stat() failed");
    fp_log = fopen(MY_LOG_FILE, "w");
    if (fp_log == NULL) {
      perror("Failed to open log file");
    } else {
      fprintf(fp_log, "Log file started.\n");
      fflush(fp_log);
    }
    pthread_mutex_unlock(&log_mutex);
    return;
  }

  // file size eexceeds pre-defined.
  if (st.st_size > MAX_LOG_FILE_SIZE) {
    // try to rename this file.
    int i = 0;
    do {
      char log_file_rename[128];
      snprintf(log_file_rename, sizeof(log_file_rename), "%s.%d", MY_LOG_FILE,
               i);
      // check whether this file exists or not.
      if (access(log_file_rename, F_OK) == 0) {
        i++;
        continue;
      }
      if (fp_log) {
        fprintf(fp_log, "Log file rotated successfully.\n");
        fflush(fp_log);
        fclose(fp_log);
        fp_log = NULL;
      }
      rename(MY_LOG_FILE, log_file_rename);
    } while (i < 1024);
  }
  if (fp_log == NULL) {
    fp_log = fopen(MY_LOG_FILE, "w");
    if (fp_log == NULL) {
      perror("Failed to open log file");
    } else {
      fprintf(fp_log, "Log file started.\n");
      fflush(fp_log);
    }
  }
  pthread_mutex_unlock(&log_mutex);
  return;
}

void log_file_append(const char *message) {
  if (!log_enabled) {
    return;
  }
  // check log file first.
  log_file_rotate();
  if (fp_log == NULL) {
    return;
  } else {
    pthread_mutex_lock(&log_mutex);
    fprintf(fp_log, "%s", message);
    pthread_mutex_unlock(&log_mutex);
  }
}

#if 0
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define UART_BUF_SIZE 4096
#define FRAME_LEN 4
#define HEADER_1 0x55
#define HEADER_2 0xAA

// Circular Buffer Structure
typedef struct {
    uint8_t buffer[UART_BUF_SIZE];
    int head; // Write index
    int tail; // Read index
    int count; // Number of bytes in buffer
} RingBuffer;

RingBuffer rb = { .head = 0, .tail = 0, .count = 0 };

// Thread-safe or Interrupt-safe add (simplified for single-thread example)
void rb_put(uint8_t data) {
    if (rb.count >= UART_BUF_SIZE) {
        // Buffer overflow: Decide strategy. 
        // For high speed, usually overwrite oldest or drop new. 
        // Here we drop new to keep consistency, or you could advance tail.
        return; 
    }
    rb.buffer[rb.head] = data;
    rb.head = (rb.head + 1) % UART_BUF_SIZE;
    rb.count++;
}

// Get a byte from circular buffer without removing it (peek)
// Returns -1 if empty
int rb_peek(int offset) {
    if (offset >= rb.count) return -1;
    int index = (rb.tail + offset) % UART_BUF_SIZE;
    return rb.buffer[index];
}

// Remove n bytes from the buffer
void rb_consume(int n) {
    if (n > rb.count) n = rb.count;
    rb.tail = (rb.tail + n) % UART_BUF_SIZE;
    rb.count -= n;
}

// Process the complete 16-bit data
void process_frame(uint8_t b2, uint8_t b3) {
    uint16_t data = ((uint16_t)b2 << 8) | b3;
    printf("Valid Frame: 55 AA %02X %02X -> Data: 0x%04X\n", b2, b3, data);
}

// Main Parsing Logic
void parse_uart_data() {
    while (rb.count >= FRAME_LEN) {
        // 1. Search for Sync Header (55 AA)
        // We scan from tail upwards. 
        // Optimization: In high speed, you might want to use memchr if buffer was linear,
        // but with circular buffer, manual loop is safer for wrap-around.
        
        int found = 0;
        int i = 0;
        
        // Look ahead up to current buffer count - 3 (since we need 4 bytes total)
        while (i <= rb.count - FRAME_LEN) {
            if (rb_peek(i) == HEADER_1 && rb_peek(i+1) == HEADER_2) {
                found = 1;
                break;
            }
            i++;
        }

        if (!found) {
            // No header found in current available data.
            // Consume all bytes? No, wait for more data.
            // But if buffer is full and no header, we might be stuck.
            // Strategy: If buffer is nearly full and no header, flush to prevent deadlock.
            if (rb.count > UART_BUF_SIZE - 10) {
                 fprintf(stderr, "Warning: Buffer full without sync. Flushing.\n");
                 rb_consume(rb.count);
            }
            return; 
        }

        // If we skipped some bytes to find the header, consume them
        if (i > 0) {
            rb_consume(i);
            continue; // Restart search from new tail
        }

        // 2. Header Found at Tail. Check if we have enough data (we already checked count >= 4)
        // Extract the payload bytes
        uint8_t b2 = rb_peek(2);
        uint8_t b3 = rb_peek(3);

        // 3. Optional: Validate checksum or range here if needed
        
        // 4. Process the frame
        process_frame(b2, b3);

        // 5. Remove the processed frame from buffer
        rb_consume(FRAME_LEN);
    }
}

// Simulated Read Thread / Main Loop
int main() {
    // Open UART (pseudo-code)
    // int fd = open("/dev/ttyS0", O_RDWR | O_NOCTTY | O_NONBLOCK);
    
    uint8_t raw_buf;
    
    while (1) {
        // 1. Read raw data from UART (Non-blocking)
        // ssize_t n = read(fd, raw_buf, sizeof(raw_buf));
        
        // SIMULATION: Injecting test data to demonstrate robustness
        // Scenario 1: Normal frame
        uint8_t test_data[] = {0x55, 0xAA, 0x12, 0x34};
        for(int k=0; k<4; k++) rb_put(test_data[k]);
        
        // Scenario 2: Garbage then valid frame
        uint8_t garbage[] = {0xFF, 0x00, 0x55, 0xAA, 0x56, 0x78};
        for(int k=0; k<6; k++) rb_put(garbage[k]);

        // Scenario 3: Split frame (Buffer wrap-around simulation)
        // Fill buffer almost to end
        // Then add 55 AA
        // Then add xx xx in next "read" cycle
        
        // 2. Parse available data
        parse_uart_data();
        
        // Small delay to prevent 100% CPU in simulation
        usleep(1000); 
    }
    return 0;
}
#endif