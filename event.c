#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <lua.h>
#include <lauxlib.h>

#include "skynet.h"
#include "skynet_malloc.h"
#include "atomic.h"
#include "spinlock.h"
#include "interest_list.h"

uint32_t skynet_context_handle(struct skynet_context *);

#define EVENT_STORAGE_DEFAULT_NUM 10000 

struct event_message{
	struct event_message *next;
    void *data;
    size_t sz;
};

struct event_server_node{
	struct event_server_node *next;
	uint32_t handle;
	struct event_message *head;
	struct event_message *tail;
    ATOM_SIZET event_count;
};

struct event_storage{
	uint32_t slot_size;
	struct event_server_node **slot;
    struct interest_list *interest_list;
    struct spinlock lock;
    ATOM_ULONG ref;
};

static struct event_message* 
create_event_message(void * data, size_t sz) {
    struct event_message* em = skynet_malloc(sizeof(struct event_message));
    memset(em,0,sizeof(struct event_message));
    em->next = NULL;
    em->data = data;  // userdata(skynet.pack), no copy
    em->sz = sz;
    return em;
}

static void 
destroy_event_message(struct event_message* em) {
    if (em) {
        skynet_free(em->data);
        em->data = NULL;
        skynet_free(em);
    }
}

static struct event_server_node* 
create_event_server_node(uint32_t handle) {
    struct event_server_node* esn = skynet_malloc(sizeof(struct event_server_node));
    memset(esn,0,sizeof(struct event_server_node));
    esn->next = NULL;
    esn->handle = handle;
    esn->head = NULL;
    esn->tail = NULL;
    ATOM_INIT(&esn->event_count, 0);
    return esn;
}

static void 
destroy_event_server_node(struct event_server_node* esn) {
    if (esn) {
        struct event_message* em = esn->head;
        struct event_message* next = NULL;
        while (em) {
            next = em->next;
            destroy_event_message(em);
            em = next;
        }
        skynet_free(esn);
    }
}

static struct event_storage* 
create_event_storage(uint32_t slot_size) {
    struct event_storage* es = skynet_malloc(sizeof(struct event_storage));
    memset(es,0,sizeof(struct event_storage));
    es->slot_size = slot_size;
    es->slot = (struct event_server_node**)skynet_malloc(slot_size * sizeof(struct event_server_node*));
    for (int i = 0; i < es->slot_size; ++i) {
        es->slot[i] = NULL;
    }
    es->interest_list = create_interest_list(10);
    ATOM_INIT(&es->ref , 0);
    SPIN_INIT(es)
    return es;
}

static void 
destroy_event_storage(struct event_storage* es) {
    if (es) {
        for (int i = 0; i < es->slot_size; i++) {
            struct event_server_node* esn = es->slot[i];
            struct event_server_node* next = NULL;
            while (esn) {
                next = esn->next;
                destroy_event_server_node(esn);
                esn = next;
            }
        }
        SPIN_DESTROY(es)
        destroy_interest_list(es->interest_list);
        skynet_free(es->slot);
        skynet_free(es);
    }
}

static void
add_massage(struct event_server_node* esn, void *data, size_t sz){
    struct event_message* em = create_event_message(data, sz);
    if (!esn->head) {
        esn->head = esn->tail = em;
    } 
    else {
        esn->tail->next = em;
        esn->tail = em;
    }
    ATOM_FINC(&esn->event_count);
}

static struct event_server_node* 
get_event_server_node(struct event_storage* es, uint32_t event_type){
    uint32_t index = event_type - 1;
    return es->slot[index];
}

int add_event_listen(struct event_storage* es, uint32_t event_type, uint32_t handle, void *data, size_t sz) {
	if(event_type > es->slot_size){
        skynet_error(NULL, "add failed! event = %u event_type in [1-%u]", event_type, es->slot_size);
        return -1;
	}
    SPIN_LOCK(es)
    struct event_server_node* esn_head = get_event_server_node(es, event_type);
    struct event_server_node* esn = esn_head;
    while (esn && esn->handle != handle) {
        esn = esn->next;
    }

    if (esn == NULL) {
        esn = create_event_server_node(handle);
        esn->next = esn_head;
        es->slot[event_type - 1] = esn;
    }
    add_massage(esn, data, sz);
    SPIN_UNLOCK(es)
    return 0;
}

