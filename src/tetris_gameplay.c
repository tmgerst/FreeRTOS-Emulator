/**
 * @file tetris_gameplay.c
 * @author Tim Gerstewitz
 * @date 20th January 2021
 * @brief Source file for the solely gameplay related parts of the tetris project, that is, moving down of a tetrimino, check if a line can be cleared, 
 * check if a tetrimino is supported on the stack, et cetera.
 */

#include <stdlib.h>
#include <time.h>
#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "TUM_Utils.h"
#include "TUM_Event.h"
#include "TUM_Font.h"
#include "TUM_Sound.h"
#include "TUM_Ball.h"
#include "TUM_Draw.h"

#include "AsyncIO.h"

#include "tetris_gameplay.h"
#include "tetris_functionality.h"
#include "main.h"
#include "queue.h"


TaskHandle_t GenerateTetriminoPermutationsTask = NULL;
TaskHandle_t SpawnTetriminoTask = NULL;

TaskHandle_t MoveTetriminoOneDownTask = NULL;
TaskHandle_t MoveTetriminoToTheRightTask = NULL;
TaskHandle_t MoveTetriminoToTheLeftTask = NULL;
TaskHandle_t RotateTetriminoCWTask = NULL;
TaskHandle_t RotateTetriminoCCWTask = NULL;

xTimerHandle LockingTetriminoTimer = NULL;

QueueHandle_t TetriminoSelectionQueue = NULL;

SemaphoreHandle_t ScreenLock = NULL;
SemaphoreHandle_t DrawSignal = NULL;

SemaphoreHandle_t GenerateBagSignal = NULL;
SemaphoreHandle_t SpawnSignal = NULL;

static SemaphoreHandle_t MoveRightSignal = NULL;
static SemaphoreHandle_t MoveLeftSignal = NULL;
static SemaphoreHandle_t RotateCWSignal = NULL;
static SemaphoreHandle_t RotateCCWSignal = NULL;


t_or_table_t orientation_table = { 0 };
play_area_t playfield = { 0 };
tetrimino_t tetrimino = { 0 };
drop_speed_table_t drop_lookup = { 0 };


// Two required function prototypes for the init functions


void clearTetriminoGrid(tetrimino_t* t);
void setTetriminoGridViaOrientation(t_or_table_t* or, tetrimino_t* t, int desired_orientation);


tile_t initTile(unsigned int color){
    tile_t new_tile;
    new_tile.height = TILE_HEIGHT;
    new_tile.width = TILE_WIDTH;
    new_tile.color = color;
    return new_tile;
}

/**
 * @brief Initializes a previously created, empty orientation table struct.
 *
 * @param or A pointer to a previously created, empty orientation table object.
 * @return The orientation table object that was passed in, filled with the orientations of all tetrimino types.
 */
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

    return *or;
}

/**
 * @brief Initializes a previously created, empty playarea struct.
 *
 * @param playarea A pointer to a previously created, empty playarea object.
 * @return The playarea object that was passed in, with the size set and all tiles colored black.
 */
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

/**
 * @brief Initializes a previously created, empty tetrimino struct.
 *
 * @param or A pointer to an already initialized orientation table object.
 * @param t A pointer to a previously created, empty tetrimino object.
 * @param name The desired shape of the tetrimino as a char, ie 'T'.
 * @param color The desired color of the tetrimino.
 * @return The playarea object that was passed in, with the size set and all tiles colored black.
 */
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

/**
 * @brief Initializes a previously created, empty drop speed table struct.
 *
 * @param d A pointer to a previously created, empty drop speed table object.
 * @return The drop speed table object that was passed in, filled with the drop speeds for each level.
 */
