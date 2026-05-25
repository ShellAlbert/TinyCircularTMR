//filename: uart_dump.c
//function: dump 3 uarts data to different files.
//date: May 25, 2025.
//author: anonymous.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <pthread.h>

typedef struct {
    const char *device_path;
    const char *output_file;
} uart_config_t;

int setup_uart(int fd, speed_t baud_rate) {
    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    //get-modify-set.
    if (tcgetattr(fd, &tty) != 0) {
        perror("Error from tcgetattr");
        return -1;
    }

    //baudrate setting.
    cfsetospeed(&tty, baud_rate);
    cfsetispeed(&tty, baud_rate);


    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    tty.c_cflag &= ~PARENB;                         // No parity
    tty.c_cflag &= ~CSTOPB;                         // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;                        // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL;                  // Enable receiver, ignore modem lines

    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    tty.c_cc[VMIN]  = 0;  
    tty.c_cc[VTIME] = 1; //timeout 100ms.

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Error from tcsetattr");
        return -1;
    }

    return 0;
}

void *uart_thread_func(void *arg) {
    uart_config_t *config = (uart_config_t *)arg;
    
    printf("[Thread] Starting for device: %s, saving to: %s\n", config->device_path, config->output_file);

    int fd = open(config->device_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("Error opening UART");
        pthread_exit(NULL);
    }

    if (setup_uart(fd, B4000000) != 0) {
        close(fd);
        pthread_exit(NULL);
    }

    FILE *fp = fopen(config->output_file, "wb");
    if (!fp) {
        perror("Error opening output file");
        close(fd);
        pthread_exit(NULL);
    }

    //loop to read data.
    char buffer[128];
    ssize_t bytes_read;
    while (1) {
        bytes_read = read(fd, buffer, sizeof(buffer));
        if (bytes_read > 0) {
            size_t written = fwrite(buffer, 1, bytes_read, fp);
            if (written != (size_t)bytes_read) {
                perror("Error writing to file");
                break;
            }
            fflush(fp); 
        } else if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                //data is unavailable, call sleep() to prevent heavy CPU load.
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

    fclose(fp);
    close(fd);
    printf("[Thread] Finished for device: %s\n", config->device_path);
    pthread_exit(NULL);
}

int main() {
    //thread ID.
    pthread_t threads[3];
    uart_config_t configs[3] = {
        {"/dev/ttyUSB0", "data_usb0.dat"},
        {"/dev/ttyUSB1", "data_usb1.dat"},
        {"/dev/ttyUSB2", "data_usb2.dat"}
    };

    for (int i = 0; i < 3; i++) {
        int rc = pthread_create(&threads[i], NULL, uart_thread_func, (void *)&configs[i]);
        if (rc) {
            fprintf(stderr, "Error: unable to create thread %d, rc=%d\n", i, rc);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("All threads completed.\n");
    return 0;
}
