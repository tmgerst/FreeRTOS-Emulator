#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Event.h"
#include "TUM_Font.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"

#include "AsyncIO.h"

#include "main.h"
// #include "pong.h"


#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

#define STATE_QUEUE_LENGTH 1

#define STARTING_STATE 0
#define STATE_PLAYING 0
#define STATE_PAUSED 1

#define STATE_CHANGE_DELAY 300
#define BUTTON_DEBOUNCE_DELAY 100

#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define mainGENERIC_PRIORITY 0

#define NUMBER_OF_STATES 2

/* Tetris constants */

#define TILE_WIDTH 20
#define TILE_HEIGHT 20

#define PLAY_AREA_WIDTH_IN_TILES 10
#define PLAY_AREA_HEIGHT_IN_TILES 22
#define PLAY_AREA_POSITION_X 220
#define PLAY_AREA_POSITION_Y -40

#define SPAWN_ROW 2
#define SPAWN_COLUMN 5

#define TETRIMINO_GRID_WIDTH 5
#define TETRIMINO_GRID_HEIGHT 5
#define TETRIMINO_GRID_CENTER 2

#define TETRIMINO_LOCKING_PERIOD 500

#define TETRIMINO_BAG_SIZE 7

/* End of Tetris constants*/

#ifdef TRACE_FUNCTIONS
#include "tracer.h"
#endif

const unsigned char next_state_signal = NEXT_TASK;

static TaskHandle_t SequentialStateMachine = NULL;
static TaskHandle_t BufferSwap = NULL;

static TaskHandle_t StatePlayingControlTask = NULL;
static TaskHandle_t StatePausedControlTask = NULL;

TaskHandle_t GenerateTetriminoPermutationsTask = NULL;
TaskHandle_t SpawnTetriminoTask = NULL;

TaskHandle_t MoveTetriminoOneDownTask = NULL;

TaskHandle_t MoveTetriminoToTheRightTask = NULL;
TaskHandle_t MoveTetriminoToTheLeftTask = NULL;
TaskHandle_t RotateTetriminoCWTask = NULL;
TaskHandle_t RotateTetriminoCCWTask = NULL;

xTimerHandle LockingTetriminoTimer = NULL;

static QueueHandle_t StateMachineQueue = NULL;
QueueHandle_t TetriminoSelectionQueue = NULL;

SemaphoreHandle_t ScreenLock = NULL;
SemaphoreHandle_t DrawSignal = NULL;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

// ################ Tetris structure code ###################

typedef struct tile {
    int height;
    int width;
    unsigned int color;
} tile_t;

tile_t initTile(unsigned int color){
    tile_t new_tile;
    new_tile.height = TILE_HEIGHT;
    new_tile.width = TILE_WIDTH;
    new_tile.color = color;
    return new_tile;
}

void drawTile(int pos_x, int pos_y, tile_t* colored_tile){
    tumDrawFilledBox(pos_x, pos_y, colored_tile->width, colored_tile->height, colored_tile->color);
}


typedef struct t_or_table_t{
    int or_T[4][4][2];  // 4 configurations of 4 points (2 coordinates) that need to be colored
    int or_J[4][4][2];
    int or_Z[2][4][2];
    int or_O[1][4][2];
    int or_S[2][4][2];
    int or_L[4][4][2];
    int or_I[2][4][2];
    SemaphoreHandle_t lock;
} t_or_table_t;

t_or_table_t orientation_table = { 0 };

