/*
	TCP for ESP32
	IMS version for XoSoft
	D. Scherly 19.04.2017

 */

#ifndef __IMS_TCP_H__
#define __IMS_TCP_H__

#ifdef __cplusplus
extern "C" {
#endif

void init_flash_variables(globalptrs_t *arg);
void init_wifi();
void removeListItemWithValue(List_t *socketList, int fd);
void printListItems( List_t *socketList );
int getMaxListValue( List_t *socketList );
void parseRecvData(char *tcpbuffer, int nbytes, int socket);
void sendReplyHTML(int socket);
void print_int_array(int *array, int size);
void tcp_task( void *pvParameter );



#ifdef __cplusplus
}
#endif

#endif /* __IMS_TCP_H__ */
