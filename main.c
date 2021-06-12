#include <stdio.h>
#include <pthread.h>
#include "curses.h"

#include "tetris.h"
#include "client_server.h" // import client library header file

/***************************************************************************/
// TETRIS MACROS (see Stephene Brennan's implementation at https://github.com/brenns10/tetris)

// 2 columns per cell makes the game much nicer.
#define COLS_PER_CELL 2

// Macro to print a cell of a specific type to a window.
#define ADD_BLOCK(w,x) waddch((w),' '|A_REVERSE|COLOR_PAIR(x)); waddch((w),' '|A_REVERSE|COLOR_PAIR(x))
#define ADD_EMPTY(w) waddch((w), ' '); waddch((w), ' ')

// TETRIS FUNC DEFNS (see Stephen Brennan's implementation at https://github.com/brenns10/tetris)

void sleep_milli(int milliseconds);
void display_board(WINDOW *w, tetris_game *obj);
void display_piece(WINDOW *w, tetris_block block);
void display_score(WINDOW *w, tetris_game *tg);
void init_colors(void);
/***************************************************************************/

// GLOBALS

WINDOW *live_chat_border, *live_chat, *chat_box_border, *chat_box, *board, *next, *hold, *score;

// Flags -- self explanatory
int in_game = 0;
int connection_open = 0;
int server_err = 0;

// See Stephen Brennan's implementation
tetris_game* tg;
tetris_move curr_move;

// Multi--threading environment
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
void* get_server_msgs(void* arg);
int get_chat_box_char(msg to_send, int i);

/* The main loop of the front end, which after taking care of initialisation and correct connection to the server,
 * via appropriate calls to the client library.
 *
 * Side note: As mentioned in the project report, running multiple instances on the same machine may lead to flickering
 * if there are not enough resources available. This is because ncurses typically needs a few milliseconds to update the
 * entire screen etc, in an ideal situation.
 */
