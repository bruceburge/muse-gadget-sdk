#pragma once
// Host mock of FreeRTOS task.h with test hooks.
#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *arg);

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_depth,
                       void *arg, UBaseType_t prio, TaskHandle_t *handle);
void vTaskDelay(TickType_t ticks);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);

// Test hooks (implemented in mock_freertos.c)
extern BaseType_t mock_task_create_result;   // what xTaskCreate returns
extern int mock_task_create_count;           // how many times called
extern char mock_last_task_name[64];         // name of most recent task
void mock_task_reset(void);
