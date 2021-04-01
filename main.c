#include <stdio.h>
#include "client_server.h"

int main(){
    int socket_fd = client_init();
    send_msg("test", socket_fd);

    return 0;
}
