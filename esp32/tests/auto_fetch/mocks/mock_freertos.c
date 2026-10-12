// Host mock implementations for FreeRTOS task/semphr APIs.
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

BaseType_t mock_task_create_result = pdPASS;
int mock_task_create_count = 0;
char mock_last_task_name[64] = {0};

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_depth,
                       void *arg, UBaseType_t prio, TaskHandle_t *handle) {
    (void)fn; (void)stack_depth; (void)arg; (void)prio; (void)handle;
    mock_task_create_count++;
    if (name) {
        strncpy(mock_last_task_name, name, sizeof(mock_last_task_name) - 1);
        mock_last_task_name[sizeof(mock_last_task_name) - 1] = '\0';
    } else {
        mock_last_task_name[0] = '\0';
    }
    return mock_task_create_result;
}

void vTaskDelay(TickType_t ticks) { (void)ticks; }

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
    (void)task;
    return 4096;
}

void mock_task_reset(void) {
    mock_task_create_result = pdPASS;
    mock_task_create_count = 0;
    mock_last_task_name[0] = '\0';
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (SemaphoreHandle_t)0x1;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t t) {
    (void)m; (void)t;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t m) {
    (void)m;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t m) { (void)m; }
