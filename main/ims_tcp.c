/*
 * UART functions for ESP32
 * XoSoft Project
 * D. Scherly 20.04.2017
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "rom/queue.h"
#include "esp_wifi_types.h"
#include "lwip/sockets.h"
#include "tcpip_adapter.h"
#include "lwip/ip4_addr.h"
#include "port/arch/cc.h"
#include "errno.h"
#include "ims_nvs.h"
#include "sdkconfig.h"

#include "ims_projdefs.h"
#include "ims_tcp.h"
#include "ims_ota.h"

static const char *TAG = "ims_tcp";

globalptrs_t *globalPtrs;
uint8_t nodeid;

global_ip_info_t globalIpInfo;	//all ip address and port info

char submitStr[20] = "";
char logbuttonstr[10] = "Start";
char calibrateStr[10] = "Start";
char sendRawDataStr[10] = "Start";
bool notfound = false;


/*
 * initialise variables from flash. Store default variables if not present.
 */
void init_flash_variables(globalptrs_t *arg){

	//set local ip info
	if( !get_flash_uint32( &globalIpInfo.localIpInfo.ip.addr, "localip") ){
		inet_pton(AF_INET, DEFAULT_LOCALIP, &globalIpInfo.localIpInfo.ip);
		set_flash_uint32(globalIpInfo.localIpInfo.ip.addr, "localip");
	}

	if( !get_flash_uint32( &globalIpInfo.localIpInfo.netmask.addr, "netmask") ){
		inet_pton(AF_INET, DEFAULT_NETMASK, &globalIpInfo.localIpInfo.netmask);
		set_flash_uint32(globalIpInfo.localIpInfo.netmask.addr, "netmask");
	}

	if( !get_flash_uint32( &globalIpInfo.localIpInfo.gw.addr, "gateway") ){
		inet_pton(AF_INET, DEFAULT_GATEWAY, &globalIpInfo.localIpInfo.gw);
		set_flash_uint32(globalIpInfo.localIpInfo.gw.addr, "gateway");
	}

	//set first remote port with default values
	if( !get_flash_uint32( &globalIpInfo.remotes[0].ip.addr, "remoteip0") ){
		inet_pton(AF_INET, DEFAULT_REMOTEIP, &globalIpInfo.remotes[0].ip);
		set_flash_uint32(globalIpInfo.remotes[0].ip.addr, "remoteip0");
	}

	if( !get_flash_uint32( &globalIpInfo.remotes[0].localPort, "localport0") ){
		globalIpInfo.remotes[0].localPort = DEFAULT_LOCALPORT;
		set_flash_uint32( DEFAULT_LOCALPORT, "localport0");
	}

	if( !get_flash_uint32( &globalIpInfo.remotes[0].remotePort, "remoteport0") ){
		globalIpInfo.remotes[0].remotePort = DEFAULT_REMOTEPORT;
		set_flash_uint32( DEFAULT_REMOTEPORT, "remoteport0");
	}

	if( !get_flash_uint8( &nodeid, "nodeid") ){
		nodeid = (uint8_t) DEFAULT_NODEID;
		set_flash_uint8( DEFAULT_NODEID, "nodeid");
	}

	if( !get_flash_uint8( &threshold, "threshold") ){
		threshold = (uint8_t) DEFAULT_THRESHOLD;
		set_flash_uint8( DEFAULT_THRESHOLD, "threshold");
	}

}

/*
 * Initialise wifi with the set ip address. Can be called at any time but connection will be lost and reset
 */
void init_wifi(){
	tcpip_adapter_init();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	//manually set ip address for wifi STA connection
	tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
	tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &globalIpInfo.localIpInfo);
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	wifi_config_t sta_config = {
			.sta = {
					.ssid = WIFI_SSID,
					.password = WIFI_PASSWORD,
					.bssid_set = false,
			}
	};

	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_ERROR_CHECK(esp_wifi_disconnect());

	//ESP_LOGI(TAG, "wifi starting, wait..");
	ESP_ERROR_CHECK(esp_wifi_connect());

}

/*
 * Find the listitem with the given value
 */
void removeListItemWithValue(List_t *socketList, int fd) {

	ListItem_t* tempItem;

	if (listLIST_IS_EMPTY( socketList ))
		return;

	int len = listCURRENT_LIST_LENGTH( socketList );
	tempItem = (ListItem_t *) listGET_HEAD_ENTRY( socketList );

	for ( int ii = 0; ii < len; ++ii ){
		if( fd == listGET_LIST_ITEM_VALUE( tempItem )){
			uxListRemove( tempItem );
			free(tempItem);

			return;
		}
		tempItem = (ListItem_t *)listGET_NEXT( tempItem );
	}

	//if we reach here, the value isnt in the list
	return;
}

