/*
 * Real-time 3-curve gnuplot pipe — sliding x-window (scroll left).
 *
 *   gcc TMR_Plot.c -o TMR_Plot.bin -lm
 *   ./TMR_Plot.bin
 *
 * If the plot only updates when you click/resize the window (common on
 * GNOME + Wayland + wxt), use one of:
 *   GDK_BACKEND=x11 ./TMR_Plot.bin
 *   or switch terminal to qt below (set term qt).
 *
 * Each frame: set xrange, plot 3x '-', pause 0, fflush.
 */
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define APP_NAME "TMR_Capture"
#define APP_VERSION "1.0.0" // May 25, 2026.

// How many samples in one chart.
#define SAMPLES (1000)
typedef struct {
  char *file_name;
  int fd;
  FILE *fp;
  double buf[SAMPLES];
  int pos;
} channel_t;

//global command parameters.
int g_CmdWrCSV=0;
int g_CmdVerbose=0;

// Signal handler to exit.
volatile int g_Exit = 0;
void signal_handler(int signo) {
  if (signo == SIGINT) {
    printf("Caught SIGINT, prepare to exit...\n");
    g_Exit = 1;
  }
}

static void push(channel_t *ch, double new_value) {
  ch->buf[ch->pos] = new_value;
  // generate a ring buffer, adjust write position.
  ch->pos = (ch->pos + 1) % SAMPLES;
}

static void send_series(FILE *gp, const channel_t *ch, long long n) {
  long long x0 = (n > SAMPLES) ? (n - SAMPLES + 1) : 1;
  int count = (n > SAMPLES) ? SAMPLES : (int)n;
  int k;

  for (k = 0; k < count; k++) {
    int idx = (n > SAMPLES) ? (ch->pos + k) % SAMPLES : k;
    fprintf(gp, "%lld %f\n", x0 + k, ch->buf[idx]);
  }
  fprintf(gp, "e\n");
}

static int gp_ok(FILE *gp) { return gp && !ferror(gp) && !feof(gp); }

void print_usage(char *app_name) {
  printf("TMR Plot Program %s\n", APP_VERSION);
  printf("%s <options>\n", app_name);
  printf("<options> list:\n");
  printf("-c write CSV file while plotting. /tmp/PhaseABC.csv\n");
  printf("-v Verbose enabled, printf more messages.\n");
  printf("-h Output help message\n");
  printf("common used combination parameters for debug -tpml\n");
  printf("long time running parameters -tp\n");
  printf("Compiled on %s %s", __DATE__, __TIME__);
}

int main(int argc, char **argv) {
  int opt;
  FILE *gp;
  int i;
  FILE *fp_csv;
  char buffer[512];
  channel_t channels[3] = {
  {"/tmp/TMR_Phase_A.fifo", -1,NULL},
  {"/tmp/TMR_Phase_B.fifo", -1,NULL},
  {"/tmp/TMR_Phase_C.fifo", -1,NULL}};
  long long n = 0;

  printf("Welcome to use %s Version %s\n", APP_NAME, APP_VERSION);

  // p: means parameter is necessary.
  // v: means no parameter.
  while ((opt = getopt(argc, argv, "cvh")) > 0) {
    switch (opt) {
    case 'c':
      g_CmdWrCSV = 1;
      break;
    case 'v':
      g_CmdVerbose = 1;
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


  setenv("GDK_BACKEND", "x11", 0);

  // gp = popen("gnuplot -persist", "w");
  gp = popen("gnuplot", "w");
  if (!gp) {
    perror("gnuplot");
    return 1;
  }

  setvbuf(gp, NULL, _IONBF, 0);

  fprintf(gp,
    "set term qt noraise title 'Real-time curve plotting of TMR Phase "
          "A,B,C'\n" // The plot is updated in the background.
          "set title '3-Phases Current Monitoring Curve' font 'Arial,20' \n"
          "set xlabel 'Sample/Time'\n"
          "set ylabel 'Value/Current'\n"
          "set yrange [-100:66666]\n"
          "unset autoscale\n"
          "set key top left\n"
          "set style line 1 lc rgb '#FF0000' lw 2\n"
          "set style line 2 lc rgb '#0000FF' lw 2\n"
          "set style line 3 lc rgb '#000000' lw 2\n");

  // install signal handler.
  signal(SIGINT, signal_handler);

  if(g_CmdWrCSV)
  {
    if(NULL==(fp_csv=fopen("/tmp/PhaseABC.csv","w")))
    {
      fprintf(stderr,"failed to open csv file!\n");
      return -1;
    }
  }

  //Initial done, enter super loop.
  while (!g_Exit) {

    // adjust xrange.
    fprintf(gp, "set xrange [%lld:%lld]\n",
      (n > SAMPLES) ? (n - SAMPLES + 1) : 1, n);
    fprintf(gp, "plot '-' title 'Phase A' with lines ls 1, "
      "     '-' title 'Phase B' with lines ls 2, "
      "     '-' title 'Phase C' with lines ls 3\n");

    float phase_current[3];
    // read Phase-A, Phase-B, Phase-C data from 3 different FIFO files.
    for (i = 0; i < 3; i++) {
      // if read fails, use this default value.
      float phase_value = -10.0f;
      if (channels[i].fp) {
        size_t byte_read =
        fread(&phase_value, sizeof(float), 1, channels[i].fp);
        if (byte_read != 1) {
          fprintf(stderr, "Channel %d, Read failed %s, reforce to open.\n", i, channels[i].file_name);
          channels[i].fp=NULL;
        } else {
          if(g_CmdVerbose){
            printf("Channel %d, get new value: %.2f\n", i, phase_value);
          }
        }
      }else{
        // try to open again.
        channels[i].fd = open(channels[i].file_name, O_RDONLY | O_NONBLOCK);
        if (channels[i].fd > 0) {
          channels[i].fp = fdopen(channels[i].fd, "r");
        }else{
          fprintf(stderr, "Channel %d, Open failed %s\n", i,
            channels[i].file_name);
        }
      }

      push(&channels[i], phase_value);
      send_series(gp, &channels[i], n);
      if(g_CmdVerbose){
        printf("Channel %d, get new value: %.2f\n", i, phase_value);
      }
      switch(i)
      {
      case 0: phase_current[0]=phase_value; break;
      case 1: phase_current[1]=phase_value; break;
      case 2: phase_current[2]=phase_value; break;
      default: //do nothing.
      }
    }
    n++;

    fprintf(gp, "pause 0\n");

    if (fflush(gp) != 0 || !gp_ok(gp))
      break;

    // usleep(20000);
    usleep(1000);

    if(g_CmdWrCSV)
    {
      snprintf(buffer,sizeof(buffer),"%.2f\t%.2f\t%.2f\n", phase_current[0],phase_current[1],phase_current[2]);
      fwrite(buffer,sizeof(char),strlen(buffer),fp_csv);
      fflush(fp_csv);
    }
  }
  fprintf(gp, "exit\n");
  fflush(gp);
  pclose(gp);
  for (i = 0; i < 3; i++) {
    fclose(channels[i].fp);
  }
  if(g_CmdWrCSV)
  {
    fclose(fp_csv);
  }
  return 0;
}

