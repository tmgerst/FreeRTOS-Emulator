/**
 * @file tetris_gameplay.c
 * @author Tim Gerstewitz
 * @date 20th January 2021
 * @brief Header file for the solely gameplay related parts of the tetris project, that is, moving down of a tetrimino, check if a line can be cleared, 
 * check if a tetrimino is supported on the stack, et cetera.
 */

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


/**
 * @brief Constructs a square with a height, a width and a color. Both tetriminos and the play area as a whole consist of tiles.
 */
typedef struct tile {
    int height;
    int width;
    unsigned int color;
} tile_t;

/**
 * @brief Stores hardcoded information about the orientation of tetriminos, ie it is used as a look up table for a tetrimino rotation.
 */
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

/**
 * @brief Stores a two dimensional array of tiles that are used as the playfield for the tetris game. Also stores the height and width 
 * of the array in pixels for drawing.
 */
typedef struct play_area {
    tile_t tiles[PLAY_AREA_HEIGHT_IN_TILES][PLAY_AREA_WIDTH_IN_TILES];
    int size_x;
    int size_y;
    SemaphoreHandle_t lock;
} play_area_t;

/**
 * @brief Stores the information that is necessary to construct and use a tetrimino, ie the name, color, orientation and a 5 by 5 grid of tiles
 * to hold the actual shape of the tetrimino.
 */
typedef struct tetrimino {
    char name;
    unsigned int color;
    unsigned int grid[5][5];
    int playfield_row;      // row position of the tetrimino center in the play area 
    int playfield_column;    // column position of the tetrimino center in the play area
    int orientation;
    SemaphoreHandle_t lock;
} tetrimino_t;

/**
 * @brief Holds a lookup table for the drop speeds of tetriminos according to the level.
 */
typedef struct drop_speed_table{
    int drop_speeds[MAX_STARTING_LEVEL+1];
    SemaphoreHandle_t lock;
} drop_speed_table_t;

extern t_or_table_t orientation_table;
extern play_area_t playfield;
extern tetrimino_t tetrimino;
extern drop_speed_table_t drop_lookup;


/**
 * @brief Initializes a tile (does not have to be created before).
 *
 * @param color The color the tile is supposed to have.
 * @return A tile with the color that was passed in.
 */
tile_t initTile(unsigned int color);

/**
 * @brief Draws a tile to the screen.
 *
 * @param pos_x The x-position where the tile is drawn in pixels.
 * @param pos_y The y-position where the tile is drawn in pixels.
 * @param tile A pointer to the tile that is to be drawn.
 */
void drawTile(int pos_x, int pos_y, tile_t* tile);

/**
 * @brief Checks for gameplay-related input (the user has pressed either A, D, LEFT or RIGHT) and signals the button presses
 * to the appropriate tasks.
 */
void checkForGameInput(void);

/**
 * @brief Tranfers the colors and shape of the tetrimino to the correct location of the play area.
 * Has to be called with both the playfield and the tetrimino mutex obtained. 
 *
 * @param playfield A pointer to the currently used play area struct.
 * @param tetrimino A pointer to the currently used tetrimino.
 */
void transferTetriminoColorsToPlayArea(play_area_t* playfield, tetrimino_t* tetrimino);

/**
 * @brief Chooses the standard color for the tetrimino based on its shape.
 * 
 * @param name The shape of the tetrimino indicated by a character, ie 'T'.
 */
unsigned int chooseColorForTetrimino(char name);

/**
 * @brief Checks if any lines on the playfield are full. If this is the case, it clears these lines and moves down the playfield.
 * Has to be called with the play area mutex obtained.
 * 
 * @param playfield Pointer to the currently used play area.
 * @return The number of lines that were cleared.
 */
int clearFullyColoredLines(play_area_t* playfield);

/**
 * @brief Draws the playarea to the screen. Has to be called with the playarea mutex obtained.
 * 
 * @param playfield Pointer to the currently used play area.
 */
void drawPlayArea(play_area_t* playfield);

/**
 * @brief Resets all tiles of the play area to their original black color state.
 * 
 * @param playfield Pointer to the currently used play area.
 */
void clearPlayArea(play_area_t* playfield);

/**
 * @brief Init function for the gameplay part of the project. 
 * 
 * @return 0 on success, -1 otherwise.
 */
int tetrisGameplayInit(void);

#endif // __TETRIS_GAMEPLAY_H__