drop_speed_table_t initDropLookUpTable(drop_speed_table_t* d){
    // Drop speed of a tetrimmino encoded as milliseconds per drop
    // Taken from meatfighter.com/nintendotetrisai and slightly changed to fit the 50Hz of the Emulator
    d->drop_speeds[0] = 800; d->drop_speeds[1] = 717;
    d->drop_speeds[2] = 633; d->drop_speeds[3] = 550;
    d->drop_speeds[4] = 467; d->drop_speeds[5] = 383;
    d->drop_speeds[6] = 300; d->drop_speeds[7] = 217;
    d->drop_speeds[8] = 133; d->drop_speeds[9] = 100;

    d->drop_speeds[10] = 83; d->drop_speeds[11] = 83;
    d->drop_speeds[12] = 83; d->drop_speeds[13] = 67;
    d->drop_speeds[14] = 67; d->drop_speeds[15] = 67;
    d->drop_speeds[16] = 50; d->drop_speeds[17] = 50;
    d->drop_speeds[18] = 50; d->drop_speeds[19] = 33;

    return *d;
}



void drawTile(int pos_x, int pos_y, tile_t* colored_tile){
    tumDrawFilledBox(pos_x, pos_y, colored_tile->width, colored_tile->height, colored_tile->color);
}


/**
 * @brief Prints the play area colors to the console for debugging purposes.
 *
 * @param p A pointer to the currently used play area.
 */
void printPlayArea(play_area_t* p){
    printf("Playfield:\n");
    for (int r = 0; r < PLAY_AREA_HEIGHT_IN_TILES; r++){
        for (int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){
            printf("%8x", p->tiles[r][c].color);
        }
        printf("\n");
    }
}

void drawPlayArea(play_area_t* playfield){
    int drawing_position_x = 0;
    int drawing_position_y = 0;

    for (int r = 0; r < PLAY_AREA_HEIGHT_IN_TILES; r++){
        for(int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){
            drawing_position_x = PLAY_AREA_POSITION_X + c*TILE_WIDTH;
            drawing_position_y = PLAY_AREA_POSITION_Y + r*TILE_HEIGHT;
            drawTile(drawing_position_x, drawing_position_y, &(playfield->tiles[r][c]));
        }
    }
}

void clearPlayArea(play_area_t* playfield){
    for (int r = 0; r < PLAY_AREA_HEIGHT_IN_TILES; r++){
        for (int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){
            playfield->tiles[r][c].color = 0;
        }
    }
}


/**
 * @brief Sets all tiles' color in the tetrimino grid to black.
 *
 * @param t A pointer to the currently used tetrimino object.
 */
void clearTetriminoGrid(tetrimino_t* t){
    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            t->grid[r][c] = 0;
        }
    }
}

/**
 * @brief Helper function that copies an orientation from the orientation table to the terimino's grid, effectively rotating it.
 *
 * @param array A pointer to the array that holds the desired orientation of the tetrimino. Therefore the array pointed to consists
 * of 4 points with 2 coordinates each.
 * @param t A pointer to the tetrimino currently in use.
 * @param desired_orientation The index to the orientation in the orientation table. 
 */
void copyOrientationIntoTetriminoGrid(int array[][4][2], tetrimino_t* t, int desired_orientation){
        t->grid[array[desired_orientation][0][0]][array[desired_orientation][0][1]] = t->color;
        t->grid[array[desired_orientation][1][0]][array[desired_orientation][1][1]] = t->color;
        t->grid[array[desired_orientation][2][0]][array[desired_orientation][2][1]] = t->color;
        t->grid[array[desired_orientation][3][0]][array[desired_orientation][3][1]] = t->color;

        t->orientation = desired_orientation;
}

/**
 * @brief Sets the tetrimino's orientation, used for rotation
 *
 * @param or Pointer to an already initialized orientation tabel struct.
 * @param t A pointer to the tetrimino currently in use.
 * @param desired_orientation The index to the orientation in the orientation table. 
 */
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

/**
 * @brief Prints all parameters of a tetrimino to the console. Used for debugging.
 *
 * @param t A pointer to the tetrimino currently in use.
 */
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

