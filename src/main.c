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
#define dds_PRIORITY          (configMAX_PRIORITIES - 1) //WHY NOT JUST 1, 2, 3, 4, 5
#define monitor_PRIORITY      (configMAX_PRIORITIES - 2)
#define generator_PRIORITY    (configMAX_PRIORITIES - 3)
#define user_task_LOW         (1) 
#define user_task_HIGH        (configMAX_PRIORITIES - 4)

#define RELEASE 1
#define COMPLETE 2
#define GET_ACTIVE_LIST 3
#define GET_COMPLETE_LIST 4
#define GET_OVERDUE_LIST 5


// typedef enum { PERIODIC, APERIODIC } task_type; //WHERE DOES IT SAY WE NEED THIS????
//
// typedef struct dd_task {
//     TaskHandle_t t_handle;
//     task_type type;
//     uint32_t task_id;
//     uint32_t release_time;
//     uint32_t absolute_deadline;
//     uint32_t completion_time;
// } dd_task;
//
// //there is a list.h that has pre-built list functionality we should use that instead because it apprently also has sorting. Ill try to implement it
// typedef struct dd_task_list {
//     dd_task task;
//     struct dd_task_list *next_task;
//     // ListItem_t list_item;
// } dd_task_list;

typedef struct {
    uint8_t msg_type; // 1: Release, 2: Complete, 3: Get Active list
    dd_task task_info;
} dds_msg;

QueueHandle_t xDDS_Queue;
QueueHandle_t xactive_Queue;
QueueHandle_t xcomplete_Queue;
QueueHandle_t xoverdue_Queue;

// void add_to_list(dd_task_list * list_head,  dd_task * new_dd_task);
// void delete_node(dd_task_list * list_head,  dd_task * done_dd_task);
// void print_list (dd_task_list* dd_task_list_head);
// void sort_list(dd_task_list* dd_task_list_head);

static void dd_scheduler(void *pvParameters);
static void monitor_task(void *pvParameters);
static void dd_task_generator(void *pvParameters);
static void user_defined_task(void *pvParameters);
static void task1(void *pvParameters);
static void task2(void *pvParameters);
static void task3(void *pvParameters);
static void assign_task_priorities(dd_task_list *head);


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
                    rcvd_msg.task_info.release_time = current_time;
                    if(find(active_list_head,&rcvd_msg.task_info.task_id)){
                    	move_to_list(&active_list_head, &overdue_list_head, rcvd_msg.task_info.task_id);
                    }
                    add_to_list(&active_list_head, &rcvd_msg.task_info);
                    break;

                case COMPLETE:
                    rcvd_msg.task_info.completion_time = current_time;
                    move_to_list(&active_list_head, &completed_list_head, rcvd_msg.task_info.task_id);
                    break;

                case GET_ACTIVE_LIST:
                    // xQueueSend(xactive_Queue, &active_list_head, 0);
                    break;

                case GET_COMPLETE_LIST:
                    // xQueueSend(xcomplete_Queue, &complete_list_head, 0);
                    break;

                case GET_OVERDUE_LIST:
                    // xQueueSend(xoverdue_Queue, &overdue_list_head, 0);
                    break;

                default:
                    // Unknown message type
                    break;
            }


            // --- EDF PRIORITY SWAP ---
            assign_task_priorities(active_list_head);

            // if (active_list_head != NULL) {
            //     // Earliest deadline gets HIGH priority
            //     vTaskPrioritySet(active_list_head->task.t_handle, user_task_HIGH);
            //
            //     // All other active tasks get LOW priority
            //     dd_task_list *temp = active_list_head->next_task;
            //     while(temp != NULL) {
            //         vTaskPrioritySet(temp->task.t_handle, user_task_LOW);
            //         temp = temp->next_task;
            //     }
            // }
        }
    }
}

/* --- Interface Implementations --- */
void release_dd_task(TaskHandle_t t_handle, task_type type, uint32_t task_id, uint32_t deadline) {
    dds_msg msg;
    msg.msg_type = RELEASE;
    msg.task_info.t_handle = t_handle;
    msg.task_info.type = type;
    msg.task_info.task_id = task_id;
    msg.task_info.release_time = xTaskGetTickCount();
    msg.task_info.absolute_deadline = deadline + msg.task_info.release_time;

    xQueueSend(xDDS_Queue, &msg, 0);
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
    // Handles for the three worker tasks
    TaskHandle_t xTask1, xTask2, xTask3;
    
    // Create the tasks once; they start suspended or at priority 0 [cite: 665]
    xTaskCreate(user_defined_task, "T1", 128, (void*)1, 0, &xTask1);
    xTaskCreate(user_defined_task, "T2", 128, (void*)2, 0, &xTask2);
    xTaskCreate(user_defined_task, "T3", 128, (void*)3, 0, &xTask3);

    for(;;) {
        uint32_t now = xTaskGetTickCount();
        
        // Example: Release Task 1 every 500ms [cite: 690]
        if (now % 500 == 0) {
            release_dd_task(xTask1, PERIODIC, 1, now + 500); 
        }
        // Add logic for Task 2 (500ms) and Task 3 (750ms) here [cite: 690]
        
        vTaskDelay(1); // Check every tick
    }
}

static void user_defined_task(void *pvParameters) {
    uint32_t my_id = (uint32_t)pvParameters;
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
//
// void add_to_list(dd_task_list * list_head,  dd_task * new_dd_task) {
//
// }
// void delete_node(dd_task_list * list_head,  dd_task * done_dd_task) {
//
// }
// void print_list (dd_task_list* dd_task_list_head) {
//
// }
// void sort_list(dd_task_list* dd_task_list_head) {

// }
void assign_task_priorities(dd_task_list *head){
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
