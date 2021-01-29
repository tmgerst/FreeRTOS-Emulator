/* 20.01.2020, Author: Tim Gerstewitz */
/* Source file for solely gameplay related code for the tetris project */

#include <stdlib.h>
#include <time.h>
#include <SDL2/SDL_scancode.h>

#include "TUM_Utils.h"
#include "TUM_Event.h"
#include "TUM_Font.h"
#include "TUM_Sound.h"
#include "TUM_Ball.h"
#include "TUM_Draw.h"

#include "AsyncIO.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "tetris_gameplay.h"
#include "main.h"
#include "queue.h"

#define BUTTON_DEBOUNCE_DELAY 100
#define LEVEL_SELECTION_QUEUE_SIZE 1

#define HEADLINE_POSITION 20

#define CHOICE_PLAY_MODE_TEXT_POSITION_Y 80
#define PLAY_MODE_BUTTONS_OFFSET_X 170
#define PLAY_MODE_BUTTONS_POSITION_Y 110
#define PLAY_MODE_BUTTONS_WIDTH 150
#define PLAY_MODE_BUTTONS_HEIGHT 30
#define PLAY_MODES_TEXT_OFFSET_X 10

#define LEVEL_SELECTION_TEXT_POSITION_Y 170
#define LEVEL_SELECTION_BOX_POSITION_Y 200
#define LEVEL_SELECTION_BOX_WIDTH 80
#define LEVEL_SELECTION_BOX_HEIGHT 30

// gameplay relevant constants
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

#define MAX_STARTING_LEVEL 19

TaskHandle_t MainMenuTask = NULL;
TaskHandle_t TetrisStatePlayingTask = NULL;
TaskHandle_t TetrisStatePausedTask = NULL;
TaskHandle_t GameOverScreenTask = NULL;

TaskHandle_t GenerateTetriminoPermutationsTask = NULL;
TaskHandle_t SpawnTetriminoTask = NULL;

TaskHandle_t MoveTetriminoOneDownTask = NULL;
TaskHandle_t MoveTetriminoToTheRightTask = NULL;
TaskHandle_t MoveTetriminoToTheLeftTask = NULL;
TaskHandle_t RotateTetriminoCWTask = NULL;
TaskHandle_t RotateTetriminoCCWTask = NULL;

TaskHandle_t ResetGameTask = NULL;
TaskHandle_t ChangeLevelTask = NULL;
TaskHandle_t ChangePlayModeTask = NULL;

static xTimerHandle LockingTetriminoTimer = NULL;

static QueueHandle_t TetriminoSelectionQueue = NULL;
static QueueHandle_t LevelChangingQueue = NULL;

SemaphoreHandle_t ScreenLock = NULL;
SemaphoreHandle_t DrawSignal = NULL;

static SemaphoreHandle_t GenerateBagSignal = NULL;
static SemaphoreHandle_t SpawnSignal = NULL;
static SemaphoreHandle_t ResetGameSignal = NULL;
static SemaphoreHandle_t ChangePlayModeSignal = NULL;

static SemaphoreHandle_t MoveRightSignal = NULL;
static SemaphoreHandle_t MoveLeftSignal = NULL;
static SemaphoreHandle_t RotateCWSignal = NULL;
static SemaphoreHandle_t RotateCCWSignal = NULL;

const int increment_level = 1;
const int decrement_level = -1;


typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

typedef struct tile {
    int height;
    int width;
    unsigned int color;
} tile_t;

typedef struct t_or_table{
    int or_T[4][4][2];  // 4 configurations of 4 points (2 coordinates) that need to be colored
    int or_J[4][4][2];
    int or_Z[2][4][2];
    int or_O[1][4][2];
    int or_S[2][4][2];
    int or_L[4][4][2];
    int or_I[2][4][2];
    SemaphoreHandle_t lock;
} t_or_table_t;

typedef struct play_area {
    tile_t tiles[PLAY_AREA_HEIGHT_IN_TILES][PLAY_AREA_WIDTH_IN_TILES];
    int size_x;
    int size_y;
    SemaphoreHandle_t lock;
} play_area_t;

