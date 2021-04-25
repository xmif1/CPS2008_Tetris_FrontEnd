#include <stdio.h>
#include <pthread.h>
#include "curses.h"
#include "client_server.h"

// GLOBALS

WINDOW* live_chat;
WINDOW* chat_box;
int connection_open = 0;
int server_err = 0;
pthread_mutex_t serverConnectionMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t live_chat_thread;
pthread_t sent_chat_thread;
pthread_t accept_p2p_thread;
pthread_t score_update_thread;
pthread_t start_game_thread;

// FUNC DEFNS

void* get_chat_msgs(void* arg);
void* send_chat_msgs(void* arg);
void* score_update(void* arg);
void* start_game(void* arg);
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

        msg recv_server_msg;
        while(connection_open){
            recv_server_msg = enqueue_server_msg(server_fd);

            if(recv_server_msg.msg_type == INVALID){
		        pthread_mutex_lock(&serverConnectionMutex);
		        connection_open = 0;
		        server_err = 1;
		        pthread_mutex_unlock(&serverConnectionMutex);
	        }else{
                switch(recv_server_msg.msg_type){
                    case CHAT: handle_chat_msg(recv_server_msg); break;
                    case NEW_GAME: handle_new_game_msg(recv_server_msg); break;
                    case START_GAME: if(pthread_create(&start_game_thread, NULL, start_game, (void*) NULL) != 0){
                                         curses_cleanup();
                                         mrerror("Error while creating thread to service new game session");
                                     }

                                     break;
                }
            }
        }

        // when connection closes, proceed to cleanup and close everything gracefully
        pthread_cancel(live_chat_thread);
        pthread_cancel(sent_chat_thread);

	curses_cleanup();

        if(server_err){
            smrerror("Server disconnected abruptly.");
        }

	if(signalGameTermination() && pthread_join(start_game_thread, NULL) != 0){
            mrerror("Error while terminating game session.");
	}

        if(pthread_join(live_chat_thread, NULL) != 0){
            mrerror("Error while terminating chat services.");
        }

        if(pthread_join(sent_chat_thread, NULL) != 0){
            mrerror("Error while terminating chat services.");
        }

        if(server_err){
            mrerror("Exiting due to server disconnection...");
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
        while((c = wgetch(chat_box)) != '\n' && c != EOF && c != '\r'){
            if((c == KEY_BACKSPACE || c == 127 || c == '\b') && i > 0){
                i--;
                to_send.msg[i] = '\0';
                wdelch(chat_box);
            }else{
                to_send.msg[i] = (char) c;
                i++;
            }

            to_send.msg = realloc(to_send.msg, i+1);
            if(to_send.msg == NULL){
                mrerror("Error while allocating memory");
            }
        }

        to_send.msg[i] = '\0';

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        if(send_msg(to_send, server_fd) < 0){
            pthread_mutex_lock(&serverConnectionMutex);
            connection_open = 0;
            server_err = 1;
            pthread_mutex_unlock(&serverConnectionMutex);
        }

        wmove(chat_box, 1, 0);
        wclrtobot(chat_box);
        wnoutrefresh(chat_box);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }

    pthread_exit(NULL);
}

void* start_game(void* arg){
    // create threads for accepting peer-to-peer connections
    if(pthread_create(&accept_p2p_thread, NULL, accept_peer_connections, (void*) NULL) != 0){
        curses_cleanup();
        mrerror("Error while creating thread to accept incoming peer to peer connections messages");
    }

    service_peer_connections(NULL);

    if(pthread_create(&score_update_thread, NULL, score_update, (void*) NULL) != 0){
        curses_cleanup();
        mrerror("Error while creating thread to send score updates to game server");
    }


    msg debug;
    debug.msg_type = CHAT;
    debug.msg = malloc(16);
    strcpy(debug.msg, "debug...");
    send_msg(debug, server_fd);

    sleep(10); // debug

    // end of game sessions: cleanup
    if(end_game() < 0){
        pthread_mutex_lock(&serverConnectionMutex);
        connection_open = 0;
        server_err = 1;
        pthread_mutex_unlock(&serverConnectionMutex);
    }

    if(pthread_join(accept_p2p_thread, NULL) != 0){
        curses_cleanup();
        mrerror("Error while terminating game session.");
    }

    if(pthread_join(score_update_thread, NULL) != 0){
        curses_cleanup();
        mrerror("Error while terminating game session.");
    }
}

void* score_update(void* arg){
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while(1){
        sleep(1);

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        int score = get_score();

        if(score < 0){
            break;
        }

        msg score_msg;
        score_msg.msg_type = SCORE_UPDATE;

        score_msg.msg = malloc(7);
        if(score_msg.msg == NULL){
            mrerror("Failed to allocate memory for score update to server");
        }
        sprintf(score_msg.msg, "%d", score);

        if(send_msg(score_msg, server_fd) < 0){
            signalGameTermination();

            pthread_mutex_lock(&serverConnectionMutex);
            connection_open = 0;
            server_err = 1;
            pthread_mutex_unlock(&serverConnectionMutex);

            break;
        }

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
