// gcc http.c base64.c -o http.bin -Llibcjson -lcjson
// export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./libcjson
#include "run_log.h"
#include "http.h"
#include "base64.h"
#include "libcjson/cJSON.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define RUN_LOG_FILE "/tmp/TMR_httpd.log"

// static device-relevant value.
const char *device_id = "1000000000020033"; // Device ID, 16-bits.
const char *equipment_name = "HuaiRou_TMR_Integration";
const char *manufacture_name = "CEPRI_Sensing_Institute";

// GET /TMR/Device_Info
// GET /TMR/Single_Frame
// GET /TMR/RAW_Frame
#define LISTEN_PORT 8080
#define BUFFER_SIZE 4096
#define BACKLOG 10

#define IMG_BUFSIZE (10 * 1024 * 1024) // Maximum size 10MB.

char *gImgBuffer;

// create static device-relevant json package.
cJSON *create_DevInfo_Json(void);
// create dynamic data-relevant json package.
cJSON *create_RawData_Json(const char *filename);

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
void send_rawdata_response(int client_fd, const char *filename);

// the main entrance.
int main(int argc, char **argv) {
  char buffer[128];
  int server_fd, client_fd;
  struct sockaddr_in address;
  int addrlen = sizeof(address);

  if(run_log_init(RUN_LOG_FILE)<0)
  {
    printf("Log engine initial failed.\n");
    return -1;
  }

  // create TCP stream socket.
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    run_log_add(LOG_ERR, strerror(errno), strlen(strerror(errno)));
    exit(EXIT_FAILURE);
  }

  // reuse addr to prevent "Address already in use" error.
  int opt = 1;
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

  gImgBuffer = (char *)malloc(IMG_BUFSIZE + 1);
  if (gImgBuffer == NULL) {
    perror("malloc() failed");
    exit(EXIT_FAILURE);
  }

  snprintf(buffer,sizeof(buffer),"Server is running on http://localhost:%d",LISTEN_PORT);
  printf("%s\n",buffer);
  run_log_add(LOG_INFO,buffer,strlen(buffer));
  run_log_flush();
  printf("Waiting for connections...\n");

  // only process one client at once.
  while (1) {
    if ((client_fd = accept(server_fd, (struct sockaddr *)&address,
                            (socklen_t *)&addrlen)) < 0) {
      perror("accept failed");
      continue;
    }

    char *client_ip = inet_ntoa(address.sin_addr);
    int client_port = ntohs(address.sin_port);
    snprintf(buffer, sizeof(buffer),"New connection from %s:%d",client_ip,client_port);
    run_log_add(LOG_INFO, buffer, strlen(buffer));
    printf("%s\n",buffer);

    // do not fork() to process.
    // we only process one client at once.
    handle_request(client_fd);

    // close short connection.
    close(client_fd);
    snprintf(buffer,sizeof(buffer),"Connection closed.");
    run_log_add(LOG_INFO, buffer, strlen(buffer));
    run_log_flush();
    printf("Connection closed.\n");
  }

  close(server_fd);
  free(gImgBuffer);
  return 0;
}

char *enc_file_to_base64(const char *filename, int *enc_size) {
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    perror(filename);
    return NULL;
  }
  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (file_size > IMG_BUFSIZE || file_size < 0) {
    printf("Error,Image buffer overflow!\n");
    return NULL;
  }
  // read data to global buffer.
  size_t bytes_read = fread(gImgBuffer, 1, file_size, fp);
  if (bytes_read != file_size) {
    printf("Error, read file data.\n");
    return NULL;
  }

  char *encoded =
      base64_encode_manual((const unsigned char *)gImgBuffer, bytes_read);
  if (encoded) {
    printf("Encoded: %s\n", encoded);
    *enc_size = bytes_read;
  }
  return encoded;
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

  printf("Received request: %s %s\n", method, path);
  snprintf(log_buffer, sizeof(log_buffer), "Received request : %s %s",method, path);
  run_log_add(LOG_INFO, log_buffer,strlen(log_buffer));
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
    send_image_response(client_fd, "Phase_A.png");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=A&Type=RawData") == 0) {
    send_rawdata_response(client_fd, "Phase_A.dat");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=B&Type=Image") == 0) {
    send_image_response(client_fd, "Phase_B.png");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=B&Type=RawData") == 0) {
    send_rawdata_response(client_fd, "Phase_B.dat");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=C&Type=Image") == 0) {
    send_image_response(client_fd, "Phase_C.png");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=C&Type=RawData") == 0) {
    send_rawdata_response(client_fd, "Phase_C.dat");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=ABC&Type=Image") == 0) {
    send_image_response(client_fd, "Phase_ABC.png");
  } else if (strcmp(path, "/TMR/Phase_Curve?Ph=ABC&Type=RawData") == 0) {
    send_rawdata_response(client_fd, "Phase_ABC.dat");
  } else if (strcmp(path, "/hello") == 0) {
    const char *text_body = "Hello, World!";
    send_response(client_fd, 200, "OK", "text/plain", text_body);
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

void send_runlog_response(int client_fd)
{
  char buffer[BUFFER_SIZE];
  int file_size;

  FILE *fp = fopen("/tmp/TMR_httpd.log", "rb");
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
           "\r\n", file_size);
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

  printf("%s\n", header);
  printf("%s\n", body);
}

