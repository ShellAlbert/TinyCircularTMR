
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// GET /TMR/Device_Info
// GET /TMR/Single_Frame
// GET /TMR/RAW_Frame
#define PORT 8080
#define BUFFER_SIZE 4096
#define BACKLOG 10

///send http response to clients.
void send_response(int client_fd, ///<
    int status_code, const char *status_message, const char *content_type, const char *body) 
{
    char header[BUFFER_SIZE];
    int body_len = strlen(body);
    
    //build HTTP protocol header.
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n",
             status_code, status_message, content_type, body_len);

    //transmit header.
    write(client_fd, header, strlen(header));
    
    //transmit body.
    write(client_fd, body, body_len);
}

//handle request from different clients.
void handle_request(int client_fd) 
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read <= 0) {
        return;
    }
    buffer[bytes_read] = '\0';

    // 简单解析请求行，提取方法 and 路径
    // 格式: GET /path HTTP/1.1
    char method[128], path[128], protocol[128];
    if (sscanf(buffer, "%s %s %s", method, path, protocol) != 3) 
    {
        send_response(client_fd, ///<
        400, "Bad Request", "text/plain", "Invalid Request");
        return;
    }

    printf("Received request: %s %s\n", method, path);

    //here we only support GET method.
    if (strcmp(method, "GET") != 0) {
        send_response(client_fd, ///<
        405, "Method Not Allowed", "text/plain", "Only GET is supported");
        return;
    }

    //basic route logic. 
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        const char *html_body = 
            "<!DOCTYPE html>"
            "<html lang=\"en\">"
            "<head><meta charset=\"UTF-8\"><title>TMR Current Sensing Server</title></head>"
            "<body style=\"font-family: sans-serif; text-align: center; padding: 50px;\">"
            "<h1>TMR Current Sensing Integration Terminal</h1>"
            "<style>"
            ".device_table {"
            "border-collapse: collapse;"
            "width: 100%;"
            "max-width: 500px;"
            "font-family: Arial, sans-serif; "
            "font-size: 14px;"
            "}"
            ".device_table td {"
            "border: 1px solid: #dddddd;"
            "padding: 10px 15px;"
            "}"
            ".device_table td:first-child {"
            "font-weight: bold;"
            "background-color: #f2f2f2;"
            "width: 40%;"
            "}"
            "</style>"
            "<table class='device_table'>"
            "<tr><td>Measured Range</td> <td>0~1000 Amps</td></tr>"
            "<tr><td>Bandwidth Range</td> <td>DC~10kHz</td></tr>"
            "<tr><td>Supported Channels</td><td>3 Phases, A - B - C</td></tr>"
            "</table>"
            "<p>Supported Channel: 3 phases A - B - C.</p>"
            "<p>Version: V1.0.0  June 16,2026</p>"
            "</body></html>";
        send_response(client_fd, 200, "OK", "text/html", html_body);
    } else if (strcmp(path, "/hello") == 0) {
        const char *text_body = "Hello, World!";
        send_response(client_fd, 200, "OK", "text/plain", text_body);
    } else {
        const char *not_found = "<html><body><h1>404 Not Found</h1><p>The requested resource was not found.</p></body></html>";
        send_response(client_fd, 404, "Not Found", "text/html", not_found);
    }
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // 1. 创建 socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 设置地址重用，避免重启时 "Address already in use"
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. 绑定地址和端口
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 监听所有网卡
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 3. 开始监听
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server is running on http://localhost:%d\n", PORT);
    printf("Waiting for connections...\n");

    // 4. 循环接受连接
    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("accept failed");
            continue;
        }

        printf("New connection accepted.\n");

        // 处理请求 (在实际生产中，这里通常会 fork 子进程或使用线程来处理，以支持并发)
        handle_request(client_fd);

        // 关闭客户端连接
        close(client_fd);
        printf("Connection closed.\n");
    }

    close(server_fd);
    return 0;
}