typedef struct tetrimino {
    char name;
    unsigned int color;
    unsigned int grid[5][5];
    int playfield_row;      // row position of the tetrimino center in the play area 
    int playfield_column;    // column position of the tetrimino center in the play area
    int orientation;
    SemaphoreHandle_t lock;
} tetrimino_t;

typedef struct stats{
    int current_score;
    int score_lookup_table[4];
    int level;
    int cleared_lines;
    int advance_level_lookup[MAX_STARTING_LEVEL+1];
    SemaphoreHandle_t lock;
} stats_t;

typedef struct drop_speed_table{
    int drop_speeds[MAX_STARTING_LEVEL+1];
    SemaphoreHandle_t lock;
} drop_speed_table_t;

typedef struct play_mode {
    unsigned char mode;
    SemaphoreHandle_t lock;
} play_mode_t;


buttons_buffer_t buttons = { 0 };
t_or_table_t orientation_table = { 0 };
play_area_t playfield = { 0 };
tetrimino_t tetrimino = { 0 };
stats_t statistics = { 0 };
drop_speed_table_t drop_lookup = { 0 };
play_mode_t play_mode = { 0 };

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

stats_t initStatistics(stats_t* statistics){
    statistics->current_score = 0;
    statistics->cleared_lines = 0;
    statistics->level = 0;

    statistics->score_lookup_table[0] = 40;     // points for clearing one line at level zero
    statistics->score_lookup_table[1] = 100;    // points for clearing two lines at level zero
    statistics->score_lookup_table[2] = 300;    // points for clearing three lines at level zero
    statistics->score_lookup_table[3] = 1200;   // points for clearing four lines at level zero --> a TETRIS

    for (int i = 0; i <= MAX_STARTING_LEVEL; i++){
        statistics->advance_level_lookup[i] = (i+1) * 10;   // ie for advancing from level 8 to 9 the player needs 
                                                            // to clear 90 lines regardless of the starting level
    }

    return *statistics;
}

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

play_mode_t initPlayMode(play_mode_t* pm){
    pm->mode = 1;   // set initial play mode to single-player
    return *pm;
}



void drawTile(int pos_x, int pos_y, tile_t* colored_tile){
    tumDrawFilledBox(pos_x, pos_y, colored_tile->width, colored_tile->height, colored_tile->color);
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

void drawPlayArea(play_area_t* p){
    int drawing_position_x = 0;
    int drawing_position_y = 0;

    for (int r = 0; r < PLAY_AREA_HEIGHT_IN_TILES; r++){
        for(int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){
            drawing_position_x = PLAY_AREA_POSITION_X + c*TILE_WIDTH;
            drawing_position_y = PLAY_AREA_POSITION_Y + r*TILE_HEIGHT;
            drawTile(drawing_position_x, drawing_position_y, &(p->tiles[r][c]));
        }
    }
}

void clearPlayArea(play_area_t* p){
    for (int r = 0; r < PLAY_AREA_HEIGHT_IN_TILES; r++){
        for (int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){
            p->tiles[r][c].color = 0;
        }
    }
}



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



void drawStatistics(stats_t* statistics){
    char score_text[10] = "SCORE";
    int score_text_width = 0; tumGetTextSize((char *) score_text, &score_text_width, NULL);
    char level_text[10] = "LEVEL";
    int level_text_width = 0; tumGetTextSize((char *) level_text, &level_text_width, NULL);
    char lines_text[10] = "LINES";
    int lines_text_width = 0; tumGetTextSize((char *) lines_text, &lines_text_width, NULL);
    
    char score_buffer[10] = { 0 };
    char level_buffer[10] = { 0 };
    char lines_buffer[10] = { 0 };

    sprintf(score_buffer, "%10i", statistics->current_score);
    sprintf(level_buffer, "%10i", statistics->level);
    sprintf(lines_buffer, "%10i", statistics->cleared_lines);

    tumDrawText(score_text, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+20, 
                20, White);
    tumDrawText(score_buffer, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+20, 
                35, White);

    tumDrawText(level_text, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+20, 
                80, White);
    tumDrawText(level_buffer, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+20, 
                95, White);

    tumDrawText(lines_text, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+20, 
                140, White);
    tumDrawText(lines_buffer, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+20, 
                155, White);
}



void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
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

void checkForFunctionalityInput(void){
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){

        // If P has been pressed pause/resume the game
        if(buttons.buttons[KEYCODE(P)]){
            buttons.buttons[KEYCODE(P)] = 0;  

            if (StateMachineQueue){

                if (current_state.lock){
                    if (xSemaphoreTake(current_state.lock, 0) == pdTRUE){

                        if (getCurrentState(&current_state) == single_playing_signal){
                            xQueueSend(StateMachineQueue, &single_paused_signal, 0);
                            xSemaphoreGive(current_state.lock);
                        }
                        if (getCurrentState(&current_state) == single_paused_signal){
                            xQueueSend(StateMachineQueue, &single_playing_signal, 0);
                            xSemaphoreGive(current_state.lock);
                        }
                    }
                    xSemaphoreGive(current_state.lock);
                }
            }
            xSemaphoreGive(buttons.lock);
        }

        // if R has been pressed, reset the game
        if (buttons.buttons[KEYCODE(R)]){
            buttons.buttons[KEYCODE(R)] = 0;
            xSemaphoreGive(ResetGameSignal);
            xSemaphoreGive(buttons.lock);
        }

        // if E has been pressed, exit to main menu
        if(buttons.buttons[KEYCODE(E)]){
            buttons.buttons[KEYCODE(E)] = 0;  

            if (StateMachineQueue){
                xQueueSend(StateMachineQueue, &main_menu_signal, 0);
            }
            xSemaphoreGive(buttons.lock);
        }        
    }
    xSemaphoreGive(buttons.lock);
}

