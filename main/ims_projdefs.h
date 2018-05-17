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


#define WIFI_SSID		"XoSoft"
#define WIFI_PASSWORD	"123qweASD"

#define DEFAULT_LOCALIP 	"192.168.0.100"//"192.168.1.10" //
#define DEFAULT_NETMASK 	"255.255.255.0"
#define DEFAULT_REMOTEIP 	"192.168.0.255"//"192.168.1.201"//
#define DEFAULT_OTASERVER 	"192.168.0.101"//"192.168.1.201"//
#define DEFAULT_NULLIP	 	"0.0.0.0"
#define DEFAULT_NODEID		5
#define DEFAULT_LOCALPORT 	14551
#define DEFAULT_REMOTEPORT	14550
#define HTTP_PORT			"8070"
#define FW_FILENAME			"/esp32_sensor.bin"
#define DEFAULT_THRESHOLD 	15

#define TCPPORT 80
#define BUFSIZE 1024
#define UDPBUFSIZE 512
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

//system event group bitmasks for other system-related parameters
#define NEW_NODEID 				BIT0
#define FW_UPDATING				BIT1
#define FW_UPDATE_SUCCESS 		BIT2
#define FW_UPDATE_FAIL 			BIT3
#define FW_UPDATE_CRITICAL_FAIL	BIT4
#define CALIBRATING				BIT5
#define SEND_RAW_DATA_ONLY		BIT6
#define NEW_THRESHOLD			BIT7

//bit masks for ADC data byte
#define ADC0 0
#define ADC1 1
#define ADC2 2
#define ADC3 3

#define NUMREMOTES 1	//Maximum number of UDP remotes

uint8_t threshold;

typedef struct udp_connection {
	ip4_addr_t ip;
	uint32_t localPort;
	uint32_t remotePort;
} udp_connection_t;

typedef struct global_ip_info {
	tcpip_adapter_ip_info_t localIpInfo;	//local tcp connection settings
	udp_connection_t remotes[NUMREMOTES];	//udp remote connection settings
} global_ip_info_t;

typedef struct {
	int size;					//number of bytes of valid data
	uint8_t* data;	//data array
} udp_data_t;

typedef struct {
	uint8_t startbyte;
	uint8_t len;
	uint8_t msgid;
	uint16_t timestamp;
	uint16_t data[4];
	uint8_t crc;
} shoe_data_t;

typedef struct {
	uint8_t startbyte;
	uint8_t len;
	uint8_t msgid;
	uint16_t timestamp;
	uint16_t sync;
	uint8_t crc;
} sync_data_t;

typedef struct {
	uint8_t nodeid;
	uint8_t counter;
	uint8_t data;
} udp_sensor_data_t;

typedef struct {
	EventGroupHandle_t wifi_event_group;
	EventGroupHandle_t system_event_group;
	QueueHandle_t udp_tx_q;	//TODO: still necessary?
	QueueHandle_t sync_tx_q;
	QueueHandle_t shoe_tx_q;
	QueueHandle_t adc_q;	//TODO: remove this
} globalptrs_t;

#ifdef __cplusplus
}
#endif


#endif /* __IMS_PROJDEFS_H__ */
