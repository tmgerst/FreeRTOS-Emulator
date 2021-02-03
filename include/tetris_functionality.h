// Created 03.02.2021, Author: Tim Gerstewitz
// Header file for the functional part of the tetris project, ie state tasks, button inputs, state transitions, etc.

#ifndef __TETRIS_FUNCTIONALITY_H__
#define __TETRIS_FUNCTIONALITY_H__

#include "semphr.h"

#define MAX_STARTING_LEVEL 19
#define BUTTON_DEBOUNCE_DELAY 100

extern TaskHandle_t MainMenuTask;
extern TaskHandle_t TetrisStatePlayingTask;
extern TaskHandle_t TetrisStatePausedTask;
extern TaskHandle_t GameOverScreenTask;

extern TaskHandle_t ResetGameTask;
extern TaskHandle_t ChangeLevelTask;
extern TaskHandle_t ChangePlayModeTask;

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

extern buttons_buffer_t buttons;
extern stats_t statistics;
extern play_mode_t play_mode;
extern high_scores_t high_scores;


int tetrisFunctionalityInit(void);


#endif // __TETRIS_FUNCTIONALITY_H__