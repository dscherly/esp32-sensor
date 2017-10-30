/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "esp_ota_ops.h"
#include "ims_projdefs.h"
#include "errno.h"
#include "sdkconfig.h"
#include "ims_ota.h"

//#define EXAMPLE_SERVER_IP   CONFIG_SERVER_IP
//#define EXAMPLE_SERVER_PORT CONFIG_SERVER_PORT
//#define EXAMPLE_FILENAME CONFIG_EXAMPLE_FILENAME
#define BUFFSIZE 1024
#define TEXT_BUFFSIZE 1024

#define SUCCESS 		0
#define FAIL	 		1
#define CRITICAL_FAIL 	2

static const char *TAG = "ota";
/*an ota data write buffer ready to write to the flash*/
char ota_write_data[BUFFSIZE + 1] = { 0 };

/*packet receive buffer*/
char text[BUFFSIZE + 1] = { 0 };

/* image total length*/
int binary_file_length = 0;

int socket_id = -1;
char http_request[64] = {0};

esp_ota_handle_t out_handle = 0;
esp_partition_t operate_partition;
globalptrs_t *globalPtrs;

/*
 * A task to perform OTA update and restart on success. On fail, return to normal program
 */
void ota_start_task ( void *pvParameter ){

    globalPtrs = (globalptrs_t *) pvParameter;

    //start OTA
    int res = ota_run();

	if( res == SUCCESS ){
		ESP_LOGI(TAG, "OTA successful");
		xEventGroupClearBits( globalPtrs->system_event_group, FW_UPDATING); //inform other tasks that update was successful
		xEventGroupSetBits( globalPtrs->system_event_group, FW_UPDATE_SUCCESS );
		esp_restart();
	}
	else if( res == CRITICAL_FAIL ){
		ESP_LOGE(TAG, "OTA critical fail");
		xEventGroupClearBits( globalPtrs->system_event_group, FW_UPDATING); //inform other tasks that update failed
		xEventGroupSetBits( globalPtrs->system_event_group, FW_UPDATE_CRITICAL_FAIL );
		esp_restart();
	}
	else {
		ESP_LOGE(TAG, "OTA fail");
		xEventGroupClearBits( globalPtrs->system_event_group, FW_UPDATING); //inform other tasks that update failed
		xEventGroupSetBits( globalPtrs->system_event_group, FW_UPDATE_FAIL );
		esp_restart();	//TODO: do not restart on non-critical fail such as no connection. Implement later if gui used instead of web interface
	}

	(void)vTaskDelete(NULL);
}

/*read buffer by byte still delim, return read bytes counts*/
int read_until(char *buffer, char delim, int len)
{
    int i = 0;
    while (buffer[i] != delim && i < len) {
        ++i;
    }
    return i + 1;
}

/* resolve a packet from http socket
 * return true if packet including \r\n\r\n that means http packet header finished, start to receive packet body
 * otherwise return false
 */
bool read_past_http_header(char text[], int total_len, esp_ota_handle_t out_handle)
{
    /* i means current position */
    int i = 0, i_read_len = 0;
    while (text[i] != 0 && i < total_len) {
        i_read_len = read_until(&text[i], '\n', total_len);
        // if we resolve \r\n line,we think packet header is finished
        if (i_read_len == 2) {
            int i_write_len = total_len - (i + 2);
            memset(ota_write_data, 0, BUFFSIZE);
            /*copy first http packet body to write buffer*/
            memcpy(ota_write_data, &(text[i + 2]), i_write_len);

            esp_err_t err = esp_ota_write( out_handle, (const void *)ota_write_data, i_write_len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%x", err);
                return false;
            } else {
                ESP_LOGI(TAG, "esp_ota_write header OK");
                binary_file_length += i_write_len;
            }
            return true;
        }
        i += i_read_len;
    }
    return false;
}

bool connect_to_http_server()
{
	long args;
	socklen_t lon;
	int valopt;
	fd_set myset;
	struct timeval tv;


    ESP_LOGI(TAG, "Server IP: %s Server Port:%s", DEFAULT_OTASERVER, HTTP_PORT);
    sprintf(http_request, "GET %s HTTP/1.1\r\nHost: %s:%s \r\n\r\n", FW_FILENAME, DEFAULT_OTASERVER, HTTP_PORT);

    int  http_connect_flag = -1;
    struct sockaddr_in sock_info;

    socket_id = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_id == -1) {
        ESP_LOGE(TAG, "Create socket failed!");
        return false;
    }

    // Set non-blocking
    args = fcntl(socket_id, F_GETFL,0);
    args |= O_NONBLOCK;
    fcntl(socket_id, F_SETFL, args);

    // set connect info
    memset(&sock_info, 0, sizeof(struct sockaddr_in));
    sock_info.sin_family = AF_INET;
    sock_info.sin_addr.s_addr = inet_addr(DEFAULT_OTASERVER);
    sock_info.sin_port = htons(atoi(HTTP_PORT));


    // connect to http server
    http_connect_flag = connect(socket_id, (struct sockaddr *)&sock_info, sizeof(sock_info));
    if (http_connect_flag < 0) {
    	if (errno == EINPROGRESS) {
    		tv.tv_sec = 3;
    		tv.tv_usec = 0;
    		FD_ZERO(&myset);
    		FD_SET(socket_id, &myset);
    		if (select(socket_id + 1, NULL, &myset, NULL, &tv) > 0) {
    			lon = sizeof(int);
    			getsockopt(socket_id, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon);
    			if (valopt) {
    				fprintf(stderr, "Error in connection() %d - %s\n", valopt, strerror(valopt));
    				return false;
    			}
    		}
    		else {
    			return false;
    		}

    	} else {
			return false;
    	}
    }

    ESP_LOGI(TAG, "Connected to server");

    // Set to blocking mode again...
    args = fcntl(socket_id, F_GETFL, 0);
    args &= (~O_NONBLOCK);
    fcntl(socket_id, F_SETFL, args);
    return true;
}

