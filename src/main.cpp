#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
// #include "tinyusb.h"  // 注意这里是 tinyusb.h

extern "C" {
#include "tusb.h"
#include "bsp/esp32s3/usb_descriptors.h" // 如果你有自定义 descriptors
}
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "usb_uac_demo";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Initializing TinyUSB UAC...");

    // 初始化 TinyUSB
    ESP_ERROR_CHECK(tusb_init());

    ESP_LOGI(TAG, "TinyUSB initialized. ESP32 should appear as a USB Audio Device now.");

    while (1) {
        // 这里只需调用 tinyusb 的 periodic task
        tud_task(); 
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
