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
const uint TRIGGER_PIN = 12;

QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;
SemaphoreHandle_t xSemaphoreTrigger;

int64_t trigger_callback(alarm_id_t id, void *user_data) {
    gpio_put(TRIGGER_PIN, 0);
    return false;
}

void pin_callback(uint gpio, uint32_t events) {
    int64_t timer;
    if (gpio == ECHO_PIN) {
        if (events & GPIO_IRQ_EDGE_RISE) {
            timer = to_us_since_boot(get_absolute_time());
            xQueueSendFromISR(xQueueTime, &timer, 0);
        } else if (events & GPIO_IRQ_EDGE_FALL) {
            timer = to_us_since_boot(get_absolute_time());
            xQueueSendFromISR(xQueueTime, &timer, 0);
        }
    }
}

void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    while (1) {
        gpio_put(TRIGGER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_put(TRIGGER_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        xSemaphoreGive(xSemaphoreTrigger);
    }
}

void echo_task(void *p) {
    int distance;
    int64_t start, stop;
    while (1) {
        if (xQueueReceive(xQueueTime, &start, portMAX_DELAY) == pdPASS) {
            if (xQueueReceive(xQueueTime, &stop, portMAX_DELAY) == pdPASS) {
                if (stop > start) {
                    uint32_t pulse_width = stop - start;
                    distance = (int)((pulse_width * 0.0343) / 2.0);
                    printf("Pulse time: %dus, Distance: %dcm\n", stop - start, distance);
                    xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
                }
            }
        }
    }
}

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
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &pin_callback);
}

void oled_task(void *p) {
    ssd1306_init();
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);
    oled1_btn_led_init();
    int distance = 0;
    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (xQueueReceive(xQueueDistance, &distance, pdMS_TO_TICKS(100)) == pdPASS) {
                if (distance > 300) {
                    gfx_clear_buffer(&disp);
                    gfx_draw_string(&disp, 0, 0, 1, "Erro no sensor");
                    gfx_show(&disp);
                } else if (distance > 2) {
                    gfx_clear_buffer(&disp);
                    gfx_draw_string(&disp, 0, 0, 1, "Distancia:");
                    char str[16];
                    sprintf(str, "%d cm", distance);
                    gfx_draw_string(&disp, 0, 10, 1, str);
                    int bar_width = (distance * 128) / 400;
                    if (bar_width > 128)
                        bar_width = 128;
                    if (bar_width < 0)
                        bar_width = 0;
                    for (int y = 20; y < 30; y++) {
                        for (int x = 0; x < bar_width; x++) {
                            gfx_draw_pixel(&disp, x, y);
                        }
                    }
                    gfx_show(&disp);
                }
                
            } else {
                gfx_clear_buffer(&disp);
                gfx_draw_string(&disp, 0, 0, 1, "Sem leitura");
                gfx_show(&disp);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int main() {
    stdio_init_all();
    xQueueTime = xQueueCreate(10, sizeof(int64_t));
    xQueueDistance = xQueueCreate(10, sizeof(int));
    xSemaphoreTrigger = xSemaphoreCreateBinary();
    xTaskCreate(trigger_task, "Trigger", 1024, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 1024, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED", 1024, NULL, 1, NULL);
    vTaskStartScheduler();
    while (1);
    return 0;
}
