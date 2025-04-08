#include <stdio.h>

#include "lvgl.h"
#include "lv_examples.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_st77916.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lvgl_port.h"

#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"

#define TAG "MAIN"
#define EXAMPLE_LCD_HOST            (SPI2_HOST)
#define EXAMPLE_PIN_NUM_LCD_PCLK    (9)
#define EXAMPLE_PIN_NUM_LCD_CS      (10)
#define EXAMPLE_PIN_NUM_LCD_DATA0   (11)
#define EXAMPLE_PIN_NUM_LCD_DATA1   (12)
#define EXAMPLE_PIN_NUM_LCD_DATA2   (13)
#define EXAMPLE_PIN_NUM_LCD_DATA3   (14)
#define EXAMPLE_PIN_NUM_LCD_RST     (47)
#define EXAMPLE_PIN_NUM_LCD_BL      (15)

#define EXAMPLE_TP_PORT             (I2C_NUM_0)
#define EXAMPLE_PIN_NUM_TP_SDA      (7)
#define EXAMPLE_PIN_NUM_TP_SCL      (8)
#define EXAMPLE_PIN_NUM_TP_RST      (40)
#define EXAMPLE_PIN_NUM_TP_INT      (41)

#define EXAMPLE_LCD_H_RES           (360)
#define EXAMPLE_LCD_V_RES           (360)
#define EXAMPLE_LCD_BIT_PER_PIXEL   (16)

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_touch_handle_t tp_handle;
static esp_lcd_panel_handle_t panel_handle = NULL;

static lv_disp_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;

void bsp_lcd_tp_init(void)
{
    i2c_master_bus_handle_t bus_handle;
    
    const i2c_master_bus_config_t i2c_bus_conf = {
        .i2c_port = EXAMPLE_TP_PORT,
        .sda_io_num = EXAMPLE_PIN_NUM_TP_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_conf, &bus_handle));
    
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    esp_lcd_new_panel_io_i2c(bus_handle, &io_config, &io_handle);

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_TP_RST,
        .int_gpio_num = EXAMPLE_PIN_NUM_TP_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .interrupt_callback = NULL,
    };

    esp_lcd_touch_new_i2c_cst816s(io_handle, &tp_cfg, &tp_handle);
}

void bsp_lcd_bl_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
    uint32_t duty_cycle = (1023 * brightness_percent) / 100; // LEDC resolution set to 10bits, thus: 100% = 1023
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_cycle);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void bsp_lcd_bl_off(void)
{
    bsp_lcd_bl_set(0);
}

void bsp_lcd_bl_on(void)
{
    bsp_lcd_bl_set(100);
}

void bsp_lcd_bl_init(void)
{
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = EXAMPLE_PIN_NUM_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };

    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ledc_timer_config(&LCD_backlight_timer);
    ledc_channel_config(&LCD_backlight_channel);

    bsp_lcd_bl_on();
}

void bsp_lcd_init(void)
{
    ESP_LOGI(TAG, "Initialize QSPI bus");
    const spi_bus_config_t bus_config = ST77916_PANEL_BUS_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_PCLK,
                                                                                 EXAMPLE_PIN_NUM_LCD_DATA0,
                                                                                 EXAMPLE_PIN_NUM_LCD_DATA1,
                                                                                 EXAMPLE_PIN_NUM_LCD_DATA2,
                                                                                 EXAMPLE_PIN_NUM_LCD_DATA3,
                                                                                 EXAMPLE_LCD_H_RES * 72 * 2);
    ESP_ERROR_CHECK(spi_bus_initialize(EXAMPLE_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    
    const esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install ST77916 panel driver");
    
    st77916_vendor_config_t vendor_config = {
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,     // Implemented by LCD command `36h`
        .bits_per_pixel = EXAMPLE_LCD_BIT_PER_PIXEL,    // Implemented by LCD command `3Ah` (16/18)
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(io_handle, &panel_config, &panel_handle));

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_disp_on_off(panel_handle, true);
}

void app_lvgl(void)
{
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = EXAMPLE_LCD_H_RES * 72,
        .double_buffer = 0,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .swap_bytes = true,
            .buff_dma = true,
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    
    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = tp_handle,
    };
    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);

}

void app_main(void)
{
    bsp_lcd_tp_init();
    bsp_lcd_init();
    bsp_lcd_bl_init();
    
    app_lvgl();

    lvgl_port_lock(0);
    lv_example_anim_3();
    lvgl_port_unlock();
}