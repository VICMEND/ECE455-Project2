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



/*-----------------------------------------------------------*/
#define dds_PRIORITY          (configMAX_PRIORITIES - 1) //WHY NOT JUST 1, 2, 3, 4, 5
#define monitor_PRIORITY      (configMAX_PRIORITIES - 2)
#define generator_PRIORITY    (configMAX_PRIORITIES - 3)
#define user_task_LOW         (1) 
#define user_task_HIGH        (configMAX_PRIORITIES - 4)

typedef enum { PERIODIC, APERIODIC } task_type; //WHERE DOES IT SAY WE NEED THIS????

typedef struct dd_task {
    TaskHandle_t t_handle;
    task_type type;
    uint16_t task_id;
    uint16_t release_time;
    uint16_t absolute_deadline;
    uint16_t completion_time;
} dd_task;

typedef struct dd_task_list {
    dd_task task;
    struct dd_task_list *next_task;
} dd_task_list;

typedef struct {
    uint8_t msg_type; // 1: Release, 2: Complete, 3: Get Lists
    dd_task task_info;
} dds_msg;

QueueHandle_t xDDS_Queue;
dd_task_list *active_list_head = NULL;
dd_task_list *completed_list_head = NULL;
dd_task_list *overdue_list_head = NULL;

static void dds_task(void *pvParameters);
static void monitor_task(void *pvParameters);
static void dd_task_generator(void *pvParameters);
static void user_defined_task(void *pvParameters);

void release_dd_task(TaskHandle_t t_handle, task_type type, uint16_t task_id, uint16_t deadline);
void complete_dd_task(uint16_t task_id);


/*-----------------------------------------------------------*/

int main(void)
{
	/* 1. Create the DDS Queue for inter-task communication [cite: 613, 725] */
    xDDS_Queue = xQueueCreate(10, sizeof(dds_msg));

    /* 2. Create the Manager Task (Highest Priority) [cite: 582] */
    xTaskCreate(dds_task, "DDS", configMINIMAL_STACK_SIZE, NULL, dds_PRIORITY, NULL);

    /* 3. Create Auxiliary Tasks [cite: 518] */
    xTaskCreate(dd_task_generator, "Gen", configMINIMAL_STACK_SIZE, NULL, generator_PRIORITY, NULL);
    xTaskCreate(monitor_task, "Monitor", configMINIMAL_STACK_SIZE, NULL, monitor_PRIORITY, NULL);

    vTaskStartScheduler();
    for(;;);
}


/* --- Core Scheduler Logic --- */
static void dds_task(void *pvParameters) {
    dds_msg rcvd_msg;
    for(;;) {
        if(xQueueReceive(xDDS_Queue, &rcvd_msg, portMAX_DELAY)) {
            uint16_t current_time = xTaskGetTickCount();

            if(rcvd_msg.msg_type == 1) { // RELEASE
                rcvd_msg.task_info.release_time = current_time;
                add_to_active_list(&active_list_head, rcvd_msg.task_info);
            } 
            else if(rcvd_msg.msg_type == 2) { // COMPLETE
                // 1. Find in Active List, move to Completed
                // 2. Remove from Active List (Logic omitted for brevity)
                rcvd_msg.task_info.completion_time = current_time;
                move_to_list(&completed_list_head, rcvd_msg.task_info);
            }

            // --- EDF PRIORITY SWAP ---
            if (active_list_head != NULL) {
                // Earliest deadline gets HIGH priority
                vTaskPrioritySet(active_list_head->task.t_handle, user_task_HIGH);
                
                // All other active tasks get LOW priority
                dd_task_list *temp = active_list_head->next_task;
                while(temp != NULL) {
                    vTaskPrioritySet(temp->task.t_handle, user_task_LOW);
                    temp = temp->next_task;
                }
            }
        }
    }
}

/* --- Interface Implementations --- */
void release_dd_task(TaskHandle_t t_handle, task_type type, uint16_t task_id, uint16_t deadline) {
    dds_msg msg;
    msg.msg_type = 1;
    msg.task_info.t_handle = t_handle;
    msg.task_info.type = type;
    msg.task_info.task_id = task_id;
    msg.task_info.absolute_deadline = deadline;
    msg.task_info.release_time = xTaskGetTickCount();
    
    xQueueSend(xDDS_Queue, &msg, 0);
}

void complete_dd_task(uint16_t task_id) {
    dds_msg msg;
    msg.msg_type = 2;
    msg.task_info.task_id = task_id;
    
    xQueueSend(xDDS_Queue, &msg, 0);
}

/* --- Auxiliary Tasks --- */
static void dd_task_generator(void *pvParameters) {
    // Handles for the three worker tasks
    TaskHandle_t xTask1, xTask2, xTask3;
    
    // Create the tasks once; they start suspended or at priority 0 [cite: 665]
    xTaskCreate(user_defined_task, "T1", 128, (void*)1, 0, &xTask1);
    xTaskCreate(user_defined_task, "T2", 128, (void*)2, 0, &xTask2);
    xTaskCreate(user_defined_task, "T3", 128, (void*)3, 0, &xTask3);

    for(;;) {
        uint16_t now = xTaskGetTickCount();
        
        // Example: Release Task 1 every 500ms [cite: 690]
        if (now % 500 == 0) {
            release_dd_task(xTask1, PERIODIC, 1, now + 500); 
        }
        // Add logic for Task 2 (500ms) and Task 3 (750ms) here [cite: 690]
        
        vTaskDelay(1); // Check every tick
    }
}

static void user_defined_task(void *pvParameters) {
    uint16_t my_id = (uint16_t)pvParameters;
    for(;;) {
        // 1. Wait for a signal to start (e.g., a semaphore)
        // 2. Do "work" (empty loop) [cite: 636]
        // 3. Call complete_dd_task [cite: 638]
        complete_dd_task(my_id);
        vTaskSuspend(NULL); // Wait for next release
    }
}

static void monitor_task(void *pvParameters) {
    for(;;) {
        // Request lists from DDS and print counts to console [cite: 670, 675]
        vTaskDelay(pdMS_TO_TICKS(1500)); // Report every hyper-period [cite: 687]
    }
}