int main(){
    client_init();

    if(server_fd >= 0){
        pthread_mutex_lock(&serverConnectionMutex);
        connection_open = 1;
        pthread_mutex_unlock(&serverConnectionMutex);

        // setting up ncurses
        initscr();
        curs_set(0);
        timeout(0);
        cbreak();

        // defining parameters for determining window sizes
        int rows = 22; int cols = 10;
        int max_x, max_y;
        getmaxyx(stdscr, max_y, max_x);
        int n_x_lines = max_x - 4 * (cols + 1);

        // defining NCURSES windows and their properties

        // the following are for the live chat portion of the screen...
        live_chat_border = newwin(max_y - 5, n_x_lines, 0, 0);
        live_chat = newwin(max_y - 7, n_x_lines - 2, 1, 1);
        scrollok(live_chat, TRUE);
        chat_box_border = newwin(5, n_x_lines, max_y - 5, 0);
        chat_box = newwin(3, n_x_lines - 2, max_y - 4, 1);
        wtimeout(chat_box, 0);

	    int offset_x = n_x_lines + 1;
        int offset_y = 0;

        // ...and these are for the tetris gameplay portion of the screen...
        board = newwin(rows + 2, 2 * cols + 2, offset_y, offset_x);
        next  = newwin(6, 10, offset_y, 2 * (cols + 1) + 1 + offset_x);
        hold  = newwin(6, 10, 7 + offset_y, 2 * (cols + 1) + 1 + offset_x);
        score = newwin(6, 10, 14 + offset_y, 2 * (cols + 1 ) + 1 + offset_x);

        // draw basic borders
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
            curses_cleanup(); // call ncurses clean up function on failure
            mrerror("Error while creating thread to service incoming server messages");
        }

        // create new msg instance to be used for sending chat messages to the server
        int msg_to_send_idx = 0;
        msg to_send;
        to_send.msg_type = CHAT;
        to_send.msg = malloc(1);
        if(to_send.msg == NULL){
            mrerror("Error while allocating memory");
        }

        msg recv_server_msg;
        while(1){ // main loop: either fetches keyboard input for sending a message over chat, or for playing a tetris game
            // pop off message from the queue of recieved message from the server
            recv_server_msg = dequeue_server_msg(); // dequeue_server_msg is a library function

            if(recv_server_msg.msg_type == INVALID){ // if message was not recieved correctly, its tagged as INVALID
                break; // in which case we break from the main loop, initiating the exit sequence
            }

            switch(recv_server_msg.msg_type){ // otherwise if valid, handle accordingly
                case CHAT: { // in the case of a chat message, simply print the data part along with a newline character
                    waddstr(live_chat, recv_server_msg.msg);
                    waddch(live_chat, '\n');
                    wrefresh(live_chat);
                } break;
                // else if the message is NEW_GAME, call the handler function provided in the library
                case NEW_GAME: handle_new_game_msg(recv_server_msg); break;
                // and similarly if the message is a START_GAME message
                case START_GAME: {
		            start_game(rows, cols); // call the start_game convience function to setup a new game session on the frontend

		            msg_to_send_idx = 0;
                    to_send.msg = realloc(to_send.msg, 1);
                    if(to_send.msg == NULL){
                        mrerror("Error while allocating memory");
                    }
		        } break; // otherwise queue was empty and hence message was tagged EMPTY
            }

            if(!in_game){ // if player is not in game, keyboard input is bound to the live chat box
		        int ret = msg_to_send_idx;
                // allocate 512 bytes for message; remove limitation? longer inputs are causing ncurses to misbehave
                to_send.msg = realloc(to_send.msg, 512);
                if(to_send.msg == NULL){
                    mrerror("Error while allocating memory");
                }

                while(1){ // fetch input from the chat box until (i) error or (ii) user finished typing (iii) message exceeds 512 bytes
                    ret = get_chat_box_char(to_send, ret);
                    if(ret >= 0 && ret < 512){
                        msg_to_send_idx = ret;
                    }else{ break;}
                }

                if(ret == -1 || ret == 512){ // send message to the server if no error has occured
                    send_chat_msg(to_send);

                    // reset message ready for new input
                    msg_to_send_idx = 0;
                    to_send.msg = realloc(to_send.msg, 1);
                    if(to_send.msg == NULL){
                        mrerror("Error while allocating memory");
                    }
		        }
            }else{ // otherwise the input is bound to the tetris instance currently running, using the input to update
                   // the state of the game and any online oppononets.

                int lines_cleared = tg_tick(tg, curr_move); // tg_tick iterates the game play by one move and returns no. of lines cleared
                gameSession.total_lines_cleared += lines_cleared;

                // in case of rising tide: (state being shared between clients over the P2P network in this case)
                if(gameSession.game_type == RISING_TIDE){
                    tg_add_lines(tg, get_lines_to_add());
                    send_cleared_lines(lines_cleared);
                }

                // check if game is over and change in_game flag accordingly; this depends on the game mode eg. if timed etc
                if(tg_game_over(tg)
                   || (gameSession.game_type == FAST_TRACK && gameSession.total_lines_cleared == gameSession.n_winlines)
                   || (gameSession.game_type == BOOMER && difftime(time(NULL), gameSession.start_time) >= (gameSession.time * 60))){

                    in_game = 0;
                }

                // update the ncurses windows to reflect the changes arising from the new move
                display_board(board, tg);
                display_piece(next, tg->next);
                display_piece(hold, tg->stored);
                display_score(score, tg);

                wrefresh(board);
                wrefresh(next);
                wrefresh(hold);
                wrefresh(score);
                sleep_milli(10); // enough time for ncurses to update the terminal window

                switch(mvwgetch(chat_box, 0, 0)){ // fetch user input and bind to a game move
                    case KEY_LEFT:
                        curr_move = TM_LEFT;
                        break;
                    case KEY_RIGHT:
                        curr_move = TM_RIGHT;
                        break;
                    case KEY_UP:
                        curr_move = TM_CLOCK;
                        break;
                    case KEY_DOWN:
                        curr_move = TM_DROP;
                        break;
                    case 'q':
                        in_game = 0;
                        curr_move = TM_NONE;
                        break;
                    case ' ':
                        curr_move = TM_HOLD;
                        break;
                    default:
                        curr_move = TM_NONE;
                }

                // update the users score in a thread-safe manner using the set_score library function
                // recall that the score field is being accessed periodically by the score update thread
                set_score(tg->points);

                if(!in_game){
                    game_cleanup();
                    flushinp();
                }
            }
        }

	    curses_cleanup(); // on termination of main loop, clean up ncurses to restore terminal session to original state

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

// Simple threaded function that repeatedly calls the library provided enqueue_server_msg function, to fetch messages from the server
void* get_server_msgs(void* arg){
    msg recv_server_msg;

    while(1){
        recv_server_msg = enqueue_server_msg(server_fd);

        if(recv_server_msg.msg_type == INVALID){
            break;
        }
    }

    pthread_exit(NULL);
}

/* Utility function for fetching characters from the live chat box, while:
 * (i)   Checking if getch was successful (if not, we return -2)
 * (ii)  Checking if the character was a \n, \r or EOF indicating that the user wishes to send the message (in this case
 *       we return -1 and null terminate the data part of the message)
 * (iii) Check if the character denotes a backspace, in which case we clear one character from the screen and remove the
 *       last character appended to the data part of the message (in this case we return the index of the result last character)
 * (iv)  Otherwise we append the character to the data part of the message and return the new index pointing to the last
 *       character.
 */
int get_chat_box_char(msg to_send, int i){ // i = index of where to add character in the data part
    int c = mvwgetch(chat_box, 0, i);

    if(c == ERR)
        return -2; // signal error
    }
    else if(c == '\n' || c == EOF || c == '\r'){
        to_send.msg[i] = '\0';
        return -1; // signal that user has finished input
    }
    else if((c == KEY_BACKSPACE || c == 127 || c == '\b') && i > 0){ // clear char from screen and remove from data part
        i--;
        to_send.msg[i] = '\0';
        wdelch(chat_box);
    }else{
        to_send.msg[i] = (char) c; // append new char
        i++; // increment index
    }

    return i; // return index pointing to last char in msg data part
}

