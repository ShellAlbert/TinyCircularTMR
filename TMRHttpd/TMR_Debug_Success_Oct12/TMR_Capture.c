/* File name: TMR_Capture.c */
/* Function: Dump data from /dev/ttyUSBx to /tmp/TMR_Phase_X.dat for http request. */
/* Version: V0.0.1 */
/* Date: October 12, 2026 */
/* Author: Anonymous */

/* The consumer is faster than the producer, so the consumer often marks the double buffer empty. */
/* CAUTION */
/* printf() takes up CPU time, reduce to use it. */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <asm-generic/errno-base.h>
#include <math.h>

#define BUF_SIZE (1024)  /* UART Read Buffer Size */
#define SAMPLE_COUNT (180000) /* how many samples will be written into http file for http request. */

typedef struct
{
	uint8_t data[BUF_SIZE];
	size_t len;
} Buffer;

typedef struct
{
	Buffer buf[2];   // Double buffer.
	int write_idx;   // only changed under mutex
	int read_idx;    // only changed under mutex
	bool data_ready; // when data is consumed, set to false, when data is produced, set to true.
	bool running;

	pthread_mutex_t mutex;
	pthread_cond_t cond;

	const char *device_node; // /dev/ttyUSBx.
	const char *http_file;   // /tmp/TMR_Phase_A.dat for http request.
	const char *plot_file;  // /tmp/TMR_Phase_A.fifo for gnuplot.
	FILE *fp_http;
	int fd_plot;

	/* write samples to a temporary file for http request */
	time_t tm_past,tm_present;
	bool wrHttpInProcess;
	int cntWrHttpSamples; /* how many samples are written to http file */

	/* do value statistics. */
	float sum_value; // the sum value.
	float max_value; // Maximum Value.
	float min_value; // Minimum Value.
	float avg_value; // Average Value.

	float sum_square;
	float mean_square;
	float root_square; // Root Mean Square.
} TMRDevice;

/* TMR Device Channel 0 */
TMRDevice dev0 = {
	.write_idx = 0,
	.read_idx = 1,
	.data_ready = false,
	.running = true,

	.device_node = "/dev/ttyUSB0",
	.http_file = "/tmp/TMR_Phase_A.dat",
	.plot_file= "/tmp/TMR_Phase_A.fifo",
	.fp_http=NULL,
	.fd_plot=-1,
};

/* TMR Device Channel 1 */
TMRDevice dev1 = {
	.write_idx = 0,
	.read_idx = 1,
	.data_ready = false,
	.running = true,

	.device_node = "/dev/ttyUSB1",
	.http_file = "/tmp/TMR_Phase_B.dat",
	.plot_file= "/tmp/TMR_Phase_B.fifo",
	.fp_http=NULL,
	.fd_plot=-1,
};

/* TMR Device Channel 2 */
TMRDevice dev2 = {
	.write_idx = 0,
	.read_idx = 1,
	.data_ready = false,
	.running = true,

	.device_node = "/dev/ttyUSB2",
	.http_file = "/tmp/TMR_Phase_C.dat",
	.plot_file= "/tmp/TMR_Phase_C.fifo",
	.fp_http=NULL,
	.fd_plot=-1,
};

/* signal handler */
void signal_handler(int signo) {
	if (signo == SIGINT || signo == SIGKILL) {
		printf("Caught SIGINT, prepare to exit...\n");

		/* graceful stop dev0*/
		pthread_mutex_lock(&dev0.mutex);
		dev0.running = false;
		pthread_cond_signal(&dev0.cond);
		pthread_mutex_unlock(&dev0.mutex);

		/* graceful stop dev1*/
		pthread_mutex_lock(&dev1.mutex);
		dev1.running = false;
		pthread_cond_signal(&dev1.cond);
		pthread_mutex_unlock(&dev1.mutex);

		/* graceful stop dev2*/
		pthread_mutex_lock(&dev2.mutex);
		dev2.running = false;
		pthread_cond_signal(&dev2.cond);
		pthread_mutex_unlock(&dev2.mutex);
	}
}

