#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "rom/ets_sys.h"

// --- LIBRERÍA PARA EL SENSOR TÁCTIL ---
#include "driver/touch_pad.h"

// --- LIBRERÍAS PARA ESP-NOW Y WIFI ---
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_now.h"

// --- DEFINICIÓN DE PINES ---
#define DHT_PIN         GPIO_NUM_27  
#define HX711_DOUT_PIN  GPIO_NUM_21  
#define HX711_SCK_PIN   GPIO_NUM_22  
#define PELTIER_PIN     GPIO_NUM_33  
#define TOUCH_PIN       TOUCH_PAD_NUM0 // Corresponde al GPIO 4 físico

// --- VARIABLES GLOBALES PARA EL PESO ---
long tara_offset = 0;
float factor_escala = -216.0; 

typedef struct {
    uint32_t id_proyecto;
    float temperatura;
    float peso;
    uint8_t estado_peltier;
} DatosCava;

DatosCava datos_enviar;

uint8_t mac_receptor[] = {0x44, 0x1B, 0xF6, 0x2E, 0xB0, 0xFC};

// ==============================================================================
// FUNCIONES DEL SENSOR DE PESO (HX711)
// ==============================================================================
void hx711_init() {
    gpio_set_direction(HX711_SCK_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(HX711_DOUT_PIN, GPIO_MODE_INPUT);
    gpio_set_level(HX711_SCK_PIN, 0);
}

uint8_t hx711_is_ready() {
    return gpio_get_level(HX711_DOUT_PIN) == 0;
}

long hx711_read() {
    long count = 0;
    int timeout_ticks = 100; 

    while (!hx711_is_ready()) {
        vTaskDelay(1); 
        timeout_ticks--;
        if (timeout_ticks <= 0) {
            return 0; 
        }
    }

    for (int i = 0; i < 24; i++) {
        gpio_set_level(HX711_SCK_PIN, 1);
        esp_rom_delay_us(1); 
        count = count << 1;
        gpio_set_level(HX711_SCK_PIN, 0);
        esp_rom_delay_us(1);
        if (gpio_get_level(HX711_DOUT_PIN)) {
            count++;
        }
    }

    gpio_set_level(HX711_SCK_PIN, 1);
    esp_rom_delay_us(1);
    gpio_set_level(HX711_SCK_PIN, 0);
    esp_rom_delay_us(1);

    if (count & 0x800000) {
        count |= 0xFF000000;
    }
    return count;
}

// ==============================================================================
// FUNCIÓN DEL SENSOR DE TEMPERATURA 
// ==============================================================================
float leer_temperatura_dht11() {
    uint8_t datos[5] = {0, 0, 0, 0, 0};
    
    gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_PIN, 0);
    vTaskDelay(20 / portTICK_PERIOD_MS); 
    gpio_set_level(DHT_PIN, 1);
    ets_delay_us(30); 
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);

    int timeout = 0;
    while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 100) return -999.0; ets_delay_us(1); }
    timeout = 0;
    while(gpio_get_level(DHT_PIN) == 0) { if(++timeout > 100) return -999.0; ets_delay_us(1); }
    timeout = 0;
    while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 100) return -999.0; ets_delay_us(1); }

    for(int i = 0; i < 40; i++) {
        timeout = 0;
        while(gpio_get_level(DHT_PIN) == 0) { if(++timeout > 100) return -999.0; ets_delay_us(1); }
        
        uint32_t tiempo_inicio = esp_timer_get_time();
        
        timeout = 0;
        while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 100) return -999.0; ets_delay_us(1); }
        
        if((esp_timer_get_time() - tiempo_inicio) > 40) {
            datos[i/8] |= (1 << (7 - (i%8)));
        }
    }

    if(datos[4] == (datos[0] + datos[1] + datos[2] + datos[3])) {
        float temperatura_precisa = datos[2] + (datos[3] / 10.0);
        return temperatura_precisa; 
    }
    return -999.0; 
}

// ==============================================================================
// INICIALIZACIÓN DE WIFI Y ESP-NOW
// ==============================================================================
void init_esp_now_unicast() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac_receptor, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        printf("Error añadiendo el peer receptor\n");
    }
}

