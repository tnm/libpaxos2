#ifndef VALUES_HANDLER_H_23R78MJT
#define VALUES_HANDLER_H_23R78MJT

typedef struct vh_value_wrapper_t {
    char * value;
    size_t value_size;
    struct vh_value_wrapper_t * next;
    // int client_socket;
} vh_value_wrapper;

int vh_init();
void vh_shutdown();

void vh_enqueue_value(char * value, size_t value_size);
void vh_push_back_value(vh_value_wrapper * vw);
vh_value_wrapper * vh_get_next_pending();
int vh_pending_list_size();
void vh_notify_client(unsigned int result, vh_value_wrapper * vw);

#endif /* end of include guard: VALUES_HANDLER_H_23R78MJT */
