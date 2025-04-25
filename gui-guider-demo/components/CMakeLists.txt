if(ESP_PLATFORM)

file(GLOB_RECURSE SOURCES *.c ./generated/images/*.c  ./generated/guider_fonts/*.c ./custom/*.c)
set(INCLUDE_DIRS ./generated ./generated/guider_fonts ./generated/guider_customer_fonts ./generated/guider_fonts ./custom)

idf_component_register(SRCS ${SOURCES}
                        INCLUDE_DIRS ${INCLUDE_DIRS}
                        REQUIRES lvgl)
idf_build_set_property(COMPILE_OPTIONS "-DLV_LVGL_H_INCLUDE_SIMPLE=1" APPEND)
else()
    message(FATAL_ERROR "ESP_PLATFORM is not defined. Try reinstalling ESP-IDF.")
endif()
    