t_or_table_t initOrientationTable(t_or_table_t* or){

    // ------------------ T structure orientations-------------------------------------------------------
    or->or_T[0][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_T[0][0][1] = TETRIMINO_GRID_CENTER;
    or->or_T[0][1][0] = TETRIMINO_GRID_CENTER;      or->or_T[0][1][1] = TETRIMINO_GRID_CENTER-1;
    or->or_T[0][2][0] = TETRIMINO_GRID_CENTER;      or->or_T[0][2][1] = TETRIMINO_GRID_CENTER;
    or->or_T[0][3][0] = TETRIMINO_GRID_CENTER;      or->or_T[0][3][1] = TETRIMINO_GRID_CENTER+1;

    or->or_T[1][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_T[1][0][1] = TETRIMINO_GRID_CENTER;
    or->or_T[1][1][0] = TETRIMINO_GRID_CENTER;      or->or_T[1][1][1] = TETRIMINO_GRID_CENTER;
    or->or_T[1][2][0] = TETRIMINO_GRID_CENTER;      or->or_T[1][2][1] = TETRIMINO_GRID_CENTER+1;
    or->or_T[1][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_T[1][3][1] = TETRIMINO_GRID_CENTER;

    or->or_T[2][0][0] = TETRIMINO_GRID_CENTER;      or->or_T[2][0][1] = TETRIMINO_GRID_CENTER-1;
    or->or_T[2][1][0] = TETRIMINO_GRID_CENTER;      or->or_T[2][1][1] = TETRIMINO_GRID_CENTER;
    or->or_T[2][2][0] = TETRIMINO_GRID_CENTER;      or->or_T[2][2][1] = TETRIMINO_GRID_CENTER+1;
    or->or_T[2][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_T[2][3][1] = TETRIMINO_GRID_CENTER;

    or->or_T[3][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_T[3][0][1] = TETRIMINO_GRID_CENTER;
    or->or_T[3][1][0] = TETRIMINO_GRID_CENTER;      or->or_T[3][1][1] = TETRIMINO_GRID_CENTER-1;
    or->or_T[3][2][0] = TETRIMINO_GRID_CENTER;      or->or_T[3][2][1] = TETRIMINO_GRID_CENTER;
    or->or_T[3][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_T[3][3][1] = TETRIMINO_GRID_CENTER;

    // ----------------------- J structure orientations ---------------------------------------------------
    or->or_J[0][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_J[0][0][1] = TETRIMINO_GRID_CENTER;
    or->or_J[0][1][0] = TETRIMINO_GRID_CENTER;      or->or_J[0][1][1] = TETRIMINO_GRID_CENTER;
    or->or_J[0][2][0] = TETRIMINO_GRID_CENTER+1;    or->or_J[0][2][1] = TETRIMINO_GRID_CENTER-1;
    or->or_J[0][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_J[0][3][1] = TETRIMINO_GRID_CENTER;

    or->or_J[1][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_J[1][0][1] = TETRIMINO_GRID_CENTER-1;
    or->or_J[1][1][0] = TETRIMINO_GRID_CENTER;      or->or_J[1][1][1] = TETRIMINO_GRID_CENTER-1;
    or->or_J[1][2][0] = TETRIMINO_GRID_CENTER;      or->or_J[1][2][1] = TETRIMINO_GRID_CENTER;
    or->or_J[1][3][0] = TETRIMINO_GRID_CENTER;      or->or_J[1][3][1] = TETRIMINO_GRID_CENTER+1;

    or->or_J[2][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_J[2][0][1] = TETRIMINO_GRID_CENTER;
    or->or_J[2][1][0] = TETRIMINO_GRID_CENTER-1;    or->or_J[2][1][1] = TETRIMINO_GRID_CENTER+1;
    or->or_J[2][2][0] = TETRIMINO_GRID_CENTER;      or->or_J[2][2][1] = TETRIMINO_GRID_CENTER;
    or->or_J[2][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_J[2][3][1] = TETRIMINO_GRID_CENTER;

    or->or_J[3][0][0] = TETRIMINO_GRID_CENTER;      or->or_J[3][0][1] = TETRIMINO_GRID_CENTER-1;
    or->or_J[3][1][0] = TETRIMINO_GRID_CENTER;      or->or_J[3][1][1] = TETRIMINO_GRID_CENTER;
    or->or_J[3][2][0] = TETRIMINO_GRID_CENTER;      or->or_J[3][2][1] = TETRIMINO_GRID_CENTER+1;
    or->or_J[3][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_J[3][3][1] = TETRIMINO_GRID_CENTER+1;

    // ----------------- Z structure orientations ----------------------------------------------------------
    or->or_Z[0][0][0] = TETRIMINO_GRID_CENTER;      or->or_Z[0][0][1] = TETRIMINO_GRID_CENTER-1; 
    or->or_Z[0][1][0] = TETRIMINO_GRID_CENTER;      or->or_Z[0][1][1] = TETRIMINO_GRID_CENTER;
    or->or_Z[0][2][0] = TETRIMINO_GRID_CENTER+1;    or->or_Z[0][2][1] = TETRIMINO_GRID_CENTER; 
    or->or_Z[0][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_Z[0][3][1] = TETRIMINO_GRID_CENTER+1; 

    or->or_Z[1][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_Z[1][0][1] = TETRIMINO_GRID_CENTER+1; 
    or->or_Z[1][1][0] = TETRIMINO_GRID_CENTER;      or->or_Z[1][1][1] = TETRIMINO_GRID_CENTER;
    or->or_Z[1][2][0] = TETRIMINO_GRID_CENTER;      or->or_Z[1][2][1] = TETRIMINO_GRID_CENTER+1; 
    or->or_Z[1][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_Z[1][3][1] = TETRIMINO_GRID_CENTER;

    // ------------------ O structure orientations -----------------------------------------------------------
    or->or_O[0][0][0] = TETRIMINO_GRID_CENTER;      or->or_O[0][0][1] = TETRIMINO_GRID_CENTER-1; 
    or->or_O[0][1][0] = TETRIMINO_GRID_CENTER;      or->or_O[0][1][1] = TETRIMINO_GRID_CENTER;
    or->or_O[0][2][0] = TETRIMINO_GRID_CENTER+1;    or->or_O[0][2][1] = TETRIMINO_GRID_CENTER-1; 
    or->or_O[0][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_O[0][3][1] = TETRIMINO_GRID_CENTER; 

    // -------------- S structure orientations -----------------------------------------------------------
    or->or_S[0][0][0] = TETRIMINO_GRID_CENTER;      or->or_S[0][0][1] = TETRIMINO_GRID_CENTER; 
    or->or_S[0][1][0] = TETRIMINO_GRID_CENTER;      or->or_S[0][1][1] = TETRIMINO_GRID_CENTER+1;
    or->or_S[0][2][0] = TETRIMINO_GRID_CENTER+1;    or->or_S[0][2][1] = TETRIMINO_GRID_CENTER-1; 
    or->or_S[0][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_S[0][3][1] = TETRIMINO_GRID_CENTER; 

    or->or_S[1][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_S[1][0][1] = TETRIMINO_GRID_CENTER; 
    or->or_S[1][1][0] = TETRIMINO_GRID_CENTER;      or->or_S[1][1][1] = TETRIMINO_GRID_CENTER;
    or->or_S[1][2][0] = TETRIMINO_GRID_CENTER;      or->or_S[1][2][1] = TETRIMINO_GRID_CENTER+1; 
    or->or_S[1][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_S[1][3][1] = TETRIMINO_GRID_CENTER+1;

    // -------------- L structure orientations ------------------------------------------------------------------------
    or->or_L[0][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_L[0][0][1] = TETRIMINO_GRID_CENTER; 
    or->or_L[0][1][0] = TETRIMINO_GRID_CENTER;      or->or_L[0][1][1] = TETRIMINO_GRID_CENTER;
    or->or_L[0][2][0] = TETRIMINO_GRID_CENTER+1;    or->or_L[0][2][1] = TETRIMINO_GRID_CENTER; 
    or->or_L[0][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_L[0][3][1] = TETRIMINO_GRID_CENTER+1;

    or->or_L[1][0][0] = TETRIMINO_GRID_CENTER;      or->or_L[1][0][1] = TETRIMINO_GRID_CENTER-1; 
    or->or_L[1][1][0] = TETRIMINO_GRID_CENTER;      or->or_L[1][1][1] = TETRIMINO_GRID_CENTER;
    or->or_L[1][2][0] = TETRIMINO_GRID_CENTER;      or->or_L[1][2][1] = TETRIMINO_GRID_CENTER+1; 
    or->or_L[1][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_L[1][3][1] = TETRIMINO_GRID_CENTER-1;

    or->or_L[2][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_L[2][0][1] = TETRIMINO_GRID_CENTER-1; 
    or->or_L[2][1][0] = TETRIMINO_GRID_CENTER-1;    or->or_L[2][1][1] = TETRIMINO_GRID_CENTER;
    or->or_L[2][2][0] = TETRIMINO_GRID_CENTER;      or->or_L[2][2][1] = TETRIMINO_GRID_CENTER; 
    or->or_L[2][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_L[2][3][1] = TETRIMINO_GRID_CENTER;

    or->or_L[3][0][0] = TETRIMINO_GRID_CENTER-1;    or->or_L[3][0][1] = TETRIMINO_GRID_CENTER+1; 
    or->or_L[3][1][0] = TETRIMINO_GRID_CENTER;      or->or_L[3][1][1] = TETRIMINO_GRID_CENTER-1;
    or->or_L[3][2][0] = TETRIMINO_GRID_CENTER;      or->or_L[3][2][1] = TETRIMINO_GRID_CENTER; 
    or->or_L[3][3][0] = TETRIMINO_GRID_CENTER;      or->or_L[3][3][1] = TETRIMINO_GRID_CENTER+1;

    // ------------------ I structure orientations ------------------------------------------------------------------
    or->or_I[0][0][0] = TETRIMINO_GRID_CENTER-2;    or->or_I[0][0][1] = TETRIMINO_GRID_CENTER; 
    or->or_I[0][1][0] = TETRIMINO_GRID_CENTER-1;    or->or_I[0][1][1] = TETRIMINO_GRID_CENTER;
    or->or_I[0][2][0] = TETRIMINO_GRID_CENTER;      or->or_I[0][2][1] = TETRIMINO_GRID_CENTER; 
    or->or_I[0][3][0] = TETRIMINO_GRID_CENTER+1;    or->or_I[0][3][1] = TETRIMINO_GRID_CENTER;

    or->or_I[1][0][0] = TETRIMINO_GRID_CENTER;      or->or_I[1][0][1] = TETRIMINO_GRID_CENTER-2; 
    or->or_I[1][1][0] = TETRIMINO_GRID_CENTER;      or->or_I[1][1][1] = TETRIMINO_GRID_CENTER-1;
    or->or_I[1][2][0] = TETRIMINO_GRID_CENTER;      or->or_I[1][2][1] = TETRIMINO_GRID_CENTER; 
    or->or_I[1][3][0] = TETRIMINO_GRID_CENTER;      or->or_I[1][3][1] = TETRIMINO_GRID_CENTER+1;

    // Some more orientations
    return *or;
}


typedef struct play_area {
    tile_t tiles[PLAY_AREA_HEIGHT_IN_TILES][PLAY_AREA_WIDTH_IN_TILES];
    int size_x;
    int size_y;
    SemaphoreHandle_t lock;
} play_area_t;

static play_area_t playfield = { 0 };

play_area_t initPlayArea(play_area_t* playarea){

    for (int r = 0; r < PLAY_AREA_HEIGHT_IN_TILES; r++){
        for(int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){
            playarea->tiles[r][c] = initTile(Black);
        }
    }

    playarea->size_x = PLAY_AREA_WIDTH_IN_TILES * TILE_WIDTH;
    playarea->size_y = PLAY_AREA_HEIGHT_IN_TILES * TILE_HEIGHT;

    return *playarea;
}

void printPlayArea(play_area_t* p){
    printf("Playfield:\n");
    for (int r = 0; r < PLAY_AREA_HEIGHT_IN_TILES; r++){
        for (int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){
            printf("%8x", p->tiles[r][c].color);
        }
        printf("\n");
    }
}

void drawPlayArea(play_area_t* area){
    int drawing_position_x = 0;
    int drawing_position_y = 0;

    for (int r = 0; r < PLAY_AREA_HEIGHT_IN_TILES; r++){
        for(int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){
            drawing_position_x = PLAY_AREA_POSITION_X + c*TILE_WIDTH;
            drawing_position_y = PLAY_AREA_POSITION_Y + r*TILE_HEIGHT;
            drawTile(drawing_position_x, drawing_position_y, &(area->tiles[r][c]));
        }
    }
}


typedef struct tetrimino_t {
    char name;
    unsigned int color;
    unsigned int grid[5][5];
    int playfield_row;      // row position of the tetrimino center in the play area 
    int playfield_column;    // column position of the tetrimino center in the play area
    int orientation;
    SemaphoreHandle_t lock;
} tetrimino_t;

tetrimino_t tetrimino = { 0 };

void clearTetriminoGrid(tetrimino_t* t){
    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            t->grid[r][c] = 0;
        }
    }
}

/* Helper function to make code more readable */
void copyOrientationIntoTetriminoGrid(int array[][4][2], tetrimino_t* t, int desired_orientation){
        t->grid[array[desired_orientation][0][0]][array[desired_orientation][0][1]] = t->color;
        t->grid[array[desired_orientation][1][0]][array[desired_orientation][1][1]] = t->color;
        t->grid[array[desired_orientation][2][0]][array[desired_orientation][2][1]] = t->color;
        t->grid[array[desired_orientation][3][0]][array[desired_orientation][3][1]] = t->color;

        t->orientation = desired_orientation;
}

void setTetriminoGridViaOrientation(t_or_table_t* or, tetrimino_t* t, int desired_orientation){
    char name = t->name;

    if (name == 'T'){
        clearTetriminoGrid(t);
        if (desired_orientation > 3){ desired_orientation = 0; }
        if (desired_orientation < 0){ desired_orientation = 3; }

        copyOrientationIntoTetriminoGrid(or->or_T, t, desired_orientation);
    }
    else if (name == 'J'){
        clearTetriminoGrid(t);
        if (desired_orientation > 3){ desired_orientation = 0; }
        if (desired_orientation < 0){ desired_orientation = 3; }

        copyOrientationIntoTetriminoGrid(or->or_J, t, desired_orientation);
    }
    else if (name == 'Z'){
        clearTetriminoGrid(t);

        if (desired_orientation > 1){ desired_orientation = 0; }
        if (desired_orientation < 0){ desired_orientation = 1; }

        copyOrientationIntoTetriminoGrid(or->or_Z, t, desired_orientation);
    }
    else if (name == 'O'){
        clearTetriminoGrid(t);

        if (desired_orientation != 0){ desired_orientation = 0; }

        copyOrientationIntoTetriminoGrid(or->or_O, t, desired_orientation);
    }
    else if (name == 'S'){
        clearTetriminoGrid(t);

        if (desired_orientation > 1){ desired_orientation = 0; }
        if (desired_orientation < 0){ desired_orientation = 1; }

        copyOrientationIntoTetriminoGrid(or->or_S, t, desired_orientation);
    }
    else if (name == 'L'){
        clearTetriminoGrid(t);

        if (desired_orientation > 3){ desired_orientation = 0; }
        if (desired_orientation < 0){ desired_orientation = 3; }

        copyOrientationIntoTetriminoGrid(or->or_L, t, desired_orientation);
    }
    else if (name == 'I'){
        clearTetriminoGrid(t);

        if (desired_orientation > 1){ desired_orientation = 0; }
        if (desired_orientation < 0){ desired_orientation = 1; }

        copyOrientationIntoTetriminoGrid(or->or_I, t, desired_orientation);
    }
}

tetrimino_t initTetrimino(t_or_table_t* or, tetrimino_t* t, char name, unsigned int color){
    t->name = name;
    t->color = color;
    t->playfield_row = 0;
    t->playfield_column = 0;

    clearTetriminoGrid(t);
    if (name == 'T'){
        setTetriminoGridViaOrientation(or, t, 2);
    }
    else if (name == 'J'){
        setTetriminoGridViaOrientation(or, t, 3);
    }
    else if (name == 'Z'){
        setTetriminoGridViaOrientation(or, t, 0);
    }
    else if (name == 'O'){
        setTetriminoGridViaOrientation(or, t, 0);
    }
    else if (name == 'S'){
        setTetriminoGridViaOrientation(or, t, 0);
    }
    else if (name == 'L'){
        setTetriminoGridViaOrientation(or, t, 1);
    }
    else if (name == 'I'){
        setTetriminoGridViaOrientation(or, t, 1);
    }

    return *t;
}

/* Debugging function to view the tetrimino in the console */
void printTetriminoInformation(tetrimino_t* t){
    printf("Structure: %c\n", t->name);
    printf("Center position: %i %i\n", t->playfield_row, t->playfield_column);
    printf("Color: %x\n", t->color);
    printf("Grid:\n");
    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            printf("%8x", t->grid[r][c]);
        }
        printf("\n");
    }
}

/* Helping function to get the tetrimino to the playfield */
int tetriminoRowToPlayfieldRow(tetrimino_t* t, int offset){
    return t->playfield_row - TETRIMINO_GRID_CENTER + offset;
}

/* Helping function to get the tetrimino to the playfield */
int tetriminoColumnToPlayfieldColumn(tetrimino_t* t, int offset){
    return t->playfield_column - TETRIMINO_GRID_CENTER + offset;
}


/* 
######################################################################
###### Functions for manipulating the game structures ################
*/
void transferTetriminoColorsToPlayArea(play_area_t* p, tetrimino_t* t){
    int corresponding_p_row = 0;
    int corresponding_p_column = 0;

    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            if (t->grid[r][c] != 0){

                corresponding_p_row = tetriminoRowToPlayfieldRow(t, r);
                corresponding_p_column = tetriminoColumnToPlayfieldColumn(t, c); 

                p->tiles[corresponding_p_row][corresponding_p_column].color = t->color;
            }
        }
    }
}

/* Sets the position of the tetrimino center. */
void setPositionOfTetriminoViaCenter(tetrimino_t* t, int playfield_row, int playfield_column){
    t->playfield_row = playfield_row;
    t->playfield_column = playfield_column;
}

void vLockingTetriminoIntoPlace(void *pvParameters){
    if(tetrimino.lock){
        if (xSemaphoreTake(tetrimino.lock, 0) == pdTRUE){

            if (playfield.lock){
                if (xSemaphoreTake(playfield.lock, 0)){
                    transferTetriminoColorsToPlayArea(&playfield, &tetrimino);
                }
                xSemaphoreGive(playfield.lock);
                xSemaphoreGive(tetrimino.lock);

                xTaskNotifyGive(SpawnTetriminoTask);
                xTimerStop(LockingTetriminoTimer, 0);
            }
            xSemaphoreGive(playfield.lock);
        }
    }
    xSemaphoreGive(tetrimino.lock);
}

/* Checks if the new tetrimino position is valid. 
Returns -1 if it is not valid.
Return 0 if the position is valid and the position is not stable.
Returns 1 if the tetrimino position is stable. */
int checkTetriminoPosition(play_area_t* p, tetrimino_t* t){
    int corresponding_p_row = 0;
    int corresponding_p_column = 0;

    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            if (t->grid[r][c] != 0){

                corresponding_p_row = tetriminoRowToPlayfieldRow(t, r);
                corresponding_p_column = tetriminoColumnToPlayfieldColumn(t, c);
                // printf("Computed playfield position: %i %i\n", corresponding_p_row, corresponding_p_column);

                // Tetrimino is above or under the play area --> error
                if ((corresponding_p_row < 0) || (corresponding_p_row > PLAY_AREA_HEIGHT_IN_TILES)){
                    printf("Row error.\n");
                    // Error handler, ie failed screen
                    return -1;
                }

                // Check if the Tetrimino crosses the bounds of the playarea to the left or right --> game over
                if ((corresponding_p_column < 0 || (corresponding_p_column >= PLAY_AREA_WIDTH_IN_TILES))){
                    printf("Column out of bounds.\n");
                    return -1;
                }
                
                // Check if the space is already occupied 
                if (p->tiles[corresponding_p_row][corresponding_p_column].color != 0){
                    printf("Position already occupied.\n");
                    return -1;
                }
            }
        }
    }

    // Has to be extra checked, since it is otherwise possible to overwrite already existing tetriminos
    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            if (t->grid[r][c] != 0){
                corresponding_p_row = tetriminoRowToPlayfieldRow(t, r);
                corresponding_p_column = tetriminoColumnToPlayfieldColumn(t, c);

                if (p->tiles[corresponding_p_row+1][corresponding_p_column].color != 0){
                    printf("Position stable.\n");
                    return 1;
                }
            }
        }
    }
    
    // scenario: The tetrimino shifts from a stable position into an unstable one --> timer has to be stopped
    if (xTimerIsTimerActive(LockingTetriminoTimer) == pdTRUE){
        xTimerStop(LockingTetriminoTimer, 0);
    }

    printf("New position valid.\n");
    return 0;
}

void removeTetriminoColorsFromCurrentPosition(play_area_t* playfield, tetrimino_t* tetrimino){
    int corresponding_p_row = 0;
    int corresponding_p_column = 0; 

    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            if (tetrimino->grid[r][c] != 0){
                corresponding_p_row = tetriminoRowToPlayfieldRow(tetrimino, r);
                corresponding_p_column = tetriminoColumnToPlayfieldColumn(tetrimino, c);

                playfield->tiles[corresponding_p_row][corresponding_p_column].color = 0;
            }
        }
    }
}

unsigned int chooseColorForTetrimino(char name){
    switch (name){
        case 'I': return Aqua;
        case 'J': return Navy;
        case 'L': return Orange;
        case 'O': return Yellow;
        case 'S': return Lime;
        case 'Z': return Red;
        case 'T': return Magenta;
        default:
            printf("Tetrimino name error, can't choose color.\n");
            return White;
    }   
}


void checkForButtonInput(void){

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){

        // if A has been pressed, give binary semaphore
        if(buttons.buttons[KEYCODE(A)]){
            buttons.buttons[KEYCODE(A)] = 0;
            xTaskNotifyGive(MoveTetriminoToTheLeftTask);
            xSemaphoreGive(buttons.lock);
        }
        // if S has been pressed, send binary semaphore
        if (buttons.buttons[KEYCODE(S)]){
            buttons.buttons[KEYCODE(S)] = 0;
            xTaskNotifyGive(MoveTetriminoOneDownTask);
            xSemaphoreGive(buttons.lock);
        }

        // if D has been pressed, send binary semaphore
        if (buttons.buttons[KEYCODE(D)]){
            buttons.buttons[KEYCODE(D)] = 0;
            xTaskNotifyGive(MoveTetriminoToTheRightTask);
            xSemaphoreGive(buttons.lock);
        }

        // if Left has been pressed, send binary semaphore
        if (buttons.buttons[KEYCODE(LEFT)]){
            buttons.buttons[KEYCODE(LEFT)] = 0;
            xTaskNotifyGive(RotateTetriminoCCWTask);
            xSemaphoreGive(buttons.lock);
        }

        // if Right has been pressed, send binary semaphore
        if (buttons.buttons[KEYCODE(RIGHT)]){
            buttons.buttons[KEYCODE(RIGHT)] = 0;
            xTaskNotifyGive(RotateTetriminoCWTask);
            xSemaphoreGive(buttons.lock);
        }

    }
    xSemaphoreGive(buttons.lock);
}


void vCalculateBagOfTetriminos(void *pvParameters){
    char names[7] = { 0 };
    char tetriminos[TETRIMINO_BAG_SIZE] = { 'T', 'J', 'Z', 'O', 'S', 'L', 'I' };
    int position = 0;

    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        printf("Permutation request received.\n");
        memcpy(names, tetriminos, 7); // set array again with names of tetriminos

        for (int i = 0; i < TETRIMINO_BAG_SIZE; i++){

random_number_again:
            position = rand() % TETRIMINO_BAG_SIZE;

            if (names[position] != 0){
                xQueueSend(TetriminoSelectionQueue, &names[position], 0);
                names[position] = 0;
            }
            else{
                // position was already pushed into queue
                goto random_number_again;
            }
        }
    }
}

void vSpawnTetrimino(void *pvParameters){
    char name_buffer = 0;
    unsigned int color = 0;
    xTaskNotifyGive(GenerateTetriminoPermutationsTask);

    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (uxQueueMessagesWaiting(TetriminoSelectionQueue) == 0){
            xTaskNotifyGive(GenerateTetriminoPermutationsTask);  // Send signal to generate a new permutation
        }
        xQueueReceive(TetriminoSelectionQueue, &name_buffer, 0);

        color = chooseColorForTetrimino(name_buffer);

        if(tetrimino.lock){
            if (xSemaphoreTake(tetrimino.lock, 0) == pdTRUE){   
                
                if(orientation_table.lock){
                    if(xSemaphoreTake(orientation_table.lock, 0) == pdTRUE){
                        tetrimino = initTetrimino(&orientation_table, &tetrimino, name_buffer, color); 
                        xSemaphoreGive(orientation_table.lock);
                    }
                    xSemaphoreGive(orientation_table.lock);
                }

                setPositionOfTetriminoViaCenter(&tetrimino, SPAWN_ROW, SPAWN_COLUMN);

                if (playfield.lock) {
                    if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){
                        transferTetriminoColorsToPlayArea(&playfield, &tetrimino);
                        xSemaphoreGive(playfield.lock);
                        xSemaphoreGive(tetrimino.lock);
                    }
                    xSemaphoreGive(playfield.lock);
                }
            }
            xSemaphoreGive(tetrimino.lock);
        }
    }
}

void vSafelyMoveTetriminoOneDown(void *pvParameters){

    int backup_row = 0;
    int backup_column = 0;
    int flag = 0;

    // TickType_t last_change = xTaskGetTickCount();

    while(1){
        // Hier muss noch ein Zeitgeber rein, in welchem Intervall das Tetrimino eins runterfallen soll
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (tetrimino.lock){
            if (xSemaphoreTake(tetrimino.lock, 0) == pdTRUE){

                if (playfield.lock) {
                    if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){

                        backup_row = tetrimino.playfield_row;
                        backup_column = tetrimino.playfield_column;

                        removeTetriminoColorsFromCurrentPosition(&playfield, &tetrimino);
                        setPositionOfTetriminoViaCenter(&tetrimino, backup_row+1, backup_column);
                        flag = checkTetriminoPosition(&playfield, &tetrimino);

                        if (flag == -1){
                            setPositionOfTetriminoViaCenter(&tetrimino, backup_row, backup_column);
                        }
                        transferTetriminoColorsToPlayArea(&playfield, &tetrimino);

                        xSemaphoreGive(playfield.lock);
                        xSemaphoreGive(tetrimino.lock);

                        if (flag == 1){
                            xTimerReset(LockingTetriminoTimer, 0);
                        }

                        backup_row = 0; backup_column = 0;
                    }
                    xSemaphoreGive(playfield.lock);
                }
            }
            xSemaphoreGive(tetrimino.lock);
        }
        // vTaskDelayUntil(&last_change, 100);
    }
}

void vMoveTetriminoToTheRight(void *pvParameters){
    int backup_row = 0;
    int backup_column = 0;
    int flag = 0;

    TickType_t last_change = xTaskGetTickCount();

    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (xTaskGetTickCount()-last_change > BUTTON_DEBOUNCE_DELAY){
            last_change = xTaskGetTickCount();

            if (tetrimino.lock){
                if (xSemaphoreTake(tetrimino.lock, 0) == pdTRUE){

                    if (playfield.lock) {
                        if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){

                            backup_row = tetrimino.playfield_row;
                            backup_column = tetrimino.playfield_column;

                            removeTetriminoColorsFromCurrentPosition(&playfield, &tetrimino);
                            setPositionOfTetriminoViaCenter(&tetrimino, backup_row, backup_column+1);
                            flag = checkTetriminoPosition(&playfield, &tetrimino);

                            if (flag == -1){
                                setPositionOfTetriminoViaCenter(&tetrimino, backup_row, backup_column);
                            }
                            transferTetriminoColorsToPlayArea(&playfield, &tetrimino);

                            xSemaphoreGive(playfield.lock);
                            xSemaphoreGive(tetrimino.lock);

                            if (flag == 1){
                                xTimerReset(LockingTetriminoTimer, 0);
                            }
                            
                            backup_row = 0;
                            backup_column = 0;

                        }
                        xSemaphoreGive(playfield.lock);
                    }
                }
                xSemaphoreGive(tetrimino.lock);
            }
        }
    }
}

void vMoveTetriminoToTheLeft(void *pvParameters){
    int backup_row = 0;
    int backup_column = 0;
    int flag = 0;

    TickType_t last_change = xTaskGetTickCount();

    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (xTaskGetTickCount()-last_change > BUTTON_DEBOUNCE_DELAY){
            last_change = xTaskGetTickCount();

            if (tetrimino.lock){
                if (xSemaphoreTake(tetrimino.lock, 0) == pdTRUE){

                    if (playfield.lock) {
                        if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){

                            backup_row = tetrimino.playfield_row;
                            backup_column = tetrimino.playfield_column;

                            removeTetriminoColorsFromCurrentPosition(&playfield, &tetrimino);
                            setPositionOfTetriminoViaCenter(&tetrimino, backup_row, backup_column-1);
                            flag = checkTetriminoPosition(&playfield, &tetrimino);

                           if (flag == -1){
                                setPositionOfTetriminoViaCenter(&tetrimino, backup_row, backup_column);
                            }
                            transferTetriminoColorsToPlayArea(&playfield, &tetrimino);

                            xSemaphoreGive(playfield.lock);
                            xSemaphoreGive(tetrimino.lock);

                            if (flag == 1){
                                xTimerReset(LockingTetriminoTimer, 0);
                            }
                            
                            backup_row = 0;
                            backup_column = 0;
                        }
                        xSemaphoreGive(playfield.lock);
                    }
                }
                xSemaphoreGive(tetrimino.lock);
            }
        }
    } 
}

void makeTetriminoFallDownFast(void *pvParameters); // not ready yet!

void vRotateTetriminoCW(void *pvParameters){
    int backup_orientation = 0;
    int flag = 0;

    TickType_t last_change = xTaskGetTickCount();

    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (xTaskGetTickCount() - last_change > BUTTON_DEBOUNCE_DELAY){
            last_change = xTaskGetTickCount();

            if (tetrimino.lock){
                if (xSemaphoreTake(tetrimino.lock, 0) == pdTRUE){

                    if (playfield.lock) {
                        if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){
                            
                            backup_orientation = tetrimino.orientation;

                            removeTetriminoColorsFromCurrentPosition(&playfield, &tetrimino);
                            
                            if (orientation_table.lock){
                                if (xSemaphoreTake(orientation_table.lock, 0) == pdTRUE){
                                    setTetriminoGridViaOrientation(&orientation_table, &tetrimino, backup_orientation-1);
                                    setPositionOfTetriminoViaCenter(&tetrimino, tetrimino.playfield_row, tetrimino.playfield_column);
                                    flag = checkTetriminoPosition(&playfield, &tetrimino);

                                    if (flag == -1) {
                                        setTetriminoGridViaOrientation(&orientation_table, &tetrimino, backup_orientation);
                                    }
                                }
                                xSemaphoreGive(orientation_table.lock);
                            }

                            transferTetriminoColorsToPlayArea(&playfield, &tetrimino);

                            xSemaphoreGive(playfield.lock);
                            xSemaphoreGive(tetrimino.lock);

                            if (flag == 1){
                                xTimerReset(LockingTetriminoTimer, 0);
                            }

                            backup_orientation = 0;
                        }
                        xSemaphoreGive(playfield.lock);
                    }
                }
                xSemaphoreGive(tetrimino.lock);
            }            
        }
    }
}

void vRotateTetriminoCCW(void *pvParameters){
    int backup_orientation = 0;
    int flag = 0;

    TickType_t last_change = xTaskGetTickCount();

    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (xTaskGetTickCount() - last_change > BUTTON_DEBOUNCE_DELAY){
            last_change = xTaskGetTickCount();

            if (tetrimino.lock){
                if (xSemaphoreTake(tetrimino.lock, 0) == pdTRUE){

                    if (playfield.lock) {
                        if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){

                            backup_orientation = tetrimino.orientation;

                            removeTetriminoColorsFromCurrentPosition(&playfield, &tetrimino);

                            if (orientation_table.lock){
                                if (xSemaphoreTake(orientation_table.lock, 0) == pdTRUE){
                                    setTetriminoGridViaOrientation(&orientation_table, &tetrimino, backup_orientation-1);
                                    setPositionOfTetriminoViaCenter(&tetrimino, tetrimino.playfield_row, tetrimino.playfield_column);
                                    flag = checkTetriminoPosition(&playfield, &tetrimino);

                                    if (flag == -1) {
                                        setTetriminoGridViaOrientation(&orientation_table, &tetrimino, backup_orientation);
                                    }
                                }
                                xSemaphoreGive(orientation_table.lock);
                            }

                            transferTetriminoColorsToPlayArea(&playfield, &tetrimino);

                            xSemaphoreGive(playfield.lock);
                            xSemaphoreGive(tetrimino.lock);

                            if (flag == 1){
                                xTimerReset(LockingTetriminoTimer, 0);
                            }

                            backup_orientation = 0;
                        }
                        xSemaphoreGive(playfield.lock);
                    }
                }
                xSemaphoreGive(tetrimino.lock);
            }            
        }
    }
}