// Wrapper function to the send_msg library function, which handles disconnection and updating of NCURSES chat windows
void send_chat_msg(msg to_send){
    // if sending to server failed, in a thread--safe manner change the flags which indicate whether an error has occurred
    // while communicating with the server, and which indicate whether the connection is still open or not
    if(send_msg(to_send, server_fd) < 0){
        pthread_mutex_lock(&serverConnectionMutex);
        connection_open = 0;
        server_err = 1;
        pthread_mutex_unlock(&serverConnectionMutex);
    }

    // clear user input from chat box, ready for new input
    wmove(chat_box, 0, 0);
    wclrtobot(chat_box);
    wrefresh(chat_box);
}

/* Called whenever a new game instance is started; In particular it is responsible for:
 * (i)   In the case of a multiplayer session, initiate the P2P setup functions using threading as necessary.
 * (ii)  Initiate a thread to periodically send score updates to the server.
 * (iii) Initiate a tetris game and change NCURSES behavior as necessary.
 */
void start_game(int rows, int cols){
    in_game = 1; // set flag to signal game start i.e. user input redirected to game instead of chat

    curr_move = TM_NONE; // initial tetris move declaration

    int n_board_rows = rows;
    if(gameSession.game_type == FAST_TRACK){ // in case of a fast track, shorten the board by the number of baselines
        n_board_rows = rows - gameSession.n_baselines;
    }

    tg = tg_create(n_board_rows, cols, gameSession.seed); // initiate a tetris game instance

    if(gameSession.game_type != CHILL){ // if multiplayer game session
        // create threads for accepting peer-to-peer connections
        if(pthread_create(&accept_p2p_thread, NULL, accept_peer_connections, (void*) NULL) != 0){
            curses_cleanup(); // call ncurses clean up function on failure
            mrerror("Error while creating thread to accept incoming peer to peer connections messages");
        }

        service_peer_connections(NULL); // run on main thread of front--end
    }

    // create thread for sending periodic score updates to the server
    if(pthread_create(&score_update_thread, NULL, score_update, (void*) NULL) != 0){
        curses_cleanup(); // call ncurses clean up function on failure
        mrerror("Error while creating thread to send score updates to game server");
    }

    // NCURSES initialization:
    init_colors();         // setup tetris colors
    keypad(chat_box, TRUE);
}

