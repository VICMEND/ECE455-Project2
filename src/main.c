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
QueueHandle_t xGen_Queue;
QueueHandle_t xUser_Queue;
QueueHandle_t xactive_Queue;
QueueHandle_t xcomplete_Queue;
QueueHandle_t xoverdue_Queue;

static void dd_scheduler(void *pvParameters);
static void monitor_task(void *pvParameters);
static void dd_task_generator1(void *pvParameters);
static void dd_task_generator2(void *pvParameters);
static void dd_task_generator3(void *pvParameters);
static void user_defined_task(void *pvParameters);

TaskHandle_t xTask1, xTask2, xTask3;

TimerHandle_t T1Timer;
TimerHandle_t T2Timer;
TimerHandle_t T3Timer;

void ReleaseCallback1(TimerHandle_t xTimer);
void ReleaseCallback2(TimerHandle_t xTimer);
void ReleaseCallback3(TimerHandle_t xTimer);

static void assign_task_priorities(dd_task_list *head);


void release_dd_task(TaskHandle_t t_handle, task_type type, uint32_t task_id, uint32_t release, uint32_t deadline);
void complete_dd_task(uint32_t task_id);

dd_task_list *get_active_dd_task_list(void);
dd_task_list *get_complete_dd_task_list(void);
dd_task_list *get_overdue_dd_task_list(void);


/*-----------------------------------------------------------*/

int main(void)
{
    xDDS_Queue = xQueueCreate(10, sizeof(dds_msg));
    xGen_Queue = xQueueCreate(10, sizeof(uint32_t));
    xactive_Queue = xQueueCreate(1, sizeof(dd_task_list*));
    xcomplete_Queue = xQueueCreate(1, sizeof(dd_task_list*));
    xoverdue_Queue = xQueueCreate(1, sizeof(dd_task_list*));

    T1Timer = xTimerCreate("T1_Timer", pdMS_TO_TICKS(T1_period), pdTRUE, (void*)1, ReleaseCallback1);
    T2Timer = xTimerCreate("T2_Timer", pdMS_TO_TICKS(T2_period), pdTRUE, (void*)2, ReleaseCallback2);
    T3Timer = xTimerCreate("T3_Timer", pdMS_TO_TICKS(T3_period), pdTRUE, (void*)3, ReleaseCallback3);

//    xTaskCreate(user_defined_task, "T1", 128, (void*)1, 0, &xTask1);
//    xTaskCreate(user_defined_task, "T2", 128, (void*)2, 0, &xTask2);
//    xTaskCreate(user_defined_task, "T3", 128, (void*)3, 0, &xTask3);

    xTaskCreate(dd_scheduler, "DDS", configMINIMAL_STACK_SIZE, NULL, dds_PRIORITY, NULL);
    xTaskCreate(dd_task_generator1, "Generator1", configMINIMAL_STACK_SIZE, NULL, generator_PRIORITY, xTask1);
    xTaskCreate(dd_task_generator2, "Generator2", configMINIMAL_STACK_SIZE, NULL, generator_PRIORITY, xTask2);
    xTaskCreate(dd_task_generator3, "Generator3", configMINIMAL_STACK_SIZE, NULL, generator_PRIORITY, xTask3);
    xTaskCreate(monitor_task, "Monitor", 512 , NULL, monitor_PRIORITY, NULL);

    vTaskStartScheduler();
   for(;;);
}