/* 
############## End of Functions for game manipulation #########################
##############################################################################
*/
void checkDraw(unsigned char status, const char *msg)
{
    if (status) {
        if (msg)
            fprintf(stderr, "[ERROR] %s, %s\n", msg,
                    tumGetErrorMessage());
        else {
            fprintf(stderr, "[ERROR] %s\n", tumGetErrorMessage());
        }
    }
}

#define FPS_AVERAGE_COUNT 50
#define FPS_FONT "IBMPlexSans-Bold.ttf"

void vDrawFPS(void)
{
    static unsigned int periods[FPS_AVERAGE_COUNT] = { 0 };
    static unsigned int periods_total = 0;
    static unsigned int index = 0;
    static unsigned int average_count = 0;
    static TickType_t xLastWakeTime = 0, prevWakeTime = 0;
    static char str[10] = { 0 };
    static int text_width;
    int fps = 0;
    font_handle_t cur_font = tumFontGetCurFontHandle();

    xLastWakeTime = xTaskGetTickCount();

    if (prevWakeTime != xLastWakeTime) {
        periods[index] =
            configTICK_RATE_HZ / (xLastWakeTime - prevWakeTime);
        prevWakeTime = xLastWakeTime;
    }
    else {
        periods[index] = 0;
    }

    periods_total += periods[index];

    if (index == (FPS_AVERAGE_COUNT - 1)) {
        index = 0;
    }
    else {
        index++;
    }

    if (average_count < FPS_AVERAGE_COUNT) {
        average_count++;
    }
    else {
        periods_total -= periods[index];
    }

    fps = periods_total / average_count;

    tumFontSelectFontFromName(FPS_FONT);

    sprintf(str, "FPS: %2d", fps);

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        checkDraw(tumDrawText(str,
                              (SCREEN_WIDTH / 2) - (text_width / 2),
                              SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 1.5,
                              Blue),
                  __FUNCTION__);

    tumFontSelectFontFromHandle(cur_font);
    tumFontPutFontHandle(cur_font);
}

