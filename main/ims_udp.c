/*
 * ims_udp.c
 * D. Scherly 20.04.2017
 * UDP packets are received from multiple remotes and transmitted to the primary remote over wifi.
 * All received UDP packets are relayed over UART to the Launchpad.
 * Packets received over UART from the Launchpad are relayed via wifi UDP packets to the primary remote.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/ip4_addr.h"
#include "errno.h"
#include "sdkconfig.h"

#include "ims_projdefs.h"
#include "ims_udp.h"
#include "ims_nvs.h"

static const char *TAG = "udp";

//data structure for a single udp connection
typedef struct udp_conn {
	int socket;
	struct sockaddr_in udpRemote;	//remote parameters
	struct sockaddr_in udpLocal;	//local parameters
} udp_conn_t;

//data structure for all udp connections
typedef struct udp_params {
	int idlecount;
	udp_conn_t udpConnection[NUMREMOTES];
} udp_params_t;


globalptrs_t *globalPtrs;
udp_params_t udpParams;

fd_set master, read_fds;
int fdmax = -1;

/*
 * initialise all sockets to -1
 */
void resetSockets(){
	for(int ii = 0; ii<NUMREMOTES; ++ii){
		udpParams.udpConnection[ii].socket = -1;
	}
}

/*
 * Retrieve udp socket data from flash and load into local data structure
 * set up socket connection for each
 */
bool init_UDP() {
	uint32_t temp, localip;
	char tempstr[20];
	int err;
	fdmax = -1;

	FD_ZERO(&master);    // clear the master and temp sets
	FD_ZERO(&read_fds);

	//localip will be the same for all.
	//if it doesnt exist, something's wrong, so exit.
	if(!get_flash_uint32( &localip, "localip" )){
		return false;
	}

	//set up sockets for multiple remotes
	for(int ii = 0; ii < NUMREMOTES; ++ii){
		//close the socket if it's already open
		if ( udpParams.udpConnection[ii].socket != -1 ){
			//socket is -1 on startup
			if ((err = close( udpParams.udpConnection[ii].socket )) < 0 ){
				ESP_LOGI(TAG, "init_udp: could not close socket, %d", err);
				continue;
			} else{
				ESP_LOGI(TAG, "UDP socket (%d:%d) closed", udpParams.udpConnection[ii].socket, ntohs(udpParams.udpConnection[ii].udpLocal.sin_port));
				udpParams.udpConnection[ii].socket = -1;
			}
		}

		//get remote ip
		sprintf(tempstr,"remoteip%d",ii);
		if(get_flash_uint32( &temp, tempstr ) && (temp > 0)){
		    udpParams.udpConnection[ii].udpRemote.sin_addr.s_addr 	= temp;
		    udpParams.udpConnection[ii].udpRemote.sin_family 		= AF_INET;
		}
		else
			continue;

		//get remote port
		sprintf(tempstr,"remoteport%d",ii);
		if(get_flash_uint32( &temp, tempstr ) && (temp > 0)){
			udpParams.udpConnection[ii].udpRemote.sin_port 		= htons(temp);
		}
		else
			continue;

		//get local port
		sprintf(tempstr,"localport%d",ii);
		if(get_flash_uint32( &temp, tempstr ) && (temp > 0)){
		    udpParams.udpConnection[ii].udpLocal.sin_family 		= AF_INET;
			udpParams.udpConnection[ii].udpLocal.sin_port 			= htons(temp);
		    udpParams.udpConnection[ii].udpLocal.sin_addr.s_addr 	= localip;
		}
		else
			continue;

		memset( udpParams.udpConnection[ii].udpLocal.sin_zero, '\0', sizeof udpParams.udpConnection[ii].udpLocal.sin_zero);
		memset( udpParams.udpConnection[ii].udpRemote.sin_zero, '\0', sizeof udpParams.udpConnection[ii].udpRemote.sin_zero);

		/*if we get here, we can init the socket and bind it to the local port and ip*/

		//connect to socket
		if((err = ( udpParams.udpConnection[ii].socket = socket(AF_INET, SOCK_DGRAM, 0))) < 0){
			ESP_LOGE(TAG, "Init_udp: could not create socket, %d", err);
			continue;
		}


		//bind the socket to the local port
		if (bind( udpParams.udpConnection[ii].socket, (struct sockaddr *) &udpParams.udpConnection[ii].udpLocal, sizeof(udpParams.udpConnection[ii].udpLocal)) < 0) {
	        ESP_LOGE(TAG, "Init_udp: could not bind to local udp port %d", udpParams.udpConnection[ii].udpLocal.sin_port);
	        continue;
	    }
		else {
			ESP_LOGI(TAG, "UDP socket (%d:%d) open",udpParams.udpConnection[ii].socket, ntohs(udpParams.udpConnection[ii].udpLocal.sin_port));
		}

		// add the listener to the master set
		FD_SET( udpParams.udpConnection[ii].socket, &master);

		if(udpParams.udpConnection[ii].socket > fdmax){
			fdmax = udpParams.udpConnection[ii].socket;
		}

	}

	xEventGroupSetBits( globalPtrs->wifi_event_group, UDP_ENABLED );
	return true;
}


