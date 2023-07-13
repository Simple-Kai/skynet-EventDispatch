#include "interest_list.h"

struct interest_list* 
create_interest_list(uint32_t size) {
    struct interest_list *list = skynet_malloc(sizeof(struct interest_list));
    memset(list,0,sizeof(struct interest_list));
    list->arr = skynet_malloc(size * sizeof(uint32_t));
    for (int i = 0; i < size; i++) {
        list->arr[i] = 0;
    }
    list->size = size;
    list->count = 0;
    return list;
}

void destroy_interest_list(struct interest_list *list) {
    skynet_free(list->arr); 
    skynet_free(list);    
}

void insert_interest_list(struct interest_list *list, uint32_t handle) {
    if (list->count == 0) {
        list->arr[0] = handle;
    }else{
        int i;
        if (list->count == list->size) {
            uint32_t *new_arr = skynet_malloc((list->size * 2) * sizeof(uint32_t));
            for (i=0;i<list->size * 2;i++) {
                if (i < list->size)
                    new_arr[i] = list->arr[i];
                else
                    new_arr[i] = 0;
            }
            skynet_free(list->arr);
            list->arr = new_arr;
            list->size *= 2;
        }
        for (i = list->count - 1; i >= 0; i--) {
            if (list->arr[i] > handle) {
                list->arr[i+1] = list->arr[i];
            } else {
                break;
            }
        }
        list->arr[i+1] = handle;   
    }
    ++list->count;
}

uint32_t find_interest_list(struct interest_list *list, uint32_t handle) {
    int left = 0, right = list->count - 1, mid;
    while (left <= right) {
        mid = (left + right) / 2;
        if (list->arr[mid] == handle) {
            return list->arr[mid];
        } else if (list->arr[mid] < handle) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return 0;
}


