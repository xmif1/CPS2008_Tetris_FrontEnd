#include <stdio.h>
#include <pthread.h>
#include "client_server.h"

int main(){
    int socket_fd = client_init();

    if(socket_fd >= 0){
       int ret = send_msg("test", socket_fd);
       enqueue_msgs(socket_fd);
       char* msg = dequeue_msgs();

       printf("%s\n", msg);
    }

  return 0;
}
