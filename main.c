#include <stdio.h>
#include "client_server.h"

int main(){
    int socket_fd = client_init();
    int ret = send_msg("test", socket_fd);

    char* msg;
    dequeue(msg);
    printf("%s\n", msg);

    return 0;
}