void vSwapBuffers(void *pvParameters)
{
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t frameratePeriod = 20;

    tumDrawBindThread(); // Setup Rendering handle with correct GL context

    while (1) {
        if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
            tumDrawUpdateScreen();
            tumEventFetchEvents(FETCH_EVENT_NONBLOCK);
            xSemaphoreGive(ScreenLock);
            xSemaphoreGive(DrawSignal);
            vTaskDelayUntil(&xLastWakeTime,
                            pdMS_TO_TICKS(frameratePeriod));
        }
    }
}

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}

// State Machine Functionality ###################################

void vCheckForStateInput(void){
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){
        if (buttons.buttons[KEYCODE(E)]){
            buttons.buttons[KEYCODE(E)] = 0;
            if(StateMachineQueue){
                xQueueSend(StateMachineQueue, &next_state_signal, 0);
                xSemaphoreGive(buttons.lock);
            }
        }
    }
    xSemaphoreGive(buttons.lock);
}

void changingStateAfterInput(unsigned char *state, unsigned char queue_input){
    if(*state + queue_input >= NUMBER_OF_STATES){
        *state = STATE_PLAYING;
    }
    else{
        *state = *state + queue_input;
    }
}

void vSequentialStateMachine(void *pvParameters){
    unsigned char current_state = STARTING_STATE;
    unsigned char queue_input = 0;
    unsigned char change_state = 1;

    TickType_t last_state_change = xTaskGetTickCount();

    while(1){
        if(change_state != 0){
            goto state_handling;
        }

        if (StateMachineQueue){
            if (xQueueReceive(StateMachineQueue, &queue_input, portMAX_DELAY) == pdTRUE){
                if (xTaskGetTickCount() - last_state_change > STATE_CHANGE_DELAY){
                    changingStateAfterInput(&current_state, queue_input);
                    change_state = 1;
                    last_state_change = xTaskGetTickCount();
                }
            }
        }

state_handling:
        if(StateMachineQueue){
            if(change_state != 0){
                switch(current_state){

                    case STATE_PLAYING:
                        vTaskSuspend(StatePausedControlTask);

                        vTaskResume(StatePlayingControlTask);
                        vTaskResume(GenerateTetriminoPermutationsTask);
                        vTaskResume(SpawnTetriminoTask);
                        vTaskResume(MoveTetriminoOneDownTask);
                        vTaskResume(MoveTetriminoToTheRightTask);
                        vTaskResume(MoveTetriminoToTheLeftTask);
                        vTaskResume(RotateTetriminoCWTask);
                        vTaskResume(RotateTetriminoCCWTask);
                        break;

                    case STATE_PAUSED:
                        vTaskSuspend(StatePlayingControlTask);
                        vTaskSuspend(GenerateTetriminoPermutationsTask);
                        vTaskSuspend(SpawnTetriminoTask);
                        vTaskSuspend(MoveTetriminoOneDownTask);
                        vTaskSuspend(MoveTetriminoToTheRightTask);
                        vTaskSuspend(MoveTetriminoToTheLeftTask);
                        vTaskSuspend(RotateTetriminoCWTask);
                        vTaskSuspend(RotateTetriminoCCWTask);

                        vTaskResume(StatePausedControlTask);
                        break;

                    default:
                        printf("Default has been hit.\n");
                        break;
                }
                change_state = 0;
            }
        }
    }
}