/**
 * @brief Converts the tetriminos' tiles' row positions in its grid to playfield row coordinates, via the playfield row 
 * parameter of the tetrimino struct, which refers to the center of the tetrimino grid. To get all tiles in the grid to the playarea,
 * an offset is used.
 *
 * @param t A pointer to the tetrimino currently in use.
 * @param offset An integer, to get non-center tiles of the tetrimino grid to the playarea.
 */
int tetriminoRowToPlayfieldRow(tetrimino_t* t, int offset){
    return t->playfield_row - TETRIMINO_GRID_CENTER + offset;
}

/**
 * @brief Converts the tetriminos' tiles' column positions in its grid to playfield row coordinates, via the playfield column 
 * parameter of the tetrimino struct, which refers to the center of the tetrimino grid. To get all tiles in the grid to the playarea,
 * an offset is used.
 *
 * @param t A pointer to the tetrimino currently in use.
 * @param offset An integer, to get non-center tiles of the tetrimino grid to the playarea.
 */
int tetriminoColumnToPlayfieldColumn(tetrimino_t* t, int offset){
    return t->playfield_column - TETRIMINO_GRID_CENTER + offset;
}



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

/**
 * @brief Sets the tetrimino's playfield_row and playfield_center parameters (that detail where the center of the tetrimino is 
 * currently on the playfield) to a desired position.
 *
 * @param t A pointer to the tetrimino currently in use.
 * @param playfield_row The row of the playfield where the tetrimino should be.
 * @param playfield_column The colunm of the playfield where the tetrimino should be.
 */
void setPositionOfTetriminoViaCenter(tetrimino_t* t, int playfield_row, int playfield_column){
    t->playfield_row = playfield_row;
    t->playfield_column = playfield_column;
}

/**
 * @brief Checks if a tetrimino's position on the playfield is valid, ie if it is obstructed by playfield bounds
 * or other tetriminos.
 *
 * @param p A pointer to the play area currently in use.
 * @param t A pointer to the tetrimmino currently in use.
 * @return -1 if the position is invalid,
 * 0 if the position is valid and the position is unstable,
 * 1 if the position is valid and stable (tetrimino supported on the stack)
 */
int checkTetriminoPosition(play_area_t* p, tetrimino_t* t){
    int corresponding_p_row = 0;
    int corresponding_p_column = 0;

    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            if (t->grid[r][c] != 0){

                corresponding_p_row = tetriminoRowToPlayfieldRow(t, r);
                corresponding_p_column = tetriminoColumnToPlayfieldColumn(t, c);
                // printf("Computed playfield position: %i %i\n", corresponding_p_row, corresponding_p_column);

                // Check if the space is already occupied 
                if (p->tiles[corresponding_p_row][corresponding_p_column].color != 0){
                    printf("Position already occupied.\n");
                    return -1;
                }

                // Tetrimino is above or under the play area --> error
                if ((corresponding_p_row < 0) || (corresponding_p_row >= PLAY_AREA_HEIGHT_IN_TILES)){
                    printf("Row error.\n");
                    // Error handler, ie failed screen
                    return -1;
                }

                // Check if the Tetrimino crosses the bounds of the playarea to the left or right --> game over
                if ((corresponding_p_column < 0 || (corresponding_p_column >= PLAY_AREA_WIDTH_IN_TILES))){
                    printf("Column out of bounds.\n");
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

                if (corresponding_p_row+1 == PLAY_AREA_HEIGHT_IN_TILES){
                    printf("Piece supported by ground.\n");
                    return 1;
                }

                if (p->tiles[corresponding_p_row+1][corresponding_p_column].color != 0){
                    printf("Position stable.\n");
                    return 1;
                }
            }
        }
    }
    
    // scenario: The tetrimino shifts from a stable position into an unstable one --> timer has to be stopped
    if (xTimerIsTimerActive(LockingTetriminoTimer) == pdTRUE){
        printf("Stop Timer.\n");
        xTimerStop(LockingTetriminoTimer, 0);
    }

    printf("New position valid.\n");
    return 0;
}

