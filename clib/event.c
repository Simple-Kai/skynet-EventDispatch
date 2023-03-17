#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <lua.h>
#include <lauxlib.h>

#include "skynet.h"
#include "skynet_malloc.h"
#include "interest_list.h"


#define EVENT_POOL_DEFAULT_NUM 200  
#define MAX_EVENT_NUM_NODE 1 << 7 
#define CHECK_INIT(e) assert(e != 0);


struct event_message{
	struct event_message *next;
    void *data;
    size_t sz;
};

struct event_server_node{
	struct event_server_node *next;
	uint32_t handle;  //服务fd
	struct event_message *head;
	struct event_message *tail;
    uint32_t event_count;
};

struct event_storage{
	uint32_t slot_size;
	struct event_server_node **slot;
    struct interest_list *interest_list;
};

static struct event_storage *E = NULL;

struct event_message* 
create_event_message(void * data, size_t sz) {
    struct event_message* em = skynet_malloc(sizeof(struct event_message));
    memset(em,0,sizeof(struct event_message));
    em->next = NULL;
    em->data = data;
    em->sz = sz;
    return em;
}

void destroy_event_message(struct event_message* em) {
    if (em) {
        skynet_free(em->data);
        em->data = NULL;
        skynet_free(em);
    }
}

struct event_server_node* 
create_event_server_node(uint32_t handle) {
    struct event_server_node* esn = skynet_malloc(sizeof(struct event_server_node));
    memset(esn,0,sizeof(struct event_server_node));
    esn->next = NULL;
    esn->handle = handle;
    esn->head = NULL;
    esn->tail = NULL;
    esn->event_count = 0;
    return esn;
}

void destroy_event_server_node(struct event_server_node* esn) {
    if (esn) {
        struct event_message* em = esn->head;
        while (em) {
            struct event_message* next = em->next;
            destroy_event_message(em);
            em = next;
        }
        skynet_free(esn);
    }
}

struct event_storage* 
create_event_storage(uint32_t slot_size) {
    struct event_storage* es = skynet_malloc(sizeof(struct event_storage));
    memset(es,0,sizeof(struct event_storage));
    es->slot_size = slot_size;
    es->slot = (struct event_server_node**)skynet_malloc(slot_size * sizeof(struct event_server_node*));
    for (int i = 0; i < es->slot_size; ++i) {
        es->slot[i] = NULL;
    }
    es->interest_list = create_interest_list(10);
    return es;
}

void destroy_event_storage(struct event_storage* es) {
    if (es) {
        for (int i = 0; i < es->slot_size; i++) {
            struct event_server_node* esn = es->slot[i];
            while (esn) {
                struct event_server_node* next = esn->next;
                destroy_event_server_node(esn);
                esn = next;
            }
        }
        destroy_interest_list(es->interest_list);
        skynet_free(es->slot);
        skynet_free(es);
    }
}

static void
add_massage(struct event_server_node* esn, void *data, size_t sz){
    if (esn->event_count == MAX_EVENT_NUM_NODE){
        skynet_error(NULL, "add massage failed! max add %d of same events!", MAX_EVENT_NUM_NODE);
        return;
    }
    struct event_message* em = create_event_message(data, sz);
    if (!esn->head) {
        esn->head = esn->tail = em;
    } else {
        esn->tail->next = em;
        esn->tail = em;
    }
    ++esn->event_count;
}

static struct event_server_node* 
get_event_server_node(struct event_storage* es, uint32_t event_type){
    uint32_t index = event_type - 1;
    return es->slot[index];
}

void add_event_listen(struct event_storage* es, uint32_t event_type, uint32_t handle, void *data, size_t sz) {
    CHECK_INIT(es)
	if(event_type > es->slot_size){
        skynet_error(NULL, "add failed! event = %u event_type in (1-200)", event_type);
        return;
	}

    struct event_server_node* esn_head = get_event_server_node(es, event_type);
    struct event_server_node* esn = esn_head;
    while (esn && esn->handle != handle) {
        esn = esn->next;
    }

    if (esn == NULL) {
        esn = create_event_server_node(handle);
        esn->next = esn_head;
        esn_head = esn;
        es->slot[event_type - 1] = esn_head;
    }
    add_massage(esn, data, sz);
}

