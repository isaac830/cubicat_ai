file(GLOB_RECURSE SRCS "ota.cpp" "main.cpp" "yuanti_18.c" "./*.cpp" "./*.c")
idf_component_register(SRCS ${SRCS}
                    PRIV_REQUIRES cubicat_s3 cubicat_spine lvgl esp_http_client mbedtls esp_websocket_client esp_hw_support 
                    PRIV_REQUIRES esp-sr json esp-opus app_update spi_flash esp_app_format esp_new_jpeg
                    INCLUDE_DIRS "./" "third_party/")
add_compile_definitions(LV_LVGL_H_INCLUDE_SIMPLE)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-variable -Wno-ignored-qualifiers)
# hack fix of esp-opus-encoder component compile error
target_compile_options(__idf_78__esp-opus-encoder PRIVATE -Wno-error=stringop-overflow)
spiffs_create_partition_image(spiffs ../spiffs_img FLASH_IN_PROJECT)