uint8_t getCRC(uint8_t *in, int len){
	uint8_t x;

	//ignore start byte
	x = *(in+1) ^ *(in+2);

	//ignore end byte
	for ( int ii = 3; ii < len - 1; ii++){
		x = x ^ *(in + ii);
	}

	return x;
}

/*
 * Send data over udp only to primary remote
 */

void udp_tx_task(void *pvParameter){
	adc_data_t in_raw;
	uint8_t outbuf_raw[13];
	udp_sensor_data_t in;
	uint8_t outbuf[6];
	uint8_t msg_id = 0x00;

	for(;;){
		if(xQueuePeek( globalPtrs->udp_tx_q, &in_raw, pdMS_TO_TICKS(5000))) {
			if((xEventGroupGetBits(globalPtrs->wifi_event_group ) & UDP_ENABLED )) {
				//receive raw sensor data
				if((xEventGroupGetBits(globalPtrs->system_event_group ) & SEND_RAW_DATA_ONLY ) > 0) {
					xQueueReceive( globalPtrs->udp_tx_q, &in_raw, 0);
					outbuf_raw[0] =	0x53;			//start byte
					outbuf_raw[1] = 9; 			//length
					outbuf_raw[2] =	in_raw.nodeid;	//node id
					outbuf_raw[3] = in_raw.counter;	//counter
					for (int ii = 0; ii < sizeof(in_raw.data); ii++){
						outbuf_raw[4 + ii] = *((uint8_t *)in_raw.data + ii);
					}
					outbuf_raw[12] = getCRC(&outbuf_raw[0], sizeof(outbuf_raw));

					sendto(udpParams.udpConnection[0].socket, outbuf_raw, sizeof(outbuf_raw), 0, (struct sockaddr * ) &udpParams.udpConnection[0].udpRemote, sizeof(udpParams.udpConnection[0].udpRemote));
					udpParams.idlecount = 0;
				}
				//receive calibrated sensor data
				else if((xEventGroupGetBits(globalPtrs->system_event_group ) & SEND_RAW_DATA_ONLY ) == 0) {
					xQueueReceive( globalPtrs->udp_tx_q, &in, 0);
					outbuf[0] = 0x53;				//start byte
					outbuf[1] =	2;					//length
					outbuf[2] = in.nodeid + msg_id;	//msg_id
					outbuf[3] = in.counter;			//counter
					outbuf[4] = in.data;			//data
					outbuf[5] = getCRC(&outbuf[0], sizeof(outbuf));

					sendto(udpParams.udpConnection[0].socket, outbuf, sizeof(outbuf), 0, (struct sockaddr * ) &udpParams.udpConnection[0].udpRemote, sizeof(udpParams.udpConnection[0].udpRemote));
					udpParams.idlecount = 0;
				}
			}
		}

		udpParams.idlecount++;

		//keep the wifi connection alive, send a packet every 120 seconds (@ 1000hz tick rate)
		if(udpParams.idlecount >= 24){
			ESP_LOGI(TAG, "wifi keep-alive");
			udpParams.idlecount = 0;
			if(xEventGroupGetBits( globalPtrs->wifi_event_group ) & (UDP_ENABLED)) {
				//send a zero integer
				sendto(udpParams.udpConnection[0].socket, &udpParams.idlecount, sizeof(int), 0, (struct sockaddr * ) &udpParams.udpConnection[0].udpRemote, sizeof(udpParams.udpConnection[0].udpRemote));
			}
		}
	}
}