/**
 * @brief Removes the tetriminos colors from the current position in the play area, effectively making the squares where the tetrimino
 * used to be black again. Used for moving the tetrimino.
 *
 * @param playfield A pointer to the play area struct currently in use.
 * @param tetrimino A pointer to the tetrimino struct currently in use.
 */
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
        case 'J': return TUMBlue;
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

int clearFullyColoredLines(play_area_t* playfield){
    int offset = 0;
    int flag = 0;
    printf("Call to line clearing function.\n");
    for (int r = PLAY_AREA_HEIGHT_IN_TILES-1; r >= 0; r--){

        if (r == offset-1){
            break;
        }  

        for (int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){

            playfield->tiles[r+offset][c].color = playfield->tiles[r][c].color;   // copy down colors
            if (playfield->tiles[r+offset][c].color == 0){
                flag = 1;   // if a non colored tile is in the row, raise flag to indicate it is noto fully colored
            }
        }

        if (flag == 0){ // if row is fully colored
        printf("Line to clear: %i\n", r);
            offset++;   // raise the number of lines that have been discovered
            printf("Offset: %i\n", offset);
        }
        flag = 0;               
    }

    if (offset > 0){
        for (int i = 0; i < offset; i++){
            for (int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){
                playfield->tiles[i][c].color = 0;    // color the rows at the top that are now empty because some lines have fallen down 
            }
        }
    }
    return offset;
}



void checkForGameInput(void){
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){

        // if A has been pressed, call moveToLeft
        if(buttons.buttons[KEYCODE(A)]){
            buttons.buttons[KEYCODE(A)] = 0;
            xSemaphoreGive(MoveLeftSignal);
            xSemaphoreGive(buttons.lock);
        }

        // if D has been pressed, call moveToRight
        if (buttons.buttons[KEYCODE(D)]){
            buttons.buttons[KEYCODE(D)] = 0;
            xSemaphoreGive(MoveRightSignal);
            xSemaphoreGive(buttons.lock);
        }

        // if Left has been pressed, call rotateLeft
        if (buttons.buttons[KEYCODE(LEFT)]){
            buttons.buttons[KEYCODE(LEFT)] = 0;
            xSemaphoreGive(RotateCWSignal);
            xSemaphoreGive(buttons.lock);
        }

        // if Right has been pressed, call rotateRight
        if (buttons.buttons[KEYCODE(RIGHT)]){
            buttons.buttons[KEYCODE(RIGHT)] = 0;
            xSemaphoreGive(RotateCCWSignal);
            xSemaphoreGive(buttons.lock);
        }

    }
    xSemaphoreGive(buttons.lock);
}


/**
 * @brief Builds the calculate bag of tetriminos task. Generates a sequence of random tetrimino names
 * of size seven, which is used for spawning during the single player mode. 
 *
 * @param pvParameters Current resources the RTOS provides.
 */
