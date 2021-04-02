#include <stdio.h>
#include <pthread.h>
#include "curses.h"
#include "client_server.h"

// GLOBALS

WINDOW* live_chat;
WINDOW* chat_box;
int connection_open = 0;

pthread_t live_chat_thread;
pthread_t sent_chat_thread;

// FUNC DEFNS

void* get_chat_msgs(void* arg);
void* send_chat_msgs(void* arg);

int main(){
    int socket_fd = client_init();

    if(socket_fd >= 0){
        connection_open = 1;

        // setting up ncurses
        int max_y, max_x;
        initscr();

        // defining windows and their properties
        getmaxyx(stdscr, max_y, max_x);
        live_chat = newwin(max_y - 5, max_x, 0, 0);
        scrollok(live_chat, TRUE);
        chat_box = newwin(5, max_x, max_y - 5, 0);

        // printing titles and border
        mvwprintw(live_chat, 0, 0, "Super Battle Tetris Chat Server\n");

        for(int i = 0; i < max_x; i++){
            mvwprintw(chat_box, 0, i, "=");
        }

        // updating
        wrefresh(live_chat);
        wrefresh(chat_box);

        if(pthread_create(&live_chat_thread, NULL, get_chat_msgs, (void*) NULL) != 0){
            perror("Error while creating thread to service incoming chat messages");
            return 1;
        }

        if(pthread_create(&sent_chat_thread, NULL, send_chat_msgs, (void*) &socket_fd) != 0){
            perror("Error while creating thread to service outgoing chat messages");
            return 1;
        }

        while(connection_open){
            enqueue_msg(socket_fd);
        }
    }
    else{
        perror("Cannot establish connection to game server");
        return 1;
    }

    return 0;
}

void* get_chat_msgs(void* arg){
    while(connection_open){
        msg recv_msg;
        recv_msg = dequeue_chat_msg();

        char to_print[MSG_SIZE+1];
        strcpy(to_print, "\n");
        strcat(to_print, recv_msg.msg);

        waddstr(live_chat, to_print);
        wrefresh(live_chat);
    }
}

void* send_chat_msgs(void* arg){
    int socket_fd = *((int*) arg);

    while(connection_open){
        msg to_send;
        to_send.msg_type = CHAT;
        wgetstr(chat_box, to_send.msg);

        if(send_msg(to_send, socket_fd) < 0){
            perror("Error communicating with game server");
            break;
        }

        wmove(chat_box, 1, 0);
        wclrtobot(chat_box);
        wrefresh(chat_box);
    }
}
