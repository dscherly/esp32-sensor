deps_config := \
	/Users/dan/esp/esp-idf/components/aws_iot/Kconfig \
	/Users/dan/esp/esp-idf/components/bt/Kconfig \
	/Users/dan/esp/esp-idf/components/esp32/Kconfig \
	/Users/dan/esp/esp-idf/components/ethernet/Kconfig \
	/Users/dan/esp/esp-idf/components/fatfs/Kconfig \
	/Users/dan/esp/esp-idf/components/freertos/Kconfig \
	/Users/dan/esp/esp-idf/components/log/Kconfig \
	/Users/dan/esp/esp-idf/components/lwip/Kconfig \
	/Users/dan/esp/esp-idf/components/mbedtls/Kconfig \
	/Users/dan/esp/esp-idf/components/openssl/Kconfig \
	/Users/dan/esp/esp-idf/components/spi_flash/Kconfig \
	/Users/dan/esp/esp-idf/components/bootloader/Kconfig.projbuild \
	/Users/dan/esp/esp-idf/components/esptool_py/Kconfig.projbuild \
	/Users/dan/esp/esp-idf/components/partition_table/Kconfig.projbuild \
	/Users/dan/esp/esp32-sensor/main/Kconfig.projbuild \
	/Users/dan/esp/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
