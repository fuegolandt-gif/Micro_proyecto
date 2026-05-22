#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"

#define LED_AZUL GPIO_NUM_8 // LED integrado C3 SuperMini

typedef struct {
    uint32_t id_proyecto;
    float temperatura;
    float peso;
    uint8_t estado_peltier;
} DatosCava;

TaskHandle_t xTareaMain = NULL;

void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len == sizeof(DatosCava)) {
        DatosCava *res = (DatosCava *)data;
        if (res->id_proyecto == 0xCABA) {
            printf("\n--- RECEPCIÓN DIRECTA ---\n");
            printf("Temp: %.1f C | Peso: %.2f g | Peltier: %s\n", 
                   res->temperatura, res->peso, res->estado_peltier ? "ON" : "OFF");
            
            // Notificar para parpadeo de LED
            if(xTareaMain) xTaskNotifyGive(xTareaMain);
        }
    }
}

void app_main(void) {
    xTareaMain = xTaskGetCurrentTaskHandle();

    // LED (Lógica invertida en C3: 0 = ON, 1 = OFF)
    gpio_reset_pin(LED_AZUL);
    gpio_set_direction(LED_AZUL, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_AZUL, 1); 

    // WIFI & ESP-NOW
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    esp_now_init();
    esp_now_register_recv_cb(recv_cb);

    while (1) {
        // Esperar notificación de datos nuevos
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        gpio_set_level(LED_AZUL, 0); // Encender
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(LED_AZUL, 1); // Apagar
    }
}