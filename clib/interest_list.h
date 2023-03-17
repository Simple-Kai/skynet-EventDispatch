#ifndef INTEREST_LIST_H
#define INTEREST_LIST_H

#include <stdint.h>
#include <string.h>

#include "skynet_malloc.h"
#include "skynet.h"

struct interest_list{
    uint32_t *arr; 
    uint32_t size;      
    uint32_t count;
};

struct interest_list* create_interest_list(uint32_t size);
void destroy_interest_list(struct interest_list *list);
void add_interest_list(struct interest_list *list, uint32_t handle);
uint32_t find_interest_list(struct interest_list *list, uint32_t handle);


#endif