// gcc http.c base64.c -o http.bin -Llibcjson -lcjson
// export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./libcjson
#include "tmr_httpd.h"
#include "libcjson/cJSON.h"
#include "modules/base64.h"
#include "modules/run_log.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// GET /TMR/Device_Info
// GET /TMR/Single_Frame
// GET /TMR/RAW_Frame
#define LISTEN_PORT 8080
#define BUFFER_SIZE 4096
#define BACKLOG 10

// create static device-relevant json package.
cJSON *create_DevInfo_Json(void);
// create dynamic data-relevant json package.
cJSON *create_RawData_Json(const char *filename, const char *phase_name);

// handle request from different clients.
int handle_request(int client_fd);

void send_devinfo_response(int client_fd);
void send_runlog_response(int client_fd);
/// send http response to clients.
void send_response(int client_fd, int status_code, const char *status_message,
                   const char *content_type, const char *body);
/// send image response to clients.
void send_image_response(int client_fd, const char *filename);
/// send raw data response to clients.
void send_rawdata_response(int client_fd, const char *filename,
                           const char *phase_name);
void send_rawdata_csv_response(int client_fd, const char *filename,
                               const char *phase_name);

// write my pid to file.
void write_pid2file(void);

// signal handler.
volatile sig_atomic_t gExitFlag = 0;
void signal_handler(int signo);

void print_usage(char *app_name);