void printListItems( List_t *socketList ){

	ListItem_t tempItem;
	tempItem = *(ListItem_t *) listGET_HEAD_ENTRY( socketList );
	int len = listCURRENT_LIST_LENGTH( socketList );
	for ( int ii = 0; ii < len; ++ii ){
		printf("%d ", listGET_LIST_ITEM_VALUE( &tempItem ));
		tempItem = *(ListItem_t *)listGET_NEXT( &tempItem );
	}
}

int getMaxListValue( List_t *socketList ) {

	ListItem_t tempItem;

	int len = listCURRENT_LIST_LENGTH( socketList );
	if( len == 0)
		return -1;

	tempItem = *(ListItem_t *) listGET_HEAD_ENTRY( socketList );
	for ( int ii = 0; ii < len-1; ++ii ){
		tempItem = *(ListItem_t *)listGET_NEXT( &tempItem );
	}

	//return value of last element in the list
	return listGET_LIST_ITEM_VALUE( &tempItem );
}

/*
 * parseRecvData looks through the received tcp data to catch http commands and requests
 * TODO parse received data better, lots of repeated code here
 */
void parseRecvData(char *tcpbuffer, int nbytes, int socket){

	char str[BUFSIZE];
	char * pch;
	int iscommand = false;
	int islocalip = false;
	int isnetmask = false;
	int isgatewayip = false;
	int isremoteip0 = false;	//todo fix these
	int isremoteport0 = false;
	int islocalport0 = false;
	int isnodeid = false;
	int isfwupdate = false;
	int iscalibrate = false;
	int isthreshold = false;
	int israwdata = false;
	int iscalright = false;
	tcpip_adapter_ip_info_t tempIpInfo;

	strcpy(str, tcpbuffer);
	pch = strtok(str, " /?&=");	//separate string by whitespace

	while (pch != NULL)
	{
		//printf("%s\n", pch);
		if(strcmp(pch, "GET") == 0) {
			iscommand = true;
		}
		else if(iscommand){
		//process commands received after the GET in the HTTP header
			if(strcmp(pch, "HTTP") == 0){				//if there are no more commands
				iscommand = false;
				break;
			}


			else if(strcmp(pch, "localip") == 0){		//a new local ip address entered
				islocalip = true;
			}
			else if(islocalip){
				inet_pton(AF_INET, pch, &tempIpInfo.ip);	//convert ip string to network format u32
				if(tempIpInfo.ip.addr != globalIpInfo.localIpInfo.ip.addr){
					globalIpInfo.localIpInfo.ip.addr = tempIpInfo.ip.addr;	//copy the uint32
					set_flash_uint32(globalIpInfo.localIpInfo.ip.addr, "localip");	//write value to flash
					strcpy(submitStr,"Settings updated<br>");
					xEventGroupSetBits( globalPtrs->wifi_event_group, NEW_LOCALIP );
				}
				islocalip = false;
			}


			else if(strcmp(pch, "netmask") == 0){		//a new netmask entered
				isnetmask = true;
			}
			else if(isnetmask){
				inet_pton(AF_INET, pch, &tempIpInfo.netmask);	//convert ip string to network format u32
				if(tempIpInfo.netmask.addr != globalIpInfo.localIpInfo.netmask.addr){
					globalIpInfo.localIpInfo.netmask.addr = tempIpInfo.netmask.addr;
					set_flash_uint32(globalIpInfo.localIpInfo.netmask.addr, "netmask");
					strcpy(submitStr,"Settings updated<br>");
					xEventGroupSetBits( globalPtrs->wifi_event_group, NEW_NETMASK );
				}
				isnetmask = false;
			}


			else if(strcmp(pch, "gatewayip") == 0){		//a new gateway ip entered
				isgatewayip = true;
			}
			else if(isgatewayip){
				inet_pton(AF_INET, pch, &tempIpInfo.gw);	//convert ip string to network format u32
				if(tempIpInfo.gw.addr != globalIpInfo.localIpInfo.gw.addr){
					globalIpInfo.localIpInfo.gw.addr = tempIpInfo.gw.addr;
					set_flash_uint32(globalIpInfo.localIpInfo.gw.addr, "gateway");
					strcpy(submitStr,"Settings updated<br>");
					xEventGroupSetBits( globalPtrs->wifi_event_group, NEW_GATEWAY );
				}
				isgatewayip = false;
			}


			else if(strcmp(pch, "remoteip0") == 0){		//a new remote ip address is entered
				isremoteip0 = true;
			}
			else if(isremoteip0){
				inet_pton(AF_INET, pch, &tempIpInfo.ip);	//convert ip string to network format u32
				if(tempIpInfo.ip.addr != globalIpInfo.remotes[0].ip.addr){
					globalIpInfo.remotes[0].ip.addr = tempIpInfo.ip.addr;
					set_flash_uint32(globalIpInfo.remotes[0].ip.addr, "remoteip0");
					strcpy(submitStr,"Settings updated<br>");
					xEventGroupSetBits( globalPtrs->wifi_event_group, NEW_REMOTEIP );
				}
				isremoteip0 = false;
			}

			else if(strcmp(pch, "localport0") == 0){	//a new local port is entered
				islocalport0 = true;
			}
			else if(islocalport0){
				int tempInt = atoi(pch);
				if(globalIpInfo.remotes[0].localPort != tempInt){
					globalIpInfo.remotes[0].localPort = tempInt;
					set_flash_uint32( globalIpInfo.remotes[0].localPort, "localport0");
					strcpy(submitStr,"Settings updated<br>");
					xEventGroupSetBits( globalPtrs->wifi_event_group, NEW_LOCALPORT );
				}
				islocalport0 = false;
			}

			else if(strcmp(pch, "remoteport0") == 0){	//a new remote port is entered
				isremoteport0 = true;
			}
			else if(isremoteport0){
				int tempInt = atoi(pch);
				if(globalIpInfo.remotes[0].remotePort != tempInt){
					globalIpInfo.remotes[0].remotePort = tempInt;
					set_flash_uint32( globalIpInfo.remotes[0].remotePort, "remoteport0" );
					strcpy(submitStr,"Settings updated<br>");
					xEventGroupSetBits( globalPtrs->wifi_event_group, NEW_REMOTEPORT );
				}
				isremoteport0 = false;
			}

			else if(strcmp(pch, "nodeid") == 0){	//a new node id is entered
				isnodeid = true;
			}
			else if(isnodeid){
				uint8_t tempInt = (uint8_t) atoi(pch);
				if(nodeid != tempInt){
					nodeid = tempInt;
					set_flash_uint8( nodeid, "nodeid" );
					strcpy(submitStr,"Settings updated<br>");
					//ESP_LOGI(TAG,"new nodeid");
					xEventGroupSetBits( globalPtrs->system_event_group, NEW_NODEID );
				}
				isnodeid = false;
			}

			else if(strcmp(pch, "fwupdate") == 0){		//a new remote ip address is entered
				isfwupdate = true;
			}
			else if(isfwupdate){
				if(strcmp(pch, "on") == 0){
					xEventGroupSetBits( globalPtrs->system_event_group, FW_UPDATING );
					vTaskDelay(200/portTICK_PERIOD_MS); //delay 200ms to allow adc interrupt to stop before ota task starts
					xTaskCreate(ota_start_task, "ota_start_task", 8196, (void *) globalPtrs, 10, NULL); //highest priority so that it isnt interrupted
				}
				isfwupdate = false;
			}

			else if(strcmp(pch, "calibrate") == 0){		//a new remote ip address is entered
				iscalibrate = true;
			}
			else if(iscalibrate){
				if(strcmp(pch, "Start") == 0){
					strcpy(calibrateStr, "Stop");
//					xEventGroupClearBits( globalPtrs->system_event_group, CALIBRATE_STOP );
					xEventGroupSetBits( globalPtrs->system_event_group, CALIBRATING ); //TODO, create task to catch events such as this to set button text - see ims_adc.c
//					xTaskCreate(calibrate_start_task, "calibrate_start_task", 4096, (void *) globalPtrs, 10, NULL); //highest priority so that it isnt interrupted
				}
				else if(strcmp(pch, "Stop") == 0){
					strcpy(calibrateStr, "Start");
					xEventGroupClearBits( globalPtrs->system_event_group, CALIBRATING );
//					xEventGroupSetBits( globalPtrs->system_event_group, CALIBRATE_STOP ); //TODO, create task to catch events such as this to set button text - see ims_adc.c
//					xTaskCreate(calibrate_start_task, "calibrate_start_task", 4096, (void *) globalPtrs, 10, NULL); //highest priority so that it isnt interrupted
				}
				iscalibrate = false;
			}

			else if(strcmp(pch, "threshold") == 0){		//a new remote ip address is entered
				isthreshold = true;
			}
			else if(isthreshold){
				uint8_t tmp = (uint8_t) atoi(pch);
				if(threshold != tmp){
					threshold = tmp;
					xEventGroupSetBits( globalPtrs->system_event_group, NEW_THRESHOLD );
//					ESP_LOGI(TAG,"test before crash1");
				}
				isthreshold = false;
			}

			else if(strcmp(pch, "rawdata") == 0){		//raw data button pressed
				israwdata = true;
			}
			else if(israwdata){
				if(strcmp(pch, "Start") == 0){
					xEventGroupSetBits( globalPtrs->system_event_group, SEND_RAW_DATA_ONLY );
					strcpy(sendRawDataStr, "Stop");
				}
				else if(strcmp(pch, "Stop") == 0){
					strcpy(sendRawDataStr, "Start");
					xEventGroupClearBits( globalPtrs->system_event_group, SEND_RAW_DATA_ONLY );
				}
				israwdata = false;
			}

			else if(strcmp(pch, "calibrate_right") == 0){		//raw data button pressed
				iscalright = true;
			}
			else if(iscalright){

				//read calibration values from input
				//set event group flag
				//set global threshold values in projdefs
				iscalright = false;
			}


			else {
				//e.g. if favicon request, send 404 not found
				notfound = true;
			}
		}

		//get the next string
		pch = strtok (NULL, " /?&=");
	}

}

