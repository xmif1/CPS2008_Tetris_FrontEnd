#include <stdio.h>
#include <pthread.h>
#include "client_server.h"

void* server_listen(void* arg);

int main(){
    int socket_fd = client_init();

    pthread_t server_thread;
    if(socket_fd >= 0){
        if(pthread_create(&server_thread, NULL, server_listen, (void*) &socket_fd) != 0){
            return -1;
        }
        else{
            int ret = send_msg("test", socket_fd);
            char* msg;
            dequeue_msgs(msg);

            printf("%s\n", msg);
        }
    }

    return 0;
}

void* server_listen(void* arg){
    int socket_fd = *((int*) arg);
    enqueue_msgs(socket_fd);
}