void vCheckForMainMenuInput(void){

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){
        // if ENTER has been pressed in the main menu, start playing
        if(buttons.buttons[KEYCODE(RETURN)]){
            buttons.buttons[KEYCODE(RETURN)] = 0;        
            if (StateMachineQueue){
                xSemaphoreGive(ResetGameSignal);
                xQueueSend(StateMachineQueue, &single_playing_signal, 1);
                xSemaphoreGive(buttons.lock);
            }
        }

        // if UP has been pressed in the main menu, increment level
        if (buttons.buttons[KEYCODE(UP)]){
            buttons.buttons[KEYCODE(UP)] = 0;
            if (LevelChangingQueue){
                xQueueSend(LevelChangingQueue, &increment_level, 0);
                xSemaphoreGive(buttons.lock);
            }
        }

        // if DOWN has been pressed in the main menu, decrement level
        if (buttons.buttons[KEYCODE(DOWN)]){
            buttons.buttons[KEYCODE(DOWN)] = 0;
            if (LevelChangingQueue){
                xQueueSend(LevelChangingQueue, &decrement_level, 0);
                xSemaphoreGive(buttons.lock);
            }
        }

        // if M has been pressed in the main menu, change play mode
        if (buttons.buttons[KEYCODE(M)]){
            buttons.buttons[KEYCODE(M)] = 0;
            xSemaphoreGive(ChangePlayModeSignal);
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
        if (GenerateBagSignal){
            if (xSemaphoreTake(GenerateBagSignal, portMAX_DELAY) == pdTRUE){

                printf("Permutation request received.\n");
                memcpy(names, tetriminos, 7); // set array again with names of tetriminos

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

void vSpawnTetrimino(void *pvParameters){
    char name_buffer = 0;
    unsigned int color = 0;
    int flag = 0;

    while(1){
        if (SpawnSignal){
            if (xSemaphoreTake(SpawnSignal, portMAX_DELAY) == pdTRUE){

                printf("Spawn request received.\n");

                if (uxQueueMessagesWaiting(TetriminoSelectionQueue) == 0){
                    xSemaphoreGive(GenerateBagSignal);  // Send signal to generate a new permutation
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
                            printf("Activate Timer.\n");    // if tetrimino was supported in previous position activate timer once
                            if (xTimerIsTimerActive(LockingTetriminoTimer) == pdFALSE){
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

void vLockingTetriminoIntoPlace(void *pvParameters){
    printf("Send locking.\n");
    xTaskNotifyGive(TetrisStatePlayingTask);
    xTimerStop(LockingTetriminoTimer, 0);
}



void vResetGame(void *pvParameters){

    TickType_t last_change = xTaskGetTickCount();

    while(1){
        if (ResetGameSignal){
            if (xSemaphoreTake(ResetGameSignal, portMAX_DELAY) == pdTRUE){

                if (xTaskGetTickCount() - last_change > BUTTON_DEBOUNCE_DELAY){
                    last_change = xTaskGetTickCount();

                    printf("Reset game.\n");

                    // Reset gamefield
                    if (playfield.lock){
                        if (xSemaphoreTake(playfield.lock, portMAX_DELAY) == pdTRUE){
                            clearPlayArea(&playfield);
                        }
                        xSemaphoreGive(playfield.lock);
                    }
                    xSemaphoreGive(playfield.lock);

                    // Reset statistics
                    if (statistics.lock){
                        if (xSemaphoreTake(statistics.lock, portMAX_DELAY) == pdTRUE){
                            statistics.cleared_lines = 0;
                            statistics.current_score = 0;
                        }
                        xSemaphoreGive(statistics.lock);
                    }
                    xSemaphoreGive(statistics.lock);

                    if (StateMachineQueue){
                        xQueueSend(StateMachineQueue, &single_playing_signal, 0);
                    }

                    // Spawn new tetrimino
                    xSemaphoreGive(GenerateBagSignal);
                    xSemaphoreGive(SpawnSignal);
                }
            }
        }
    }
}

void vChangeLevel(void *pvParameters){
    int level_change_buffer = 0;
    TickType_t last_change = xTaskGetTickCount();

    while(1){
        if (xTaskGetTickCount() - last_change > BUTTON_DEBOUNCE_DELAY){
            last_change = xTaskGetTickCount();

            if (LevelChangingQueue){
                if (xQueueReceive(LevelChangingQueue, &level_change_buffer, portMAX_DELAY) == pdTRUE){
                    printf("Level change: %i\n", level_change_buffer);

                    if (statistics.lock){
                        if (xSemaphoreTake(statistics.lock, portMAX_DELAY) == pdTRUE){

                            if (statistics.level + level_change_buffer > MAX_STARTING_LEVEL){
                                statistics.level = 0;
                            }
                            else if (statistics.level + level_change_buffer < 0){
                                statistics.level = MAX_STARTING_LEVEL;
                            }
                            else{
                                statistics.level = statistics.level + level_change_buffer;
                            }

                            xSemaphoreGive(statistics.lock);
                        }
                        xSemaphoreGive(statistics.lock);
                    }
                }
            }
        }
    }
}

void vChangePlayMode(void *pvParameters){
    TickType_t last_change = xTaskGetTickCount();

    while(1){
        if(ChangePlayModeSignal){
            if (xSemaphoreTake(ChangePlayModeSignal, portMAX_DELAY) == pdTRUE){

                if (xTaskGetTickCount() - last_change > BUTTON_DEBOUNCE_DELAY){
                    last_change = xTaskGetTickCount();

                    if (play_mode.lock){
                        if (xSemaphoreTake(play_mode.lock, portMAX_DELAY) == pdTRUE){

                            if (play_mode.mode == 1){
                                play_mode.mode = 2;  // change play mode to two-player mode
                                printf("Play mode: %i\n", play_mode.mode);
                            }
                            else if (play_mode.mode == 2){
                                play_mode.mode = 1; // change play mode to single-player mode
                                printf("Play mode: %i\n", play_mode.mode);
                            }
                            xSemaphoreGive(play_mode.lock);
                        }
                        xSemaphoreGive(play_mode.lock);
                    }
                
                }
            }
        }
    }
}



void vMainMenu(void *pvParameters){
    unsigned int text_color_single_player = Gray;
    unsigned int text_color_two_player = Gray;

    char headline_text[50] = "Tetris - Main Menu";
    int headline_text_width = 0;
    tumGetTextSize((char *) headline_text, &headline_text_width, NULL);

    char play_mode_headline_text[50] = "Choose your play mode ( [M] ):";
    int play_mode_headline_text_width = 0;
    tumGetTextSize((char*) play_mode_headline_text, &play_mode_headline_text_width, NULL);

    char single_player_mode_text[30] = "Single-Player Mode";
    int single_player_mode_text_width = 0;
    tumGetTextSize((char *) single_player_mode_text, &single_player_mode_text_width, NULL);

    char two_player_mode_text[30] = "Two-Player Mode";
    int two_player_mode_text_width = 0;
    tumGetTextSize((char *) two_player_mode_text, &two_player_mode_text_width, NULL);

    char press_enter_text[70] = "Press [Enter] to start a game with the selected options.";
    int press_enter_text_width = 0;
    tumGetTextSize((char *) press_enter_text, &press_enter_text_width, NULL);

    char level_selection_text[50] = "Choose your starting level ( [UP] / [DOWN] ):";
    int level_selection_text_width = 0;
    tumGetTextSize((char *) level_selection_text, &level_selection_text_width, NULL);

    char selected_level[5] = { 0 };
    int selected_level_width = 0;

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE){

                xGetButtonInput();
                vCheckForMainMenuInput();

                xSemaphoreTake(ScreenLock, portMAX_DELAY);

                // Draw Background
                tumDrawClear(Gray);
                tumDrawFilledBox(PLAY_AREA_POSITION_X, PLAY_AREA_POSITION_Y, PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH, 
                            PLAY_AREA_HEIGHT_IN_TILES*TILE_HEIGHT, Black);

                // Draw Headline
                tumDrawText(headline_text,SCREEN_WIDTH/2-headline_text_width/2, HEADLINE_POSITION, TUMBlue);

                if (play_mode.lock){
                    if (xSemaphoreTake(play_mode.lock, 0) == pdTRUE){
                        if (play_mode.mode == 1){
                            text_color_single_player = Green;
                            text_color_two_player = Gray;
                            tumDrawBox(SCREEN_WIDTH/2-PLAY_MODE_BUTTONS_OFFSET_X-1, PLAY_MODE_BUTTONS_POSITION_Y-1, PLAY_MODE_BUTTONS_WIDTH+3, 
                                        PLAY_MODE_BUTTONS_HEIGHT+3, Lime);
                        }
                        else{
                            text_color_single_player = Gray;
                            text_color_two_player = Green;
                            tumDrawBox(SCREEN_WIDTH/2+PLAY_MODE_BUTTONS_OFFSET_X-PLAY_MODE_BUTTONS_WIDTH-1, PLAY_MODE_BUTTONS_POSITION_Y-1, 
                                        PLAY_MODE_BUTTONS_WIDTH+2, PLAY_MODE_BUTTONS_HEIGHT+3, Lime);
                        }
                        xSemaphoreGive(play_mode.lock);
                    }
                    xSemaphoreGive(play_mode.lock);
                }

                // Draw play mode options
                tumDrawText(play_mode_headline_text, SCREEN_WIDTH/2-play_mode_headline_text_width/2, CHOICE_PLAY_MODE_TEXT_POSITION_Y, Lime);

                tumDrawFilledBox(SCREEN_WIDTH/2-PLAY_MODE_BUTTONS_OFFSET_X, PLAY_MODE_BUTTONS_POSITION_Y, PLAY_MODE_BUTTONS_WIDTH, 
                                PLAY_MODE_BUTTONS_HEIGHT, White);
                tumDrawText(single_player_mode_text, SCREEN_WIDTH/2-PLAY_MODE_BUTTONS_OFFSET_X/2-single_player_mode_text_width/2-PLAY_MODES_TEXT_OFFSET_X,
                                PLAY_MODE_BUTTONS_POSITION_Y+PLAY_MODE_BUTTONS_HEIGHT/5, text_color_single_player);

                tumDrawFilledBox(SCREEN_WIDTH/2+PLAY_MODE_BUTTONS_OFFSET_X-PLAY_MODE_BUTTONS_WIDTH, PLAY_MODE_BUTTONS_POSITION_Y, PLAY_MODE_BUTTONS_WIDTH, 
                                PLAY_MODE_BUTTONS_HEIGHT, White);
                tumDrawText(two_player_mode_text, SCREEN_WIDTH/2+PLAY_MODE_BUTTONS_OFFSET_X/2-two_player_mode_text_width/2+PLAY_MODES_TEXT_OFFSET_X,
                                PLAY_MODE_BUTTONS_POSITION_Y+PLAY_MODE_BUTTONS_HEIGHT/5, text_color_two_player);

                // Draw level selection options
                tumDrawText(level_selection_text, SCREEN_WIDTH/2-level_selection_text_width/2, LEVEL_SELECTION_TEXT_POSITION_Y, Orange);
                tumDrawFilledBox(SCREEN_WIDTH/2-LEVEL_SELECTION_BOX_WIDTH/2, LEVEL_SELECTION_BOX_POSITION_Y, LEVEL_SELECTION_BOX_WIDTH, 
                                LEVEL_SELECTION_BOX_HEIGHT, White);
                tumDrawBox(SCREEN_WIDTH/2-LEVEL_SELECTION_BOX_WIDTH/2-1, LEVEL_SELECTION_BOX_POSITION_Y-1, LEVEL_SELECTION_BOX_WIDTH+3, 
                                LEVEL_SELECTION_BOX_HEIGHT+3, Orange);
                
                // Get current level
                if (statistics.lock){
                    if (xSemaphoreTake(statistics.lock, 0) == pdTRUE){
                        sprintf(selected_level, "%2u", statistics.level);
                        xSemaphoreGive(statistics.lock);
                    }
                    xSemaphoreGive(statistics.lock);
                }

                // Display current level in the box
                if(!tumGetTextSize((char *)selected_level, &selected_level_width, NULL))
                    tumDrawText(selected_level, SCREEN_WIDTH/2-selected_level_width/2, LEVEL_SELECTION_BOX_POSITION_Y+LEVEL_SELECTION_BOX_HEIGHT/5, Orange);

                // Draw Text for starting the game
                tumDrawText(press_enter_text, SCREEN_WIDTH/2-press_enter_text_width/2, SCREEN_HEIGHT-60, White);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }
        }
    }
}

void vTetrisStatePlaying(void *pvParameters){

    char text[50] = "Playing Single-Player Mode";
    int text_width = 0;
    tumGetTextSize((char *) text, &text_width, NULL);

    int lines_to_clear = 0;

    xSemaphoreGive(SpawnSignal);
    printf("Initial spawn request.\n");

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE){

                xGetButtonInput();
                checkForGameInput();
                checkForFunctionalityInput();

                xSemaphoreTake(ScreenLock, portMAX_DELAY);

                tumDrawClear(Gray);

                // if locking request has been received
                if (xTaskNotifyStateClear(NULL) == pdTRUE){

                    if(tetrimino.lock){
                        if (xSemaphoreTake(tetrimino.lock, 0) == pdTRUE){
                            
                            if (playfield.lock){
                                if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){
                                    transferTetriminoColorsToPlayArea(&playfield, &tetrimino);
                                    lines_to_clear = clearFullyColoredLines(&playfield);
                                    xSemaphoreGive(playfield.lock);
                                    xSemaphoreGive(tetrimino.lock);
                                    printf("Spawn request.\n");
                                    xSemaphoreGive(SpawnSignal);
                                    xTimerStop(LockingTetriminoTimer, 0);
                                }
                            }
                            xSemaphoreGive(playfield.lock);
                        }
                    }
                    xSemaphoreGive(tetrimino.lock);

                    if (lines_to_clear > 0){
                        if (statistics.lock){
                            if (xSemaphoreTake(statistics.lock, 0) == pdTRUE){
                                statistics.cleared_lines += lines_to_clear;
                                statistics.current_score += statistics.score_lookup_table[lines_to_clear-1] * (statistics.level+1);
                                
                                // Check if the level needs to be incremented
                                if (statistics.cleared_lines >= statistics.advance_level_lookup[statistics.level]){
                                    statistics.level++;
                                    xSemaphoreGive(statistics.lock);
                                }
                            }
                            xSemaphoreGive(statistics.lock);
                        }
                    }

                }

                if (playfield.lock){
                    if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){
                        drawPlayArea(&playfield);
                        xSemaphoreGive(playfield.lock);
                    }
                    xSemaphoreGive(playfield.lock);
                }

                if (statistics.lock){
                    if (xSemaphoreTake(statistics.lock, 0) == pdTRUE){
                        drawStatistics(&statistics);
                        xSemaphoreGive(statistics.lock);
                    }
                    xSemaphoreGive(statistics.lock);
                }

                tumDrawText(text,SCREEN_WIDTH/2-text_width/2, SCREEN_HEIGHT-60, Lime);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }
        }
    }
}

void vTetrisStatePaused(void *pvParameters){

    char text[50] = "Paused Single-Player Mode";
    int text_width = 0;
    tumGetTextSize((char *) text, &text_width, NULL);

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE){

                xGetButtonInput();
                checkForGameInput();
                checkForFunctionalityInput();

                xSemaphoreTake(ScreenLock, portMAX_DELAY);

                tumDrawClear(Gray);

                if (playfield.lock){
                    if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){
                        drawPlayArea(&playfield);
                        xSemaphoreGive(playfield.lock);
                    }
                    xSemaphoreGive(playfield.lock);
                }

                if (statistics.lock){
                    if (xSemaphoreTake(statistics.lock, 0) == pdTRUE){
                        drawStatistics(&statistics);
                        xSemaphoreGive(statistics.lock);
                    }
                    xSemaphoreGive(statistics.lock);
                }

                tumDrawText(text,SCREEN_WIDTH/2-text_width/2, SCREEN_HEIGHT-60, Orange);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }
        }
    }
}