bool ota_init()
{
    esp_err_t err;
    esp_partition_t *find_partition;
    find_partition = (esp_partition_t*) malloc(sizeof(esp_partition_t));

    const esp_partition_t *esp_current_partition = esp_ota_get_boot_partition();
    if (esp_current_partition->type != ESP_PARTITION_TYPE_APP) {
        ESP_LOGE(TAG, "Error: esp_current_partition->type != ESP_PARTITION_TYPE_APP");
        return false;
    }

    memset(&operate_partition, 0, sizeof(esp_partition_t));
    /*choose which OTA image should we write to*/
    switch (esp_current_partition->subtype) {
    case ESP_PARTITION_SUBTYPE_APP_FACTORY:
        find_partition->subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
        break;
    case  ESP_PARTITION_SUBTYPE_APP_OTA_0:
        find_partition->subtype = ESP_PARTITION_SUBTYPE_APP_OTA_1;
        break;
    case ESP_PARTITION_SUBTYPE_APP_OTA_1:
        find_partition->subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
        break;
    default:
        break;
    }
    find_partition->type = ESP_PARTITION_TYPE_APP;

    const esp_partition_t *partition = esp_partition_find_first(find_partition->type, find_partition->subtype, NULL);
    assert(partition != NULL);
    memset(&operate_partition, 0, sizeof(esp_partition_t));
    err = esp_ota_begin( partition, OTA_SIZE_UNKNOWN, &out_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed err=0x%x!", err);
        free(find_partition);
        return false;
    } else {
        memcpy(&operate_partition, partition, sizeof(esp_partition_t));
        ESP_LOGI(TAG, "esp_ota_begin init OK");
        free(find_partition);
        return true;
    }
    free(find_partition);
    return false;
}

int ota_run(void)
{
    esp_err_t err;
    ESP_LOGI(TAG, "Starting OTA task");

    /*connect to http server*/
    if (connect_to_http_server()) {
//        ESP_LOGI(TAG, "Connected to http server");
    } else {
		ESP_LOGI(TAG, "HTTP server not found");
		return FAIL;
	}

    int res = -1;
    /*send GET request to http server*/
    res = send(socket_id, http_request, strlen(http_request), 0);
    if (res == -1) {
        ESP_LOGE(TAG, "Send GET request to server failed");
        return FAIL;
    } else {
//        ESP_LOGI(TAG, "Send GET request to server succeeded");
    }

    if ( ota_init() ) {
//        ESP_LOGI(TAG, "OTA Init succeeded");
    } else {
        ESP_LOGE(TAG, "OTA Init failed");
		return CRITICAL_FAIL;

    }

    bool resp_body_start = false, flag = true;
    /*deal with all receive packet*/
    while (flag) {
        memset(text, 0, TEXT_BUFFSIZE);
        memset(ota_write_data, 0, BUFFSIZE);
        int buff_len = recv(socket_id, text, TEXT_BUFFSIZE, 0);
        if (buff_len < 0) { /*receive error*/
            ESP_LOGE(TAG, "Error: receive data error! errno=%d", errno);
            return CRITICAL_FAIL;

        } else if (buff_len > 0 && !resp_body_start) { /*deal with response header*/
            memcpy(ota_write_data, text, buff_len);
            resp_body_start = read_past_http_header(text, buff_len, out_handle);

        } else if (buff_len > 0 && resp_body_start) { /*deal with response body*/
            memcpy(ota_write_data, text, buff_len);
            err = esp_ota_write( out_handle, (const void *)ota_write_data, buff_len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%x", err);
                return CRITICAL_FAIL;
            }
            binary_file_length += buff_len;

        } else if (buff_len == 0) {  /*packet over*/
            flag = false;
            ESP_LOGI(TAG, "Connection closed, %d bytes received", binary_file_length);
            close(socket_id);

        } else {
            ESP_LOGE(TAG, "Unexpected recv result");
            return CRITICAL_FAIL;
        }
    }

    if (esp_ota_end(out_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed!");
		return CRITICAL_FAIL;

    }
    err = esp_ota_set_boot_partition(&operate_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed! err=0x%x", err);
		return CRITICAL_FAIL;
    }
    return SUCCESS;
}