/* Cleanup function for the end of a game session, responsible for terminating any initiated threads, restoring NCURSES
 * behaviour as before for chatting, etc...
 */
void game_cleanup(){
    // cleanup ncurses windows used during game play
    wclear(board); wrefresh(board);
    wclear(next); wrefresh(next);
    wclear(hold); wrefresh(hold);
    wclear(score); wrefresh(score);

    pthread_cancel(score_update_thread); // cancel the score update thread

    if(pthread_join(score_update_thread, NULL) != 0){ // and wait to join thread
        curses_cleanup(); // call ncurses clean up function on failure
        mrerror("Error while terminating game session.");
    }

    // if sending to server failed, in a thread--safe manner change the flags which indicate whether an error has occurred
    // while communicating with the server, and which indicate whether the connection is still open or not
    if(end_game() < 0){
        pthread_mutex_lock(&serverConnectionMutex);
        connection_open = 0;
        server_err = 1;
        pthread_mutex_unlock(&serverConnectionMutex);
    }

    if(gameSession.game_type != CHILL){
        if(pthread_join(accept_p2p_thread, NULL) != 0){ // wait until P2P connection thread exits...
            curses_cleanup(); // call ncurses clean up function on failure
            mrerror("Error while terminating game session.");
        }
    }

    reset_lcg();

    //NCURSES reset to original state:
    keypad(chat_box, FALSE);

    // Clear chat box of any garbage characters
    wmove(chat_box, 0, 0);
    wrefresh(chat_box);
}

// Periodically sends, in a thread--safe manner, the player's score to the server.
void* score_update(void* arg){
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while(1){ // until pthread_cancel called on the thread instance
        sleep(1);

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        int score = get_score(); // get score in a thread-safe manner using the library provided getter

        if(score < 0){ // if score is invalid...
            break;
        }

        // prepare message for sending score update to the server...
        msg score_msg;
        score_msg.msg_type = SCORE_UPDATE;

        score_msg.msg = malloc(7); // allocate enough memory in the data part
        if(score_msg.msg == NULL){
            mrerror("Failed to allocate memory for score update to server");
        }
        sprintf(score_msg.msg, "%d", score); // and cast the score from int to string

        if(send_msg(score_msg, server_fd) < 0){ // then attempt to send to the server...
            signalGameTermination(); // if failed, in a thread safe manner change in_game flag to 0...

            // in a thread--safe manner change the flags which indicate whether an error has occurred while communicating
            // with the server, and which indicate whether the connection is still open or not
            pthread_mutex_lock(&serverConnectionMutex);
            connection_open = 0;
            server_err = 1;
            pthread_mutex_unlock(&serverConnectionMutex);

            break;
        }

        free(score_msg.msg); // free memory as necessary

        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }

    pthread_exit(NULL);
}

// Simple NCURSES clean up function that restores the terminal back to its original state
void curses_cleanup(){
    delwin(live_chat);
    delwin(chat_box);
    delwin(live_chat_border);
    delwin(chat_box_border);
    delwin(board);
    delwin(next);
    delwin(hold);
    delwin(score);

    clear();
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
    wborder(w, '|', '|', '-', '-', '+', '+', '+', '+');
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
    wborder(w, '|', '|', '-', '-', '+', '+', '+', '+');
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