void vGameOverScreen(void *pvParameters){
    char text[50] = "Game over!";
    int text_width = 0;
    tumGetTextSize((char *) text, &text_width, NULL);

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE){

                xGetButtonInput();
                checkForFunctionalityInput();

                xSemaphoreTake(ScreenLock, portMAX_DELAY);

                tumDrawClear(Gray);
                tumDrawFilledBox(PLAY_AREA_POSITION_X, PLAY_AREA_POSITION_Y, PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH, 
                            PLAY_AREA_HEIGHT_IN_TILES*TILE_HEIGHT, Black);

                if (statistics.lock){
                    if (xSemaphoreTake(statistics.lock, 0) == pdTRUE){
                        drawStatistics(&statistics);
                        xSemaphoreGive(statistics.lock);
                    }
                    xSemaphoreGive(statistics.lock);
                }

                tumDrawText(text,SCREEN_WIDTH/2-text_width/2, SCREEN_HEIGHT/2, Orange);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }
        }
    }
}



int tetrisInit(void){

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
        PRINT_ERROR("Failed to create orientation table lock.");
        goto err_orientation_table_lock;
    }

    statistics.lock = xSemaphoreCreateMutex();
    if (!statistics.lock){
        PRINT_ERROR("Failed to create statistics lock.");
        goto err_statistics_lock;
    }

    drop_lookup.lock = xSemaphoreCreateMutex();
    if (!drop_lookup.lock){
        PRINT_ERROR("Faield to create drop lookup lock.");
        goto err_drop_lookup_lock;
    }

    play_mode.lock = xSemaphoreCreateMutex();
    if (!play_mode.lock){
        PRINT_ERROR("Failed to create play mode lock.");
        goto err_play_mode_lock;
    }

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

    ResetGameSignal = xSemaphoreCreateBinary();
    if (!ResetGameSignal){
        PRINT_ERROR("Failed to create reset game signal.");
        goto err_reset_game_signal;
    }

    ChangePlayModeSignal = xSemaphoreCreateBinary();
    if (!ChangePlayModeSignal){
        PRINT_ERROR("Failed to create change game mode signal.");
        goto err_change_play_mode_signal;
    }

    // Message sending
    TetriminoSelectionQueue = xQueueCreate(TETRIMINO_BAG_SIZE, sizeof(char));
    if (!TetriminoSelectionQueue){
        PRINT_ERROR("Could not open tetrimino selection queue.");
        goto err_selection_queue;
    }

    LevelChangingQueue = xQueueCreate(LEVEL_SELECTION_QUEUE_SIZE, sizeof(int));
    if (!LevelChangingQueue){
        PRINT_ERROR("Could not open level selection queue.");
        goto err_level_queue;
    }

    // Timers
    LockingTetriminoTimer = xTimerCreate("LockingTetriminoTimer", TETRIMINO_LOCKING_PERIOD, pdTRUE, (void*) 0, vLockingTetriminoIntoPlace);
    if (!LockingTetriminoTimer){
        PRINT_ERROR("Could not create locking timer");
        goto err_locking_timer;
    }

    // Tasks
    if (xTaskCreate(vMainMenu, "MainMenuTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-2, 
                    &MainMenuTask) != pdPASS) {
        PRINT_TASK_ERROR("MainMenuTask");
        goto err_main_menu_task;
    } 

    if (xTaskCreate(vTetrisStatePlaying, "TetrisStatePlayingTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-2, 
                    &TetrisStatePlayingTask) != pdPASS) {
        PRINT_TASK_ERROR("TetrisStatePlayingTask");
        goto err_tetris_state_playing_task;
    } 

    if (xTaskCreate(vTetrisStatePaused, "TetrisStatePausedTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-2, 
                    &TetrisStatePausedTask) != pdPASS) {
        PRINT_TASK_ERROR("TetrisStatePausedTask");
        goto err_tetris_state_paused_task;
    } 

    if (xTaskCreate(vGameOverScreen, "GameOverScreenTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-2, 
                    &GameOverScreenTask) != pdPASS) {
        PRINT_TASK_ERROR("GameOverScreenTask");
        goto err_game_over_screen_task;
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

    if (xTaskCreate(vResetGame, "ResetGameTask", 
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY+2, 
                    &ResetGameTask) != pdPASS) {
        PRINT_TASK_ERROR("ResetGameTask");
        goto err_reset_game_task;
    }   

    if (xTaskCreate(vChangeLevel, "ChangeLevelTask", 
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY+1,
                    &ChangeLevelTask) != pdPASS){
        PRINT_TASK_ERROR("ChangeLevelTask");
        goto err_change_level_task;
    }   

    if (xTaskCreate(vChangePlayMode, "ChangePlayModeTask", 
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY+1, 
                    &ChangePlayModeTask) != pdPASS){
        PRINT_TASK_ERROR("ChangePlayModeTask");
        goto err_change_play_mode_task;
    }

    vTaskSuspend(MainMenuTask);
    vTaskSuspend(TetrisStatePlayingTask);
    vTaskSuspend(TetrisStatePausedTask);
    vTaskSuspend(GameOverScreenTask);

    vTaskSuspend(GenerateTetriminoPermutationsTask);
    vTaskSuspend(SpawnTetriminoTask);
    vTaskSuspend(MoveTetriminoOneDownTask);
    vTaskSuspend(MoveTetriminoToTheRightTask);
    vTaskSuspend(MoveTetriminoToTheLeftTask);
    vTaskSuspend(RotateTetriminoCWTask);
    vTaskSuspend(RotateTetriminoCCWTask);

    vTaskSuspend(ResetGameTask);
    vTaskSuspend(ChangeLevelTask);
    vTaskSuspend(ChangePlayModeTask);

    orientation_table = initOrientationTable(&orientation_table);
    playfield = initPlayArea(&playfield);
    statistics = initStatistics(&statistics);
    drop_lookup = initDropLookUpTable(&drop_lookup);
    play_mode = initPlayMode(&play_mode);

    srand(time(NULL));

    return 0;

err_change_play_mode_task:
    vTaskDelete(ChangePlayModeTask);
err_change_level_task:
    vTaskDelete(ChangeLevelTask);
err_reset_game_task:
    vTaskDelete(ResetGameTask);
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
err_game_over_screen_task:
    vTaskDelete(GameOverScreenTask);
err_tetris_state_paused_task:
    vTaskDelete(TetrisStatePausedTask);
err_tetris_state_playing_task:
    vTaskDelete(TetrisStatePlayingTask);
err_main_menu_task:
    vTaskDelete(MainMenuTask);

err_locking_timer:  
    xTimerDelete(LockingTetriminoTimer, 0);

err_level_queue:
    vQueueDelete(LevelChangingQueue);
err_selection_queue:
    vQueueDelete(TetriminoSelectionQueue);

err_change_play_mode_signal:
    vSemaphoreDelete(ChangePlayModeSignal);
err_reset_game_signal:
    vSemaphoreDelete(ResetGameSignal);
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

err_play_mode_lock:
    vSemaphoreDelete(play_mode.lock);
err_drop_lookup_lock:
    vSemaphoreDelete(drop_lookup.lock);
err_statistics_lock:
    vSemaphoreDelete(statistics.lock);
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

    return -1;
}




