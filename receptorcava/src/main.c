#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"

// --- PIN DEL LED EN EL C3 SUPERMINI ---
#define LED_AZUL GPIO_NUM_8 

// --- LA ESTRUCTURA DEBE SER IDÉNTICA AL EMISOR ---
typedef struct {
    uint32_t id_proyecto;
    float temperatura;
    float peso;
    uint8_t estado_peltier;
} DatosCava;

DatosCava datos_recibidos;
TaskHandle_t xTareaMain = NULL;

// ==============================================================================
// CALLBACK DE RECEPCIÓN ESP-NOW
// ==============================================================================
void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len == sizeof(DatosCava)) {
        memcpy(&datos_recibidos, data, sizeof(datos_recibidos));
        
        // Filtro de seguridad: Solo mostramos datos de tu emisor (0xCABA)
        if (datos_recibidos.id_proyecto == 0xCABA) {
            printf("\n--- TELEMETRÍA CAVA DE SANGRE (EN RUTA) ---\n");
            
            if (datos_recibidos.temperatura == -999.0) {
                printf("Temperatura: [ERROR DE LECTURA]\n");
            } else {
                printf("Temperatura: %.1f C\n", datos_recibidos.temperatura);
            }
            
            printf("Peso en vivo (fluctuando): %.2f g\n", datos_recibidos.peso);
            printf("Módulo SP1848: %s\n", datos_recibidos.estado_peltier ? "ENCENDIDO" : "APAGADO");
            printf("-------------------------------------------\n");
            
            // Avisar al bucle principal para que parpadee el LED
            if(xTareaMain) xTaskNotifyGive(xTareaMain);
        }
    }
}

// ==============================================================================
// BUCLE PRINCIPAL
// ==============================================================================
void app_main(void) {
    xTareaMain = xTaskGetCurrentTaskHandle();

    // 1. Configurar LED (Lógica invertida típica en el C3: 1=OFF, 0=ON)
    gpio_reset_pin(LED_AZUL);
    gpio_set_direction(LED_AZUL, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_AZUL, 1); 

    // 2. Inicializar WIFI & ESP-NOW
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    esp_now_init();
    esp_now_register_recv_cb(recv_cb);

    printf("\n==================================================\n");
    printf("  RECEPTOR C3 INICIADO - ESPERANDO DATOS... \n");
    printf("==================================================\n");

    // 3. Bucle infinito (Manejo del LED)
    while (1) {
        // Se queda dormido hasta que recv_cb() recibe un paquete válido
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        // Parpadeo del LED azul
        gpio_set_level(LED_AZUL, 0); // Encender
        vTaskDelay(pdMS_TO_TICKS(100)); // Esperar 100ms
        gpio_set_level(LED_AZUL, 1); // Apagar
    }
}