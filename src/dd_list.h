//
// Created by victo on 21/03/2026.
//

#ifndef DD_LIST_H
#define DD_LIST_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../FreeRTOS_Source/include/FreeRTOS.h"
#include "../FreeRTOS_Source/include/queue.h"
#include "../FreeRTOS_Source/include/semphr.h"
#include "../FreeRTOS_Source/include/task.h"
#include "../FreeRTOS_Source/include/timers.h"

// -----------------------------
// Structs from your project
// -----------------------------
typedef enum { PERIODIC, APERIODIC } task_type; //WHERE DOES IT SAY WE NEED THIS????

typedef struct dd_task {
    TaskHandle_t t_handle;
    task_type type;
    uint32_t task_id;
    uint32_t release_time;
    uint32_t absolute_deadline;
    uint32_t completion_time;
} dd_task;

typedef struct dd_task_list {
    dd_task task;
    struct dd_task_list *next_task;
} dd_task_list;

// -----------------------------
// Linked List API
// -----------------------------
void add_to_list(dd_task_list **head,dd_task *new_task);
dd_task_list* remove_node(dd_task_list **head, uint32_t task_id);
void print_list(dd_task_list *head);
void sort_list(dd_task_list *head);
void move_to_list(dd_task_list **origin_list, dd_task_list **destination_list, uint32_t task_id);

#endif