/* forward declaration */
int setup_uart(int fd, speed_t baud_rate);
static void try_swap(TMRDevice *d);
void *uart_reader_thread(void *arg);

/* parse data, eliminate prefix sync headers. */
/* dump data to http file for http request. */
/* dump data to fifo file for gnuplot plotting. */
void *parser_thread(void *arg);
void parse_complete_frame(TMRDevice *dev,uint8_t *frame, size_t len);
void dump_frame2http(TMRDevice *dev, unsigned short value);
void dump_frame2plot(TMRDevice *dev, unsigned short value);
void reset_value_statistics(TMRDevice *dev)
{
	dev->sum_value=0.0f; // the sum value.
	dev->max_value=0.0f; // Maximum Value.
	dev->min_value=0.0f;  // Minimum Value.
	dev->avg_value=0.0f; // Average Value.
	dev->sum_square=0.0f;
	dev->mean_square=0.0f;
	dev->root_square=0.0f; // Root Mean Square.
}
void dump_statistic2file(TMRDevice *dev);

/* ------- super loop ----- */
int main(void)
{
	fprintf(stdout, "Welcome to TMR_Capture program\n");
	fprintf(stdout, "Version: 0.0.1\n");
	fprintf(stdout,"Compiled on %s %s\n",__DATE__,__TIME__);
	fprintf(stdout, "Phase A , /dev/ttyUSB0 -> /tmp/TMR_Phase_A.dat, /tmp/TMR_Phase_A.fifo \n");
	fprintf(stdout, "Phase B , /dev/ttyUSB1 -> /tmp/TMR_Phase_B.dat, /tmp/TMR_Phase_B.fifo \n");
	fprintf(stdout, "Phase C , /dev/ttyUSB2 -> /tmp/TMR_Phase_C.dat, /tmp/TMR_Phase_C.fifo \n");
	fprintf(stdout,"\n\n");

	// install signal handler.
	signal(SIGINT, signal_handler);
	// for a named FIFO, when reader exits, it emits SIG_PIPE will cause writer to
	// exit. here we ignore SIGPIPE.
	signal(SIGPIPE, SIG_IGN);

	/* device 0 , channel 0 */
	pthread_mutex_init(&dev0.mutex, NULL);
	pthread_cond_init(&dev0.cond, NULL);

	pthread_t uart_tid0, parser_tid0;
	pthread_create(&uart_tid0, NULL, uart_reader_thread, &dev0);
	pthread_create(&parser_tid0, NULL, parser_thread, &dev0);

	/* device 1 , channel 1 */
	pthread_mutex_init(&dev1.mutex, NULL);
	pthread_cond_init(&dev1.cond, NULL);

	pthread_t uart_tid1, parser_tid1;
	pthread_create(&uart_tid1, NULL, uart_reader_thread, &dev1);
	pthread_create(&parser_tid1, NULL, parser_thread, &dev1);

	/* device 2 , channel 2 */
	pthread_mutex_init(&dev2.mutex, NULL);
	pthread_cond_init(&dev2.cond, NULL);

	pthread_t uart_tid2, parser_tid2;
	pthread_create(&uart_tid2, NULL, uart_reader_thread, &dev2);
	pthread_create(&parser_tid2, NULL, parser_thread, &dev2);

	/* waiting until Ctrl+C to break */
	fprintf(stdout,"threads are running, press Ctrl+C to break...\n");
	pthread_join(uart_tid0, NULL);
	pthread_join(parser_tid0, NULL);
	pthread_join(uart_tid1, NULL);
	pthread_join(parser_tid1, NULL);
	pthread_join(uart_tid2, NULL);
	pthread_join(parser_tid2, NULL);

	/* do cleaning before quit */
	pthread_cond_destroy(&dev0.cond);
	pthread_mutex_destroy(&dev0.mutex);

	pthread_cond_destroy(&dev1.cond);
	pthread_mutex_destroy(&dev1.mutex);


	pthread_cond_destroy(&dev2.cond);
	pthread_mutex_destroy(&dev2.mutex);

	fclose(dev0.fp_http);
	fclose(dev1.fp_http);
	fclose(dev2.fp_http);
	fprintf(stdout,"TMR_Capture quit normally.\n");
	return 0;
}

