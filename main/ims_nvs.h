/*
	NVS functions for ESP32
	IMS version for XoSoft
	D. Scherly 19.04.2017

 */

#ifndef __IMS_NVS_H__
#define __IMS_NVS_H__

#ifdef __cplusplus
extern "C" {
#endif


bool erase_flash_key( const char *label );

bool set_flash_uint32( uint32_t ip, const char *label );
bool get_flash_uint32( uint32_t *ip, const char *label );

bool set_flash_uint8( uint8_t value, const char *label );
bool get_flash_uint8( uint8_t *value, const char *label );

bool get_flash_str( char *str, const char *label );
bool set_flash_str( char *str, const char *label );

#ifdef __cplusplus
}
#endif

#endif /* __IMS_NVS_H__ */
