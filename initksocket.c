#include "ksocket.h"

pthread_t receiver_thread_id, sender_thread_id;

void *receiver_thread(void *arg);
void *sender_thread(void *arg);
void garbage_collector(int signum);

int main() {
    init_shared_memory();
    pthread_create(&receiver_thread_id, NULL, receiver_thread, NULL);
    pthread_create(&sender_thread_id, NULL, sender_thread, NULL);
    signal(SIGTERM, garbage_collector);
    while (1) {
        sleep(1); 
    }
    return 0;
}