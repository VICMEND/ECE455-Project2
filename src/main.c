/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include "stm32f4_discovery.h"
/* Kernel includes. */
#include "stm32f4xx.h"
#include "../FreeRTOS_Source/include/FreeRTOS.h"
#include "../FreeRTOS_Source/include/queue.h"
#include "../FreeRTOS_Source/include/semphr.h"
#include "../FreeRTOS_Source/include/task.h"
#include "../FreeRTOS_Source/include/timers.h"
#include "dd_list.h"


/*-----------------------------------------------------------*/
#define dds_PRIORITY          (configMAX_PRIORITIES - 1) 
#define monitor_PRIORITY      (configMAX_PRIORITIES - 2)
#define generator_PRIORITY    (configMAX_PRIORITIES - 3)
#define user_task_LOW         (1) 
#define user_task_HIGH        (configMAX_PRIORITIES - 4)

#define RELEASE 1
#define COMPLETE 2
#define GET_ACTIVE_LIST 3
#define GET_COMPLETE_LIST 4
#define GET_OVERDUE_LIST 5

/* **************************************************** */

#define T1_period 500
#define T2_period 500
#define T3_period 750

#define T1_exTime 95
#define T2_exTime 150
#define T3_exTime 250

/* **************************************************** */

// #define T1_period 250
// #define T2_period 500
// #define T3_period 750

// #define T1_exTime 95
// #define T2_exTime 150
// #define T3_exTime 250

/* **************************************************** */

// #define T1_period 500
// #define T2_period 500
// #define T3_period 500

// #define T1_exTime 100
// #define T2_exTime 200
// #define T3_exTime 200

/* **************************************************** */

typedef struct {
    uint8_t msg_type; // Release, Complete, Get Active list
    dd_task task_info;
} dds_msg;

QueueHandle_t xDDS_Queue;
QueueHandle_t xactive_Queue;
QueueHandle_t xcomplete_Queue;
QueueHandle_t xoverdue_Queue;

static void dd_scheduler(void *pvParameters);
static void monitor_task(void *pvParameters);
static void dd_task_generator(void *pvParameters);
static void user_defined_task(void *pvParameters);
static void task1(void *pvParameters);
static void task2(void *pvParameters);
static void task3(void *pvParameters);
static void assign_task_priorities(dd_task_list *head);

TimerHandle_t T1Timer;
TimerHandle_t T2Timer;
TimerHandle_t T3Timer;  

void release_dd_task(TaskHandle_t t_handle, task_type type, uint32_t task_id, uint32_t deadline);
void complete_dd_task(uint32_t task_id);
dd_task_list *get_active_dd_task_list(void);
dd_task_list *get_complete_dd_task_list(void);
dd_task_list *get_overdue_dd_task_list(void);


/*-----------------------------------------------------------*/

int main(void)
{
	/* 1. Create the DDS Queue for inter-task communication [cite: 613, 725] */
    xDDS_Queue = xQueueCreate(10, sizeof(dds_msg));
    xactive_Queue = xQueueCreate(1, sizeof(dd_task_list*));
    xcomplete_Queue = xQueueCreate(1, sizeof(dd_task_list*));
    xoverdue_Queue = xQueueCreate(1, sizeof(dd_task_list*));

    T1Timer = xTimerCreate("T1_Timer", pdMS_TO_TICKS(T1_period), pdTRUE, (void*)1, ReleaseCallback1);
    T2Timer = xTimerCreate("T2_Timer", pdMS_TO_TICKS(T2_period), pdTRUE, (void*)2, ReleaseCallback2);
    T3Timer = xTimerCreate("T3_Timer", pdMS_TO_TICKS(T3_period), pdTRUE, (void*)3, ReleaseCallback3);

    /* 2. Create the Manager Task (Highest Priority) [cite: 582] */
    xTaskCreate(dd_scheduler, "DDS", configMINIMAL_STACK_SIZE, NULL, dds_PRIORITY, NULL);

    /* 3. Create Auxiliary Tasks [cite: 518] */
    xTaskCreate(dd_task_generator, "Gen", configMINIMAL_STACK_SIZE, NULL, generator_PRIORITY, NULL);
    xTaskCreate(monitor_task, "Monitor", configMINIMAL_STACK_SIZE, NULL, monitor_PRIORITY, NULL);

    vTaskStartScheduler();
    for(;;);
}


