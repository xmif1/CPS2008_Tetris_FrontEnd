#include <stdio.h>
#include <pthread.h>
#include "curses.h"
#include "client_server.h"

// GLOBALS

WINDOW* live_chat;
WINDOW* chat_box;
int connection_open = 0;
int server_err = 0;
pthread_mutex_t connectionMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t live_chat_thread;
pthread_t sent_chat_thread;

// FUNC DEFNS

void* get_chat_msgs(void* arg);
void* send_chat_msgs(void* arg);
void curses_cleanup();

int main(){
    client_init();

    if(server_fd >= 0){
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
            curses_cleanup();
            mrerror("Error while creating thread to service incoming chat messages");
        }

        if(pthread_create(&sent_chat_thread, NULL, send_chat_msgs, (void*) NULL) != 0){
            curses_cleanup();
            mrerror("Error while creating thread to service outgoing chat messages");
        }

        while(connection_open){
            if(enqueue_server_msg(server_fd) <= 0){
		        pthread_mutex_lock(&connectionMutex);
		        connection_open = 0;
		        server_err = 1;
		        pthread_mutex_unlock(&connectionMutex);
	        }
        }

        // when connection closes, proceed to cleanup and close everything gracefully
        pthread_cancel(live_chat_thread);
        pthread_cancel(sent_chat_thread);

        if(pthread_join(live_chat_thread, NULL) != 0){
            curses_cleanup();
            mrerror("Error while terminating chat services.");
        }

        if(pthread_join(sent_chat_thread, NULL) != 0){
            curses_cleanup();
            mrerror("Error while terminating chat services.");
        }

        curses_cleanup();
        if(server_err){
            mrerror("Server disconnected abruptly.");
        }
    }
    else{
        mrerror("Cannot establish connection to game server");
    }

    return 0;
}

void* get_chat_msgs(void* arg){
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    while(connection_open){
        msg recv_msg;
        recv_msg = dequeue_chat_msg();

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        waddstr(live_chat, recv_msg.msg);
        waddch(live_chat, '\n');
        wrefresh(live_chat);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }

    pthread_exit(NULL);
}

void* send_chat_msgs(void* arg){
    while(connection_open){
        msg to_send;
        to_send.msg = malloc(1);
        if(to_send.msg == NULL){
            mrerror("Error while allocating memory");
        }

        to_send.msg_type = CHAT;

        int c; int i = 0;
        while((c = wgetch(chat_box)) != '\n' && c != EOF){
            to_send.msg[i] = (char) c;
            i++;

            to_send.msg = realloc(to_send.msg, i+1);
            if(to_send.msg == NULL){
                mrerror("Error while allocating memory");
            }
        }

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        if(send_msg(to_send, server_fd) < 0){
            pthread_mutex_lock(&connectionMutex);
            connection_open = 0;
            server_err = 1;
            pthread_mutex_unlock(&connectionMutex);
        }

        wmove(chat_box, 1, 0);
        wclrtobot(chat_box);
        wnoutrefresh(chat_box);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }

    pthread_exit(NULL);
}

void curses_cleanup(){
    delwin(live_chat);
    delwin(chat_box);
    endwin();
    fflush(stdout);
}
