#ifndef __TETRIS_GAMEPLAY_H__
#define __TETRIS_GAMEPLAY_H__

#include "semphr.h"

#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)

#define MAX_STARTING_LEVEL 19

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

#define TETRIMINO_QUEUE_RECEIVE_DELAY 2



extern TaskHandle_t GenerateTetriminoPermutationsTask;
extern TaskHandle_t SpawnTetriminoTask;
extern TaskHandle_t MoveTetriminoOneDownTask;
extern TaskHandle_t MoveTetriminoToTheRightTask;
extern TaskHandle_t MoveTetriminoToTheLeftTask;
extern TaskHandle_t RotateTetriminoCWTask;
extern TaskHandle_t RotateTetriminoCCWTask;

extern SemaphoreHandle_t SpawnSignal;
extern SemaphoreHandle_t GenerateBagSignal;

extern QueueHandle_t TetriminoSelectionQueue;

extern xTimerHandle LockingTetriminoTimer;



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

typedef struct drop_speed_table{
    int drop_speeds[MAX_STARTING_LEVEL+1];
    SemaphoreHandle_t lock;
} drop_speed_table_t;

extern t_or_table_t orientation_table;
extern play_area_t playfield;
extern tetrimino_t tetrimino;
extern drop_speed_table_t drop_lookup;



int tetrisGameplayInit(void);

tile_t initTile(unsigned int color);
void drawTile(int pos_x, int pos_y, tile_t* tile);

void checkForGameInput(void);

void transferTetriminoColorsToPlayArea(play_area_t* playfield, tetrimino_t* tetrimino);
unsigned int chooseColorForTetrimino(char name);

int clearFullyColoredLines(play_area_t* playfield);
void drawPlayArea(play_area_t* playfield);
void clearPlayArea(play_area_t* playfield);

#endif // __TETRIS_GAMEPLAY_H__