/* --- Core Scheduler Logic --- */
static void dd_scheduler(void *pvParameters) {

    xTimerStart(T1Timer, 0);
    xTimerStart(T2Timer, 0);
    xTimerStart(T3Timer, 0);

    dd_task_list* active_list_head = NULL;
    dd_task_list* completed_list_head = NULL;
    dd_task_list* overdue_list_head = NULL;
    dds_msg rcvd_msg;

    for(;;) {
        while(xQueueReceive(xDDS_Queue, &rcvd_msg, portMAX_DELAY)) {
            uint32_t current_time = xTaskGetTickCount();

            switch (rcvd_msg.msg_type)
            {
                case RELEASE:
                    if(find(active_list_head, rcvd_msg.task_info.task_id)){
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
                    xQueueSend(xcomplete_Queue, &completed_list_head, 0);
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
//static void dd_task_generator(void *pvParameters) {
//	uint32_t now = xTaskGetTickCount();
//	int id;
//	xQueueReceive(xGen_Queue, &id, portMAX_DELAY);
//	switch(id){
//		case 1:
//
//			xTaskCreate(user_defined_task, "T1", 128, (void*)1, 0, &xTask1);
//
//			vTaskSuspend(xTask1);
//
//			release_dd_task(xTask1, PERIODIC, 1, now, T1_period);
//
//			break;
//
//		case 2:
//
//			xTaskCreate(user_defined_task, "T2", 128, (void*)2, 0, &xTask2);
//
//			vTaskSuspend(xTask2);
//
//			release_dd_task(xTask2, PERIODIC, 2, now, T2_period);
//
//			break;
//
//		case 3:
//
//			xTaskCreate(user_defined_task, "T3", 128, (void*)3, 0, &xTask3);
//
//			vTaskSuspend(xTask3);
//
//			release_dd_task(xTask3, PERIODIC, 3, now, T3_period);
//
//			break;
//
//	}
//}

static void dd_task_generator1(void *pvParameters) {
	uint32_t now = xTaskGetTickCount();

	xTaskCreate(user_defined_task, "T1", 128, (void*)1, 0, &xTask1);

	vTaskSuspend(xTask1);

	release_dd_task(xTask1, PERIODIC, 1, now, T1_period);

	vTaskSuspend( NULL );

}

static void dd_task_generator2(void *pvParameters) {
	uint32_t now = xTaskGetTickCount();

	xTaskCreate(user_defined_task, "T2", 128, (void*)2, 0, &xTask2);

	vTaskSuspend(xTask2);

	release_dd_task(xTask2, PERIODIC, 2, now, T2_period);

	vTaskSuspend( NULL );

}

static void dd_task_generator3(void *pvParameters) {
	uint32_t now = xTaskGetTickCount();

	xTaskCreate(user_defined_task, "T3", 128, (void*)3, 0, &xTask3);

	vTaskSuspend(xTask3);

	release_dd_task(xTask3, PERIODIC, 3, now, T3_period);

	vTaskSuspend( NULL );

}

void ReleaseCallback1(TimerHandle_t xTimer) {
	int id = 1;
	xQueueSend(xGen_Queue, &id, 0);
    vTaskResume(dd_task_generator1);
}

void ReleaseCallback2(TimerHandle_t xTimer) {
	int id = 2;
	xQueueSend(xGen_Queue, &id, 0);
	vTaskResume(dd_task_generator2);
}

void ReleaseCallback3(TimerHandle_t xTimer) {
	int id = 3;
	xQueueSend(xGen_Queue, &id, 0);
	vTaskResume(dd_task_generator3);
}

static void user_defined_task(void *pvParameters) {
	uint32_t my_id = (uint32_t)pvParameters;

    uint32_t execution_time_ms;

    if (my_id == 1) execution_time_ms = T1_exTime;
	else if (my_id == 2) execution_time_ms = T2_exTime;
	else  execution_time_ms = T3_exTime;

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
//        printf("Active Tasks: %u\n", (unsigned int)active_count);
//        printf("Completed Tasks: %u\n", (unsigned int)complete_count);
//        printf("Overdue Tasks: %u\n", (unsigned int)overdue_count);
    }
}

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

void vApplicationMallocFailedHook( void )
{
	/* The malloc failed hook is enabled by setting
	configUSE_MALLOC_FAILED_HOOK to 1 in FreeRTOSConfig.h.

	Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( xTaskHandle pxTask, signed char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configconfigCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected.  pxCurrentTCB can be
	inspected in the debugger if the task name passed into this function is
	corrupt. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
volatile size_t xFreeStackSpace;

	/* The idle task hook is enabled by setting configUSE_IDLE_HOOK to 1 in
	FreeRTOSConfig.h.

	This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amount of FreeRTOS heap that
	remains unallocated. */
	xFreeStackSpace = xPortGetFreeHeapSize();

	if( xFreeStackSpace > 100 )
	{
		/* By now, the kernel has allocated everything it is going to, so
		if there is a lot of heap remaining unallocated then
		the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
		reduced accordingly. */
	}
}
