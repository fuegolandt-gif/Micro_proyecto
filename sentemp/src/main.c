#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

// Pin de datos del DHT11 cambiado al GPIO 27
#define DHT_PIN GPIO_NUM_27 

int leer_temperatura_dht11() {
    uint8_t datos[5] = {0, 0, 0, 0, 0};
    
    gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_PIN, 0);
    vTaskDelay(20 / portTICK_PERIOD_MS); 
    gpio_set_level(DHT_PIN, 1);
    ets_delay_us(30); 
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);

    int timeout = 0;
    while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 100) return -1; ets_delay_us(1); }
    timeout = 0;
    while(gpio_get_level(DHT_PIN) == 0) { if(++timeout > 100) return -1; ets_delay_us(1); }
    timeout = 0;
    while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 100) return -1; ets_delay_us(1); }

    for(int i = 0; i < 40; i++) {
        timeout = 0;
        while(gpio_get_level(DHT_PIN) == 0) { if(++timeout > 100) return -1; ets_delay_us(1); }
        
        uint32_t tiempo_inicio = esp_timer_get_time();
        
        timeout = 0;
        while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 100) return -1; ets_delay_us(1); }
        
        if((esp_timer_get_time() - tiempo_inicio) > 40) {
            datos[i/8] |= (1 << (7 - (i%8)));
        }
    }

    if(datos[4] == (datos[0] + datos[1] + datos[2] + datos[3])) {
        return datos[2]; 
    }
    
    return -1; 
}

void app_main(void) {
    while (1) {
        int temp = leer_temperatura_dht11();
        
        if (temp != -1) {
            printf("Temperatura: %d C\n", temp);
        } else {
            printf("Error leyendo el sensor.\n");
        }
        
        vTaskDelay(2000 / portTICK_PERIOD_MS); 
    }
}