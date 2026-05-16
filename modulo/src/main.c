#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// Asegúrate de poner el número del pin donde conectaste el PWM del módulo
#define PELTIER_PIN GPIO_NUM_33

void app_main(void) {
    // Configuramos el pin como salida
    gpio_reset_pin(PELTIER_PIN);
    gpio_set_direction(PELTIER_PIN, GPIO_MODE_OUTPUT);

    printf("Iniciando prueba de potencia del modulo MOSFET...\n");
    
    // 1. MANDAR SEÑAL DE ON (Dejar pasar los 12V)
    gpio_set_level(PELTIER_PIN, 1);
    printf("¡Corriente fluyendo! Celda y ventiladores ENCENDIDOS.\n");
    
    // 2. Mantener el paso de corriente durante 10 segundos exactos para que verifiques
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    
    // 3. MANDAR SEÑAL DE OFF (Cortar los 12V por seguridad)
    gpio_set_level(PELTIER_PIN, 0);
    printf("Prueba finalizada. Corriente cortada de forma segura.\n");

    // Bucle infinito vacío para que el ESP32 se quede tranquilo y no se reinicie
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS); 
    }
}