// ==============================================================================
// BUCLE PRINCIPAL DEL SISTEMA (APP_MAIN)
// ==============================================================================
void app_main(void) {
    // 1. Inicialización básica
    hx711_init();
    gpio_reset_pin(PELTIER_PIN);
    gpio_set_direction(PELTIER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PELTIER_PIN, 0); 
    
    init_esp_now_unicast();

    // --- CONFIGURACIÓN DEL SENSOR TOUCH ---
    touch_pad_init();
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    touch_pad_config(TOUCH_PIN, 0);

    printf("\n--- INICIANDO SISTEMA DE CAVA DE SANGRE (TX) ---\n");

    // 2. Calibración Inicial (Tare)
    printf("Calculando Tare. Por favor, mantenga la cava vacia...\n");
    long suma = 0;
    for(int i = 0; i < 10; i++){
        suma += hx711_read();
        vTaskDelay(100 / portTICK_PERIOD_MS); 
    }
    tara_offset = suma / 10;
    printf("Tare completado. Offset: %ld\n", tara_offset);
    printf("Puede ingresar las pintas de sangre.\n\n");

    // 3. ESTADO DE ESPERA (STANDBY)
    printf("==================================================\n");
    printf(" SISTEMA EN ESPERA. TOQUE EL PIN GPIO 4 PARA INICIAR\n");
    printf("==================================================\n");

    uint16_t valor_touch;
    while (1) {
        touch_pad_read(TOUCH_PIN, &valor_touch);
        
        // Un valor menor a 500 indica que un dedo está tocando el pin
        if (valor_touch < 500) {
            printf("\n>>> TOQUE DETECTADO (Valor: %d). INICIANDO LECTURAS <<<\n\n", valor_touch);
            break; // Rompe el bucle de espera y avanza al sistema principal
        }
        
        // Pequeña pausa para no saturar el procesador
        vTaskDelay(100 / portTICK_PERIOD_MS); 
    }

    // Variables para el filtro anti-errores del Wi-Fi
    float ultima_temp_valida = 0.0;
    bool primera_lectura_exitosa = false;

    // 4. ESTADO DE OPERACIÓN (BUCLE PRINCIPAL)
    while (1) {
        float temp = leer_temperatura_dht11();
        long valor_bruto = hx711_read();
        float peso_gramos = (valor_bruto - tara_offset) / factor_escala;
        uint8_t estado_actual_peltier = 0;

        if (temp != -999.0) { 
            ultima_temp_valida = temp; 
            primera_lectura_exitosa = true;
        } else if (primera_lectura_exitosa) {
            temp = ultima_temp_valida; 
        }

        if (primera_lectura_exitosa) { 
            if (temp > 7.0) {
                gpio_set_level(PELTIER_PIN, 1); 
                estado_actual_peltier = 1;
                printf("Temp: %.1f C -> [ALERTA] Umbral superado (>7.0C). Modulo SP1848-27145 ENCENDIDO\n", temp);
            } else if (temp <= 5.0) {
                gpio_set_level(PELTIER_PIN, 0); 
                estado_actual_peltier = 0;
                printf("Temp: %.1f C -> Temperatura ideal alcanzada (<=5.0C). Modulo SP1848-27145 APAGADO\n", temp);
            } else {
                estado_actual_peltier = gpio_get_level(PELTIER_PIN);
                printf("Temp: %.1f C -> Dentro del rango seguro. Manteniendo estado actual...\n", temp);
            }
        } else {
            printf("Esperando primera lectura estable del sensor de temperatura...\n");
            gpio_set_level(PELTIER_PIN, 0); 
        }

        printf("Peso actual: %.2f g\n", peso_gramos);

        datos_enviar.id_proyecto = 0xCABA;
        datos_enviar.temperatura = temp;
        datos_enviar.peso = peso_gramos;
        datos_enviar.estado_peltier = estado_actual_peltier;

        esp_err_t resultado_envio = esp_now_send(mac_receptor, (uint8_t *) &datos_enviar, sizeof(datos_enviar));
        
        if (resultado_envio == ESP_OK) {
            printf("--> Datos enviados correctamente a %02X:%02X:%02X:%02X:%02X:%02X\n", 
                   mac_receptor[0], mac_receptor[1], mac_receptor[2], 
                   mac_receptor[3], mac_receptor[4], mac_receptor[5]);
        } else {
            printf("--> Error al enviar datos.\n");
        }
        printf("--------------------------------------------------\n");

        vTaskDelay(2500 / portTICK_PERIOD_MS); 
    }
}