volatile int gVerbose = 0;
// the main entrance.
int main(int argc, char **argv) {
  int opt;
  char buffer[128];
  struct sigaction sa;
  int server_fd, client_fd;
  struct sockaddr_in address;
  int addrlen = sizeof(address);

  // p: means parameter is necessary.
  // v: means no parameter.
  while ((opt = getopt(argc, argv, "vh")) > 0) {
    switch (opt) {
    case 'v':
      gVerbose = 1;
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

  if (run_log_init(TMR_RUN_LOG_FILE) < 0) {
    printf("Log engine initial failed.\n");
    return -1;
  }

  printf("%s\n", TMR_VERSION_NO);
  printf("Running log will be recorded into %s file.\n", TMR_RUN_LOG_FILE);

  // create TCP stream socket.
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    run_log_add(LOG_ERR, strerror(errno), strlen(strerror(errno)));
    exit(EXIT_FAILURE);
  }

  // reuse addr to prevent "Address already in use" error.
  opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // bind address and LISTEN_PORT.
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY; // listen on all NICs.
  address.sin_port = htons(LISTEN_PORT);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    run_log_add(LOG_ERR, strerror(errno), strlen(strerror(errno)));
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  // start to listen.
  if (listen(server_fd, BACKLOG) < 0) {
    run_log_add(LOG_ERR, strerror(errno), strlen(strerror(errno)));
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  snprintf(buffer, sizeof(buffer), "Server is running on http://localhost:%d",
           LISTEN_PORT);
  printf("%s\n", buffer);
  run_log_add(LOG_INFO, buffer, strlen(buffer));
  run_log_flush();
  printf("Waiting for connections...\n");

  // install signal handler.
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = signal_handler;
  sa.sa_flags = 0;
  if (sigaction(SIGINT, &sa, NULL) < 0) {
    run_log_add(LOG_ERR, strerror(errno), strlen(strerror(errno)));
  }
  // write my pid to file.
  write_pid2file();

  // only process one client at once.
  while (!gExitFlag) {
    if ((client_fd = accept(server_fd, (struct sockaddr *)&address,
                            (socklen_t *)&addrlen)) < 0) {
      perror("accept failed");
      continue;
    }

    char *client_ip = inet_ntoa(address.sin_addr);
    int client_port = ntohs(address.sin_port);
    snprintf(buffer, sizeof(buffer), "New connection from %s:%d", client_ip,
             client_port);
    run_log_add(LOG_INFO, buffer, strlen(buffer));
    printf("%s\n", buffer);

    // do not fork() to process.
    // we only process one client at once.
    handle_request(client_fd);

    // close short connection.
    close(client_fd);
    snprintf(buffer, sizeof(buffer), "Connection closed.");
    run_log_add(LOG_INFO, buffer, strlen(buffer));
    run_log_flush();
    printf("Connection closed.\n");
  }
  close(server_fd);
  printf("program exited normally.\n");
  return 0;
}

void print_usage(char *app_name) {
  printf("%s\n", TMR_VERSION_NO);
  printf("%s <options>\n", app_name);
  printf("<options> list:\n");
  printf("-v Verbose enabled, printf more messages.\n");
  printf("-h Output help message\n");
  printf("Compiled on %s %s", __DATE__, __TIME__);
}

// write my pid to file.
void write_pid2file(void) {
  FILE *fp_pid = fopen(TMR_PID_FILE, "w");
  if (fp_pid) {
    pid_t mypid = getpid();
    fprintf(fp_pid, "%d\n", mypid);
    fclose(fp_pid);
    fp_pid = NULL;
  }
}

// SIGINT=2, kill -2 pid
// SIGKILL=9, kill -9 pid
void signal_handler(int signo) {
  if (signo == SIGINT || signo == SIGKILL) {
    gExitFlag = 1;
  }
}

// handle request from different clients.
int handle_request(int client_fd) {
  char buffer[BUFFER_SIZE];
  char log_buffer[BUFFER_SIZE];

  ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
  if (bytes_read <= 0) {
    return -1;
  }
  buffer[bytes_read] = '\0';

  // parse request line.
  // format: GET /path HTTP/1.1
  char method[128], path[128], protocol[128];
  if (sscanf(buffer, "%s %s %s", method, path, protocol) != 3) {
    send_response(client_fd, 400, ///<
                  "Bad Request", "text/plain", "Invalid Request");
    return -1;
  }

  if(gVerbose)
  {
    printf("Received request: %s %s\n", method, path);
  }
  snprintf(log_buffer, sizeof(log_buffer), "Received request : %s %s", method,
           path);
  run_log_add(LOG_INFO, log_buffer, strlen(log_buffer));
  run_log_flush();

  // here we only support GET method.
  if (strcmp(method, "GET") != 0) {
    send_response(client_fd, 405, ///<
                  "Method Not Allowed", "text/plain", "Only GET is supported.");
    return -1;
  }

  // processing different request paths.
  // http://127.0.0.1:8080/index.html

  // http://ip:port/TMR/DevInfo

  // http://ip:port/TMR/Phase_A?Req=Render_Image
  // http://ip:port/TMR/Phase_A?Req=Raw_Data

  // http://ip:port/TMR/Phase_B?Req=Render_Image
  // http://ip:port/TMR/Phase_B?Req=Raw_Data

  // http://ip:port/TMR/Phase_C?Req=Render_Image
  // http://ip:port/TMR/Phase_C?Req=Raw_Data

  // http://ip:port/TMR/Phase_ABC?Req=Render_Image
  // http://ip:port/TMR/Phase_ABC?Req=Raw_Data
  if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
    send_response(client_fd, 200, ///<
                  "OK", "text/html", MAIN_PAGE_HTML);
  } else if (strcmp(path, "/TMR/Dev_Info") == 0) {
    send_devinfo_response(client_fd);
  } else if (strcmp(path, "/TMR/Run_Log") == 0) {
    send_runlog_response(client_fd);
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=A&Type=Image") == 0) {
    send_image_response(client_fd, TMR_PHASE_A_IMG_FILE);
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=A&Type=RawData") == 0) {
    send_rawdata_response(client_fd, TMR_PHASE_A_RAW_FILE, "A");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=A&Type=RawData_CSV") == 0) {
    send_rawdata_csv_response(client_fd, TMR_PHASE_A_RAW_FILE, "A");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=B&Type=Image") == 0) {
    send_image_response(client_fd, TMR_PHASE_B_IMG_FILE);
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=B&Type=RawData") == 0) {
    send_rawdata_response(client_fd, TMR_PHASE_B_RAW_FILE, "B");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=B&Type=RawData_CSV") == 0) {
    send_rawdata_csv_response(client_fd, TMR_PHASE_B_RAW_FILE, "B");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=C&Type=Image") == 0) {
    send_image_response(client_fd, TMR_PHASE_C_IMG_FILE);
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=C&Type=RawData") == 0) {
    send_rawdata_response(client_fd, TMR_PHASE_C_RAW_FILE, "C");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=C&Type=RawData_CSV") == 0) {
    send_rawdata_csv_response(client_fd, TMR_PHASE_C_RAW_FILE, "C");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=ABC&Type=Image") == 0) {
    send_image_response(client_fd, TMR_PHASE_ABC_IMG_FILE);
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=ABC&Type=RawData") == 0) {
    send_rawdata_response(client_fd, TMR_PHASE_ABC_RAW_FILE, "ABC");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=ABC&Type=RawData_CSV") == 0) {
    send_rawdata_csv_response(client_fd, TMR_PHASE_ABC_RAW_FILE, "ABC");
  } else {
    send_response(client_fd, 404, ///<
                  "Not Found", "text/html", ERROR_PAGE_HTML);
  }
  return 0;
}

void send_devinfo_response(int client_fd) {
  char buffer[BUFFER_SIZE];
  int file_size;

  // create DevInfo Json.
  cJSON *node_device = create_DevInfo_Json();
  char *pjson = cJSON_Print(node_device);

  snprintf(buffer, sizeof(buffer),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n"
           "\r\n",
           (int)strlen(pjson));
  // tx html header.
  write(client_fd, buffer, strlen(buffer));
  // tx html body.
  write(client_fd, pjson, strlen(pjson));
  cJSON_free(node_device);
  free(pjson);
}

void send_runlog_response(int client_fd) {
  char buffer[BUFFER_SIZE];
  int file_size;

  FILE *fp = fopen(TMR_RUN_LOG_FILE, "rb");
  if (fp == NULL) {
    char *html_body = "File was not found.";
    snprintf(buffer, sizeof(buffer),
             "HTTP/1.1 400 Bad Request\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n",
             (int)strlen(html_body));
    // transmit header.
    write(client_fd, buffer, strlen(buffer));
    write(client_fd, html_body, strlen(html_body));
    return;
  }

  fseek(fp, 0, SEEK_END);
  file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // build HTTP protocol header.
  snprintf(buffer, sizeof(buffer),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n"
           "\r\n",
           file_size);
  // transmit header.
  write(client_fd, buffer, strlen(buffer));
  size_t rd_bytes;
  while ((rd_bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
    write(client_fd, buffer, rd_bytes);
  }
  fclose(fp);
}

/// send http response to clients.
void send_response(int client_fd, int status_code, ///<
                   const char *status_message, const char *content_type,
                   const char *body) {
  char header[BUFFER_SIZE];
  int body_len = strlen(body);

  // build HTTP protocol header.
  snprintf(header, sizeof(header),
           "HTTP/1.1 %d %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n"
           "\r\n",
           status_code, status_message, content_type, body_len);

  // transmit header.
  write(client_fd, header, strlen(header));

  // transmit body.
  write(client_fd, body, body_len);

  if(gVerbose)
  {
    printf("%s\n", header);
    printf("%s\n", body);
  }
}

/// send http response to clients.
void send_image_response(int client_fd, const char *filename) {
  char buffer[BUFFER_SIZE];
  int file_size;

  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    char msg_buffer[BUFFER_SIZE];
    snprintf(msg_buffer, sizeof(msg_buffer), "File %s not found.", filename);
    run_log_add(LOG_ERR, msg_buffer, strlen(msg_buffer));

    snprintf(
        buffer, sizeof(buffer),
        "<!DOCTYPE html>"
        "<html lang=\"en\">"
        "<head><meta charset=\"UTF-8\"><title>TMR Terminal</title></head>"
        "<body><h1>404 Not Found</h1>"
        "<p>The requested resource %s was not found.</p>"
        "<p>You guy are so funny, DO NOT ATTEMPT TO CRACK ME.</p>"
        "<p>Please obey protocol specification to get what you want, lol.</p>"
        "</body></html>",
        filename);
    send_response(client_fd, 404, ///<
                  "Not found", "text/html", buffer);
    return;
  }

  fseek(fp, 0, SEEK_END);
  file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // build HTTP protocol header.
  snprintf(buffer, sizeof(buffer),
           "HTTP/1.1 %d %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n"
           "\r\n",
           200, "OK", "image/png", file_size);
  // transmit header.
  write(client_fd, buffer, strlen(buffer));
  size_t rd_bytes;
  while ((rd_bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
    write(client_fd, buffer, rd_bytes);
  }
  fclose(fp);
}

/// send raw data response to clients.
void send_rawdata_response(int client_fd, const char *filename,
                           const char *phase_name) {
  char buffer[BUFFER_SIZE];
  int file_size;

  // 400 bad request if file not found.
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    char msg_buffer[BUFFER_SIZE];
    snprintf(msg_buffer, sizeof(msg_buffer), "File %s not found.", filename);
    run_log_add(LOG_ERR, msg_buffer, strlen(msg_buffer));

    snprintf(
        buffer, sizeof(buffer),
        "<!DOCTYPE html>"
        "<html lang=\"en\">"
        "<head><meta charset=\"UTF-8\"><title>TMR Terminal</title></head>"
        "<body><h1>404 Not Found</h1>"
        "<p>The requested resource %s was not found.</p>"
        "<p>You guy are so funny, DO NOT ATTEMPT TO CRACK ME.</p>"
        "<p>Please obey protocol specification to get what you want, lol.</p>"
        "</body></html>",
        filename);
    send_response(client_fd, 404, ///<
                  "Not found", "text/html", buffer);
    return;
  }

  // create RawData Json.
  cJSON *node_RawData = create_RawData_Json(filename, phase_name);
  char *pjson = cJSON_Print(node_RawData);

  snprintf(buffer, sizeof(buffer),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/plain; charset=utf-8\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n"
           "\r\n",
           (int)strlen(pjson));
  // tx html header.
  write(client_fd, buffer, strlen(buffer));
  // tx html body.
  write(client_fd, pjson, strlen(pjson));
  cJSON_free(node_RawData);
  free(pjson);
  pjson = NULL;
}

/// send raw data response to clients.
void send_rawdata_csv_response(int client_fd, const char *filename,
                               const char *phase_name) {
  char buffer[BUFFER_SIZE];
  int file_size;

  snprintf(buffer, sizeof(buffer), "%s.csv", filename);

  // 400 bad request if file not found.
  FILE *fp_csv = fopen(buffer, "rb");
  if (fp_csv == NULL) {
    char msg_buffer[BUFFER_SIZE];
    snprintf(msg_buffer, sizeof(msg_buffer), "File %s not found.", filename);
    run_log_add(LOG_ERR, msg_buffer, strlen(msg_buffer));

    snprintf(
        buffer, sizeof(buffer),
        "<!DOCTYPE html>"
        "<html lang=\"en\">"
        "<head><meta charset=\"UTF-8\"><title>TMR Terminal</title></head>"
        "<body><h1>404 Not Found</h1>"
        "<p>The requested resource %s.csv was not found.</p>"
        "<p>You guy are so funny, DO NOT ATTEMPT TO CRACK ME.</p>"
        "<p>Please obey protocol specification to get what you want, lol.</p>"
        "</body></html>",
        filename);
    send_response(client_fd, 404, ///<
                  "Not found", "text/html", buffer);
    return;
  }

  // get file size.
  fseek(fp_csv, 0, SEEK_END);
  file_size = ftell(fp_csv);
  fseek(fp_csv, 0, SEEK_SET);

  // build HTTP protocol header.
  snprintf(buffer, sizeof(buffer),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n"
           "\r\n",
           file_size);
  // transmit header.
  write(client_fd, buffer, strlen(buffer));
  size_t rd_bytes;
  while ((rd_bytes = fread(buffer, 1, sizeof(buffer), fp_csv)) > 0) {
    write(client_fd, buffer, rd_bytes);
  }
  fclose(fp_csv);
  fp_csv = NULL;
}

// create static device-relevant json package.
cJSON *create_DevInfo_Json(void) {
  time_t current_time = time(NULL);
  struct tm *tm_info = localtime(&current_time);
  char current_time_buffer[128];
  strftime(current_time_buffer, sizeof(current_time_buffer),
           "%Y-%m-%d %H:%M:%S", tm_info);

  cJSON *node_root = cJSON_CreateObject();
  cJSON_AddItemToObject(node_root, "id", cJSON_CreateString(device_id));
  cJSON_AddItemToObject(node_root, "type", cJSON_CreateString("TMR_DEVICE"));
  cJSON_AddItemToObject(node_root, "timestamp",
                        cJSON_CreateNumber(current_time));
  // never expire.
  cJSON_AddNumberToObject(node_root, "expire", -1);

  cJSON *node_param = cJSON_CreateObject();
  cJSON_AddItemToObject(node_param, "cmd", cJSON_CreateString("STATIC"));

  cJSON *node_data = cJSON_CreateObject();
  cJSON_AddItemToObject(node_data, "token", cJSON_CreateString(""));
  cJSON_AddItemToObject(node_data, "timestamp",
                        cJSON_CreateString(current_time_buffer));

  cJSON *node_body = cJSON_CreateObject();
  cJSON_AddItemToObject(node_body, "eqpName",
                        cJSON_CreateString(equipment_name));
  cJSON_AddItemToObject(node_body, "manufactorName",
                        cJSON_CreateString(manufacture_name));
  cJSON_AddItemToObject(node_body, "begDate",
                        cJSON_CreateString(current_time_buffer));
  cJSON_AddItemToObject(node_body, "OpticalRx",
                        cJSON_CreateString("A1 JUN326"));
  cJSON_AddItemToObject(node_body, "TinyTMR",
                        cJSON_CreateString("RevA1 JUN326"));
  /////////////////////////////////////////////////
  cJSON_AddItemToObject(node_data, "body", node_body);
  cJSON_AddItemToObject(node_param, "data", node_data);
  cJSON_AddItemToObject(node_root, "param", node_param);

  return node_root;
}
// create dynamic data-relevant json package.
cJSON *create_RawData_Json(const char *filename, const char *phase_name) {
  // get file size.
  int file_size = 0;
  FILE *fp = fopen(filename, "rb");
  if (fp != NULL) {
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
  }

  time_t current_time = time(NULL);
  struct tm *tm_info = localtime(&current_time);
  char current_time_buffer[128];
  strftime(current_time_buffer, sizeof(current_time_buffer),
           "%Y-%m-%d %H:%M:%S", tm_info);

  cJSON *node_root = cJSON_CreateObject();
  cJSON_AddItemToObject(node_root, "id", cJSON_CreateString(device_id));
  cJSON_AddItemToObject(node_root, "type", cJSON_CreateString("TMR_DATA"));
  cJSON_AddItemToObject(node_root, "timestamp",
                        cJSON_CreateNumber(current_time));
  // never expire.
  cJSON_AddNumberToObject(node_root, "expire", -1);

  cJSON *node_param = cJSON_CreateObject();
  cJSON_AddItemToObject(node_param, "cmd", cJSON_CreateString("DYNAMIC"));

  cJSON *node_data = cJSON_CreateObject();
  cJSON_AddItemToObject(node_data, "token", cJSON_CreateString(""));
  cJSON_AddItemToObject(node_data, "timestamp",
                        cJSON_CreateString(current_time_buffer));

  // data section.
  cJSON *node_body = cJSON_CreateArray();
  // node_temp.
  cJSON *node_temp = cJSON_CreateObject();
  cJSON_AddItemToObject(node_temp, "fieldName",
                        cJSON_CreateString("Temperature"));
  cJSON_AddItemToObject(node_temp, "fieldValue", cJSON_CreateNumber(24.5));
  cJSON_AddItemToObject(node_temp, "unit",
                        cJSON_CreateString("\xE2\x84\x83")); // ℃
  cJSON_AddItemToArray(node_body, node_temp);

  // Phase.
  cJSON *node_phase = cJSON_CreateObject();
  cJSON_AddItemToObject(node_phase, "fieldName", cJSON_CreateString("Phase"));
  cJSON_AddItemToObject(node_phase, "fieldValue",
                        cJSON_CreateString(phase_name));
  cJSON_AddItemToArray(node_body, node_phase);

  // Encoder.
  cJSON *node_encoder = cJSON_CreateObject();
  cJSON_AddItemToObject(node_encoder, "fieldName",
                        cJSON_CreateString("Encoder"));
  cJSON_AddItemToObject(node_encoder, "fieldValue",
                        cJSON_CreateString("base64"));
  cJSON_AddItemToArray(node_body, node_encoder);

  // number of samples.
  cJSON *node_number_of_samples = cJSON_CreateObject();
  cJSON_AddItemToObject(node_number_of_samples, "fieldName",
                        cJSON_CreateString("Number of Samples"));
  cJSON_AddNumberToObject(node_number_of_samples, "fieldValue",
                          (int)(file_size / 4));
  cJSON_AddItemToObject(node_number_of_samples, "unit",
                        cJSON_CreateString("float")); // ℃

  cJSON_AddItemToArray(node_body, node_number_of_samples);

  // prepare raw data.
  char *base64_enc_buf = NULL;
  if (fp != NULL) {
    // read all data bytes.
    char *raw_data_buf = (char *)malloc(file_size);
    if (raw_data_buf == NULL) {
      printf("error at malloc()\n");
    } else {
      printf("filesize=%d\n", file_size);
      // read all data.
      fread(raw_data_buf, file_size, 1, fp);

      // encoded with base64.
      base64_enc_buf =
          base64_encode_manual((const unsigned char *)raw_data_buf, file_size);
      if (base64_enc_buf && gVerbose) {
        printf("Encoded: %s\n", base64_enc_buf);
      }

      // decoder with base64.
      size_t decoded_len = 0;
      unsigned char *base64_dec_buf =
          base64_decode_manual(base64_enc_buf, &decoded_len);
      if (base64_dec_buf != NULL && decoded_len != 0) {
        // dump all float samples to a csv file.
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "%s.csv", filename);
        FILE *fp_csv = fopen(buffer, "w");
        if (fp_csv) {
          float *sampled_data = (float *)base64_dec_buf;
          for (int i = 0; i < decoded_len / 4; i++) {
            // printf("%d\t%.2f\n", i, sampled_data[i]);
            snprintf(buffer, sizeof(buffer), "%d,%.2f\n", i, sampled_data[i]);
            fwrite(buffer, strlen(buffer), 1, fp_csv);
          }
          fclose(fp_csv);
          snprintf(buffer, sizeof(buffer), "dump float to %s.csv done.",
                   filename);
          run_log_add(LOG_INFO, buffer, strlen(buffer));
        }
        free(base64_dec_buf);
        base64_dec_buf = NULL;
      }
      free(raw_data_buf);
      raw_data_buf = NULL;
    }
  }

  // node_data.
  cJSON *node_current = cJSON_CreateObject();
  cJSON_AddItemToObject(node_current, "fieldName",
                        cJSON_CreateString("Transient Current"));
  if (base64_enc_buf != NULL) {
    cJSON_AddItemToObject(node_current, "fieldValue",
                          cJSON_CreateString(base64_enc_buf));
    cJSON_AddItemToObject(node_current, "unit", cJSON_CreateString("Amps"));
    cJSON_AddItemToArray(node_body, node_current);

    free(base64_enc_buf);
  }

  /////////////////////////////////////////////////////////////////////////
  cJSON_AddItemToObject(node_data, "body", node_body);
  cJSON_AddItemToObject(node_param, "data", node_data);
  cJSON_AddItemToObject(node_root, "param", node_param);

  return node_root;
}