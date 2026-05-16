#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

// Definición de pines
#define HX711_DOUT_PIN GPIO_NUM_21
#define HX711_SCK_PIN  GPIO_NUM_22

// Variables para la calibración
long tara_offset = 0;
float factor_escala = 1.0; 

// Configuración inicial de los pines
void hx711_init() {
    gpio_set_direction(HX711_SCK_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(HX711_DOUT_PIN, GPIO_MODE_INPUT);
    gpio_set_level(HX711_SCK_PIN, 0);
}

// Verifica si el HX711 tiene datos listos para enviar
uint8_t hx711_is_ready() {
    return gpio_get_level(HX711_DOUT_PIN) == 0;
}

// Función de bajo nivel para leer los 24 bits del sensor (Versión corregida Anti-Watchdog)
long hx711_read() {
    long count = 0;
    int timeout_ticks = 100; // Máximo de iteraciones esperando al HX711

    // Esperar a que el pin de datos baje (con timeout de seguridad)
    while (!hx711_is_ready()) {
        vTaskDelay(1); // Cede exactamente 1 tick, dejando que IDLE0 corra y resetee el WDT
        timeout_ticks--;
        
        if (timeout_ticks <= 0) {
            printf("Error: HX711 desconectado o no responde.\n");
            return 0; // Retorna 0 para evitar que el ESP32 se cuelgue
        }
    }

    // Leer los 24 bits desplazando el reloj
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

    // Enviar un pulso extra (el 25) para configurar la ganancia en 128 (Canal A)
    gpio_set_level(HX711_SCK_PIN, 1);
    esp_rom_delay_us(1);
    gpio_set_level(HX711_SCK_PIN, 0);
    esp_rom_delay_us(1);

    // El HX711 entrega los datos en complemento a 2. 
    // Convertimos esos 24 bits a un entero de 32 bits con signo.
    if (count & 0x800000) {
        count |= 0xFF000000;
    }

    return count;
}

void app_main() {
    hx711_init();
    printf("Iniciando lectura del HX711...\n");

    // 1. Calcular la Tara (Promediando 10 lecturas iniciales sin peso)
    long suma = 0;
    for(int i = 0; i < 10; i++){
        suma += hx711_read();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    tara_offset = suma / 10;
    printf("Tara completada. Valor base (offset): %ld\n", tara_offset);

    // 2. Factor de escala temporal (Deberás ajustarlo con la regla de tres)
    factor_escala = -216; // Ejemplo: si 1000 gramos dan una lectura de 978460, entonces 1 gramo = 978.46 unidades del HX711 

    // Bucle principal
    while (1) {
        long valor_bruto = hx711_read();
        
        // Calcular el peso final aplicando la tara y la escala
        float peso_gramos = (valor_bruto - tara_offset) / factor_escala;

        printf("Lectura Bruta: %ld \t Peso: %.2f g\n", valor_bruto, peso_gramos);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}