//filename: tcpupload_process.c
//function: pack 3 phases data and upload with json format via TCP.
//date: May 25, 2025.
//author: anonymous.


//gcc json_test.c -I libcjson -Llibcjson -lcjson
//export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./libcjson 
//https://dongshao.blog.csdn.net/article/details/106699410

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <errno.h>
#include "libcjson/cJSON.h"

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 8080
#define BUFFER_SIZE 1024


cJSON *create_Json(void)
{
	cJSON *node_root=cJSON_CreateObject();
	cJSON_AddItemToObject(node_root, "name", cJSON_CreateString("Manjaro"));
	cJSON_AddNumberToObject(node_root, "age", 80.5);

	cJSON *node_pro=cJSON_CreateObject();
	cJSON_AddItemToObject(node_pro, "English", cJSON_CreateNumber(4));
	cJSON_AddItemToObject(node_pro, "Mandarin", cJSON_CreateNumber(4));
	cJSON_AddItemToObject(node_pro, "Physical Education", cJSON_CreateNumber(4));

	cJSON *node_language=cJSON_CreateArray();
	cJSON_AddItemToArray(node_language, cJSON_CreateString("C++"));
	cJSON_AddItemToArray(node_language, cJSON_CreateString("Verilog"));

	cJSON *node_phone=cJSON_CreateObject();
	cJSON_AddItemToObject(node_phone, "number", cJSON_CreateString("13522296239"));
	cJSON_AddItemToObject(node_phone, "type", cJSON_CreateString("Retirement Home"));

	cJSON *node_course=cJSON_CreateArray();
	cJSON *node_item1=cJSON_CreateObject();
	cJSON_AddItemToObject(node_item1, "name", cJSON_CreateString("Kernel Devel"));
	cJSON_AddNumberToObject(node_item1, "price", 20.2);
	cJSON_AddItemToArray(node_course,node_item1);

	cJSON *node_item2=cJSON_CreateObject();
	cJSON_AddItemToObject(node_item2, "name", cJSON_CreateString("APP Devel"));
	cJSON_AddNumberToObject(node_item2, "price", 18.2);
	cJSON_AddItemToArray(node_course,node_item2);

	/////////////////////////////////////////////////
	cJSON_AddItemToObject(node_root, "professional", node_pro);
	cJSON_AddItemToObject(node_root,"language",node_language);
	cJSON_AddItemToObject(node_root, "phone", node_phone);
	cJSON_AddItemToObject(node_root,"courses",node_course);

	cJSON_AddBoolToObject(node_root, "vip", 1);
	cJSON_AddNullToObject(node_root, "Address");

	return node_root;
}

// int main(int argc, char **argv)
// {
//     cJSON *node_root=create_Json();
//     char *pjson=cJSON_Print(node_root);
//     printf("%s\n",pjson);
//     free(pjson);
//     cJSON_free(node_root);
//     return 0;
// }


void error_handling(const char *message) {
	perror(message);
	exit(EXIT_FAILURE);
}
int is_socket_connected(int sock_fd)
{
	if(sock_fd<0)
	{
		return 0;
	}
	struct tcp_info info={0};
	socklen_t len=sizeof(info);
	if(getsockopt(sock_fd, IPPROTO_TCP, TCP_INFO, &info, &len)<0)
	{
		perror("getsockopt");
		return 0;
	}
	return (info.tcpi_state == TCP_ESTABLISHED)?(1):(0);	
}
int main(int argc, char *argv[]) {
	int sock_fd;
	struct sockaddr_in server_addr;
	char send_buffer[BUFFER_SIZE];
	char recv_buffer[BUFFER_SIZE];
	ssize_t bytes_sent, bytes_received;

	//parse command line parameters.
	char *server_ip = DEFAULT_SERVER_IP;
	int server_port = DEFAULT_SERVER_PORT;

	if (argc > 1) {
		server_ip = argv[1];
	}
	if (argc > 2) {
		server_port = atoi(argv[2]);
	}

	//create json object.
	cJSON *node_root=create_Json();
	char *pjson=cJSON_Print(node_root);

	while(1)
	{
		if(sock_fd<0 || !is_socket_connected(sock_fd))
		{
			printf("Socket is not connected. Reconnecting...\n");
			close(sock_fd);
			sock_fd=-1; 

			sock_fd = socket(AF_INET, SOCK_STREAM, 0);
			if (sock_fd < 0) 
			{
				error_handling("Socket creation failed");
				sleep(5);
				continue;
			}

			memset(&server_addr, 0, sizeof(server_addr));
			server_addr.sin_family = AF_INET;
			server_addr.sin_port = htons(server_port); 

			if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) 
			{
				error_handling("Invalid address / Address not supported");
			}

			if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
			{
				error_handling("Connection failed");
			}
			printf("Connected successfully, to %s:%d\n", server_ip, server_port);	
		}	

		// snprintf(send_buffer, sizeof(send_buffer), "Hello from Client!");
		// bytes_sent = send(sock_fd, send_buffer, strlen(send_buffer), 0);
		// if (bytes_sent < 0) {
		// 	error_handling("Send failed");
		// }
		// printf("Sent: %s\n", send_buffer);


		// bytes_received = recv(sock_fd, recv_buffer, sizeof(recv_buffer) - 1, 0);
		// if (bytes_received < 0) {
		// 	error_handling("Receive failed");
		// } else if (bytes_received == 0) {
		// 	printf("Server closed connection.\n");
		// } else {
		// 	recv_buffer[bytes_received] = '\0'; // 确保字符串以null结尾
		// 	printf("Received: %s\n", recv_buffer);
		// }

		bytes_sent = send(sock_fd, pjson, strlen(pjson), 0);
		if (bytes_sent < 0) {
			error_handling("Send failed");
		}
		printf("Sent: %s\n", pjson);	
		sleep(5);
	}
	
	free(pjson);
	cJSON_free(node_root);

	close(sock_fd);
	printf("Connection closed.\n");

	return 0;
}
