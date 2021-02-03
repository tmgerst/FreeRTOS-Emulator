// Created: 03.02.2021, Author: Tim Gerstewitz
// Source file for the functional part of the tetris project, ie state tasks, button inputs, state transitions, etc.

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

#define LEVEL_SELECTION_QUEUE_SIZE 1



TaskHandle_t MainMenuTask = NULL;
TaskHandle_t TetrisStatePlayingTask = NULL;
TaskHandle_t TetrisStatePausedTask = NULL;
TaskHandle_t GameOverScreenTask = NULL;

TaskHandle_t ResetGameTask = NULL;
TaskHandle_t ChangeLevelTask = NULL;
TaskHandle_t ChangePlayModeTask = NULL;

static SemaphoreHandle_t ResetGameSignal = NULL;
static SemaphoreHandle_t ChangePlayModeSignal = NULL;

static QueueHandle_t LevelChangingQueue = NULL;



const int increment_level = 1;
const int decrement_level = -1;

buttons_buffer_t buttons = { 0 };
stats_t statistics = { 0 };
play_mode_t play_mode = { 0 };
high_scores_t high_scores = { 0 };



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

play_mode_t initPlayMode(play_mode_t* pm){
    pm->mode = 1;   // set initial play mode to single-player
    return *pm;
}

high_scores_t initHighScores(high_scores_t* hs){
    hs->starting_level = 0;
    for (int i = 0; i <= MAX_STARTING_LEVEL; i++){
        hs->score[i][0] = 0;
        hs->score[i][1] = 0;
        hs->score[i][2] = 0;
    }
    return *hs;
}


// Needs to be called with both the statistics mutex and the high scores mutex obtained!
void updateHighScores(stats_t* statistics, high_scores_t* high_scores){

    // check if third highest high score needs to be updated
    if (statistics->current_score >= high_scores->score[high_scores->starting_level][2] &&
        statistics->current_score < high_scores->score[high_scores->starting_level][1]){
            high_scores->score[high_scores->starting_level][2] = statistics->current_score;
    }

    // check if second highest high score needs to be updated
    if (statistics->current_score >= high_scores->score[high_scores->starting_level][1] &&
        statistics->current_score < high_scores->score[high_scores->starting_level][0]){
            // push former second position to third
            high_scores->score[high_scores->starting_level][2] = high_scores->score[high_scores->starting_level][1];
            // update second position
            high_scores->score[high_scores->starting_level][1] = statistics->current_score;
    }

    // check if highest high score needs to be updated
    if (statistics->current_score >= high_scores->score[high_scores->starting_level][0]){
            // push former first position to second
            high_scores->score[high_scores->starting_level][1] = high_scores->score[high_scores->starting_level][0];
            // update first position
            high_scores->score[high_scores->starting_level][0] = statistics->current_score;
    }
}