/*
 * Sends the configuration page based on the state of the global variables
 */
void sendReplyHTML(int socket){

	char sendbuf[4096] = { 0 };

	char ipbuf[20];
	char nmbuf[20];
	char gwbuf[20];
	char ripbuf0[20];

	//get string representations of ip addresses
	inet_ntop(AF_INET,&globalIpInfo.localIpInfo.ip,ipbuf,20);
	inet_ntop(AF_INET,&globalIpInfo.localIpInfo.netmask,nmbuf,20);
	inet_ntop(AF_INET,&globalIpInfo.localIpInfo.gw,gwbuf,20);
	inet_ntop(AF_INET,&globalIpInfo.remotes[0].ip,ripbuf0,20);

	//reply with some html
	sprintf(sendbuf, "HTTP/1.1 200 OK\r\n"
			"Content-Type: text/html\r\n\r\n"
			"<html>\n"
			"<head>\n"
			"<title>Wifi Sensor</title>\n"
			"</head>\n"
			"<body>\n"
			"<h3>Wifi Sensor configuration</h3>\n"
			"<form action=\"\" method=\"get\">\n"
			"<p>Node ID:&nbsp;<input name=\"nodeid\" type=\"number\" min=\"0\" max=\"255\" value=\"%d\" /></p>"
			"<table border=\"0\">\n"
			"<tr><th colspan=\"2\">Local settings</th><th colspan=\"2\">UDP Remote</th></tr>\n"
			"<tr>\n"
			"<td>Local IP:</td><td><input type=\"text\" name=\"localip\" value=\"%s\" size=\"11\" maxlength=\"15\"></td>\n"
			"<td>IP address:</td><td><input type=\"text\" name=\"remoteip0\" value=\"%s\" size=\"11\" maxlength=\"15\"></td>\n"
			"</tr>\n"
			"<tr>\n"
			"<td>Netmask:</td><td><input type=\"text\" name=\"netmask\" value=\"%s\" size=\"11\" maxlength=\"15\"></td>\n"
			"<td>Local port:</td><td><input type=\"text\" name=\"localport0\" value=\"%d\" size=\"11\"></td>"
			"</tr>\n"
			"<tr><td>Gateway:</td>\n"
			"<td><input type=\"text\" name=\"gatewayip\" value=\"%s\" size=\"11\" maxlength=\"15\"></td>\n"
			"<td>Remote port:</td><td><input type=\"text\" name=\"remoteport0\" value=\"%d\" size=\"11\"></td>\n"
			"</tr>\n"
			"</table>\n"
			"<font color=\"green\">%s</font>\n"
			"<input type=\"submit\" value=\"Save\">\n"
			"</form>\n"

			"<form action=\"\" method=\"get\">\n"
			"Sensor calibration:&nbsp;\n"
			"<input type=\"hidden\" name=\"calibrate\" value=\"%s\" disabled=\"disabled\">"
			"<input type=\"submit\" value=\"%s\" disabled=\"disabled\">\n"
			"</form>\n"

			"<form action=\"\" method=\"get\">\n"
			"<p>Threshold:&nbsp;<input name=\"threshold\" type=\"number\" min=\"0\" max=\"100\" value=\"%d\"  disabled=\"disabled\" size=\"8\"/>&nbsp;%%\n"
			"<input type=\"submit\" value=\"set\" disabled=\"disabled\">\n"
			"</form>\n"

			"<form action=\"\" method=\"get\">\n"
			"Transmit raw sensor data only:&nbsp;\n"
			"<input type=\"hidden\" name=\"rawdata\" value=\"%s\" disabled=\"disabled\">"
			"<input type=\"submit\" onclick=\"rawdata\" value=\"%s\" disabled=\"disabled\">\n"
			"</form>\n"

			"<h3>Firmware update</h3>\n"
			"<form action=\"\" method=\"get\">\n"
			"Update firmware? <input type=\"checkbox\" name=\"fwupdate\"><br>To update: Start an http server on port 8070 in the directory with the new .bin file<br>(e.g. python -m SimpleHTTPServer 8070)<br>\n"
			"<br><input type=\"submit\" value=\"Update\">\n"
			"</form>\n"
			"<p></p>\n"
			"<form action=\"\"><input type=\"submit\" value=\"Refresh page\">\n"
			"</form></body></html>\r\n", nodeid, ipbuf, ripbuf0, nmbuf, globalIpInfo.remotes[0].localPort, gwbuf, globalIpInfo.remotes[0].remotePort, submitStr, calibrateStr, calibrateStr, threshold, sendRawDataStr, sendRawDataStr);
	if (send(socket, sendbuf, sizeof(sendbuf), 0) == -1) { //this has to be sizeof the whole buffer
		perror("send");
	}

	//reset the submit text
	strncpy(submitStr, "\0", 1);
}


