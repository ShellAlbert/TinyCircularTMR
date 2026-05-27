//filename: tcpupload_process.c
//function: pack 3 phases data and upload with json format via TCP.
//date: May 25, 2025.
//author: anonymous.


//https://dongshao.blog.csdn.net/article/details/106699410

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcjson/cJSON.h"

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

int main(int argc, char **argv)
{
    cJSON *node_root=create_Json();
    char *pjson=cJSON_Print(node_root);
    printf("%s\n",pjson);
    free(pjson);
    cJSON_free(node_root);
    return 0;
}