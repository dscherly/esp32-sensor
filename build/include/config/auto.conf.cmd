deps_config := \
	/c/esp32/esp-idf/components/bt/Kconfig \
	/c/esp32/esp-idf/components/esp32/Kconfig \
	/c/esp32/esp-idf/components/ethernet/Kconfig \
	/c/esp32/esp-idf/components/freertos/Kconfig \
	/c/esp32/esp-idf/components/log/Kconfig \
	/c/esp32/esp-idf/components/lwip/Kconfig \
	/c/esp32/esp-idf/components/mbedtls/Kconfig \
	/c/esp32/esp-idf/components/openssl/Kconfig \
	/c/esp32/esp-idf/components/spi_flash/Kconfig \
	/c/esp32/esp-idf/components/bootloader/Kconfig.projbuild \
	/c/esp32/esp-idf/components/esptool_py/Kconfig.projbuild \
	/c/esp32/esp-idf/components/partition_table/Kconfig.projbuild \
	/c/esp32/esp32-sensor/main/Kconfig.projbuild \
	/c/esp32/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