/* -------------------------------------------------
 * Helper: perform the swap under lock
 * ------------------------------------------------- */
static void try_swap(TMRDevice *d)
{
	pthread_mutex_lock(&d->mutex);

	if (!d->data_ready)
	{
		/* swap the two buffers */
		int old_write = d->write_idx;
		d->write_idx = d->read_idx;
		d->read_idx = old_write;

		d->buf[d->write_idx].len = 0; /* clear the new write buffer */
		d->data_ready = true;

		pthread_cond_signal(&d->cond);
	}
	/* else: parser still busy → drop this buffer (policy choice) */

	pthread_mutex_unlock(&d->mutex);
}

/* -------------------------------------------------
 * UART reader thread (producer)
 * ------------------------------------------------- */
void *uart_reader_thread(void *arg)
{
	TMRDevice *d = (TMRDevice *)arg;
	int fd = -1;

	while (d->running)
	{
		/* try to open device node */
		if (fd < 0)
		{
			fd = open(d->device_node, O_RDONLY | O_NONBLOCK | O_NOCTTY);
			if (fd < 0)
			{
				fprintf(stderr, "error, failed to open: %s: %s\n", d->device_node, strerror(errno));
				d->running = false;
				break;
			}
			if (setup_uart(fd, B4000000) < 0)
			{
				fprintf(stderr, "error, failed to setup UART: %s: %s\n", d->device_node, strerror(errno));
				close(fd);
				fd = -1;
				break;
			}
		}

		/* ---------- lock-free write path ---------- */
		Buffer *wbuf = &d->buf[d->write_idx]; /* write_idx is stable until swap */

		/* Calculate how many available space in the buffer */
		int available_space = BUF_SIZE - wbuf->len;
		if (available_space > 0)
		{
			int bytes_read = read(fd, wbuf->data + wbuf->len, available_space);
			if (bytes_read > 0)
			{
				wbuf->len += bytes_read;
			}
			else if (bytes_read < 0)
			{
				if(errno==EAGAIN || errno==EWOULDBLOCK)
				{
					usleep(1000); 
				}else{
					fprintf(stderr, "error, failed to read from UART: %s: %s\n", d->device_node, strerror(errno));
					close(fd);
					fd = -1;
					d->running = false;
					break; 
				}
			}else{ //bytes_read==0
				fprintf(stderr,"End of file reached.\n");
				close(fd);
				fd = -1;
				d->running = false;
				break; 
			}
		}
		/* Decide whether to hand the buffer over */
		if (wbuf->len >= BUF_SIZE)
		{
			try_swap(d); /* only this part takes the mutex */
		}
	}
	close(fd);
	fprintf(stdout,"producer thread exits %s.\n",d->device_node);
	return NULL;
}

/* -------------------------------------------------
 * Parser thread (consumer)
 * ------------------------------------------------- */
