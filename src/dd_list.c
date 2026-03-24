//
// Created by victo on 21/03/2026.
//
#include "dd_list.h"
#include <stdio.h>
#include <stdlib.h>


void add_to_list(dd_task_list **head, dd_task *new_task)
{
    // 1. Allocate new node
    dd_task_list *new_node = pvPortMalloc(sizeof(dd_task_list));
    if (new_node == NULL) return;

    new_node->task = *new_task;
    new_node->next_task = NULL;

    // 2. Insert at head if list is empty OR earlier deadline
    if (*head == NULL ||
        (*head)->task.absolute_deadline > new_task->absolute_deadline)
    {
        new_node->next_task = *head;
        *head = new_node;
        return;
    }

    // 3. Traverse to find correct insertion point
    dd_task_list *current = *head;

    while (current->next_task != NULL &&
           current->next_task->task.absolute_deadline < new_task->absolute_deadline)
    {
        current = current->next_task;
    }
    // 4. Insert node
    new_node->next_task = current->next_task;
    current->next_task = new_node;
}

dd_task_list* remove_node(dd_task_list **head, uint32_t task_id) {
    if (*head == NULL) {
        return NULL;
    }
    dd_task_list *current = *head;
    if (current->task.task_id == task_id) {
        *head = current->next_task;
        current->next_task = NULL;
        return current;
    }
    while (current->next_task != NULL &&
              current->next_task->task.task_id != task_id){
        current = current->next_task;
    }
    if (current->next_task == NULL)
        return NULL;

    // Case 3: delete the node
    dd_task_list *removed = current->next_task;
    current->next_task = removed->next_task;
    removed->next_task = NULL;
    return removed;
}

void print_list(dd_task_list *head)
{
    printf("---- Task List ----\n");

    dd_task_list *current = head;

    while (current != NULL) {
        printf("Task ID: %u | Deadline: %u | Release: %u | Completion: %u\n",
               current->task.task_id,
               current->task.absolute_deadline,
               current->task.release_time,
               current->task.completion_time);

        current = current->next_task;
    }
    printf("-------------------\n");
}

void move_to_list(dd_task_list **origin_list, dd_task_list **destination_list, uint32_t task_id) {
    dd_task_list* node = remove_node(origin_list, task_id);
    if (node == NULL) {
        return;
    }
    add_to_list(destination_list, &node->task);
    vPortFree(node);
}

int find(dd_task_list *head, uint32_t task_id){
	 dd_task_list *current = head;
	    while (current != NULL) {
	    	if(current->task->task_id == task_id){
	    		return 1;
	    	}
	        current = current->next_task;
	    }
	    return 0;
}