/// send http response to clients.
void send_image_response(int client_fd, const char *filename) {
  char buffer[BUFFER_SIZE];
  int file_size;

  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    printf("file not found : %s\n", filename);
    // build HTTP protocol header.
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
void send_rawdata_response(int client_fd, const char *filename) {
  char buffer[BUFFER_SIZE];
  int file_size;

  // create RawData Json.
  cJSON *node_RawData = create_RawData_Json(filename);
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

  // char buffer[BUFFER_SIZE];
  // int file_size;

  // FILE *fp = fopen(filename, "rb");
  // if (fp == NULL) {
  //   printf("file not found : %s\n", filename);
  //   // build HTTP protocol header.
  //   char *html_body = "File was not found.";
  //   snprintf(buffer, sizeof(buffer),
  //            "HTTP/1.1 400 Bad Request\r\n"
  //            "Content-Type: text/plain\r\n"
  //            "Content-Length: %d\r\n"
  //            "Connection: close\r\n"
  //            "\r\n",
  //            (int)strlen(html_body));
  //   // transmit header.
  //   write(client_fd, buffer, strlen(buffer));
  //   write(client_fd, html_body, strlen(html_body));
  //   return;
  // }

  // fseek(fp, 0, SEEK_END);
  // file_size = ftell(fp);
  // fseek(fp, 0, SEEK_SET);
  // // read all data bytes.
  // char *raw_data_buf = (char *)malloc(file_size);
  // if (raw_data_buf == NULL) {
  //   printf("error at malloc()\n");
  //   return;
  // }
  // printf("filesize=%d\n", file_size);
  // fread(raw_data_buf, file_size, 1, fp);
  // char *base64_enc_buf =
  //     base64_encode_manual((const unsigned char *)raw_data_buf, file_size);
  // if (base64_enc_buf) {
  //   printf("Encoded: %s\n", base64_enc_buf);
  // }

  // // build HTTP protocol header.
  // snprintf(buffer, sizeof(buffer),
  //          "HTTP/1.1 200 OK\r\n"
  //          "Content-Type: text/plain\r\n"
  //          "Content-Length: %d\r\n"
  //          "Connection: close\r\n"
  //          "\r\n",
  //          file_size);
  // // transmit header.
  // write(client_fd, buffer, strlen(buffer));
  // write(client_fd, base64_enc_buf, strlen(base64_enc_buf));
  // fclose(fp);
  // free(base64_enc_buf);
  // free(raw_data_buf);
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

  /////////////////////////////////////////////////
  cJSON_AddItemToObject(node_data, "body", node_body);
  cJSON_AddItemToObject(node_param, "data", node_data);
  cJSON_AddItemToObject(node_root, "param", node_param);

  return node_root;
}
// create dynamic data-relevant json package.
cJSON *create_RawData_Json(const char *filename) {
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
  cJSON_AddItemToObject(node_temp, "unit", cJSON_CreateString("\xE2\x84\x83")); //℃
  cJSON_AddItemToArray(node_body, node_temp);

  // prepare raw data.
  char *base64_enc_buf = NULL;
  FILE *fp = fopen(filename, "rb");
  if (fp != NULL) {
    // get file size.
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // read all data bytes.
    char *raw_data_buf = (char *)malloc(file_size);
    if (raw_data_buf == NULL) {
      printf("error at malloc()\n");
    } else {
      printf("filesize=%d\n", file_size);
      fread(raw_data_buf, file_size, 1, fp);

      // encoded with base64.
      base64_enc_buf =
          base64_encode_manual((const unsigned char *)raw_data_buf, file_size);
      if (base64_enc_buf) {
        printf("Encoded: %s\n", base64_enc_buf);
      }
      free(raw_data_buf);
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