/* --- Core Scheduler Logic --- */
static void dd_scheduler(void *pvParameters) {

    dd_task_list* active_list_head = NULL;
    dd_task_list* completed_list_head = NULL;
    dd_task_list* overdue_list_head = NULL;
    dds_msg rcvd_msg;

    for(;;) {
        if(xQueueReceive(xDDS_Queue, &rcvd_msg, portMAX_DELAY)) {
            uint32_t current_time = xTaskGetTickCount();

            switch (rcvd_msg.msg_type)
            {
                case RELEASE:
                    if(find(active_list_head,&rcvd_msg.task_info.task_id)){
                    	move_to_list(&active_list_head, &overdue_list_head, rcvd_msg.task_info.task_id);
                    }
                    add_to_list(&active_list_head, &rcvd_msg.task_info);
                    vTaskResume(rcvd_msg.task_info.t_handle);
                    break;

                case COMPLETE:
                    rcvd_msg.task_info.completion_time = current_time;
                    move_to_list(&active_list_head, &completed_list_head, rcvd_msg.task_info.task_id);
                    break;

                case GET_ACTIVE_LIST:
                     xQueueSend(xactive_Queue, &active_list_head, 0);
                    break;

                case GET_COMPLETE_LIST:
                     xQueueSend(xcomplete_Queue, &complete_list_head, 0);
                    break;

                case GET_OVERDUE_LIST:
                     xQueueSend(xoverdue_Queue, &overdue_list_head, 0);
                    break;

                default:
                    // Unknown message type
                    break;
            }


            // --- EDF PRIORITY SWAP ---
            assign_task_priorities(active_list_head);
        }
    }
}

/* --- Interface Implementations --- */
void release_dd_task(TaskHandle_t t_handle, task_type type, uint32_t task_id, uint32_t release, uint32_t deadline) {
    dds_msg msg;
    msg.msg_type = RELEASE;
    msg.task_info.t_handle = t_handle;
    msg.task_info.type = type;
    msg.task_info.task_id = task_id;
    msg.task_info.release_time = release;
    msg.task_info.absolute_deadline = release + deadline;

    xQueueSend(xDDS_Queue, &msg, 0);
}

void ReleaseCallback1(TimerHandle_t xTimer) {
    uint32_t now = xTaskGetTickCount();
    release_dd_task(xTask1, PERIODIC, 1, now, T1_period);
}
void ReleaseCallback2(TimerHandle_t xTimer) {
    uint32_t now = xTaskGetTickCount();
    release_dd_task(xTask2, PERIODIC, 1, now, T1_period);
}
void ReleaseCallback3(TimerHandle_t xTimer) {
    uint32_t now = xTaskGetTickCount();
    release_dd_task(xTask3, PERIODIC, 1, now, T1_period);
}

void complete_dd_task(uint32_t task_id) {
    dds_msg msg;
    msg.msg_type = COMPLETE;
    msg.task_info.task_id = task_id;
    
    xQueueSend(xDDS_Queue, &msg, 0);
}

dd_task_list* get_active_dd_task_list(void) {
    dds_msg msg;
    msg.msg_type = GET_ACTIVE_LIST;
    dd_task_list* active_list_head = NULL;
    xQueueSend(xDDS_Queue, &msg, 0);
    xQueueReceive(xactive_Queue, &active_list_head, portMAX_DELAY);
    return active_list_head;
}

dd_task_list* get_complete_dd_task_list(void) {
    dds_msg msg;
    msg.msg_type = GET_COMPLETE_LIST;
    dd_task_list* complete_list_head = NULL;
    xQueueSend(xDDS_Queue, &msg, 0);
    xQueueReceive(xcomplete_Queue, &complete_list_head, portMAX_DELAY);
    return complete_list_head;
}