void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
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

        // if E has been pressed, update high scores and exit to main menu
        if(buttons.buttons[KEYCODE(E)]){
            buttons.buttons[KEYCODE(E)] = 0;  

            // update high scores
            if (statistics.lock){
                if (xSemaphoreTake(statistics.lock, 0) == pdTRUE){

                    if (high_scores.lock){
                        if (xSemaphoreTake(high_scores.lock, 0) == pdTRUE){

                            updateHighScores(&statistics, &high_scores);
                            xSemaphoreGive(high_scores.lock);
                        }
                        xSemaphoreGive(high_scores.lock);
                    }
                    xSemaphoreGive(statistics.lock);
                }
                xSemaphoreGive(statistics.lock);
            }

            // exit to main menu
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
                xSemaphoreGive(ResetGameSignal); printf("Reset game signal given.\n");
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

    char high_score_headline_text[50] = { 0 };
    int high_score_headline_text_width = 0;
    char first_high_score_text_buffer[20] = { 0 };
    int first_high_score_text_buffer_width = 0;
    char second_high_score_text_buffer[20] = { 0 };
    int second_high_score_text_buffer_width = 0;
    char third_high_score_text_buffer[20] = { 0 };
    int third_high_score_text_buffer_width = 0;

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


                // Determine which play mode is currently chosen and modify the drawing accordingly
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

                // Update starting level for high scores based on current user decision
                if (high_scores.lock){
                    if (xSemaphoreTake(high_scores.lock, 0) == pdTRUE){

                        sprintf(high_score_headline_text, "Highscores for starting level: %2i", high_scores.starting_level);
                        sprintf(first_high_score_text_buffer, "1. %10i", high_scores.score[high_scores.starting_level][0]);
                        sprintf(second_high_score_text_buffer, "2. %10i", high_scores.score[high_scores.starting_level][1]);
                        sprintf(third_high_score_text_buffer, "3. %10i", high_scores.score[high_scores.starting_level][2]);

                        xSemaphoreGive(high_scores.lock);
                    }
                    xSemaphoreGive(high_scores.lock);
                }

                // Draw high score texts
                if(!tumGetTextSize((char *)high_score_headline_text, &high_score_headline_text_width, NULL))
                    tumDrawText(high_score_headline_text, SCREEN_WIDTH/2-high_score_headline_text_width/2, 250, White);

                if(!tumGetTextSize((char *)first_high_score_text_buffer, &first_high_score_text_buffer_width, NULL))
                    tumDrawText(first_high_score_text_buffer, SCREEN_WIDTH/2-first_high_score_text_buffer_width/2, 280, White);

                if(!tumGetTextSize((char *)second_high_score_text_buffer, &second_high_score_text_buffer_width, NULL))
                    tumDrawText(second_high_score_text_buffer, SCREEN_WIDTH/2-second_high_score_text_buffer_width/2, 310, White);

                if(!tumGetTextSize((char *)third_high_score_text_buffer, &third_high_score_text_buffer_width, NULL))
                    tumDrawText(third_high_score_text_buffer, SCREEN_WIDTH/2-third_high_score_text_buffer_width/2, 340, White);

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
                                // Update cleared lines and current score
                                statistics.cleared_lines += lines_to_clear;
                                statistics.current_score += statistics.score_lookup_table[lines_to_clear-1] * (statistics.level+1);
                                
                                // Check if the level needs to be incremented
                                if (statistics.cleared_lines >= statistics.advance_level_lookup[statistics.level]){
                                    statistics.level++;

                                    // Check if highest level has been reached
                                    if (statistics.level > MAX_STARTING_LEVEL){
                                        statistics.level = MAX_STARTING_LEVEL;
                                    }
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

                    // Update high scores and reset statistics
                    if (statistics.lock){
                        if (xSemaphoreTake(statistics.lock, portMAX_DELAY) == pdTRUE){
                            if (high_scores.lock){
                                if (xSemaphoreTake(high_scores.lock, 0) == pdTRUE){

                                    updateHighScores(&statistics, &high_scores);
                                    xSemaphoreGive(high_scores.lock);
                                }
                                xSemaphoreGive(high_scores.lock);
                            }

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

                    // Update level in the statistics struct
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

                            // Update the high scores struct with the current starting level
                            if (high_scores.lock){
                                if (xSemaphoreTake(high_scores.lock, 0) == pdTRUE){
                                    high_scores.starting_level = statistics.level;
                                    xSemaphoreGive(high_scores.lock);
                                }
                                xSemaphoreGive(high_scores.lock);
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



int tetrisFunctionalityInit(void){

    // Mutexes
    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock.");
        goto err_buttons_lock;
    }

    statistics.lock = xSemaphoreCreateMutex();
    if (!statistics.lock){
        PRINT_ERROR("Failed to create statistics lock.");
        goto err_statistics_lock;
    }

    play_mode.lock = xSemaphoreCreateMutex();
    if (!play_mode.lock){
        PRINT_ERROR("Failed to create play mode lock.");
        goto err_play_mode_lock;
    }

    high_scores.lock = xSemaphoreCreateMutex();
    if (!high_scores.lock){
        PRINT_ERROR("Failed to create high scores lock.");
        goto err_high_scores_lock;
    }

    // Binary semaphores for signaling
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

    // Messages
    LevelChangingQueue = xQueueCreate(LEVEL_SELECTION_QUEUE_SIZE, sizeof(int));
    if (!LevelChangingQueue){
        PRINT_ERROR("Could not open level selection queue.");
        goto err_level_queue;
    }

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

    vTaskSuspend(ResetGameTask);
    vTaskSuspend(ChangeLevelTask);
    vTaskSuspend(ChangePlayModeTask);

    statistics = initStatistics(&statistics);
    play_mode = initPlayMode(&play_mode);
    high_scores = initHighScores(&high_scores);

    return 0;

err_change_play_mode_task:
    vTaskDelete(ChangePlayModeTask);
err_change_level_task:
    vTaskDelete(ChangeLevelTask);
err_reset_game_task:
    vTaskDelete(ResetGameTask);

err_game_over_screen_task:
    vTaskDelete(GameOverScreenTask);
err_tetris_state_paused_task:
    vTaskDelete(TetrisStatePausedTask);
err_tetris_state_playing_task:
    vTaskDelete(TetrisStatePlayingTask);
err_main_menu_task:
    vTaskDelete(MainMenuTask);

err_level_queue:
    vQueueDelete(LevelChangingQueue);

err_change_play_mode_signal:
    vSemaphoreDelete(ChangePlayModeSignal);
err_reset_game_signal:
    vSemaphoreDelete(ResetGameSignal);

err_high_scores_lock:
    vSemaphoreDelete(high_scores.lock);
err_play_mode_lock:
    vSemaphoreDelete(play_mode.lock);
err_statistics_lock:
    vSemaphoreDelete(statistics.lock);
err_buttons_lock:
    vSemaphoreDelete(buttons.lock);

    return -1;
}




