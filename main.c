#include <stdio.h>
#include <pthread.h>
#include "curses.h"

#include "tetris.h"
#include "client_server.h"

/***************************************************************************/
// TETRIS MACROS (see Stephene Brennan's implementation at https://github.com/brenns10/tetris)

// 2 columns per cell makes the game much nicer.
#define COLS_PER_CELL 2

// Macro to print a cell of a specific type to a window.
#define ADD_BLOCK(w,x) waddch((w),' '|A_REVERSE|COLOR_PAIR(x)); waddch((w),' '|A_REVERSE|COLOR_PAIR(x))
#define ADD_EMPTY(w) waddch((w), ' '); waddch((w), ' ')

// TETRIS FUNC DEFNS (see Stephene Brennan's implementation at https://github.com/brenns10/tetris)

void sleep_milli(int milliseconds);
void display_board(WINDOW *w, tetris_game *obj);
void display_piece(WINDOW *w, tetris_block block);
void display_score(WINDOW *w, tetris_game *tg);
void init_colors(void);
/***************************************************************************/

// GLOBALS

WINDOW* live_chat, chat_box, board, next, hold, score;

int in_game = 0;
int connection_open = 0;
int server_err = 0;

pthread_mutex_t serverConnectionMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t screenMutex = PTHREAD_MUTEX_INITIALIZER;

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

        pthread_mutex_lock(&screenMutex);
        if(!in_game){
            wrefresh(live_chat);
        }
        pthread_mutex_unlock(&screenMutex);

        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }

    pthread_exit(NULL);
}

void* send_chat_msgs(void* arg){
    while(connection_open){
        pthread_mutex_lock(&screenMutex);
        if(in_game){
            pthread_mutex_unlock(&screenMutex);
            sleep(1);

            continue;
        }
        pthread_mutex_unlock(&screenMutex);

        flushinp();
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
    pthread_mutex_lock(&screenMutex);
    in_game = 1;
    pthread_mutex_unlock(&screenMutex);

    tetris_game *tg;
    tetris_move move = TM_NONE;

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

    // delwin(chat_box);

    // NCURSES initialization:
    cbreak();              // pass key presses to program, but not signals
    noecho();              // don't echo key presses to screen
    keypad(stdscr, TRUE);  // allow arrow keys
    timeout(0);            // no blocking on getch()
    curs_set(0);           // set the cursor to invisible
    init_colors();         // setup tetris colors

    // Create windows for each section of the interface.
    board = newwin(tg->rows + 2, 2 * tg->cols + 2, 0, 0);
    next  = newwin(6, 10, 0, 2 * (tg->cols + 1) + 1);
    hold  = newwin(6, 10, 7, 2 * (tg->cols + 1) + 1);
    score = newwin(6, 10, 14, 2 * (tg->cols + 1 ) + 1);

    display_board(board, tg);
    display_piece(next, tg->next);
    display_piece(hold, tg->stored);
    display_score(score, tg);
    doupdate();

    sleep(10); // debug

    //NCURSES reset to original state:
    nocbreak();             // allow signals
    echo();                 // echo key presses to screen
    keypad(stdscr, FALSE);  // disallow arrow keys
    timeout(-1);      // reset default behaviour of getch()
    curs_set(1);            // set the cursor back to being visible

    delwin(board);
    delwin(next);
    delwin(hold);
    delwin(score);

//    // reinitialise chat window
//    chat_box = newwin(5, max_x, max_y - 5, 0);
//    for(int i = 0; i < max_x; i++){
//        mvwprintw(chat_box, 0, i, "=");
//    }
//
//    // updating
//    wrefresh(chat_box);

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

    pthread_mutex_lock(&screenMutex);
    in_game = 0;
    pthread_mutex_unlock(&screenMutex);

    pthread_exit(NULL);
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

/***************************************************************************//**
 * The following functions are based Stephen Brennan's Tetris implementation,
 * found at https://github.com/brenns10/tetris, with modifications accordingly.
 * Modified functions are annotated accordingly. The following have been removed
 * since the feature set is beyond the scope of this assignment:
 * (i)   void boss_mode(void);
 * (ii)  void save(tetris_game *game, WINDOW *w);
 * (iii) int main(int argc, char **argv); [the main game logic is implemented in
 *       void* start_game(void* arg) along with the required multi-threading and
 *       peer-to-peer logic]
 *
 * The original copyright is stated below, see StephenBrennan_Tetris_LICENSE.txt
 * for further details:
 *
 * Copyright (c) 2015, Stephen Brennan.  Released under the Revised BSD License.
 ******************************************************************************/

void sleep_milli(int milliseconds){
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = milliseconds * 1000 * 1000;
    nanosleep(&ts, NULL);
}

// Print the tetris board onto the ncurses window.
void display_board(WINDOW *w, tetris_game *obj){
    int i, j;
    box(w, 0, 0);
    for (i = 0; i < obj->rows; i++) {
        wmove(w, 1 + i, 1);
        for (j = 0; j < obj->cols; j++) {
            if (TC_IS_FILLED(tg_get(obj, i, j))) {
                ADD_BLOCK(w,tg_get(obj, i, j));
            } else {
                ADD_EMPTY(w);
            }
        }
    }
    wnoutrefresh(w);
}

// Display a tetris piece in a dedicated window.
void display_piece(WINDOW *w, tetris_block block){
    int b;
    tetris_location c;
    wclear(w);
    box(w, 0, 0);
    if (block.typ == -1) {
        wnoutrefresh(w);
        return;
    }
    for (b = 0; b < TETRIS; b++) {
        c = TETROMINOS[block.typ][block.ori][b];
        wmove(w, c.row + 1, c.col * COLS_PER_CELL + 1);
        ADD_BLOCK(w, TYPE_TO_CELL(block.typ));
    }
    wnoutrefresh(w);
}

// Display score information in a dedicated window.
void display_score(WINDOW *w, tetris_game *tg){
    wclear(w);
    box(w, 0, 0);
    wprintw(w, "Score\n%d\n", tg->points);
    wprintw(w, "Level\n%d\n", tg->level);
    wprintw(w, "Lines\n%d\n", tg->lines_remaining);
    wnoutrefresh(w);
}

// Do the NCURSES initialization steps for color blocks.
void init_colors(void){
    start_color();
    //init_color(COLOR_ORANGE, 1000, 647, 0);
    init_pair(TC_CELLI, COLOR_CYAN, COLOR_BLACK);
    init_pair(TC_CELLJ, COLOR_BLUE, COLOR_BLACK);
    init_pair(TC_CELLL, COLOR_WHITE, COLOR_BLACK);
    init_pair(TC_CELLO, COLOR_YELLOW, COLOR_BLACK);
    init_pair(TC_CELLS, COLOR_GREEN, COLOR_BLACK);
    init_pair(TC_CELLT, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(TC_CELLZ, COLOR_RED, COLOR_BLACK);
}