dd_task_list* get_overdue_dd_task_list(void) {
    dds_msg msg;
    msg.msg_type = GET_OVERDUE_LIST;
    dd_task_list* overdue_list_head = NULL;
    xQueueSend(xDDS_Queue, &msg, 0);
    xQueueReceive(xoverdue_Queue, &overdue_list_head, portMAX_DELAY);
    return overdue_list_head;
}

/* --- Auxiliary Tasks --- */
static void dd_task_generator(void *pvParameters) {

    TaskHandle_t xTask1, xTask2, xTask3;

    xTaskCreate(user_defined_task, "T1", 128, (void*)1, 0, &xTask1);
    xTaskCreate(user_defined_task, "T2", 128, (void*)2, 0, &xTask2);
    xTaskCreate(user_defined_task, "T3", 128, (void*)3, 0, &xTask3);

    for(;;) {
        uint32_t now = xTaskGetTickCount();
        
        if (now % T1_period == 0) {
            xTimerStart()
        }

        if (now % T2_period == 0) {
            release_dd_task(xTask2, PERIODIC, 2, now, T2_period); 
        }

        if (now % T3_period == 0) {
            release_dd_task(xTask3, PERIODIC, 3, now, T3_period); 
        }
        
        vTaskDelay(1); // Check every tick
    }
}

static void user_defined_task(void *pvParameters) {
    uint32_t my_id = (uint32_t)pvParameters;

    uint32_t execution_time_ms;

    // Determine execution time based on task ID from Test Bench #1 
    if (my_id == 1) execution_time_ms = T1_exTime;
    else if (my_id == 2) execution_time_ms = T2_exTime;
    else if (my_id == 3) execution_time_ms = T3_exTime;
    else execution_time_ms = 50; // Default

    for(;;) {

        uint32_t start_tick = xTaskGetTickCount();
        while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(execution_time_ms)) {
            // Busy wait to simulate execution [cite: 636]
        }

        complete_dd_task(my_id);
        
        vTaskSuspend(NULL); // Wait for next release
    }
}

static void monitor_task(void *pvParameters) {
    for(;;) {
        // The Monitor Task must execute even if there are active tasks
        // Report every hyper-period (1500ms for Test Bench #1)
        vTaskDelay(pdMS_TO_TICKS(1500)); 

        // 1. Get the lists using interface functions
        dd_task_list *active = get_active_dd_task_list();
        dd_task_list *complete = get_complete_dd_task_list();
        dd_task_list *overdue = get_overdue_dd_task_list();

        // 2. Count the nodes in each list
        uint32_t active_count = 0, complete_count = 0, overdue_count = 0;
        
        dd_task_list *curr = active;
        while(curr != NULL) { active_count++; curr = curr->next_task; }
        
        curr = complete;
        while(curr != NULL) { complete_count++; curr = curr->next_task; }
        
        curr = overdue;
        while(curr != NULL) { overdue_count++; curr = curr->next_task; }

        // 3. Report system information to console
        printf("\n--- Monitor Report (%u ms) ---\n", (unsigned int)xTaskGetTickCount());
        printf("Active Tasks: %u\n", (unsigned int)active_count);
        printf("Completed Tasks: %u\n", (unsigned int)complete_count);
        printf("Overdue Tasks: %u\n", (unsigned int)overdue_count);
    }
}

void assign_task   _priorities(dd_task_list *head){
    dd_task_list *current = head;

    if (current == NULL) {
        return; // no tasks to schedule
    }

    // 1. First task in the list gets HIGH priority
    vTaskPrioritySet(current->task.t_handle, user_task_HIGH);

    // 2. All remaining tasks get LOW priority
    current = current->next_task;

    while (current != NULL) {
        vTaskPrioritySet(current->task.t_handle, user_task_LOW);
        current = current->next_task;
    }
}
