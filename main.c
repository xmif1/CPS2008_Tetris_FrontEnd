#include <stdio.h>
#include <pthread.h>
#include "client_server.h"

int main(){
    int socket_fd = client_init();

    if(socket_fd >= 0){
        msg sent_msg = {.msg = "test", .msg_type = CHAT};
        int ret = send_msg(sent_msg, socket_fd);
        enqueue_msg(socket_fd);
        msg recv_msg = dequeue_msg();

        printf("%s\n", recv_msg.msg);
    }

  return 0;
}
