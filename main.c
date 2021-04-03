#include <stdio.h>
#include <pthread.h>
#include "curses.h"
#include "client_server.h"

// GLOBALS

WINDOW* live_chat;
WINDOW* chat_box;
int connection_open = 0;
pthread_mutex_t threadMutex = PTHREAD_MUTEX_INITIALIZER;

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
        mvwprintw(live_chat, 0, 0, "Super Battle Tetris Chat Server\n\n");

        for(int i = 0; i < max_x; i++){
            mvwprintw(chat_box, 0, i, "=");
        }

        // updating
        wrefresh(live_chat);
        wrefresh(chat_box);

        // create threads for sending and receiving chat messages while connection is open
        if(pthread_create(&live_chat_thread, NULL, get_chat_msgs, (void*) NULL) != 0){
            endwin();
            mrerror("Error while creating thread to service incoming chat messages");
        }

        if(pthread_create(&sent_chat_thread, NULL, send_chat_msgs, (void*) &socket_fd) != 0){
            endwin();
            mrerror("Error while creating thread to service outgoing chat messages");
        }

        while(connection_open){
            enqueue_msg(socket_fd);
        }

        // when connection closes, proceed to cleanup and close everything gracefully
        pthread_cancel(live_chat_thread);
        pthread_cancel(sent_chat_thread);

        if(pthread_join(live_chat_thread, NULL) != 0){
            endwin();
            mrerror("Error while terminating chat services.");
        }

        if(pthread_join(sent_chat_thread, NULL) != 0){
            endwin();
            mrerror("Error while terminating chat services.");
        }

        endwin();
    }
    else{
        mrerror("Cannot establish connection to game server");
    }

    return 0;
}

void* get_chat_msgs(void* arg){
    while(connection_open){
        msg recv_msg;
        recv_msg = dequeue_chat_msg();

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        char to_print[MSG_SIZE+1];
        strcpy(to_print, "\n");
        strcat(to_print, recv_msg.msg);

        waddstr(live_chat, to_print);
        wrefresh(live_chat);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }
}

void* send_chat_msgs(void* arg){
    int socket_fd = *((int*) arg);

    while(connection_open){
        msg to_send;
        to_send.msg_type = CHAT;
        wgetstr(chat_box, to_send.msg);

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        if(send_msg(to_send, socket_fd) < 0){
            smrerror("Error communicating with game server");

            pthread_mutex_lock(&threadMutex);
            connection_open = 0;
            pthread_mutex_unlock(&threadMutex);
        }

        wmove(chat_box, 1, 0);
        wclrtobot(chat_box);
        wrefresh(chat_box);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }
}
