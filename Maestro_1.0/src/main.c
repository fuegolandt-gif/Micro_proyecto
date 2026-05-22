//Este codigo es para DHT11

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "rom/ets_sys.h"

// --- DEFINICIÓN DE PINES ---
#define DHT_PIN         GPIO_NUM_27  // Sensor de Temperatura
#define HX711_DOUT_PIN  GPIO_NUM_21  // Sensor de Peso (Datos)
#define HX711_SCK_PIN   GPIO_NUM_22  // Sensor de Peso (Reloj)
#define PELTIER_PIN     GPIO_NUM_33  // Módulo MOSFET (Peltier y Ventiladores)

// --- VARIABLES GLOBALES PARA EL PESO ---
long tara_offset = 0;
float factor_escala = -216.0; // Recuerda calibrar este valor con una regla de tres

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

    // Esperar a que el pin de datos baje
    while (!hx711_is_ready()) {
        vTaskDelay(1); 
        timeout_ticks--;
        if (timeout_ticks <= 0) {
            return 0; // Evita que el ESP32 se congele por el Task Watchdog
        }
    }

    // Leer los 24 bits
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

    // Pulso 25 para configurar la ganancia en 128
    gpio_set_level(HX711_SCK_PIN, 1);
    esp_rom_delay_us(1);
    gpio_set_level(HX711_SCK_PIN, 0);
    esp_rom_delay_us(1);

    // Convertir datos en complemento a 2
    if (count & 0x800000) {
        count |= 0xFF000000;
    }
    return count;
}

// ==============================================================================
// FUNCIÓN DEL SENSOR DE TEMPERATURA (DHT22) - CORREGIDA PARA WOKWI
// ==============================================================================
float leer_temperatura_dht22() {
    uint8_t datos[5] = {0, 0, 0, 0, 0};
    
    // 1. Señal de inicio (Host pulsa en BAJO por 2ms)
    gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_PIN, 0);
    esp_rom_delay_us(2000); // 2 milisegundos exactos sin pausar FreeRTOS
    
    // 2. Host suelta la línea y espera
    gpio_set_level(DHT_PIN, 1);
    esp_rom_delay_us(30); 
    
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(DHT_PIN); // VITAL: Activar resistencia interna Pull-Up

    int timeout = 0;
    
    // 3. Esperar respuesta del sensor (LOW, luego HIGH)
    // Aumentamos el timeout a 1000 para compensar el simulador
    while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 1000) return -999.0; esp_rom_delay_us(1); }
    timeout = 0;
    while(gpio_get_level(DHT_PIN) == 0) { if(++timeout > 1000) return -999.0; esp_rom_delay_us(1); }
    timeout = 0;
    while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 1000) return -999.0; esp_rom_delay_us(1); }

    // 4. Leer los 40 bits
    for(int i = 0; i < 40; i++) {
        timeout = 0;
        // Espera a que termine el estado bajo
        while(gpio_get_level(DHT_PIN) == 0) { if(++timeout > 1000) return -999.0; esp_rom_delay_us(1); }
        
        uint32_t tiempo_inicio = esp_timer_get_time();
        
        timeout = 0;
        // Mide cuánto dura el estado alto
        while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 1000) return -999.0; esp_rom_delay_us(1); }
        
        // Si dura más de 40 microsegundos, es un '1' lógico
        if((esp_timer_get_time() - tiempo_inicio) > 40) {
            datos[i/8] |= (1 << (7 - (i%8)));
        }
    }

    // 5. Verificar Checksum y convertir
    uint8_t checksum = datos[0] + datos[1] + datos[2] + datos[3];
    
    if(datos[4] == checksum) {
        uint16_t temp_raw = (datos[2] << 8) | datos[3];
        float temperatura_precisa = (temp_raw & 0x7FFF) / 10.0;
        
        if (datos[2] & 0x80) {
            temperatura_precisa = -temperatura_precisa;
        }
        
        return temperatura_precisa; 
    }
    
    return -999.0; // Error de Checksum
}
// ==============================================================================
// BUCLE PRINCIPAL DEL SISTEMA (APP_MAIN)
// ==============================================================================
void app_main(void) {
    // 1. Inicialización de periféricos
    hx711_init();
    gpio_reset_pin(PELTIER_PIN);
    gpio_set_direction(PELTIER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PELTIER_PIN, 0); // Asegurar que el MOSFET empiece APAGADO

    printf("\n--- INICIANDO SISTEMA DE CAVA DE SANGRE ---\n");

    // 2. Calibración Inicial (Tare) de la balanza
    printf("Calculando Tare. Por favor, mantenga la cava vacia...\n");
    long suma = 0;
    for(int i = 0; i < 10; i++){
        suma += hx711_read();
        vTaskDelay(100 / portTICK_PERIOD_MS); 
    }
    tara_offset = suma / 10;
    printf("Tare completado. Offset: %ld\n", tara_offset);
    printf("Puede ingresar las pintas de sangre.\n\n");

    // 3. Bucle infinito de control (cada 10 segundos)
    while (1) {
        // --- A. Lectura de Sensores ---
        float temp = leer_temperatura_dht22(); // <- Cambio aquí a DHT22
        long valor_bruto = hx711_read();
        float peso_gramos = (valor_bruto - tara_offset) / factor_escala;

        // --- B. Lógica de Control de Temperatura (On/Off) ---
        if (temp != -999.0) { 
            if (temp > 12.0) {
                gpio_set_level(PELTIER_PIN, 1); // Enciende MOSFET
                printf("Temp: %.1f C -> [ALERTA] Umbral superado (>12.0C). Enfriamiento ENCENDIDO\n", temp);
            } else if (temp <= 5.0) {
                gpio_set_level(PELTIER_PIN, 0); // Apaga MOSFET
                printf("Temp: %.1f C -> Temperatura ideal alcanzada (<=5.0C). Enfriamiento APAGADO\n", temp);
            } else {
                // Zona muerta (entre 5.1C y 12.0C): mantiene el estado actual
                printf("Temp: %.1f C -> Dentro del rango seguro. Manteniendo estado actual...\n", temp);
            }
        } else {
            printf("Error leyendo el sensor de temperatura.\n");
            // Apagado preventivo si falla el sensor
            gpio_set_level(PELTIER_PIN, 0); 
        }

        // --- C. Impresión de Peso ---
        printf("Peso actual: %.2f g\n", peso_gramos);
        printf("--------------------------------------------------\n");

        // --- D. Esperar antes del siguiente ciclo ---
        vTaskDelay(2500 / portTICK_PERIOD_MS); 
    }
}