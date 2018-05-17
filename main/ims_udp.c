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

//lookup table for crc calculation
uint8_t crc8_poly1[256] = {0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,\
		0x24,0x23,0x2A,0x2D,0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,\
		0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,0xE0,0xE7,0xEE,0xE9,\
		0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,\
		0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,\
		0xB4,0xB3,0xBA,0xBD,0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,\
		0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,0xB7,0xB0,0xB9,0xBE,\
		0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,\
		0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,\
		0x03,0x04,0x0D,0x0A,0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,\
		0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,0x89,0x8E,0x87,0x80,\
		0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,\
		0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,\
		0xDD,0xDA,0xD3,0xD4,0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,\
		0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,0x19,0x1E,0x17,0x10,\
		0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,\
		0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,\
		0x6A,0x6D,0x64,0x63,0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,\
		0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,0xAE,0xA9,0xA0,0xA7,\
		0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,\
		0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,\
		0xFA,0xFD,0xF4,0xF3};


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
	int broadcast = 1;
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

		//for 0 socket which broadcasts to remote logging systems
		if( ii == 0 ){
			// this call allows broadcast packets to be sent
			if (setsockopt(udpParams.udpConnection[ii].socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast) == -1) {
				perror("setsockopt (SO_BROADCAST)");
				return false;
			}
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


uint8_t getChecksum(uint8_t *in, int len){
	uint8_t x;

	//ignore start byte
	x = *(in+1) ^ *(in+2);

	//ignore end byte
	for ( int ii = 3; ii < len - 1; ii++){
		x = x ^ *(in + ii);
	}

	return x;
}

//Get the 8-bit polynomial CRC of the data to be sent, exluding the start and end byte from the calculation
uint8_t getCRC8(uint8_t *in, int len){
	uint8_t tmp = 0;

	for(int jj = 1; jj < (len-1); jj++){
		tmp = crc8_poly1[in[jj] ^ tmp];
	}
	return tmp;
}

/*
 * Send data over udp only to primary remote
 */
void udp_tx_task(void *pvParameter){
	sync_data_t sync_in;
	shoe_data_t shoe_in;
	uint8_t outbuf[512];
	uint8_t bufsize = 0;

	for(;;){
		if(xQueuePeek( globalPtrs->sync_tx_q, &sync_in, 0)) {
			if(xQueueReceive( globalPtrs->sync_tx_q, &sync_in, pdMS_TO_TICKS(1))) {
				if((xEventGroupGetBits(globalPtrs->wifi_event_group ) & UDP_ENABLED)) {
					outbuf[0] = sync_in.startbyte;
					outbuf[1] = sync_in.len;
					outbuf[2] = sync_in.msgid;
					outbuf[3] = (uint8_t) sync_in.timestamp;
					outbuf[4] = (uint8_t) (sync_in.timestamp >> 8);
					outbuf[5] = (uint8_t) sync_in.sync;
					outbuf[6] = (uint8_t) (sync_in.sync >> 8);
					outbuf[7] = getCRC8( (uint8_t*) &outbuf, 8 );
					bufsize = 8;
					sendto(udpParams.udpConnection[0].socket, outbuf, bufsize, 0, (struct sockaddr * ) &udpParams.udpConnection[0].udpRemote, sizeof(udpParams.udpConnection[0].udpRemote));
				}
			}
		}
		else if(xQueuePeek( globalPtrs->shoe_tx_q, &shoe_in, 0)) {
			if(xQueueReceive( globalPtrs->shoe_tx_q, &shoe_in, pdMS_TO_TICKS(1))) {
				if((xEventGroupGetBits(globalPtrs->wifi_event_group ) & UDP_ENABLED)) {
					outbuf[0] = shoe_in.startbyte;
					outbuf[1] = shoe_in.len;
					outbuf[2] = shoe_in.msgid;
					outbuf[3] = (uint8_t) shoe_in.timestamp;
					outbuf[4] = (uint8_t) (shoe_in.timestamp >> 8);
					outbuf[5] = (uint8_t) shoe_in.data[0];
					outbuf[6] = (uint8_t) (shoe_in.data[0] >> 8);
					outbuf[7] = (uint8_t) shoe_in.data[1];
					outbuf[8] = (uint8_t) (shoe_in.data[1] >> 8);
					outbuf[9] = (uint8_t) shoe_in.data[2];
					outbuf[10] = (uint8_t) (shoe_in.data[2] >> 8);
					outbuf[11] = (uint8_t) shoe_in.data[3];
					outbuf[12] = (uint8_t) (shoe_in.data[3] >> 8);
					outbuf[13] = getCRC8( (uint8_t*) &outbuf, 14 );
					bufsize = 14;
					sendto(udpParams.udpConnection[0].socket, outbuf, bufsize, 0, (struct sockaddr * ) &udpParams.udpConnection[0].udpRemote, sizeof(udpParams.udpConnection[0].udpRemote));
				}
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
    TaskHandle_t xHandle = NULL;

	while(1){
		if((xEventGroupGetBits( globalPtrs->wifi_event_group ) & (WIFI_READY | UDP_ENABLED)) == WIFI_READY){
			init_UDP();
		}
		else if((xEventGroupGetBits( globalPtrs->wifi_event_group ) & (WIFI_READY | UDP_ENABLED)) == (WIFI_READY | UDP_ENABLED)){
			if( xHandle == NULL ){
				xTaskCreate(udp_tx_task, "udp_tx_task", 4096, NULL, 3, &xHandle);		//start udp transmit task
				ESP_LOGI(TAG,"udp task created");
			}
		}
		else if((xEventGroupGetBits( globalPtrs->wifi_event_group ) & (WIFI_READY | UDP_ENABLED)) == UDP_ENABLED){
			if( xHandle != NULL ) {	//calling delete on NULL handle causes calling task to be deleted
				vTaskDelete( xHandle );
				ESP_LOGI(TAG,"udp task deleted");
			}
		}
		else if((xEventGroupGetBits( globalPtrs->wifi_event_group ) & (NEW_LOCALPORT | NEW_REMOTEIP | NEW_REMOTEPORT )) > 0 ){
			//restart udp if udp connection address or ports have changed
			xEventGroupClearBits( globalPtrs->wifi_event_group, ( NEW_LOCALPORT | NEW_REMOTEIP | NEW_REMOTEPORT | UDP_ENABLED ));
			init_UDP();
		}

		vTaskDelay(pdMS_TO_TICKS(200));
	}
}
