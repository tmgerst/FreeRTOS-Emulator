/**
 * @file tetris_functionality.h
 * @author Tim Gerstewitz
 * @date 3rd February 2021
 * @brief Header file for the functional parts of the tetris project, that is, button presses, statistics, not gameplay-related drawings, and generally
 * all things that don't have an immediate relation to the gameplay part of the project. Defines structs, and exposes tasks and functions to the other files.
 */

#ifndef __TETRIS_FUNCTIONALITY_H__
#define __TETRIS_FUNCTIONALITY_H__

#include "semphr.h"
#include "tetris_gameplay.h"

#define MAX_STARTING_LEVEL 19
#define BUTTON_DEBOUNCE_DELAY 100

#define SINGLE_MODE 1
#define DOUBLE_MODE 2

#define NUMBER_OF_GENERATOR_MODES 5

extern TaskHandle_t MainMenuTask;
extern TaskHandle_t TetrisStateSinglePlayingTask;
extern TaskHandle_t TetrisStateSinglePausedTask;
extern TaskHandle_t TetrisStateDoublePlayingTask;
extern TaskHandle_t TetrisStateDoublePausedTask;
extern TaskHandle_t GameOverScreenTask;

extern TaskHandle_t ResetGameTask;
extern TaskHandle_t ChangeLevelTask;
extern TaskHandle_t ChangePlayModeTask;
extern TaskHandle_t ChangeGeneratorModeTask;

extern TaskHandle_t UDPControlTask;

extern SemaphoreHandle_t DoubleModeNextSignal;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

/**
 * @brief Stores statistics information for the tetris game, ie current score, level and cleared lines.
 */
typedef struct stats{
    int current_score;
    int score_lookup_table[4];
    int level;
    int cleared_lines;
    int advance_level_lookup[MAX_STARTING_LEVEL+1];
    SemaphoreHandle_t lock;
} stats_t;

/**
 * @brief Stores the current play mode, so either single player or double player mode.
 */
typedef struct play_mode {
    unsigned char mode;
    SemaphoreHandle_t lock;
} play_mode_t;

/**
 * @brief Stores the top three high scores for the starting level that was selected at the beginning of the game.
 *  Is shared between single and double player mode.
 */
typedef struct high_scores{
    int starting_level;
    int score[MAX_STARTING_LEVEL+1][3]; // the three highest scores for each level will be displayed
    SemaphoreHandle_t lock;
} high_scores_t;

/**
 * @brief Stores the current mode the tetris generator is in (FAIR, EASY, HARD, ...) and whether the generator is active or not. 
 */
typedef struct current_gen_mode{
    char* mode;
    unsigned char generator_active;
    SemaphoreHandle_t lock;
} current_gen_mode_t;

/**
 * @brief Similar structure to the play_area struct, but smaller. A four by four array of tiles used to display the upcoming tetrimino 
 * during gameplay. 
 */
typedef struct next_tetrimino_display{
    tile_t display[4][4];
    SemaphoreHandle_t lock;
} next_tetrimino_display_t;

extern buttons_buffer_t buttons;
extern stats_t statistics;
extern play_mode_t play_mode;
extern high_scores_t high_scores;
extern current_gen_mode_t generator_mode;
extern next_tetrimino_display_t next_display;

/**
 * @brief Init function for the functionality part of the project. 
 * 
 * @return 0 on success, -1 otherwise.
 */
int tetrisFunctionalityInit(void);


#endif // __TETRIS_FUNCTIONALITY_H__