// End of State Machine Functionality #######################################

void vStatePlayingControl(void *pvParameters){

    char text[50] = "Playing (Press E to change state)";
    int text_width = 0;
    tumGetTextSize((char *) text, &text_width, NULL);

    playfield = initPlayArea(&playfield);
    orientation_table = initOrientationTable(&orientation_table);

    xTaskNotifyGive(SpawnTetriminoTask);
    printf("Playing Control sent spawn request.\n");

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE) {

                tumEventFetchEvents(FETCH_EVENT_BLOCK | FETCH_EVENT_NO_GL_CHECK); // Query events backend for new events, ie. button presses
                xGetButtonInput(); // Update global input    
                xSemaphoreTake(ScreenLock, portMAX_DELAY);    

                tumDrawClear(Gray);

                /* Current game functionality */
                checkForButtonInput();

                if (playfield.lock){
                    if (xSemaphoreTake(playfield.lock, portMAX_DELAY) == pdTRUE){
                        drawPlayArea(&playfield);
                    }
                    xSemaphoreGive(playfield.lock);
                }
                xSemaphoreGive(playfield.lock);

                /* End of current game functionality */

                tumDrawText(text,SCREEN_WIDTH/2-text_width/2, SCREEN_HEIGHT-60, Lime);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
                vCheckForStateInput();
            }
        }
    }

}

