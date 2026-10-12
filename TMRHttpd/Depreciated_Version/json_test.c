//https://dongshao.blog.csdn.net/article/details/106699410

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "libcjson/cJSON.h"
#include "base64.h"

//static device-relevant value.
const char *device_id="1000000000020033"; //Device ID, 16-bits.
const char *equipment_name="HuaiRou_TMR_Integration";
const char *manufacture_name="CEPRI_Sensing_Institute";


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
//create static device-relevant json package.
cJSON *create_Device_Json(void)
{
    time_t current_time=time(NULL);
    struct tm *tm_info=localtime(&current_time);
    char current_time_buffer[128];
    strftime(current_time_buffer,sizeof(current_time_buffer),"%Y-%m-%d %H:%M:%S",tm_info);


    cJSON *node_root=cJSON_CreateObject();
    cJSON_AddItemToObject(node_root, "id", cJSON_CreateString(device_id));
    cJSON_AddItemToObject(node_root, "type", cJSON_CreateString("TMR_DEVICE"));
    cJSON_AddItemToObject(node_root, "timestamp", cJSON_CreateNumber(current_time));
    //never expire.
    cJSON_AddNumberToObject(node_root, "expire", -1);

    cJSON *node_param=cJSON_CreateObject();
    cJSON_AddItemToObject(node_param, "cmd", cJSON_CreateString("STATIC"));

    cJSON *node_data=cJSON_CreateObject();
    cJSON_AddItemToObject(node_data, "token", cJSON_CreateString(""));
    cJSON_AddItemToObject(node_data, "timestamp", cJSON_CreateString(current_time_buffer));

    cJSON *node_body=cJSON_CreateObject();
    cJSON_AddItemToObject(node_body, "eqpName", cJSON_CreateString(equipment_name));
    cJSON_AddItemToObject(node_body, "manufactorName", cJSON_CreateString(manufacture_name));
    cJSON_AddItemToObject(node_body, "begDate", cJSON_CreateString(current_time_buffer));

    /////////////////////////////////////////////////
    cJSON_AddItemToObject(node_data, "body", node_body);
    cJSON_AddItemToObject(node_param,"data",node_data);
    cJSON_AddItemToObject(node_root, "param", node_param);

    return node_root;
}
//create dynamic data-relevant json package.
cJSON *create_Data_Json(void)
{
    time_t current_time=time(NULL);
    struct tm *tm_info=localtime(&current_time);
    char current_time_buffer[128];
    strftime(current_time_buffer,sizeof(current_time_buffer),"%Y-%m-%d %H:%M:%S",tm_info);


    cJSON *node_root=cJSON_CreateObject();
    cJSON_AddItemToObject(node_root, "id", cJSON_CreateString(device_id));
    cJSON_AddItemToObject(node_root, "type", cJSON_CreateString("TMR_DATA"));
    cJSON_AddItemToObject(node_root, "timestamp", cJSON_CreateNumber(current_time));
    //never expire.
    cJSON_AddNumberToObject(node_root, "expire", -1);

    cJSON *node_param=cJSON_CreateObject();
    cJSON_AddItemToObject(node_param, "cmd", cJSON_CreateString("DYNAMIC"));

    cJSON *node_data=cJSON_CreateObject();
    cJSON_AddItemToObject(node_data, "token", cJSON_CreateString(""));
    cJSON_AddItemToObject(node_data, "timestamp", cJSON_CreateString(current_time_buffer));

    //data section.
    cJSON *node_body=cJSON_CreateArray();
    //node_temp.
    cJSON *node_temp=cJSON_CreateObject();
    cJSON_AddItemToObject(node_temp, "fieldName", cJSON_CreateString("Temperature"));
    cJSON_AddItemToObject(node_temp, "fieldValue", cJSON_CreateNumber(24.5));
    cJSON_AddItemToObject(node_temp, "unit", cJSON_CreateString("℃"));
    cJSON_AddItemToArray(node_temp,node_body);
    //node_data.

    /////////////////////////////////////////////////
    cJSON_AddItemToObject(node_data, "body", node_body);
    cJSON_AddItemToObject(node_param,"data",node_data);
    cJSON_AddItemToObject(node_root, "param", node_param);

    return node_root;
}
int main(int argc, char **argv)
{
    float current_value[1024];
    for(int i=0;i<1024;i++)
    {
        current_value[i]=10.0f+i;
    }

    //device json.
    cJSON *node_device=create_Device_Json();
    char *pjson=cJSON_Print(node_device);
    printf("%s\n",pjson);
    free(pjson);
    cJSON_free(node_device);


    //data json.

    // 编码
    char *encoded = base64_encode_manual((const unsigned char *)current_value, sizeof(current_value));
    if (encoded) {
        printf("Encoded: %s\n", encoded);
        
        // 解码
        size_t decoded_len = 0;
        unsigned char *decoded = base64_decode_manual(encoded, &decoded_len);
        if (decoded) {
            printf("decode len:%ld\n",decoded_len);
            float *current_dec_value=(float*)decoded;
            for(int i=0;i<1024;i++)
            {
                printf("%d  =  %.2f\n",i,current_dec_value[i]);
            }
            free(decoded);
        }
        free(encoded);
    }
    return 0;
}