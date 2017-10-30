/*
	UDP for ESP32
	IMS version for XoSoft
	D. Scherly 20.04.2017

 */

#ifndef __IMS_UDP_H__
#define __IMS_UDP_H__


#ifdef __cplusplus
extern "C" {
#endif


void resetSockets();
bool init_UDP();//int *udpSocket, struct sockaddr_in *udpClient, struct sockaddr_in *udpServer);
uint8_t getChecksum(uint8_t *in, int len);
uint8_t getCRC8(uint8_t *in, int len);
void udp_tx_task(void *pvParameter);
void udp_rx_task(void *pvParameter);
void udp_main_task(void *pvParameter);



#ifdef __cplusplus
}
#endif


#endif /* __IMS_UDP_H__ */