/*
 * Send data over udp only to primary remote
 */

void udp_tx_rawdata_task(void *pvParameter){

	adc_data_t in;
	uint8_t outbuf[sizeof(in.nodeid) + sizeof(in.counter) + sizeof(in.data) + 3];

	for(;;){
		if(xQueueReceive( globalPtrs->udp_tx_q, &in, pdMS_TO_TICKS(5000))) {
			if((xEventGroupGetBits(globalPtrs->wifi_event_group ) & UDP_ENABLED)) {
				//normal running, send data over UDP
				outbuf[1] =	in.nodeid;
				outbuf[2] = in.counter;
				outbuf[3] = in.counter >> 8;
				outbuf[4] = in.counter >> 16;
				outbuf[5] = in.counter >> 24;
				for (int ii = 0; ii < sizeof(in.data); ii++){
					outbuf[6 + ii] = *((uint8_t *)in.data + ii);
				}
				outbuf[14] = getCRC(&outbuf[0], sizeof(outbuf));
				sendto(udpParams.udpConnection[0].socket, outbuf, sizeof(outbuf), 0, (struct sockaddr * ) &udpParams.udpConnection[0].udpRemote, sizeof(udpParams.udpConnection[0].udpRemote));
				udpParams.idlecount = 0;
			}
		}

		udpParams.idlecount++;

		//keep the wifi connection alive, send a packet every 120 seconds (@ 1000hz tick rate)
		if(udpParams.idlecount >= 24){
			ESP_LOGI(TAG, "wifi keep-alive");
			udpParams.idlecount = 0;
			if(xEventGroupGetBits( globalPtrs->wifi_event_group ) & (UDP_ENABLED)) {
				//send a zero integer
				sendto(udpParams.udpConnection[0].socket, &udpParams.idlecount, sizeof(int), 0, (struct sockaddr * ) &udpParams.udpConnection[0].udpRemote, sizeof(udpParams.udpConnection[0].udpRemote));
			}
		}
	}
}

/*
 * @brief event handler for UDP queue
 * Data in the UART receive data queue is sent over UDP. Data received over UDP is placed in the UART transmit queue
 */
void udp_main_task(void *pvParameter)
{
    globalPtrs = (globalptrs_t *) pvParameter;

    udpParams.idlecount = 0;
    resetSockets();

	xTaskCreate(udp_tx_task, "udp_tx_task", 4096, NULL, 9, NULL);		//start udp transmit task

	while(1){
		if((xEventGroupGetBits( globalPtrs->wifi_event_group ) & (WIFI_READY | UDP_ENABLED)) == WIFI_READY){
			//wifi enabled, udp not enabled
			init_UDP();
		}
		else if((xEventGroupGetBits( globalPtrs->wifi_event_group ) & (NEW_LOCALPORT | NEW_REMOTEIP | NEW_REMOTEPORT )) > 0 ){
			//restart udp if udp connection address or ports have changed
			xEventGroupClearBits( globalPtrs->wifi_event_group, ( NEW_LOCALPORT | NEW_REMOTEIP | NEW_REMOTEPORT | UDP_ENABLED ));
			init_UDP();
		}

		vTaskDelay(pdMS_TO_TICKS(200));
	}
}