void vStatePausedControl(void *pvParameters){

    char text[50] = "Paused (Press E to change state)";
    int text_width = 0;
    tumGetTextSize((char *) text, &text_width, NULL);

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE) {

                tumEventFetchEvents(FETCH_EVENT_BLOCK | FETCH_EVENT_NO_GL_CHECK); // Query events backend for new events, ie. button presses
                xGetButtonInput(); // Update global input    
                xSemaphoreTake(ScreenLock, portMAX_DELAY);    

                tumDrawClear(Gray);

                // Under construction: When returning to the playing state, the playarea keeps the tetrimino and a new one is spawned
                /*
                if (playfield.lock){
                    if (xSemaphoreTake(playfield.lock, portMAX_DELAY) == pdTRUE){
                        drawPlayArea(&playfield);
                    }
                    xSemaphoreGive(playfield.lock);
                }
                xSemaphoreGive(playfield.lock);
                */

                tumDrawText(text,SCREEN_WIDTH/2-text_width/2, SCREEN_HEIGHT-60, Orange);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
                vCheckForStateInput();
            }
        }
    }

}


int main(int argc, char *argv[])
{
    char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);
    printf("Path: %s\n", bin_folder_path);

    printf("Initializing: ");

    if (tumDrawInit(bin_folder_path)) {
        PRINT_ERROR("Failed to intialize drawing");
        goto err_init_drawing;
    }

    if (tumEventInit()) {
        PRINT_ERROR("Failed to initialize events");
        goto err_init_events;
    }

    if (tumSoundInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize audio");
        goto err_init_audio;
    }

    atexit(aIODeinit);

    DrawSignal = xSemaphoreCreateBinary(); // Screen buffer locking
    if (!DrawSignal) {
        PRINT_ERROR("Failed to create draw signal");
        goto err_draw_signal;
    }

    ScreenLock = xSemaphoreCreateMutex();
    if (!ScreenLock) {
        PRINT_ERROR("Failed to create screen lock");
        goto err_screen_lock;
    }

    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock.");
        goto err_buttons_lock;
    }

    playfield.lock = xSemaphoreCreateMutex();
    if (!playfield.lock) {
        PRINT_ERROR("Failed to create playfield lock.");
        goto err_playfield_lock;
    }

    tetrimino.lock = xSemaphoreCreateMutex();
    if (!tetrimino.lock) {
        PRINT_ERROR("Failed to create tetrimino lock.");
        goto err_tetrimino_lock;
    }

    orientation_table.lock = xSemaphoreCreateMutex();
    if (!orientation_table.lock){
        PRINT_ERROR("Failed to create orientation table lock");
        goto err_orientation_table_lock;
    }

    // Message sending
    StateMachineQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateMachineQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_state_queue;
    }

    TetriminoSelectionQueue = xQueueCreate(TETRIMINO_BAG_SIZE, sizeof(char));
    if (!TetriminoSelectionQueue){
        PRINT_ERROR("Coukd not open tetrimino selection queue.");
        goto err_selection_queue;
    }

    // Timers
    LockingTetriminoTimer = xTimerCreate("LockingTetriminoTimer", TETRIMINO_LOCKING_PERIOD, pdTRUE, (void*) 0, vLockingTetriminoIntoPlace);
    if (!LockingTetriminoTimer){
        PRINT_ERROR("Could not create locking timer");
        goto err_locking_timer;
    }

    // Tasks
    if (xTaskCreate(vSequentialStateMachine, "SequentialStateMachine",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES - 1, 
                    &SequentialStateMachine) != pdPASS) {
        PRINT_TASK_ERROR("StateMachine");
        goto err_statemachine;
    }

    if (xTaskCreate(vSwapBuffers, "BufferSwapTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES,
                    &BufferSwap) != pdPASS) {
        PRINT_TASK_ERROR("BufferSwapTask");
        goto err_bufferswap;
    }

    if (xTaskCreate(vStatePlayingControl, "StatePlayingControlTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-3,
                    &StatePlayingControlTask) != pdPASS) {
        PRINT_TASK_ERROR("StatePlayingControlTask");
        goto err_state_playing_control_task;
    }

    if (xTaskCreate(vStatePausedControl, "StatePausedControlTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-3,
                    &StatePausedControlTask) != pdPASS) {
        PRINT_TASK_ERROR("StatePausedControlTask");
        goto err_state_paused_control_task;
    } 

    if (xTaskCreate(vCalculateBagOfTetriminos, "GenerateTetriminoPermutationsTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY+1, 
                    &GenerateTetriminoPermutationsTask) != pdPASS) {
        PRINT_TASK_ERROR("GenerateTetriminoPermutationsTask");
        goto err_generate_permutations_task;
    } 

    if (xTaskCreate(vSpawnTetrimino, "SpawnTetriminoTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY, 
                    &SpawnTetriminoTask) != pdPASS) {
        PRINT_TASK_ERROR("SpawnTetriminoTask");
        goto err_spawn_tetrimino_task;
    } 

    if (xTaskCreate(vSafelyMoveTetriminoOneDown, "MoveTetriminoOneDownTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY, 
                    &MoveTetriminoOneDownTask) != pdPASS) {
        PRINT_TASK_ERROR("MoveTetriminoOneDownTask");
        goto err_move_one_down_task;
    } 

    if (xTaskCreate(vMoveTetriminoToTheRight, "MoveTetriminoToTheRightTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY, 
                    &MoveTetriminoToTheRightTask) != pdPASS) {
        PRINT_TASK_ERROR("MoveTetriminoToTheRightTask");
        goto err_move_tetrimino_right;
    } 

    if (xTaskCreate(vMoveTetriminoToTheLeft, "MoveTetriminoToTheLeftTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY, 
                    &MoveTetriminoToTheLeftTask) != pdPASS) {
        PRINT_TASK_ERROR("MoveTetriminoToTheLeftTask");
        goto err_move_tetrimino_left;
    }

    if (xTaskCreate(vRotateTetriminoCW, "RotateTetriminoCWTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY, 
                    &RotateTetriminoCWTask) != pdPASS) {
        PRINT_TASK_ERROR("RotateTetriminoCWTask");
        goto err_rotate_tetrimino_clockwise;
    }  

    if (xTaskCreate(vRotateTetriminoCCW, "RotateTetriminoCCWTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY, 
                    &RotateTetriminoCCWTask) != pdPASS) {
        PRINT_TASK_ERROR("RotateTetriminoCCWTask");
        goto err_rotate_tetrimino_counterclockwise;
    }      

    vTaskSuspend(GenerateTetriminoPermutationsTask);
    vTaskSuspend(SpawnTetriminoTask);
    vTaskSuspend(MoveTetriminoOneDownTask);
    vTaskSuspend(MoveTetriminoToTheRightTask);
    vTaskSuspend(MoveTetriminoToTheLeftTask);
    vTaskSuspend(RotateTetriminoCWTask);
    vTaskSuspend(RotateTetriminoCCWTask);

    // pongInit();

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_rotate_tetrimino_counterclockwise:
    vTaskDelete(RotateTetriminoCCWTask);
