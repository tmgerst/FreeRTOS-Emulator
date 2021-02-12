// Created 03.02.2021, Author: Tim Gerstewitz
// Header file for the functional part of the tetris project, ie state tasks, button inputs, state transitions, etc.

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

typedef struct stats{
    int current_score;
    int score_lookup_table[4];
    int level;
    int cleared_lines;
    int advance_level_lookup[MAX_STARTING_LEVEL+1];
    SemaphoreHandle_t lock;
} stats_t;

typedef struct play_mode {
    unsigned char mode;
    SemaphoreHandle_t lock;
} play_mode_t;

typedef struct high_scores{
    int starting_level;
    int score[MAX_STARTING_LEVEL+1][3]; // the three highest scores for each level will be displayed
    SemaphoreHandle_t lock;
} high_scores_t;

typedef struct current_gen_mode{
    char* mode;
    unsigned char generator_active;
    SemaphoreHandle_t lock;
} current_gen_mode_t;

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


int tetrisFunctionalityInit(void);


#endif // __TETRIS_FUNCTIONALITY_H__