void vCalculateBagOfTetriminos(void *pvParameters){
    char names[TETRIMINO_BAG_SIZE] = { 0 };
    char tetriminos[TETRIMINO_BAG_SIZE] = { 'T', 'J', 'Z', 'O', 'S', 'L', 'I' };
    int position = 0;

    while(1){
        if (GenerateBagSignal){
            if (xSemaphoreTake(GenerateBagSignal, portMAX_DELAY) == pdTRUE){

                printf("Permutation request received.\n");
                memcpy(names, tetriminos, TETRIMINO_BAG_SIZE); // set array again with names of tetriminos

                for (int i = 0; i < TETRIMINO_BAG_SIZE; i++){

random_number_again:
                    position = rand() % TETRIMINO_BAG_SIZE;

                    if (names[position] != 0){
                        printf("Set position: %i\n", position);
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
    }
}

/**
 * @brief Implements the spawn task for tetriminos.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vSpawnTetrimino(void *pvParameters){
    char name_buffer = 0;
    unsigned int color = 0;
    int flag = 0;

    while(1){
        if (SpawnSignal){
            if (xSemaphoreTake(SpawnSignal, portMAX_DELAY) == pdTRUE){

                printf("Spawn request received.\n");

                if (uxQueueMessagesWaiting(TetriminoSelectionQueue) <= 1){
                    if (play_mode.lock){
                        if (xSemaphoreTake(play_mode.lock, 0) == pdTRUE){
                            
                            if (play_mode.mode == SINGLE_MODE){
                                xSemaphoreGive(play_mode.lock);
                                xSemaphoreGive(GenerateBagSignal);  // Send signal to generate a new permutation
                            }
                            else if (play_mode.mode == DOUBLE_MODE){
                                xSemaphoreGive(play_mode.lock);
                                printf("Spawn task gives signal for generator.\n");
                                xSemaphoreGive(DoubleModeNextSignal);
                            }
                        }
                        xSemaphoreGive(play_mode.lock);
                    }
                }

                if (xQueueReceive(TetriminoSelectionQueue, &name_buffer, TETRIMINO_QUEUE_RECEIVE_DELAY) == pdTRUE){
                    // if the spawn task receives something, the generator is active. This assumption can also be done in the single player mode,
                    // since it does no damage there and the generator's activeness is checked additionally when entering the two player mode.
                    if (generator_mode.lock){
                        if (xSemaphoreTake(generator_mode.lock, 0) == pdTRUE){
                            generator_mode.generator_active = 1;
                            xSemaphoreGive(generator_mode.lock);
                        }
                        xSemaphoreGive(generator_mode.lock);
                    }
                }

                printf("Spawn task received letter: %c\n", name_buffer);

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

                        name_buffer = 0;

                        if (playfield.lock) {
                            if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){

                                setPositionOfTetriminoViaCenter(&tetrimino, SPAWN_ROW, SPAWN_COLUMN);
                                flag = checkTetriminoPosition(&playfield, &tetrimino);
                                printf("Flag: %i\n", flag);
                                // in case position spawn position is valid
                                if (flag != -1){
                                    transferTetriminoColorsToPlayArea(&playfield, &tetrimino);
                                }

                                xSemaphoreGive(playfield.lock);
                                xSemaphoreGive(tetrimino.lock);

                                // if position is invalid, then the game is over 
                                if(flag == -1){
                                    if (StateMachineQueue){
                                        xQueueSend(StateMachineQueue, &game_over_signal, 0);
                                    }
                                }
                                flag = 0;
                            }
                            xSemaphoreGive(playfield.lock);
                        }
                    }
                    xSemaphoreGive(tetrimino.lock);
                }
            }
        }
    }
}

/**
 * @brief Moves down a tetrimino by one and checks if the position is valid.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vSafelyMoveTetriminoOneDown(void *pvParameters){

    int backup_row = 0;
    int backup_column = 0;
    int flag = 0;
    int drop_delay = 0;

    while(1){
        if (statistics.lock){
            if (xSemaphoreTake(statistics.lock, 0) == pdTRUE){

                if (drop_lookup.lock){
                    if (xSemaphoreTake(drop_lookup.lock, 0) == pdTRUE){

                        if (statistics.level <= MAX_STARTING_LEVEL){
                            drop_delay = drop_lookup.drop_speeds[statistics.level]; // get drop delay for current level
                        }
                        else{
                            drop_delay = drop_lookup.drop_speeds[MAX_STARTING_LEVEL];
                        }
                        xSemaphoreGive(drop_lookup.lock);
                        xSemaphoreGive(statistics.lock);
                    }
                    xSemaphoreGive(drop_lookup.lock);
                }
            }
            xSemaphoreGive(statistics.lock);
        }
        printf("Drop delay: %i\n", drop_delay);


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
                            // if tetrimino was supported in previous position activate timer once
                            if (xTimerIsTimerActive(LockingTetriminoTimer) == pdFALSE){
                                printf("Reset Timer.\n");
                                xTimerReset(LockingTetriminoTimer, 0);
                            }
                        }
                        transferTetriminoColorsToPlayArea(&playfield, &tetrimino);

                        xSemaphoreGive(playfield.lock);
                        xSemaphoreGive(tetrimino.lock);

                        backup_row = 0; backup_column = 0;
                    }
                    xSemaphoreGive(playfield.lock);
                }
            }
            xSemaphoreGive(tetrimino.lock);
        }  
        vTaskDelay(drop_delay);
    }
}

/**
 * @brief Moves a tetrimino one field to the right, triggers once the approriate button has been pressed.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vMoveTetriminoToTheRight(void *pvParameters){
    int backup_row = 0;
    int backup_column = 0;
    int flag = 0;
    TickType_t last_change = xTaskGetTickCount();

    while(1){
        if(MoveRightSignal){
            if (xSemaphoreTake(MoveRightSignal, portMAX_DELAY) == pdTRUE){

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
    }
}

/**
 * @brief Moves a tetrimino one field to the left, triggers once the approriate button has been pressed.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vMoveTetriminoToTheLeft(void *pvParameters){
    int backup_row = 0;
    int backup_column = 0;
    int flag = 0; 
    TickType_t last_change = xTaskGetTickCount();

    while(1){

        if (MoveLeftSignal){
            if (xSemaphoreTake(MoveLeftSignal, portMAX_DELAY) == pdTRUE) {
        
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
    } 
}

/**
 * @brief Rotates a tetrimino clockwise one time, triggers once the approriate button has been pressed.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vRotateTetriminoCW(void *pvParameters){
    int backup_orientation = 0;
    int flag = 0;

    TickType_t last_change = xTaskGetTickCount();

    while(1){

        if (RotateCWSignal){
            if (xSemaphoreTake(RotateCWSignal, portMAX_DELAY) == pdTRUE){
        
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
    }
}

/**
 * @brief Rotates a tetrimino counterclockwise one time, triggers once the approriate button has been pressed.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vRotateTetriminoCCW(void *pvParameters){
    int backup_orientation = 0;
    int flag = 0;
    TickType_t last_change = xTaskGetTickCount();

    while(1){

        if (RotateCCWSignal){
            if (xSemaphoreTake(RotateCCWSignal, portMAX_DELAY) == pdTRUE){

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
    }
}

/**
 * @brief Callback function for the tetrimino locking timer. Once the timer is over, the function finally transfers the tetrimino 
 * to the playfield and requests a new tetrimino from the spawn task.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vLockingTetriminoIntoPlace(void *pvParameters){
    printf("Send locking.\n");
    if (play_mode.lock){
        if (xSemaphoreTake(play_mode.lock, 0) == pdTRUE){

            if (play_mode.mode == SINGLE_MODE){
                xTaskNotifyGive(TetrisStateSinglePlayingTask);
                xTimerStop(LockingTetriminoTimer, 0);
            }
            if (play_mode.mode == DOUBLE_MODE){
                xTaskNotifyGive(TetrisStateDoublePlayingTask);
                xTimerStop(LockingTetriminoTimer, 0);
            }
            xSemaphoreGive(play_mode.lock);
        }
        xSemaphoreGive(play_mode.lock);
    }
}



int tetrisGameplayInit(void){

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
        PRINT_ERROR("Failed to create orientation table lock.");
        goto err_orientation_table_lock;
    }

    drop_lookup.lock = xSemaphoreCreateMutex();
    if (!drop_lookup.lock){
        PRINT_ERROR("Faield to create drop lookup lock.");
        goto err_drop_lookup_lock;
    }

    // Binary Semaphores for signaling
    GenerateBagSignal = xSemaphoreCreateBinary();
    if (!GenerateBagSignal){
        PRINT_ERROR("Failed to create generate bag signal.");
        goto err_generate_bag_signal;
    }

    SpawnSignal = xSemaphoreCreateBinary();
    if (!SpawnSignal){
        PRINT_ERROR("Failed to create spawn signal.");
        goto err_spawn_signal;
    }

    MoveRightSignal = xSemaphoreCreateBinary();
    if (!MoveRightSignal){
        PRINT_ERROR("Failed to move right signal.");
        goto err_move_right_signal;
    }

    MoveLeftSignal = xSemaphoreCreateBinary();
    if (!MoveLeftSignal){
        PRINT_ERROR("Failed to move left signal.");
        goto err_move_left_signal;
    }

    RotateCWSignal = xSemaphoreCreateBinary();
    if (!RotateCWSignal){
        PRINT_ERROR("Failed to create rotate clockwise signal.");
        goto err_rotate_clockwise_signal;
    }

    RotateCCWSignal = xSemaphoreCreateBinary();
    if (!RotateCCWSignal){
        PRINT_ERROR("Failed to create rotate counterclockwise signal.");
        goto err_rotate_counterclockwise_signal;
    }

    // Message sending
    TetriminoSelectionQueue = xQueueCreate(TETRIMINO_BAG_SIZE+1, sizeof(char));
    if (!TetriminoSelectionQueue){
        PRINT_ERROR("Could not open tetrimino selection queue.");
        goto err_selection_queue;
    }

    // Timers
    LockingTetriminoTimer = xTimerCreate("LockingTetriminoTimer", TETRIMINO_LOCKING_PERIOD, pdTRUE, (void*) 0, vLockingTetriminoIntoPlace);
    if (!LockingTetriminoTimer){
        PRINT_ERROR("Could not create locking timer");
        goto err_locking_timer;
    }

    // Tasks 
    if (xTaskCreate(vCalculateBagOfTetriminos, "GenerateTetriminoPermutationsTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY+2, 
                    &GenerateTetriminoPermutationsTask) != pdPASS) {
        PRINT_TASK_ERROR("GenerateTetriminoPermutationsTask");
        goto err_generate_permutations_task;
    } 

    if (xTaskCreate(vSpawnTetrimino, "SpawnTetriminoTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY+1, 
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

    orientation_table = initOrientationTable(&orientation_table);
    playfield = initPlayArea(&playfield);
    drop_lookup = initDropLookUpTable(&drop_lookup);

    srand(time(NULL));

    return 0;

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

err_locking_timer:  
    xTimerDelete(LockingTetriminoTimer, 0);

err_selection_queue:
    vQueueDelete(TetriminoSelectionQueue);

err_rotate_counterclockwise_signal:
    vSemaphoreDelete(RotateCCWSignal);
err_rotate_clockwise_signal:
    vSemaphoreDelete(RotateCWSignal);
err_move_left_signal:
    vSemaphoreDelete(MoveLeftSignal);
err_move_right_signal:
    vSemaphoreDelete(MoveRightSignal);
err_spawn_signal:
    vSemaphoreDelete(SpawnSignal);
err_generate_bag_signal:
    vSemaphoreDelete(GenerateBagSignal);

err_drop_lookup_lock:
    vSemaphoreDelete(drop_lookup.lock);
err_orientation_table_lock:
    vSemaphoreDelete(orientation_table.lock);
err_tetrimino_lock:
    vSemaphoreDelete(tetrimino.lock);
err_playfield_lock:
    vSemaphoreDelete(playfield.lock);

err_screen_lock:
    vSemaphoreDelete(ScreenLock);
err_draw_signal:
    vSemaphoreDelete(DrawSignal);

    return -1;
}