err_rotate_tetrimino_clockwise:
    vTaskDelete(RotateTetriminoCWTask);
err_move_tetrimino_left:
    vTaskDelete(MoveTetriminoToTheLeftTask);
err_move_tetrimino_right:
    vTaskDelete(MoveTetriminoToTheRightTask);
err_move_one_down_task:
    vTaskDelete(MoveTetriminoOneDownTask);
err_spawn_tetrimino_task:
    vTaskDelete(SpawnTetriminoTask);
err_generate_permutations_task:
    vTaskDelete(GenerateTetriminoPermutationsTask);

err_state_paused_control_task:
    vTaskDelete(StatePausedControlTask);
err_state_playing_control_task:
    vTaskDelete(StatePlayingControlTask);
err_bufferswap:
    vTaskDelete(BufferSwap);
err_statemachine:
    vTaskDelete(SequentialStateMachine);

err_locking_timer:  
    xTimerDelete(LockingTetriminoTimer, 0);

err_selection_queue:
    vQueueDelete(TetriminoSelectionQueue);
err_state_queue:
    vQueueDelete(StateMachineQueue);

err_orientation_table_lock:
    vSemaphoreDelete(orientation_table.lock);
err_tetrimino_lock:
    vSemaphoreDelete(tetrimino.lock);
err_playfield_lock:
    vSemaphoreDelete(playfield.lock);

err_buttons_lock:
    vSemaphoreDelete(buttons.lock);
err_screen_lock:
    vSemaphoreDelete(ScreenLock);
err_draw_signal:
    vSemaphoreDelete(DrawSignal);

err_init_audio:
    tumSoundExit();
err_init_events:
    tumEventExit();
err_init_drawing:
    tumDrawExit();
    return EXIT_FAILURE;
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vMainQueueSendPassed(void)
{
    /* This is just an example implementation of the "queue send" trace hook. */
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vApplicationIdleHook(void)
{
#ifdef __GCC_POSIX__
    struct timespec xTimeToSleep, xTimeSlept;
    /* Makes the process more agreeable when using the Posix simulator. */
    xTimeToSleep.tv_sec = 1;
    xTimeToSleep.tv_nsec = 0;
    nanosleep(&xTimeToSleep, &xTimeSlept);
#endif
}
