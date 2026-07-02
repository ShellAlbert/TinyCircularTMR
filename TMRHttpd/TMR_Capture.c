// filename: TMR_Capture.c
// function: dump 3 uarts data to different named FIFO.
// date: May 25, 2025.
// author: anonymous.

// quick command for only gnuplot testing
//./uart_dump.bin -s -p -v

#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
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

#define APP_VERSION "1.0.0" // May 25, 2026.
#define MY_PID_FILE "/tmp/TMR_Capture.pid"
#define MY_LOG_FILE "/tmp/TMR_Capture.log"

// global exit flag.
volatile int g_Exit = 0;

int verbose = 0;
int simulation_mode = 0;
int tcp_dump = 0;
int plot_dump = 0;

// using rename strategy to avoid TMR_httpd.bin read empty file.
// rename and mv is atomic operation in linux.
typedef struct {
  const char *device_path;
  const char *http_file_tmp; // temporary file for http service.
  const char *http_file;     // for http service.
  const char *plot_fifo;     // for local gnuplot.

  FILE *fp_http;
  FILE *fp_plot;
  int fd_plot; //NON_BLOCK only valid in open()/write()/close() API layer.
} uart_config_t;

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
      if(config->fd_plot<0)
      {
        config->fd_plot=open(config->plot_fifo, O_WRONLY|O_NONBLOCK);
      }else{
        size_t ret=write(config->fd_plot,&phase_value,sizeof(phase_value));
        if(ret<0)
        {
          printf("write fifo failed, force to reopen.\n");
          config->fd_plot=-1; 
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
  fclose(config->fp_plot);
  printf("[Thread] Finished for device: %s\n", config->device_path);
  pthread_exit(0);
}
//////////////////////////////////////////////////////////////////////////////
#define UART_BUF_SIZE 4096
#define SYNC_HEADER 0x55

// Circular Buffer Structure
typedef struct {
    unsigned char buffer[UART_BUF_SIZE];
    int head; // Write index
    int tail; // Read index
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

// Get a byte from circular buffer without removing it (peek)
// Returns -1 if empty
int rb_peek(RingBuffer_t *rb, int offset) {
  if (offset >= rb->count) 
  {
    return -1;
  }
  int index = (rb->tail + offset) % UART_BUF_SIZE;
  return rb->buffer[index];
}

// Remove n bytes from the buffer
void rb_consume(RingBuffer_t *rb, int n) {
  if (n > rb->count) 
  {
    n = rb->count;
  }
  rb->tail = (rb->tail + n) % UART_BUF_SIZE;
  rb->count -= n;
}

// hardware thread function.
void *hw_thread_func(void *arg) {
  int fd;
  FILE *fp_tcp, *fp_plot;
  RingBuffer_t rb={.head=0,.tail=0,.count=0};

  uart_config_t *config = (uart_config_t *)arg;
  printf("[Thread] Starting for device: %s, dumping to: %s and %s.\n",
         config->device_path, config->http_file, config->plot_fifo);

  // open UART device file.
  if ((fd = open(config->device_path, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0) {
    // perror("Error opening UART");
    fprintf(stderr, "Error opening file %s, %s.\n", config->device_path,
            strerror(errno));
    pthread_exit(NULL);
  }

  // configure parameters.
  if (setup_uart(fd, B4000000) != 0) {
    fprintf(stderr, "Error setting 4000000 baudrate %s, %s.\n",
            config->device_path, strerror(errno));
    close(fd);
    pthread_exit(NULL);
  }

  // open TCP file.
  fp_tcp = fopen(config->http_file, "wb");
  if (!fp_tcp) {
    // perror("Error opening output file");
    fprintf(stderr, "Error opening file %s, %s.\n", config->http_file,
            strerror(errno));
    close(fd);
    pthread_exit(NULL);
  }

  // loop to read data.
  char buffer[128];
  ssize_t bytes_read;
  while (1) {
    // read data from UART.
    bytes_read = read(fd, buffer, sizeof(buffer));
    if (bytes_read > 0) {
      // write data to tcp FIFO.
      size_t wr_bytes1 = fwrite(buffer, 1, bytes_read, fp_tcp);
      if (wr_bytes1 != (size_t)bytes_read) {
        perror("Error writing to file");
        break;
      }
      fflush(fp_tcp);

      // write data to plot FIFO.
      size_t wr_bytes2 = fwrite(buffer, 1, bytes_read, fp_plot);
      if (wr_bytes2 != (size_t)bytes_read) {
        perror("Error writing to file");
        break;
      }
      fflush(fp_plot);
    } else if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // data is unavailable, call sleep() to prevent heavy CPU load.
        usleep(1000);
        continue;
      } else {
        perror("Error reading from UART");
        break;
      }
    } else {
      // bytes_read == 0, not happen normally.
      usleep(1000);
    }
  }

  fclose(fp_tcp);
  fclose(fp_plot);
  close(fd);
  printf("[Thread] Finished for device: %s\n", config->device_path);
  pthread_exit(NULL);
}

void print_usage(char *app_name) {
  printf("Data Transfer UART -> Named FIFO %s\n", APP_VERSION);
  printf("%s <options>\n", app_name);
  printf("<options> list:\n");
  printf("-i <filename>  Write PID to the specified file.\n");
  printf("-s Simulation mode enabled, bypass the hardware.\n");
  printf("-t TCP dump enabled.\n");
  printf("-p Plot dump enabled.\n");
  printf("-v Verbose enabled, printf more messages.\n");
  printf("-h Output help message\n");
  printf("Compiled on %s %s", __DATE__, __TIME__);
}
// write my pid to file.
void write_pid2file(void) {
  FILE *fp_pid = fopen(MY_PID_FILE, "w");
  if (fp_pid) {
    pid_t mypid = getpid();
    fprintf(fp_pid, "%d\n", mypid);
    fclose(fp_pid);
    fp_pid = NULL;
  }
}

int main(int argc, char **argv) {
  int opt;
  char *file_pid = NULL;

  // thread ID.
  pthread_t threads[3];
  uart_config_t configs[3] = {
      {"/dev/ttyUSB0", "/tmp/TMR_Phase_A.dat.tmp", "/tmp/TMR_Phase_A.dat",
       "/tmp/TMR_Phase_A.fifo", 0, 0,-1},
      {"/dev/ttyUSB1", "/tmp/TMR_Phase_B.dat.tmp", "/tmp/TMR_Phase_B.dat",
       "/tmp/TMR_Phase_B.fifo", 0, 0,-1},
      {"/dev/ttyUSB2", "/tmp/TMR_Phase_C.dat.tmp", "/tmp/TMR_Phase_C.dat",
       "/tmp/TMR_Phase_C.fifo", 0, 0,-1}};

  // p: means parameter is necessary.
  // v: means no parameter.
  while ((opt = getopt(argc, argv, "i:stpvh")) > 0) {
    switch (opt) {
    case 'i':
      file_pid = optarg;
      break;
    case 's':
      simulation_mode = 1;
      printf("Simulation mode enabled.\n");
      break;
    case 't': // TCP dump mode.
      tcp_dump = 1;
      break;
    case 'p': // Plot dump mode.
      plot_dump = 1;
      break;
    case 'v':
      verbose = 1;
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
  //for a named FIFO, when reader exits, it emits SIG_PIPE will cause writer to exit.
  //here we ignore SIGPIPE.
  signal(SIGPIPE,SIG_IGN);

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

    for (int i = 0; i < 3; i++) {
      pthread_join(threads[i], NULL);
    }
  }

  printf("All threads exited normally.\n");
  return 0;
}



#if 0
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

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