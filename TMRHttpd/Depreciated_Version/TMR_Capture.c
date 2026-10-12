// filename: TMR_Capture.c
// function: dump 3 uarts data to different named FIFO.
// date: May 25, 2025.
// author: anonymous.

// TMR_Capture.bin -tp -d 0
//-device 0: /dev/ttyUSB0
//-device 1: /dev/ttyUSB1
//-device 2: /dev/ttyUSB2

#define _GNU_SOURCE 1 // necessary for F_SETPIPE_SZ
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
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

void print_usage(char *app_name) {
  printf("TMR Capture Program %s\n", APP_VERSION);
  printf("%s <options>\n", app_name);
  printf("<options> list:\n");
  printf(
      "-d DeviceNode, 0:/dev/ttyUSB0, 1:/dev/ttyUSB1, 2:/dev/ttyUSB2, etc.\n");
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
void write_pid2file(char *filename) {
  FILE *fp_pid = fopen(filename, "w");
  if (fp_pid) {
    pid_t mypid = getpid();
    printf("my pid is %d\n", mypid);
    fprintf(fp_pid, "%d\n", mypid);
    fclose(fp_pid);
    fp_pid = NULL;
  } else {
    perror("fopen() failed\n");
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv) {
  int opt;
  int fd_uart = -1, fd_plot = -1;
  FILE *fp_http = NULL, *fp_plot = NULL;


  printf("Welcome to use %s Version %s\n", APP_NAME, APP_VERSION);

  // p: means parameter is necessary.
  // v: means no parameter.
  int device_node = 0;
  while ((opt = getopt(argc, argv, "stpvmlhd:")) > 0) {
    switch (opt) {
    case 's':
      simulation_mode = 1;
      // log_file_append("Simulation mode enabled.\n");
      break;
    case 't': // TCP dump mode.
      tcp_dump = 1;
      // log_file_append("TCP dump mode enabled.\n");
      break;
    case 'p': // Plot dump mode.
      plot_dump = 1;
      // log_file_append("Plot dump mode enabled.\n");
      break;
    case 'v':
      verbose = 1;
      // log_file_append("Verbose mode enabled.\n");
      break;
    case 'm':
      massive_verbose = 1;
      // log_file_append("Massive verbose mode enabled.\n");
      break;
    case 'l':
      log_enabled = 1;
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    case 'd':
      // optarg holds the argument provided to -d
      device_node = atoi(optarg);
      printf("Device path specified: %d\n", device_node);
      break;
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
  ///////////////////////////////////////////////////////////////////////////////////
  char *mypid_file = NULL;
  char *device_file = NULL;
  char *device_log_file = NULL;
  char *device_http_file = NULL;
  char *device_http_file_tmp = NULL;
  char *device_statistic_file = NULL;
  char *device_statistic_file_tmp = NULL;
  char *device_fifo_file = NULL;
  switch (device_node) {
  case 0:
    mypid_file = "/tmp/TMR_Phase_A.pid";
    device_file = "/dev/ttyUSB0";
    device_log_file = "/tmp/TMR_Phase_A.log";
    device_http_file = "/tmp/TMR_Phase_A.dat";
    device_http_file_tmp = "/tmp/TMR_Phase_A.dat.tmp";
    device_statistic_file = "/tmp/TMR_Phase_A.statistic";
    device_statistic_file_tmp = "/tmp/TMR_Phase_A.statistic.tmp";
    device_fifo_file = "/tmp/TMR_Phase_A.fifo";
    break;
  case 1:
    mypid_file = "/tmp/TMR_Phase_B.pid";
    device_file = "/dev/ttyUSB1";
    device_log_file = "/tmp/TMR_Phase_B.log";
    device_http_file = "/tmp/TMR_Phase_B.dat";
    device_http_file_tmp = "/tmp/TMR_Phase_B.dat.tmp";
    device_statistic_file = "/tmp/TMR_Phase_B.statistic";
    device_statistic_file_tmp = "/tmp/TMR_Phase_B.statistic.tmp";
    device_fifo_file = "/tmp/TMR_Phase_B.fifo";
    break;
  case 2:
    mypid_file = "/tmp/TMR_Phase_C.pid";
    device_file = "/dev/ttyUSB2";
    device_log_file = "/tmp/TMR_Phase_C.log";
    device_http_file = "/tmp/TMR_Phase_C.dat";
    device_http_file_tmp = "/tmp/TMR_Phase_C.dat.tmp";
    device_statistic_file = "/tmp/TMR_Phase_C.statistic";
    device_statistic_file_tmp = "/tmp/TMR_Phase_C.statistic.tmp";
    device_fifo_file = "/tmp/TMR_Phase_C.fifo";
    break;
  default:
    mypid_file = "/tmp/TMR_Phase_A.pid";
    device_file = "/dev/ttyUSB0";
    device_log_file = "/tmp/TMR_Phase_A.log";
    device_http_file = "/tmp/TMR_Phase_A.dat";
    device_http_file_tmp = "/tmp/TMR_Phase_A.dat.tmp";
    device_statistic_file = "/tmp/TMR_Phase_A.statistic";
    device_statistic_file_tmp = "/tmp/TMR_Phase_A.statistic.tmp";
    device_fifo_file = "/tmp/TMR_Phase_A.fifo";
    break;
  }
  ///////////////////////////////////////////////////////////////////////////////////

  // install signal handler.
  signal(SIGINT, signal_handler);
  // for a named FIFO, when reader exits, it emits SIG_PIPE will cause writer to
  // exit. here we ignore SIGPIPE.
  signal(SIGPIPE, SIG_IGN);

  // write my pid to file.
  write_pid2file(mypid_file);

  time_t time_start, time_end;
  volatile int capture_in_process = 0;
  volatile int captured_samples = 0;

  char msgFormat[128];

  // open UART device file: /dev/ttyUSB0.
  if ((fd_uart = open(device_file, O_RDONLY | O_NOCTTY)) < 0) {
    fprintf(stderr, "open() failed %s:%s\n", device_file, strerror(errno));
    return -1;
  }

  // configure parameters: 4Mbps,8N1.
  if (setup_uart(fd_uart, B4000000) != 0) {
    fprintf(stderr, "failed to set 4000000 baudrate %s, %s.\n", device_file,
            strerror(errno));
    close(fd_uart);
    return -1;
  }

  // open TCP temporary FIFO file: /tmp/TMR_Phase_A.dat.tmp.
  fp_http = fopen(device_http_file_tmp, "wb");
  if (!fp_http) {
    fprintf(stderr, "Error opening output file %s, %s.\n", device_http_file_tmp,
            strerror(errno));
    return -1;
  }

  // create the FIFO if it doesn't exist. /tmp/TMR_Phase_A.fifo
  // mkfifo returns -1 if it already exists (EEXIST), which is fine.
  if (plot_dump) {
    if (mkfifo(device_fifo_file, 0666) < 0) {
      if (errno != EEXIST) {
        perror("mkfifo plot_fifo failed");
        // If it already exists, we can still proceed, so don't exit
        // immediately. Check if it's actually a FIFO or just a regular file
        // error.
        exit(-1);
      }
    }
  }

  // ADC sample frequency is 100K Samples Per Second.
  // 55 XX XX XX : 4 bytes of each frame.
  // So each time we process 1 second data, 4 bytes*100K=400K.
  char buffer[400010];
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

  printf("Initial done, super loop starts.\n");
  // loop to read data.
  time(&time_start);
  while (!g_Exit) {
    // read maximum bytes as possible as we can.
    int bytes_available = sizeof(buffer) - buffer_len;
    // try to read device.
    int bytes_read = read(fd_uart, buffer + buffer_len, bytes_available);
    // printf("bytes_read:%d\n",bytes_read);
    if (bytes_read > 0) {
      buffer_len += bytes_read;

      // start to parse buffer.
      if (buffer_len < 400000) {
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
        printf("loop times:%d, remain bytes:%d\n", loop_times, remain_bytes);
      }
      unsigned char *pData = (unsigned char *)(buffer + valid_index);

      //loop to process batch data.
      for (int i = 0; i < loop_times; i++, pData += 4) {
        unsigned short high_bytes = ((unsigned short)pData[1] & 0xFF) << 8;
        unsigned short low_bytes = (unsigned short)pData[2] & 0x00FF;
        unsigned short ADC_Value_16bits = high_bytes | low_bytes;

        //bypass unprocessed data to exit in time.
        if (g_Exit) {
          break;
        }

        if (massive_verbose && log_enabled) {
          printf("%s, %04x %02x %02x %02x %02x\n", device_fifo_file,
                 ADC_Value_16bits, pData[0], pData[1], pData[2], pData[3]);
        }

        // linear mapping ADC value to realistic current.
        // y=kx+b.
        float k_coefficient = 1.0f;
        float b_offset = 0.0f; //-32768.0f;
        float TMR_Current = ADC_Value_16bits * k_coefficient + b_offset;

        ////////////////////////////////////////////////////////////////////////////////////////////////
        // write to fifo for real-time plotting.
        if (plot_dump) {
          if (fd_plot < 0) {
            printf("try to open %s\n", device_fifo_file);
            fd_plot =
                open(device_fifo_file, O_WRONLY | O_NONBLOCK /*| O_NOCTTY*/);
            // cat /proc/sys/fs/pipe-max-size  1048576
            // echo 65536 | sudo tee  /proc/sys/fs/pipe-max-size
            // must run with root to set fifo size.
            if (fd_plot > 0) {
              printf("%s, fd_plot:%d\n", device_fifo_file, fd_plot);
              if (fcntl(fd_plot, F_SETPIPE_SZ, 4096) == -1) {
                perror("fcntl F_SETPIPE_SZ");
                printf("errno=%d\n", errno);
              }
            } else {
              perror("open() failed, try next time");
              usleep(1000 * 1000 * 10);
              // No such device or address
              // open() succeeds until some one opens the named FIFO for
              // reading.
            }
          } else {
            // float tmr_current = ADC_Value_16bits;
            size_t ret = write(fd_plot, &TMR_Current, sizeof(TMR_Current));
            if (ret < 0) {
              printf("write fifo failed, force to reopen.\n");
              fd_plot = -1;
            }
          }
        }
        ////////////////////////////////////////////////////////////////////////////////////////////////

        // write to tmp file for http showing.
        // fetch data every 10 seconds.
        if (!capture_in_process) {
          time(&time_end);
          double second_escaped = difftime(time_end, time_start);
          if (second_escaped > 2.0) {
            capture_in_process = 1;
            fp_http = fopen(device_http_file_tmp, "wb");
            if (massive_verbose && log_enabled) {
              printf("2s escaped, start to write %s\n", device_http_file_tmp);
            }
          }
        } else {
          if (captured_samples >= 1024) { // only sample 1024 times.
            fflush(fp_http);
            fclose(fp_http);
            if (massive_verbose && log_enabled) {
              printf("write %d samples to file %s\n", captured_samples,
                     device_http_file_tmp);
            }
            // using rename strategy to prevend http server reads empty file.
            // rename operation is atomic.
            if (rename(device_http_file_tmp, device_http_file) != 0) {
              remove(device_http_file_tmp);
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
              printf("sum=%.2f,min=%.2f,max=%.2f,avg=%.2f,rms=%.2f\n",
                     batch.sumValue, batch.minValue, batch.maxValue,
                     batch.avgValue, batch.root_square);
            }
            ////////////////////////////////////////////////////////////////////////////////////////////////////////
            // write results into file.
            FILE *fp_rms = fopen(device_statistic_file_tmp, "w");
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
              if (rename(device_statistic_file_tmp, device_statistic_file) !=
                  0) {
                remove(device_statistic_file_tmp);
                printf("rename() failed.\n");
              }
            }
            //////////////////////////////////////////////////////////////////////////////////////////////////
            // reset counters.
            batch.sumValue = 0;
            batch.maxValue = 0;
            batch.minValue = 65535.0f;
            batch.avgValue = 0;
            batch.sum_square = 0;
            batch.mean_square = 0;
            batch.root_square = 0;
          } else {
            fwrite(&TMR_Current, sizeof(float), 1, fp_http);
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
  close(fd_plot);
  close(fd_uart);
  return 0;
}
////////////////////////////////////////////////////////////////////////////////////////