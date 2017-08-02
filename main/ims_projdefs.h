/*
	UDP for ESP32
	IMS version for XoSoft
	D. Scherly 20.04.2017

 */

#ifndef __IMS_PROJDEFS_H__
#define __IMS_PROJDEFS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "freertos/event_groups.h"


#define WIFI_SSID		"gzb-99507"//"XoSoft"
#define WIFI_PASSWORD	"cd5h-6j1m-txga-fkga"//"123qweASD"

#define DEFAULT_LOCALIP 	"192.168.1.10" //"192.168.0.10"
#define DEFAULT_NETMASK 	"255.255.255.0"
#define DEFAULT_GATEWAY 	"192.168.1.1"//"192.168.0.1"
#define DEFAULT_REMOTEIP 	"192.168.1.201"//"192.168.0.101"
#define DEFAULT_NULLIP	 	"0.0.0.0"
#define DEFAULT_NODEID		10
#define DEFAULT_LOCALPORT 	16500
#define DEFAULT_REMOTEPORT	16501
#define HTTP_PORT			"8070"
#define FW_FILENAME			"/esp32_sensor.bin"

#define TCPPORT 80
#define BUFSIZE 1024
#define ADCBUFSIZE 4
#define MAXSTRLENGTH 255
#define MAXFILENAMELENGTH 8

//wifi event group bitmasks for parameter checking
#define CONNECTED_BIT 	BIT0
#define NEW_LOCALIP 	BIT1
#define NEW_NETMASK  	BIT2
#define NEW_GATEWAY  	BIT3
#define NEW_REMOTEIP  	BIT4
#define NEW_REMOTEPORT 	BIT5
#define NEW_LOCALPORT  	BIT6
#define WIFI_READY		BIT7
#define UDP_ENABLED  	BIT8
//#define NEW_NODEID		BIT9
//#define FW_UPDATING		BIT10

//system event group bitmasks for other system-related parameters
#define NEW_NODEID 				BIT0
#define FW_UPDATING				BIT1
#define FW_UPDATE_SUCCESS 		BIT2
#define FW_UPDATE_FAIL 			BIT3
#define FW_UPDATE_CRITICAL_FAIL	BIT4
#define NUMREMOTES 1	//Maximum number of UDP remotes

typedef struct udp_connection {
	ip4_addr_t ip;
	uint32_t localPort;
	uint32_t remotePort;
} udp_connection_t;

typedef struct global_ip_info {
	tcpip_adapter_ip_info_t localIpInfo;	//local tcp connection settings
	udp_connection_t remotes[NUMREMOTES];	//udp remote connection settings
}global_ip_info_t;

typedef struct {
	uint8_t nodeid;
	uint32_t counter;
	int size;					//number of bytes of valid data
	uint16_t data[ADCBUFSIZE];	//data array
} adc_data_t;

typedef struct {
	EventGroupHandle_t wifi_event_group;
	EventGroupHandle_t system_event_group;
	QueueHandle_t udp_tx_q;
	//uint8_t nodeid;
} globalptrs_t;

#ifdef __cplusplus
}
#endif


#endif /* __IMS_PROJDEFS_H__ */
