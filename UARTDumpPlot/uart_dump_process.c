//filename: uart_dump_process.c
//function: dump 3 uarts data to different named FIFO.
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
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>


#define APP_VERSION "1.0.0" //May 25, 2026.

//global exit flag.
volatile int g_Exit=0;

int verbose=0;
int simulation_mode=0; 
int tcp_dump=0; 
int plot_dump=0;


typedef struct {
    const char *device_path;
    const char *tcp_fifo;
    const char *plot_fifo;

    FILE *fp_tcp;
    FILE *fp_plot;
} uart_config_t;

//signal handler.
void signal_handler(int signo)
{
    switch(signo)
    {
        case SIGINT: 
            printf("Caught SIGINT....\n");
            g_Exit=1; 
            break;
        default:
            break;
    }
}

int setup_uart(int fd, speed_t baud_rate) 
{
    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    //get-modify-set.
    if (tcgetattr(fd, &tty) != 0)
    {
        //perror(), short format for fprintf(stderr, "%s: %s\n", str, strerror(errno));
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

    if (tcsetattr(fd, TCSANOW, &tty) != 0) 
    {
        perror("Error from tcsetattr");
        return -1;
    }
    return 0;
}

//simulation thread function.
void *simulation_thread_func(void *arg)
{
    uart_config_t *config = (uart_config_t *)arg;
    printf("[Thread] Starting for device: %s, dumping to: %s and %s.\n", config->device_path, config->tcp_fifo, config->plot_fifo);

    //create the FIFO if it doesn't exist
    //mkfifo returns -1 if it already exists (EEXIST), which is fine.
    if(tcp_dump)
    {
        if(mkfifo(config->tcp_fifo,0666)<0)
        {
            perror("mkfifo tcp_fifo failed");
            // If it already exists, we can still proceed, so don't exit immediately.
            // Check if it's actually a FIFO or just a regular file error.
            pthread_exit(NULL);
        }
    }

    //create the FIFO if it doesn't exist
    //mkfifo returns -1 if it already exists (EEXIST), which is fine.
    if(plot_dump)
    {
        if(mkfifo(config->plot_fifo,0666)<0)
        {
            perror("mkfifo plot_fifo failed");
            // If it already exists, we can still proceed, so don't exit immediately.
            // Check if it's actually a FIFO or just a regular file error.
            pthread_exit(NULL);
        }
    }

    //open TCP FIFO file.
    if(tcp_dump)
    {
        if(!(config->fp_tcp=fopen(config->tcp_fifo, "wb")))
        {
            //perror("Error opening output file");
            fprintf(stderr, "Error opening file %s, %s.\n", config->tcp_fifo, strerror(errno));
            pthread_exit(NULL);
        }
    }

    //open plot FIFO file.
    if(plot_dump)
    {
        if(!(config->fp_plot=fopen(config->plot_fifo, "wb")))
        {
            //perror("Error opening output file");
            fprintf(stderr, "Error opening file %s, %s.\n", config->tcp_fifo, strerror(errno));
            pthread_exit(NULL);
        }
    }

    srand((unsigned)time(NULL));

    while(!g_Exit)
    {
        float phase_value;
        switch(config->device_path[strlen(config->device_path)-1])
        {
            case '0':
                phase_value=(double)rand()/RAND_MAX *2.0;
                break;
            case '1':
                phase_value=(double)rand()/RAND_MAX *5.0;
                break;
            case '2':
                phase_value=(double)rand()/RAND_MAX *10.0;
                break;
            default:
                phase_value=(double)rand()/RAND_MAX;
                break;
        }
        
        if(tcp_dump)
        {
            fwrite(&phase_value,sizeof(float),1,config->fp_tcp);
        }
        if(plot_dump)
        {
            fwrite(&phase_value,sizeof(float),1,config->fp_plot);
        }

        //sleep to prevent heavy CPU load.
        usleep(1000*50);
    }
    fclose(config->fp_tcp);
    fclose(config->fp_plot);
    printf("[Thread] Finished for device: %s\n", config->device_path);
    pthread_exit(0);
}
//hardware thread function.
void *hw_thread_func(void *arg) {
    int fd;
    FILE *fp_tcp;
    FILE *fp_plot;
    uart_config_t *config = (uart_config_t *)arg;
    printf("[Thread] Starting for device: %s, dumping to: %s and %s.\n", config->device_path, config->tcp_fifo, config->plot_fifo);

    //open UART device file.
    if((fd=open(config->device_path, O_RDWR | O_NOCTTY | O_NONBLOCK))<0)
    {
        //perror("Error opening UART");
        fprintf(stderr, "Error opening file %s, %s.\n", config->device_path, strerror(errno));
        pthread_exit(NULL);
    }

    //configure parameters.
    if (setup_uart(fd, B4000000) != 0) {
        fprintf(stderr, "Error setting 4000000 baudrate %s, %s.\n", config->device_path, strerror(errno));
        close(fd);
        pthread_exit(NULL);
    }

    //open TCP file.
    fp_tcp = fopen(config->tcp_fifo, "wb");
    if (!fp_tcp) {
        //perror("Error opening output file");
        fprintf(stderr, "Error opening file %s, %s.\n", config->tcp_fifo, strerror(errno));
        close(fd);
        pthread_exit(NULL);
    }

    //loop to read data.
    char buffer[128];
    ssize_t bytes_read;
    while (1) 
    {
        //read data from UART.
        bytes_read = read(fd, buffer, sizeof(buffer));
        if (bytes_read > 0) 
        {
            //write data to tcp FIFO.
            size_t wr_bytes1 = fwrite(buffer, 1, bytes_read, fp_tcp);
            if (wr_bytes1 != (size_t)bytes_read) 
            {
                perror("Error writing to file");
                break;
            }
            fflush(fp_tcp); 

            //write data to plot FIFO.
            size_t wr_bytes2 = fwrite(buffer, 1, bytes_read, fp_plot);
            if (wr_bytes2 != (size_t)bytes_read) 
            {
                perror("Error writing to file");
                break;
            }
            fflush(fp_plot); 
        } else if (bytes_read < 0) 
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) 
            {
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

    fclose(fp_tcp);
    fclose(fp_plot);
    close(fd);
    printf("[Thread] Finished for device: %s\n", config->device_path);
    pthread_exit(NULL);
}

void print_usage(char *app_name)
{
    printf("Data Transfer UART -> Named FIFO %s\n", APP_VERSION);
    printf("%s <options>\n", app_name);
    printf("<options> list:\n");
    printf("-i <filename>  Write PID to the specified file.\n");
    printf("-s Simulation mode enabled, bypass the hardware.\n");
    printf("-t TCP dump enabled.\n");
    printf("-p Plot dump enabled.\n");
    printf("-v Verbose enabled, printf more messages.\n");
    printf("-h Output help message\n");
    printf("Compiled on %s %s", __DATE__,__TIME__);
}
int main(int argc, char **argv) {
    int opt;
    char *file_pid=NULL;

    //thread ID.
    pthread_t threads[3];
    uart_config_t configs[3] = {
        {"/dev/ttyUSB0", "/tmp/phase_a_tcp.fifo", "/tmp/phase_a_plot.fifo",0,0},
        {"/dev/ttyUSB1", "/tmp/phase_b_tcp.fifo", "/tmp/phase_b_plot.fifo",0,0},
        {"/dev/ttyUSB2", "/tmp/phase_c_tcp.fifo", "/tmp/phase_c_plot.fifo",0,0}
    };

    //p: means parameter is necessary.
    //v: means no parameter.
    while((opt=getopt(argc,argv,"i:stpvh"))>0)
    {
        switch(opt)
        {
            case 'i': 
                file_pid=optarg;
                break;
            case 's': 
                simulation_mode=1;
                printf("Simulation mode enabled.\n");
                break;
            case 't': //TCP dump mode.
                tcp_dump=1;
                break;
            case 'p': //Plot dump mode.
                plot_dump=1;
                break;
            case 'v':
                verbose=1; 
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case '?':
                if(optopt=='p'){
                    fprintf(stderr, "Option -p requires an argument.");
                }else{
                    fprintf(stderr, "Unknown option %c\n", optopt);
                }
                return -1;
            default:
                break;
        }
    }

    //install signal handler.
    signal(SIGINT,signal_handler);

    //write PID to file.
    if(file_pid)
    {
        FILE *fp=fopen(file_pid,"w");
        if(!fp)
        {
            fprintf(stderr, "Error at open PID file:%s\n", strerror(errno));
            return -1;    
        }
        fprintf(fp,"%d\n",getpid());
        fclose(fp);
        printf("PId written to file %s\n",file_pid);
    }

    if(simulation_mode)
    {
        //simulation mode enabled, bypass the hardware.
        for (int i = 0; i < 3; i++) {
            int rc = pthread_create(&threads[i], NULL, simulation_thread_func, (void *)&configs[i]);
            if (rc) {
                fprintf(stderr, "Error: unable to create thread %d, rc=%d\n", i, rc);
                exit(-1);
            }
        }

        for (int i = 0; i < 3; i++) {
            pthread_join(threads[i], NULL);
        }
    }else{
        //create threads.
        for (int i = 0; i < 3; i++) {
            int rc = pthread_create(&threads[i], NULL, hw_thread_func, (void *)&configs[i]);
            if (rc) {
                fprintf(stderr, "Error: unable to create thread %d, rc=%d\n", i, rc);
                exit(-1);
            }
        }

        for (int i = 0; i < 3; i++) 
        {
            pthread_join(threads[i], NULL);
        }
    }

    printf("All threads completed.\n");
    return 0;
}