void del_event_listen(struct event_storage* es, uint32_t event_type, uint32_t handle, const void *data, size_t size){
    SPIN_LOCK(es)
    struct event_server_node* curr = get_event_server_node(es, event_type);
    while (curr && curr->handle != handle) {
        curr = curr->next;
    }
    if (curr) {
        struct event_message* em = curr->head;
        struct event_message* prev = NULL;
        while (em) {
            if (em->sz == size && memcmp(em->data, data, size) == 0){
                if (prev == NULL){
                    curr->head = em->next;
                } 
                else {
                    prev->next = em->next;
                }
                destroy_event_message(em);
                ATOM_FDEC(&curr->event_count);
                break;
            }
            prev = em;
            em = em->next;
        }
    }
    SPIN_UNLOCK(es)
}

static void 
delete_event_server_node(struct event_storage* es, uint32_t event_type, uint32_t handle) {
    struct event_server_node* esn_head = get_event_server_node(es, event_type);
    struct event_server_node* prev = NULL;
    struct event_server_node* curr = esn_head;
    while (curr) {
        if (curr->handle == handle) {
            if (prev == NULL) {
                es->slot[event_type - 1] = curr->next;
            }
            else {
                prev->next = curr->next;
            }
            destroy_event_server_node(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
}

void clear_event_server_node(struct event_storage* es, uint32_t handle){
    SPIN_LOCK(es)
    for (int i = 1; i <= es->slot_size; ++i) {
        delete_event_server_node(es, i, handle);
    }
    SPIN_UNLOCK(es)
}

static void 
merge_data(const void *front , size_t front_sz, const void *back, size_t back_sz, void ** p_buffer, size_t *p_buff_sz){
    size_t new_sz = front_sz + back_sz;
    *p_buffer = skynet_malloc(new_sz);
    memcpy(*p_buffer, front, front_sz);
    memcpy(*p_buffer + front_sz, back, back_sz);
    *p_buff_sz = new_sz;
}

static void 
dispatch_all_by_node(struct event_server_node* esn, struct skynet_context *context, uint32_t source, void *param, size_t param_sz){
    struct event_message* em = esn->head;
    void *buffer = NULL;
    size_t buff_sz;
    int type;
    while (em) {
        buffer = em->data;
        buff_sz = em->sz;
        type = PTYPE_RESERVED_LUA;
        if (param_sz != 0){
            merge_data(buffer, buff_sz, param, param_sz, &buffer, &buff_sz);
            type |= PTYPE_TAG_DONTCOPY;
        }
        skynet_send(context, source, esn->handle, type, 0, buffer, buff_sz);
        em = em->next;
    }
}

void dispatch_event(struct event_storage* es, uint32_t event_type, uint32_t source, struct skynet_context *ctx, void *param, size_t sz){
    SPIN_LOCK(es)
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
    SPIN_UNLOCK(es)
    if (param){
        skynet_free(param);
    }
}

void add_interest_list(struct event_storage* es, uint32_t handle) {
    SPIN_LOCK(es)
    insert_interest_list(es->interest_list, handle);
    SPIN_UNLOCK(es)
}

uint32_t get_event_sum(struct event_storage* es, uint32_t event_type){
    uint32_t sum = 0;
    struct event_server_node* node = NULL;
    if (event_type == 0){
        for (int i = 0; i < es->slot_size; ++i) {
            node = es->slot[i];
            while (node) {
                sum += ATOM_LOAD(&node->event_count);
                node = node->next;
            } 
        }
    }   
    else{
        node = get_event_server_node(es, event_type);
        while (node) {
            sum += ATOM_LOAD(&node->event_count);
            node = node->next;
        }   
    }
    return sum;
}


static struct event_storage *E = NULL; //single instance
static struct spinlock opening = {0};

static void
open_event_core(int n) {
    if (E == NULL) {
        spinlock_lock(&opening);
        if (E == NULL) {
            E = create_event_storage(n>0?n:EVENT_STORAGE_DEFAULT_NUM); //10000 ptr use memory 85kb
        }
        spinlock_unlock(&opening);
    }
    ATOM_FINC(&E->ref);
}

static int
close_event_core() {
    if (E != NULL && ATOM_FDEC(&E->ref) == 1) {
        destroy_event_storage(E);
        E = NULL;
        return 0;
    }
    return -1;
}


//------------lua接口-------------------
#define CHECK_EVENTCORE_DUMP(L, e) if (!e) {luaL_error(L, "add event error!");}
#define CONTEXT_HANDLE(L) skynet_context_handle((struct skynet_context *)lua_touserdata(L, lua_upvalueindex(1)))

static int levent_open(lua_State* L) {
    int n = lua_tointeger(L, 1);
    open_event_core(n);
    return 0;
}

static int levent_close(lua_State* L) {
    if (close_event_core()) {
        uint32_t handle = lua_tointeger(L, lua_upvalueindex(1));
        clear_event_server_node(E, handle);
    }
    return 0;
}

static int ladd_interest(lua_State *L) {
    CHECK_EVENTCORE_DUMP(L, E)
    uint32_t handle = CONTEXT_HANDLE(L);
    add_interest_list(E, handle);
    return 0;
}

static int ladd_event_listen(lua_State* L) {
    CHECK_EVENTCORE_DUMP(L, E)
    uint32_t event_type = luaL_checkinteger(L, 1);
    void * msg = lua_touserdata(L, 2);
    int sz = luaL_checkinteger(L, 3);
    uint32_t handle = CONTEXT_HANDLE(L);
    if (add_event_listen(E, event_type, handle, msg, sz)) {
        skynet_free(msg);
        luaL_error(L, "add event error!");
    }
    return 0;
}

static int ldel_event_listen(lua_State* L) {
    CHECK_EVENTCORE_DUMP(L, E)
    uint32_t event_type = luaL_checkinteger(L, 1);
    void * msg = lua_touserdata(L, 2);
    int sz = luaL_checkinteger(L, 3);
    uint32_t handle = CONTEXT_HANDLE(L);
    del_event_listen(E, event_type, handle, msg, sz);
    skynet_free(msg);
    return 0;
}

static int ldispatch(lua_State* L) {
    CHECK_EVENTCORE_DUMP(L, E)
    uint32_t event_type = luaL_checkinteger(L, 1);
    void *param = lua_touserdata(L, 2);
    size_t sz = luaL_checkinteger(L, 3);
    struct skynet_context *ctx = lua_touserdata(L, lua_upvalueindex(1));
    uint32_t handle = skynet_context_handle(ctx);
    dispatch_event(E, event_type, handle, ctx, param, sz);
    return 0;
}

static int lget_event_sum(lua_State* L) {
    CHECK_EVENTCORE_DUMP(L, E)
    uint32_t event_type = 0;
    if (lua_gettop(L) != 0){
        event_type = luaL_checkinteger(L, 1);
    }
    uint32_t sum = get_event_sum(E, event_type);
    lua_pushinteger(L, sum);
    return 1;
}



int luaopen_eventcore(lua_State* L) {
    luaL_checkversion(L);

    lua_pushcfunction(L, levent_open);
    lua_call(L, 0, 0);

    luaL_Reg l[] = {
        {"register", ladd_interest},
        {"add", ladd_event_listen},
        {"del", ldel_event_listen},
        {"dispatch", ldispatch},
        { NULL, NULL },
    };

    luaL_Reg l2[] = {
        {"sum", lget_event_sum},
        { NULL, NULL },
    };

    lua_createtable(L, 0, sizeof(l)/sizeof(l[0]) + sizeof(l2)/sizeof(l2[0]) -2);
    lua_getfield(L, LUA_REGISTRYINDEX, "skynet_context");
    struct skynet_context *ctx = lua_touserdata(L, -1);
    if (ctx == NULL) {
        return luaL_error(L, "Init skynet context first");
    }

    luaL_setfuncs(L,l,1);
    luaL_setfuncs(L,l2,0);

    lua_createtable(L, 0, 1);
    lua_pushstring(L, "__gc");
    lua_pushinteger(L, skynet_context_handle(ctx));
    lua_pushcclosure(L, levent_close, 1);
    lua_rawset(L, -3);
    lua_setmetatable(L, -2);

    return 1;
}
