#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>
#include <time.h>

volatile int g_Exit=0;

typedef struct{
    char *file_name;
    FILE *fp;
}fifo_config_t;


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


int main() {
    int i;
    float phase_value[3];
    long long n=0;
    fifo_config_t configs[3]={
        {"/tmp/phase_a_plot.dat",0},
        {"/tmp/phase_b_plot.dat",0},
        {"/tmp/phase_c_plot.dat",0}
    };

    for(i=0;i<3;i++)
    {
            //1. Create the FIFO if it doesn't exist
            //mkfifo returns -1 if it already exists (EEXIST), which is fine.
        if(mkfifo(configs[i].file_name,0666)<0)
        {
            perror("mkfifo failed");
            // If it already exists, we can still proceed, so don't exit immediately.
            // Check if it's actually a FIFO or just a regular file error.
            return -1;
        }
    }
    printf("Writer: Waiting for reader to open the FIFO...\n");

    // 2. Open the FIFO for writing
    // This call will BLOCK until a reader opens the FIFO for reading.
    for(i=0;i<3;i++)
    {
        configs[i].fp=fopen(configs[i].file_name,"w");
        if(configs[i].fp<0)
        {
            perror("open for writing failed");
        }
    }

    printf("Writer: Reader connected. Sending messages...\n");
    srand((unsigned)time(NULL));
    while(g_Exit)
    {
        //generate phase value.
        phase_value[0]=fmod((double)(n % 64), 64.0) / 64.0;
        phase_value[1]=0.5 + 0.5 * sin(2.0 * M_PI * (double)n / 32.0);
        phase_value[2]=(double)rand() / RAND_MAX;
        n++;

        //write data to FIFO.
        fwrite(&phase_value[0],sizeof(float),1,configs[0].fp);
        fwrite(&phase_value[1],sizeof(float),1,configs[1].fp);
        fwrite(&phase_value[2],sizeof(float),1,configs[2].fp);

        printf("New Value: [0]=%.2f, [1]=%.2f, [2]=%.2f\n", phase_value[0],phase_value[1],phase_value[2]);
        //sleep to prevent heavy CPU load.
        usleep(1000*50);
    }

    for(int i=0;i<3;i++)
    {
        fclose(configs[i].fp);
        unlink(configs[i].file_name);
    }

    printf("Writer: Finished.\n");
    return 0;
}
