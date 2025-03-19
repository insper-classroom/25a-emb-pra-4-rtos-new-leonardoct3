/*
 * LED blink with FreeRTOS - Versão Ajustada para Sincronização
 */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

const uint BTN_1_OLED = 28;
const uint BTN_2_OLED = 26;
const uint BTN_3_OLED = 27;

const uint LED_1_OLED = 20;
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

const uint ECHO_PIN = 13;
const uint TRIGGER_PIN = 12;  // Ajuste conforme o hardware

volatile int time_start = 0;
volatile int time_end = 0;

QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;

//
// Função de callback da interrupção para o pino de eco.
// Ela captura os instantes de subida e descida e, quando ambos
// estão disponíveis, calcula o tempo de pulso e envia para a fila.
//
void pin_callback(uint gpio, uint32_t events) {
    if (gpio == ECHO_PIN) {
        if (events & GPIO_IRQ_EDGE_RISE) {
            time_start = to_us_since_boot(get_absolute_time());
        } else if (events & GPIO_IRQ_EDGE_FALL) {
            time_end = to_us_since_boot(get_absolute_time());
        }
        if (time_start > 0 && time_end > 0) {
            int pulse_time = time_end - time_start;
            BaseType_t higherPriorityTaskWoken = pdFALSE;
            xQueueSendToBackFromISR(xQueueTime, &pulse_time, &higherPriorityTaskWoken);
            time_start = 0;
            time_end = 0;
            portYIELD_FROM_ISR(higherPriorityTaskWoken);
        }
    }
}

//
// Tarefa de trigger: envia um pulso para o sensor.
// Usa sleep_us para garantir um pulso de 10 µs.
// (A função sleep_us é bloqueante, mas como o delay é muito curto, não prejudica o sistema.)
//
void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);

    while (1) {
        gpio_put(TRIGGER_PIN, 1);
        sleep_us(10);  // Pulso de 10 µs
        gpio_put(TRIGGER_PIN, 0);
        // Aguarda 500 ms para o próximo disparo
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//
// Tarefa de eco: aguarda a medição de tempo de pulso (bloqueante),
// calcula a distância e envia o valor para a fila de distância.
//
void echo_task(void *p) {
    int pulse_time = 0;
    int distance;
    while (1) {
        // Bloqueia até receber um valor de tempo de pulso
        if (xQueueReceive(xQueueTime, &pulse_time, portMAX_DELAY) == pdPASS) {
            // Calcula a distância:
            // Fórmula: distância = (pulse_time * 0.0343) / 2
            distance = (int)((pulse_time * 0.0343f) / 2.0f);
            printf("Pulse time: %dus, Distance: %dcm\n", pulse_time, distance);
            // Envia a distância para a fila
            xQueueSendToBack(xQueueDistance, &distance, portMAX_DELAY);
        }
    }
}

//
// Inicializa pinos para botões e LEDs, e configura a interrupção do pino de eco.
//
void oled1_btn_led_init(void) {
    gpio_init(LED_1_OLED);
    gpio_set_dir(LED_1_OLED, GPIO_OUT);

    gpio_init(LED_2_OLED);
    gpio_set_dir(LED_2_OLED, GPIO_OUT);

    gpio_init(LED_3_OLED);
    gpio_set_dir(LED_3_OLED, GPIO_OUT);

    gpio_init(BTN_1_OLED);
    gpio_set_dir(BTN_1_OLED, GPIO_IN);
    gpio_pull_up(BTN_1_OLED);

    gpio_init(BTN_2_OLED);
    gpio_set_dir(BTN_2_OLED, GPIO_IN);
    gpio_pull_up(BTN_2_OLED);

    gpio_init(BTN_3_OLED);
    gpio_set_dir(BTN_3_OLED, GPIO_IN);
    gpio_pull_up(BTN_3_OLED);

    // Inicializa o pino do sensor de eco e configura a interrupção
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &pin_callback);
}

//
// Tarefa OLED: aguarda a chegada de uma medição de distância e a exibe.
// Se não houver nova medição em um período determinado, exibe "Sem leitura".
//
void oled_task(void *p) {
    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    printf("Inicializando botões e LEDs\n");
    oled1_btn_led_init();

    int distance = 0;

    while (1) {
        // Aguarda até 1000 ms por uma nova medição
        if (xQueueReceive(xQueueDistance, &distance, pdMS_TO_TICKS(1000)) == pdPASS) {
            if (distance > 300 || distance < 0) {
                gfx_clear_buffer(&disp);
                gfx_draw_string(&disp, 0, 0, 1, "Erro!");
                gfx_show(&disp);
            } else {
                gfx_clear_buffer(&disp);
                gfx_draw_string(&disp, 0, 0, 1, "Distancia:");
                char str[16];
                sprintf(str, "%d cm", distance);
                gfx_draw_string(&disp, 0, 10, 1, str);
                gfx_show(&disp);
            }
        } else {
            // Se timeout, exibe "Sem leitura"
            gfx_clear_buffer(&disp);
            gfx_draw_string(&disp, 0, 0, 1, "Erro!");
            gfx_show(&disp);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//
// Função principal: inicializa filas e tarefas.
//
int main() {
    stdio_init_all();
    printf("Start RTOS\n");

    // Cria as filas para transmitir os tempos e as distâncias
    xQueueTime = xQueueCreate(10, sizeof(int));
    xQueueDistance = xQueueCreate(10, sizeof(int));

    // Cria as tarefas
    xTaskCreate(trigger_task, "Trigger", 1024, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 1024, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED", 1024, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1)
        ;
    return 0;
}