void parse_complete_frame(TMRDevice *dev,uint8_t *frame, size_t len)
{
	/* 55 xx xx xx , combine two bytes to a short. */
	unsigned short high_bytes = ((unsigned short)frame[1] & 0xFF) << 8;
	unsigned short low_bytes = ((unsigned short)frame[2] & 0x00FF) << 0;
	unsigned short ADC_Value_16bits = high_bytes | low_bytes;

	/* write 8000 samples to a temporary file for http request */
	/* update every 5 seconds */
	dump_frame2http(dev, ADC_Value_16bits);

	/* enable following code will cause abnormal data/curve. */
	/* !!!!!!!!!!!!!!!!!!!!!!!! */
	/* printf() takes up much time, eliminate them!!!! */

	/* write to fifo file for gnuplot real-time plotting. */
	//dump_frame2plot(dev, ADC_Value_16bits);
}
void dump_frame2http(TMRDevice *dev, unsigned short value)
{
	if(!dev->wrHttpInProcess) //wrHttpInProcess==false.
	{
		time(&dev->tm_present);
		double second_escaped = difftime(dev->tm_present, dev->tm_past);
		if(second_escaped>10.0f)
		{
			dev->wrHttpInProcess=true;
			/* write temporary file first, then atomic rename */
			char filename[64];
			snprintf(filename,sizeof(filename),"%s.tmp", dev->http_file);
			dev->fp_http=fopen(filename,"w");
			printf("10s escaped, write http file %s.tmp ...\n", dev->http_file);

			/* reset value statistics. */
			reset_value_statistics(dev);
		}
	}else{ //wrHttpInProcess==true.
		if(dev->cntWrHttpSamples>=SAMPLE_COUNT) //already written 360000 samples.
		{
			fflush(dev->fp_http);
			fclose(dev->fp_http);
			dev->wrHttpInProcess=false;
			dev->cntWrHttpSamples=0;

			/* using rename strategy to prevend http server reads empty file. */
			/* rename operation is atomic. */
			char filename[64];
			snprintf(filename,sizeof(filename),"%s.tmp", dev->http_file);
			if(rename(filename, dev->http_file) != 0) {
				remove(filename);
				fprintf(stderr,"failed to rename(),%s.\n",strerror(errno));
			}
			fprintf(stdout, "write done %s.\n", dev->http_file);

			/* write statistics value to temporary file for http request. */
			dump_statistic2file(dev);

			/* update time */
			time(&dev->tm_past);
			time(&dev->tm_present);
		}else{
			char buffer[64];
			//snprintf(buffer,sizeof(buffer),"%02x %02x %02x %02x %d\n",frame[0],frame[1],frame[2],frame[3],ADC_Value_16bits);

#if 0
			//write string for debug.
			snprintf(buffer,sizeof(buffer),"%d\n",value);
			fwrite(buffer,strlen(buffer),1,dev->fp_http);
#else
			//write float for distribution.
			/* linear mapping ADC value to realistic current. */
			/* y=kx+b. */
			float k_coefficient = 1.0f;
			float b_offset = -32768.0f; //0.0f;
			float TMR_Current = value * k_coefficient + b_offset;
			fwrite(&TMR_Current,sizeof(TMR_Current),1,dev->fp_http);

			/* do value statistics. */
			/* sorting algorithm to find out the min/max value. */
			dev->max_value = (TMR_Current > dev->max_value) ? (TMR_Current) : (dev->max_value);
			dev->min_value = (TMR_Current < dev->min_value) ? (TMR_Current) : (dev->min_value);
			dev->sum_value += TMR_Current;
			// 1.calculate squares.
			dev->sum_square += pow(TMR_Current, 2);
#endif
			dev->cntWrHttpSamples++;
		}
	}
}
void dump_frame2plot(TMRDevice *dev, unsigned short value)
{
	/* try to open FIFO file */
	if(dev->fd_plot<0)
	{
		dev->fd_plot=open(dev->plot_file, O_WRONLY | O_NONBLOCK | O_NOCTTY);
	}
	// cat /proc/sys/fs/pipe-max-size  1048576
	// echo 65536 | sudo tee  /proc/sys/fs/pipe-max-size
	// must run with root to set fifo size.
	if (dev->fd_plot > 0) {
		size_t ret;

		/* linear mapping ADC value to realistic current. */
		/* y=kx+b. */
		float k_coefficient = 1.0f;
		float b_offset = -32768.0f; //0.0f;
		float TMR_Current = value * k_coefficient + b_offset;

		if((ret=write(dev->fd_plot, &TMR_Current, sizeof(TMR_Current)))<0) {
			fprintf(stderr,"write fifo failed %s, force to reopen.\n", dev->plot_file);
			dev->fd_plot = -1;
		}
	}
}
void dump_statistic2file(TMRDevice *dev)
{
	/* first create temporary file, write data, then rename. */
	/* because rename() is automic operation. */
	char filename[128];
	snprintf(filename,sizeof(filename),"%s.statistic.tmp", dev->http_file);

	/* calculate the average value. */
	dev->avg_value = dev->sum_value / SAMPLE_COUNT;
	/* 2.calculate mean. */
	dev->mean_square = dev->sum_square / SAMPLE_COUNT;
	/* 3.calculate root. */
	dev->root_square = sqrt(dev->mean_square);

	FILE *fp_rms = fopen(filename, "w");
	if (fp_rms) {
		char buffer[128];
		snprintf(buffer, sizeof(buffer), "sum=%.2f,min=%.2f,max=%.2f,avg=%.2f,rms=%.2f\n",
				dev->sum_value, dev->min_value, dev->max_value,
				dev->avg_value, dev->root_square);

		fwrite(buffer, strlen(buffer), 1, fp_rms);
		fflush(fp_rms);
		fclose(fp_rms);

		/* using rename strategy to prevend http server reads empty file. */
		/* rename operation is atomic. */
		char dest_file_name[128];
		snprintf(dest_file_name,sizeof(dest_file_name),"%s.statistic", dev->http_file);
		if (rename(filename, dest_file_name) != 0) {
			remove(filename);
			fprintf(stderr,"failed to rename() %s: %s.\n", filename, strerror(errno));
		}
	}
}
void *parser_thread(void *arg)
{
	TMRDevice *d = (TMRDevice *)arg;
	time(&d->tm_past); 
	d->wrHttpInProcess=false;
	d->cntWrHttpSamples=0;

	uint8_t remaining_buffer[4]; // 55 xx xx xx.
	uint8_t remaining_len = 0;

	/* create FIFO file first. */
	if (mkfifo(d->plot_file, 0666)==-1) {
		if (errno == EEXIST) {
			fprintf(stderr,"FIFO %s already exists.\n",d->plot_file);
		}else{
			fprintf(stderr,"mkfifo %s failed:%s.\n",d->plot_file, strerror(errno));
			pthread_exit(NULL);
		}
	}

	while (1)
	{
		pthread_mutex_lock(&d->mutex);

		while (!d->data_ready && d->running)
		{
			pthread_cond_wait(&d->cond, &d->mutex);
		}

		if (!d->running && !d->data_ready)
		{
			pthread_mutex_unlock(&d->mutex);
			break;
		}

		/* We now own the read buffer */
		Buffer *rbuf = &d->buf[d->read_idx];
		size_t len = rbuf->len;

		/* ------------- Zero-Copy to enhance performance  ------------- */
		/* process previous round remaining bytes */
		uint8_t lack_bytes=0; 
		if(remaining_len > 0)
		{
			lack_bytes = 4 - remaining_len;
			memcpy(remaining_buffer + remaining_len, &rbuf->data[0], lack_bytes);
			remaining_len = 0;
			parse_complete_frame(d,remaining_buffer, 4);
		}
		/* sliding window to check sync head bytes */
		int i = lack_bytes;
		while(i<len-4)
		{
			if (rbuf->data[i + 0] == 0x55 && rbuf->data[i + 4] == 0x55)
			{
				parse_complete_frame(d,&rbuf->data[i + 0], 4);
				i+=4;
			}else{
				i++;
			}
		}
		/* remaining bytes less than 4, save them for next round. */
		if((len-i)>0) 
		{
			memcpy(remaining_buffer, rbuf->data + i, len - i);
			remaining_len = len - i;
		}

		d->data_ready = false; /* mark as consumed */
		pthread_mutex_unlock(&d->mutex);
	}
	fprintf(stdout,"consumer thread exits %s.\n",d->device_node);
	return NULL;
}



//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
int setup_uart(int fd, speed_t baud_rate)
{
	struct termios tty;
	memset(&tty, 0, sizeof(tty));

	// get-modify-set.
	if (tcgetattr(fd, &tty) != 0)
	{
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
	tty.c_cflag |= CREAD | CLOCAL;              // Enable receiver, ignore modem lines

	tty.c_iflag &=
		~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	tty.c_oflag &= ~OPOST;
	tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 1; // timeout 100ms.

	if (tcsetattr(fd, TCSANOW, &tty) != 0)
	{
		perror("Error from tcsetattr");
		return -1;
	}
	return 0;
}