/*
 * Sends the a not found message to a browser
 */
void send404ReplyHTML(int socket){

	char sendbuf[64] = { 0 };

	sprintf(sendbuf, "\r\nHTTP/1.1 404 \r\n\r\n");
	if (send(socket, sendbuf, sizeof(sendbuf), 0) == -1) { //this has to be sizeof the whole buffer
		perror("send");
	}
	//printf("\n%s\n\n",sendbuf); //print sent data. TODO: comment out when not needed

}

//print a line containing array data
void print_int_array(int *array, int size){
	printf("UDP Send: ");
	for ( int jj = 0; jj < size; jj++){
		printf("%d ", array[jj]);
	}
	printf("\n");
}


/*
 * Task to get and set parameter settings from a web interface on port 80 at the local ip address of the ESP
 */
void tcp_task( void *pvParameter ){
	int tcpChild, fdmax;
	int listener;     // listening socket descriptor
	int ii = 0, nbytes = 0;
	char tcpbuffer[BUFSIZE] = { 0 };
	struct sockaddr_in tcpServer;
	struct sockaddr_in remoteaddr; // client address
	socklen_t addrlen;
	struct timeval tv;
	fd_set tcpmaster, tcpreadfds;
	List_t socketList;

    globalPtrs = (globalptrs_t *) pvParameter;

	while(1){
		if( xEventGroupGetBits( globalPtrs->wifi_event_group ) & (WIFI_READY) ){
			tv.tv_sec = 0;
			tv.tv_usec = 1000;		//1 ms timeout

			tcpServer.sin_family = AF_INET;  			//leave this as is
			tcpServer.sin_port = htons(TCPPORT);		//http port
			tcpServer.sin_addr.s_addr = INADDR_ANY;
			memset(tcpServer.sin_zero, '\0', sizeof tcpServer.sin_zero);	//zero the rest of the struct
			listener = socket(PF_INET, SOCK_STREAM, 0);
			fcntl(listener, F_SETFL, O_NONBLOCK);

			if (bind(listener, (struct sockaddr * )&tcpServer, sizeof(tcpServer))
					< 0) {
				perror("bind failed");
				return;
			}

			if (listen(listener, 10) == -1) {
				perror("listen");
				exit(1);
			}

			FD_ZERO(&tcpreadfds);  				//clear all entries from set of filedescriptors
			FD_SET(listener, &tcpreadfds);		//add tcpSocket to the set

			//add tcpSocket to listitem
			ListItem_t firstListItem;
			vListInitialiseItem(&firstListItem);
			firstListItem.xItemValue = listener;

			//add listitem to list
			vListInitialise(&socketList);
			vListInsert(&socketList, &firstListItem);

			fdmax = getMaxListValue( &socketList );		//keep track of highest socket number
			tcpmaster = tcpreadfds;						//'select' modifies readfds, so keep a backup of readfds for each loop

			for (;;) {
				ii = 0;
				tcpreadfds = tcpmaster;
				select(fdmax + 1, &tcpreadfds, NULL, NULL, &tv);
				ListItem_t tempItem;
				tempItem = *(ListItem_t *) listGET_HEAD_ENTRY( &socketList );	//get first item in the list
				int len = listCURRENT_LIST_LENGTH( &socketList );

				//loop through all file descriptors in the list
				for ( int aa = 0; aa < len; ++aa ){
					ii = listGET_LIST_ITEM_VALUE( &tempItem );
					//ESP_LOGI(TAG,"checking socket %d",ii);

					//if there is new data on a fd
					if (FD_ISSET(ii, &tcpreadfds)) {
						//new tcp connection request
						if (ii == listener) {
							addrlen = sizeof remoteaddr;
							tcpChild = accept(listener,
									(struct sockaddr * ) &remoteaddr,
									&addrlen);

							if (tcpChild == -1)
								perror("accept");
							else {
								FD_SET(tcpChild, &tcpmaster);

								//Create new listitem and set its value
								ListItem_t *newListItem = malloc(sizeof(ListItem_t));
								vListInitialiseItem(newListItem);
								listSET_LIST_ITEM_VALUE( newListItem, tcpChild );

								//Add new listitem to list
								vListInsert(&socketList, newListItem);

								fdmax = getMaxListValue( &socketList );
							}
						} else {
							//read bytes from a client
							if ((nbytes = recv(ii, tcpbuffer, sizeof(tcpbuffer), 0)) <= 0) {
								//got error or connection closed by client
								if(nbytes < 0)
									perror("recvfrom failed");
							}
							else {
								//some data received
								tcpbuffer[nbytes + 1] = '\0';
//								printf("\n%s\n\n",tcpbuffer); //print received data. TODO: comment out when not needed

								parseRecvData(tcpbuffer, nbytes, ii);
//								sendReplyHTML(ii);

								if (notfound){
									send404ReplyHTML(ii);
									notfound = false;
								} else{
									sendReplyHTML(ii);
								}

								if( (xEventGroupGetBits( globalPtrs->wifi_event_group ) & (NEW_LOCALIP | NEW_NETMASK | NEW_GATEWAY )) > 0 ){
									//restart wifi if there are new ip addresses
									xEventGroupClearBits( globalPtrs->wifi_event_group, (NEW_LOCALIP | NEW_NETMASK | NEW_GATEWAY ));
									init_wifi();
								}
							}
							//close connection
							close(ii);
							FD_CLR(ii, &tcpmaster);
							removeListItemWithValue( &socketList, ii);
							fdmax = getMaxListValue( &socketList );
						}
					}
					tempItem = *(ListItem_t *)listGET_NEXT( &tempItem );
				}

				//check if a firmware update has been completed
				//TODO: leaving this here for eventual update to show update progress in web interface or standalone app
				if( (xEventGroupGetBits( globalPtrs->system_event_group ) & FW_UPDATE_SUCCESS ) > 0 ){
					xEventGroupClearBits( globalPtrs->system_event_group, ( FW_UPDATE_SUCCESS ));
//					ESP_LOGI(TAG,"OTA successful, restarting...");
//					esp_restart();
				}
				else if( (xEventGroupGetBits( globalPtrs->system_event_group ) & FW_UPDATE_FAIL ) > 0 ){
					xEventGroupClearBits( globalPtrs->system_event_group, ( FW_UPDATE_FAIL ));
//					ESP_LOGE(TAG,"OTA failed, try again.");
					//restart adc timer and interrupt
				}
				else if( (xEventGroupGetBits( globalPtrs->system_event_group ) & FW_UPDATE_CRITICAL_FAIL ) > 0 ){
					xEventGroupClearBits( globalPtrs->system_event_group, ( FW_UPDATE_CRITICAL_FAIL ));
//					ESP_LOGE(TAG,"OTA failed, restarting...");
//					esp_restart();
				}

				vTaskDelay(pdMS_TO_TICKS(10)); //delay to reduce processor load
			}
		}
		vTaskDelay(pdMS_TO_TICKS(50));
	}
}




