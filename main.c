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

WINDOW *live_chat_border, *live_chat, *chat_box_border, *chat_box, *board, *next, *hold, *score;

int in_game = 0;
int connection_open = 0;
int server_err = 0;

tetris_game* tg;

pthread_mutex_t serverConnectionMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t server_conn_thread;
pthread_t accept_p2p_thread;
pthread_t score_update_thread;

// FUNC DEFNS

void send_chat_msg();
void game_cleanup();
void curses_cleanup();
void start_game(int rows, int cols);
void* score_update(void* arg);
void* get_chat_msgs(void* arg);
int get_chat_box_char(msg to_send, int i)

int main(){
    client_init();

    if(server_fd >= 0){
        pthread_mutex_lock(&serverConnectionMutex);
        connection_open = 1;
        pthread_mutex_unlock(&serverConnectionMutex);

        // setting up ncurses
        initscr();

        // defining windows and their properties
        int rows = 22; int cols = 10;

        int max_x, max_y;
        getmaxyx(stdscr, max_y, max_x);
        int n_x_lines = max_x - 4 * (cols + 1);

        live_chat_border = newwin(max_y - 5, n_x_lines, 0, 0);
        live_chat = newwin(max_y - 7, n_x_lines - 2, 1, 1);
        scrollok(live_chat, TRUE);
        chat_box_border = newwin(5, n_x_lines, max_y - 5, 0);
        chat_box = newwin(3, n_x_lines - 2, max_y - 4, 1);
        wtimeout(chat_box, 0);

	    int offset_x = n_x_lines + 1;
        int offset_y = 0;

        // Create windows for each section of the tetris board
        board = newwin(rows + 2, 2 * cols + 2, offset_y, offset_x);
        next  = newwin(6, 10, offset_y, 2 * (cols + 1) + 1 + offset_x);
        hold  = newwin(6, 10, 7 + offset_y, 2 * (cols + 1) + 1 + offset_x);
        score = newwin(6, 10, 14 + offset_y, 2 * (cols + 1 ) + 1 + offset_x);

	    wborder(live_chat_border, '|', '|', '-', '-', '+', '+', '|', '|');
	    wborder(chat_box_border, '|', '|', '-', '-', '|', '|', '+', '+');

        // printing titles and border
        mvwprintw(live_chat, 0, 0, "Super Battle Tetris Chat Server\n\n");

        // updating
	    wrefresh(live_chat_border);
	    wrefresh(chat_box_border);
        wrefresh(live_chat);
        wrefresh(chat_box);

        // create threads for sending and receiving server messages while connection is open
        if(pthread_create(&server_conn_thread, NULL, get_server_msgs, (void*) NULL) != 0){
            curses_cleanup();
            mrerror("Error while creating thread to service incoming server messages");
        }

        // msg used for sends over chat
        int msg_to_send_idx = 0;
        msg to_send;
        to_send.msg_type = CHAT;
        to_send.msg = malloc(1);
        if(to_send.msg == NULL){
            mrerror("Error while allocating memory");
        }

        msg recv_server_msg;
        while(1){
            recv_server_msg = dequeue_server_msg();

            if(recv_server_msg.msg_type == INVALID){
                break;
            }

            switch(recv_server_msg.msg_type){
                case CHAT: {
                    waddstr(live_chat, recv_msg.msg);
                    waddch(live_chat, '\n');
                    wrefresh(live_chat);
                } break;
                case NEW_GAME: handle_new_game_msg(recv_server_msg); break;
                case START_GAME: start_game(rows, cols); break;
            }

            if(!in_game){
                while(msg_to_send_idx >= 0){
                    msg_to_send_idx = get_chat_box_char(to_send, msg_to_send_idx)
                }

                if(msg_to_send_idx == -1){
                    send_chat_msg(to_send);

                    msg_to_send_idx = 0;
                    to_send.msg = realloc(to_send.msg, 1);
                    if(to_send.msg == NULL){
                        mrerror("Error while allocating memory");
                    }
                }
            }else{
                sleep(10);
                in_game = 0;
            }
        }

	    curses_cleanup();

        if(server_err){
            smrerror("Server disconnected abruptly.");
        }

        if(signalGameTermination()){
                mrerror("Error while terminating game session.");
        }

        if(pthread_join(server_conn_thread, NULL) != 0){
            mrerror("Error while terminating chat services.");
        }

        if(server_err){
            mrerror("Exiting due to server disconnection...");
        }
    }else{
        mrerror("Cannot establish connection to game server");
    }

    return 0;
}

void* get_server_msgs(void* arg){
    while(1){
        recv_server_msg = enqueue_server_msg(server_fd);

        if(recv_server_msg.msg_type == INVALID){
            break;
        }
    }

    pthread_exit(NULL);
}

int get_chat_box_char(msg to_send, int i){
    int c = mvwgetch(chat_box, 0, i);

    if(c == ERR){
        return -2;
    }
    else if(c == '\n' || c == EOF || c == '\r'){
        to_send.msg[i] = '\0';
        return -1;
    }
    else if((c == KEY_BACKSPACE || c == 127 || c == '\b') && i > 0){
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

    return i;
}

void send_chat_msg(msg to_send){
    if(send_msg(to_send, server_fd) < 0){
        pthread_mutex_lock(&serverConnectionMutex);
        connection_open = 0;
        server_err = 1;
        pthread_mutex_unlock(&serverConnectionMutex);
    }

    wmove(chat_box, 0, 0);
    wclrtobot(chat_box);
    wrefresh(chat_box);
}

void start_game(int rows, int cols){
    in_game = 1;

    tetris_move move = TM_NONE;
    tg = tg_create(rows, cols);

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

    // NCURSES initialization:
    noecho();
    cbreak();
    init_colors();         // setup tetris colors

    display_board(board, tg);
    display_piece(next, tg->next);
    display_piece(hold, tg->stored);
    display_score(score, tg);

    wrefresh(board);
    wrefresh(next);
    wrefresh(hold);
    wrefresh(score);
}

// end of game sessions: cleanup
void game_cleanup(){
    wclear(board); wrefresh(board);
    wclear(next); wrefresh(next);
    wclear(hold); wrefresh(hold);
    wclear(score); wrefresh(score);

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

    //NCURSES reset to original state:
    echo();
    nocbreak();
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
    delwin(live_chat_border);
    delwin(chat_box_border);
    delwin(board);
    delwin(next);
    delwin(hold);
    delwin(score);

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