void delete_event_server_node(struct event_storage* es, uint32_t event_type, uint32_t handle) {
    CHECK_INIT(es)
    struct event_server_node* esn_head = get_event_server_node(es, event_type);
    struct event_server_node* prev = NULL;
    struct event_server_node* curr = esn_head;
    while (curr) {
        if (curr->handle == handle) {
            if (prev == NULL) {
                es->slot[event_type - 1] = curr->next;
            } else {
                prev->next = curr->next;
            }
            destroy_event_server_node(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
}

void delete_event_server_node_all(struct event_storage* es, uint32_t handle){
    for (int i = 1; i <= es->slot_size; ++i) {
        delete_event_server_node(es, i, handle);
    }
}

static void 
merge_data(void *front , size_t front_sz, void *back, size_t back_sz, void ** p_buffer, size_t *p_buff_sz){
    size_t new_sz = front_sz + back_sz;
    *p_buffer = skynet_malloc(new_sz);
    memcpy(*p_buffer, front, front_sz);
    memcpy(*p_buffer + front_sz, back, back_sz);
    *p_buff_sz = new_sz;
}

static void 
dispatch_all_by_node(struct event_server_node* esn, struct skynet_context *context, uint32_t source, void *param, size_t param_sz){
    struct event_message* em = esn->head;
    while (em) {
        void *buffer = em->data;
        size_t buff_sz = em->sz;
        int type = PTYPE_RESERVED_LUA;
        if (param_sz != 0){
            merge_data(buffer, buff_sz, param, param_sz, &buffer, &buff_sz);
            type |= PTYPE_TAG_DONTCOPY;
        }
        skynet_send(context, source, esn->handle, type, 0, buffer, buff_sz);
        em = em->next;
    }
}

void dispatch_event(struct event_storage* es, uint32_t event_type, uint32_t source, struct skynet_context *ctx, void *param, size_t sz){
    CHECK_INIT(es)
    struct event_server_node* curr = get_event_server_node(es, event_type);
    int in_interest = 0;
    if (find_interest_list(es->interest_list, source))
        in_interest = 1;

    while (curr) {
        if (in_interest || curr->handle == source || find_interest_list(es->interest_list, curr->handle)) {
            dispatch_all_by_node(curr, ctx, source, param, sz);
        }
        curr = curr->next;
    }   

    if (param){
        skynet_free(param);
    }     
}

void register_interest_list(struct event_storage* es, uint32_t handle) {
    CHECK_INIT(es)
    add_interest_list(es->interest_list, handle);
}

uint32_t get_event_sum(struct event_storage* es, uint32_t event_type){
    uint32_t sum = 0;
    struct event_server_node* node = NULL;
    if (event_type == 0){
        for (int i = 0; i < es->slot_size; ++i) {
            node = es->slot[i];
            while (node) {
                sum += node->event_count;
                node = node->next;
            } 
        }
    }   
    else{
        node = get_event_server_node(es, event_type);
        while (node) {
            sum += node->event_count;
            node = node->next;
        }   
    }
    node = NULL;
    return sum;
}


void event_storage_init(uint32_t n){
	assert(E == NULL);
	E = create_event_storage(n>0?n:EVENT_POOL_DEFAULT_NUM);
}

//------------lua接口-------------------
static int levent_storage_init(lua_State* L) {
    uint32_t n = luaL_checkinteger(L, 1);
    event_storage_init(n);
    return 0;
}

static int lregister_interest_list(lua_State *L) {
    uint32_t handle = luaL_checkinteger(L, 1);
    register_interest_list(E, handle);
    return 0;
}

static int ladd_event_listen(lua_State* L) {
    uint32_t event_type = luaL_checkinteger(L, 1);
    uint32_t handle = luaL_checkinteger(L, 2);
    void * msg = lua_touserdata(L, 3);
    int sz = luaL_checkinteger(L, 4);
    add_event_listen(E, event_type, handle, msg, sz);
    return 0;
}

static int ldispatch_event(lua_State* L) {
    uint32_t event_type = luaL_checkinteger(L, 1);
    uint32_t handle = luaL_checkinteger(L, 2);
    void *param = lua_touserdata(L, 3);
    size_t sz = luaL_checkinteger(L, 4);
    struct skynet_context *ctx = lua_touserdata(L, lua_upvalueindex(1));
    dispatch_event(E, event_type, handle, ctx, param, sz);
    return 0;
}

static int lclear_event(lua_State* L) {
    uint32_t handle = luaL_checkinteger(L, 1);
    delete_event_server_node_all(E, handle);
    return 0;
}

static int lget_event_sum(lua_State* L) {
    uint32_t event_type = luaL_checkinteger(L, 1);
    uint32_t sum = get_event_sum(E, event_type);
    lua_pushinteger(L, sum);
    return 1;
}

int luaopen_event(lua_State* L) {
    luaL_checkversion(L);

    luaL_Reg l[] = {
        {"dispatch", ldispatch_event},
        { NULL, NULL },
    };

    luaL_Reg l2[] = {
        {"init", levent_storage_init},
        {"register", lregister_interest_list},
        {"add", ladd_event_listen},
        {"sum", lget_event_sum},
        {"clear", lclear_event},
        { NULL, NULL },
    };

    lua_createtable(L, 0, sizeof(l)/sizeof(l[0]) + sizeof(l2)/sizeof(l2[0]) -2);
    lua_getfield(L, LUA_REGISTRYINDEX, "skynet_context");
    struct skynet_context *ctx = lua_touserdata(L,-1);
    if (ctx == NULL) {
        return luaL_error(L, "Init skynet context first");
    }

    luaL_setfuncs(L,l,1);
    luaL_setfuncs(L,l2,0);
    return 1;
}
