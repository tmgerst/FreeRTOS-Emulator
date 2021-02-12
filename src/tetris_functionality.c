/**
 * @file tetris_functionality.c
 * @author Tim Gerstewitz
 * @date 3rd February 2021
 * @brief Source file for the functional parts of the tetris project, that is, button presses, statistics, not gameplay-related drawings, and generally
 * all things that don't have an immediate relation to the gameplay part of the project.
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

#define POINTS_FOR_CLEARING_ONE_LINE 40
#define POINTS_FOR_CLEARING_TWO_LINES 100
#define POINTS_FOR_CLEARING_THREE_LINES 300
#define POINTS_FOR_CLEARING_FOUR_LINES 1200

#define MAIN_MENU_PLAYFIELD_POS_X 140
#define MAIN_MENU_PLAYFIELD_WIDTH 360

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

#define HIGH_SCORE_TEXT_POSTION_Y 250
#define HIGH_SCORE_TEXT_OFFSET 30

#define CURRENT_STATE_DESCRIPITION_OFFSET_Y 60

#define STATISTICS_SCORE_POSITION_Y 20
#define STATISTICS_VALUE_OFFSET 15
#define STATISTICS_TEXT_OFFSET 60

#define NEXT_TERIMINO_DISPLAY_WIDTH_IN_TILES 4
#define NEXT_TETRIMINO_DISPLAY_HEIGHT_IN_TILES 4
#define NEXT_TETRIMINO_DISPLAY_POSITION_Y 220

#define CONTROL_BUTTONS_POSITION_X 15
#define CONTROL_DESCRIPTIONS_POSITION_X 75
#define CONTROLS_TEXT_OFFSET 20
#define FUNCTIONALITIY_CONTROLS_POSITION_Y 120

#define LEVEL_SELECTION_QUEUE_SIZE 1
#define GENERATOR_MODE_QUEUE_SIZE 1

#define GENERATOR_INACTIVE 0
#define GENERATOR_ACTIVE 1

#define COMMAND_NEXT "NEXT"
#define COMMAND_MODE "MODE"
#define COMMAND_LIST "LIST"

#define FIRST_GEN_MODE "FAIR"
#define SECOND_GEN_MODE "RANDOM"
#define THIRD_GEN_MODE "EASY"
#define FOURTH_GEN_MODE "HARD"
#define FIFTH_GEN_MODE "DETERMINISTIC"

#define IS_GENERATOR_RUNNING_TIMER_PERIOD 200



TaskHandle_t MainMenuTask = NULL;
TaskHandle_t TetrisStateSinglePlayingTask = NULL;
TaskHandle_t TetrisStateSinglePausedTask = NULL;
TaskHandle_t TetrisStateDoublePlayingTask = NULL;
TaskHandle_t TetrisStateDoublePausedTask = NULL;
TaskHandle_t GameOverScreenTask = NULL;

TaskHandle_t ResetGameTask = NULL;
TaskHandle_t ChangeLevelTask = NULL;
TaskHandle_t ChangePlayModeTask = NULL;
TaskHandle_t ChangeGeneratorModeTask = NULL;

TaskHandle_t UDPControlTask = NULL;

static SemaphoreHandle_t ResetGameSignal = NULL;
static SemaphoreHandle_t ChangePlayModeSignal = NULL;

static SemaphoreHandle_t HandleUDP = NULL;
static SemaphoreHandle_t GetGeneratorModeSignal = NULL;
static SemaphoreHandle_t ChangeGeneratorModeSignal = NULL;
SemaphoreHandle_t DoubleModeNextSignal = NULL;

static QueueHandle_t LevelChangingQueue = NULL;
static QueueHandle_t GetGeneratorModeQueue = NULL;
static QueueHandle_t ChangeGeneratorModeQueue = NULL;

static xTimerHandle IsGeneratorRunningTimer = NULL;


// Constants for sending via queues
const int increment_level = 1;
const int decrement_level = -1;

buttons_buffer_t buttons = { 0 };
stats_t statistics = { 0 };
play_mode_t play_mode = { 0 };
high_scores_t high_scores = { 0 };
current_gen_mode_t generator_mode = { 0 };
next_tetrimino_display_t next_display = { 0 };

#define UDP_BUFFER_SIZE 1024
#define UDP_RECEIVE_PORT 1234
#define UDP_TRANSMIT_PORT 1235

aIO_handle_t udp_soc_receive = NULL;


/**
 * @brief Initializes a previously created empty statistics object.
 *
 * @param statistics A previously created, empty stats struct as a pointer.
 * @return The stats struct which was passed in filled with the appropriate information.
 */
stats_t initStatistics(stats_t* statistics){
    statistics->current_score = 0;
    statistics->cleared_lines = 0;
    statistics->level = 0;

    statistics->score_lookup_table[0] = POINTS_FOR_CLEARING_ONE_LINE;     // points for clearing one line at level zero
    statistics->score_lookup_table[1] = POINTS_FOR_CLEARING_TWO_LINES;    // points for clearing two lines at level zero
    statistics->score_lookup_table[2] = POINTS_FOR_CLEARING_THREE_LINES;    // points for clearing three lines at level zero
    statistics->score_lookup_table[3] = POINTS_FOR_CLEARING_FOUR_LINES;   // points for clearing four lines at level zero --> a TETRIS

    for (int i = 0; i <= MAX_STARTING_LEVEL; i++){
        statistics->advance_level_lookup[i] = (i+1) * 10;   // ie for advancing from level 8 to 9 the player needs 
                                                            // to clear 90 lines regardless of the starting level
    }

    return *statistics;
}

/**
 * @brief Initializes a previously created empty play mode object.
 *
 * @param pm A previously created, empty play_mode struct as a pointer.
 * @return The play_mode struct which was passed in, where play mode has been initialized to SINGLE_MODE.
 */
play_mode_t initPlayMode(play_mode_t* pm){
    pm->mode = SINGLE_MODE;   // set initial play mode to single-player
    xSemaphoreGive(play_mode.lock);
    return *pm;
}

/**
 * @brief Initializes a previously created empty high scores object.
 *
 * @param hs A previously created, empty high scores struct as a pointer.
 * @return The high scores struct which was passed in, where all high scores have been initialitźed to zero.
 */
high_scores_t initHighScores(high_scores_t* hs){
    hs->starting_level = 0;
    for (int i = 0; i <= MAX_STARTING_LEVEL; i++){
        hs->score[i][0] = 0;
        hs->score[i][1] = 0;
        hs->score[i][2] = 0;
    }
    return *hs;
}

/**
 * @brief Initializes a previously created empty current_gen_mode object.
 *
 * @param gm A previously created, empty current_gen_mode struct as a pointer.
 * @return The current_gen_mode struct which was passed in, with mode set to FAIR and generator_active set to INACTIVE.
 */
current_gen_mode_t initGeneratorMode(current_gen_mode_t* gm){
    gm->mode = "FAIR";
    gm->generator_active = GENERATOR_INACTIVE;
    return *gm;
}

/**
 * @brief Initializes a previously created empty next_tetrimino_display object.
 *
 * @param gm A previously created, empty next_tetrimino_display struct as a pointer.
 * @return The next_tetrimino_display struct which was passed in, with all tiles set to Black as initial colors.
 */
next_tetrimino_display_t initNextTetriminoDisplay(next_tetrimino_display_t* ntd){
    for (int r = 0; r < NEXT_TETRIMINO_DISPLAY_HEIGHT_IN_TILES; r++){
        for (int c = 0; c < NEXT_TERIMINO_DISPLAY_WIDTH_IN_TILES; c++){
            ntd->display[r][c] = initTile(Black);
        }
    }

    return *ntd;
}

/**
 * @brief Checks if one of the three high scores has to be updated for the current starting level, and does so if it is needed.
 * Needs to be called with both the statistics mutex and the high scores mutex obtained.
 *
 * @param statistics An initialized stats struct as a pointer. 
 * @param high_scores An initialized high_scores struct as a pointer.
 */
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

/**
 * @brief Checks if any of the buttons associated with functionality parts (P, R, E, M) have been pressed and triggers approriate actions.
 */
void checkForFunctionalityInput(void){
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){

        // If P has been pressed pause/resume the game
        if(buttons.buttons[KEYCODE(P)]){
            buttons.buttons[KEYCODE(P)] = 0;  

            if (StateMachineQueue){

                if (current_state.lock){
                    if (xSemaphoreTake(current_state.lock, 0) == pdTRUE){

                        if (current_state.state == single_playing_signal){
                            xQueueSend(StateMachineQueue, &single_paused_signal, 0);
                            xSemaphoreGive(current_state.lock);
                        }
                        if (current_state.state == single_paused_signal){
                            xQueueSend(StateMachineQueue, &single_playing_signal, 0);
                            xSemaphoreGive(current_state.lock);
                        }
                        
                        if (current_state.state == double_playing_signal){
                            xQueueSend(StateMachineQueue, &double_paused_signal, 0);
                            xSemaphoreGive(current_state.lock);
                        }
                        if (current_state.state == double_paused_signal){
                            xQueueSend(StateMachineQueue, &double_playing_signal, 0);
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

        // if M has been pressed in the two player mode, change generator mode
        if (buttons.buttons[KEYCODE(M)]){
            buttons.buttons[KEYCODE(M)] = 0;

            if (play_mode.lock){
                if (xSemaphoreTake(play_mode.lock, 0) == pdTRUE){
                    
                    if (play_mode.mode == DOUBLE_MODE){
                        xSemaphoreGive(ChangeGeneratorModeSignal);
                    }
                    xSemaphoreGive(play_mode.lock);
                }
                xSemaphoreGive(play_mode.lock);
            }
            xSemaphoreGive(buttons.lock);
        }

    }
    xSemaphoreGive(buttons.lock);
}

/**
 * @brief Checks if any of the buttons associated with the main menu (RETURN, UP, DOWN, M) have been pressed and triggers approriate actions.
 */
void vCheckForMainMenuInput(void){

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){
        // if ENTER has been pressed in the main menu, start playing
        if(buttons.buttons[KEYCODE(RETURN)]){
            buttons.buttons[KEYCODE(RETURN)] = 0;        
            if (StateMachineQueue){
                xSemaphoreGive(ResetGameSignal);

                if (play_mode.lock){
                    if (xSemaphoreTake(play_mode.lock, 0) == pdTRUE){

                        if (play_mode.mode == SINGLE_MODE){
                            xSemaphoreGive(play_mode.lock);
                            xSemaphoreGive(buttons.lock);
                            xQueueSend(StateMachineQueue, &single_playing_signal, 1);
                        }
                        if (play_mode.mode == DOUBLE_MODE){
                            xSemaphoreGive(play_mode.lock);
                            xSemaphoreGive(buttons.lock);
                            xQueueSend(StateMachineQueue, &double_playing_signal, 1);
                        }
                    }
                    xSemaphoreGive(play_mode.lock);
                }
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


/**
 * @brief Draws the current score, level and cleared lines to the screen while playing or paused.
 * Needs to be called with the stats struct mutex obtained.
 * 
 * @param statistics An initialized stats struct that holds the current game's statistics.
 */
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

    sprintf(score_buffer, "%i", statistics->current_score);
    sprintf(level_buffer, "%i", statistics->level);
    sprintf(lines_buffer, "%i", statistics->cleared_lines);

    tumDrawText(score_text, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+TILE_WIDTH, 
                STATISTICS_SCORE_POSITION_Y, White);
    tumDrawText(score_buffer, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+TILE_WIDTH, 
                STATISTICS_SCORE_POSITION_Y+STATISTICS_VALUE_OFFSET, White);

    tumDrawText(level_text, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+TILE_WIDTH, 
                STATISTICS_SCORE_POSITION_Y+STATISTICS_TEXT_OFFSET, White);
    tumDrawText(level_buffer, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+TILE_WIDTH, 
                STATISTICS_SCORE_POSITION_Y+STATISTICS_TEXT_OFFSET+STATISTICS_VALUE_OFFSET, White);

    tumDrawText(lines_text, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+TILE_WIDTH, 
                STATISTICS_SCORE_POSITION_Y+2*STATISTICS_TEXT_OFFSET, White);
    tumDrawText(lines_buffer, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+TILE_WIDTH, 
                STATISTICS_SCORE_POSITION_Y+2*STATISTICS_TEXT_OFFSET+STATISTICS_VALUE_OFFSET, White);
}

/**
 * @brief Draws the next tetrimino display to the screen based on the next upcoming tetrimino
 * 
 * @param next_display An initialized next_tetrimino_display struct as a pointer.
 */
void drawNextTetrimino(next_tetrimino_display_t* next_display){
    char name_buffer = 0;
    char name_text[2] = { 0 };
    char next_text[10] = "NEXT";

    unsigned int tetrimino_color = 0;
    int drawing_position_x = 0;
    int drawing_position_y = 0;

    if (TetriminoSelectionQueue){
        if (xQueuePeek(TetriminoSelectionQueue, &name_buffer, 0) == pdTRUE){
            sprintf(name_text, "%c", name_buffer);
        }
    }
    
    if (next_display->lock){
        if (xSemaphoreTake(next_display->lock, 0) == pdTRUE){
            
            // Clear previous next tetrimino_display
            for (int r = 0; r < NEXT_TETRIMINO_DISPLAY_HEIGHT_IN_TILES; r++){
                for (int c = 0; c < NEXT_TERIMINO_DISPLAY_WIDTH_IN_TILES; c++){
                    next_display->display[r][c].color = Black;
                }
            }

            // Set the tiles in the display according to the next tetrimino
            tetrimino_color = chooseColorForTetrimino(name_buffer);
            switch (name_buffer){
                case 'I':
                    next_display->display[0][1].color = tetrimino_color; next_display->display[1][1].color = tetrimino_color;
                    next_display->display[2][1].color = tetrimino_color; next_display->display[3][1].color = tetrimino_color;
                    break;

                case 'J':
                    next_display->display[1][1].color = tetrimino_color; next_display->display[2][1].color = tetrimino_color;
                    next_display->display[3][1].color = tetrimino_color; next_display->display[3][2].color = tetrimino_color;
                    break;

                case 'L':
                    next_display->display[0][1].color = tetrimino_color; next_display->display[1][1].color = tetrimino_color;
                    next_display->display[2][1].color = tetrimino_color; next_display->display[0][2].color = tetrimino_color;
                    break;

                case 'O':
                    next_display->display[1][1].color = tetrimino_color; next_display->display[1][2].color = tetrimino_color;
                    next_display->display[2][1].color = tetrimino_color; next_display->display[2][2].color = tetrimino_color;
                    break;

                case 'S':
                    next_display->display[2][1].color = tetrimino_color; next_display->display[3][1].color = tetrimino_color;
                    next_display->display[1][2].color = tetrimino_color; next_display->display[2][2].color = tetrimino_color;
                    break;
                case 'Z':
                    next_display->display[0][1].color = tetrimino_color; next_display->display[1][1].color = tetrimino_color;
                    next_display->display[1][2].color = tetrimino_color; next_display->display[2][2].color = tetrimino_color;
                    break;
                case 'T':
                    next_display->display[0][1].color = tetrimino_color; next_display->display[1][1].color = tetrimino_color;
                    next_display->display[2][1].color = tetrimino_color; next_display->display[1][2].color = tetrimino_color;
                    break;
                default:
                    printf("Next Tetrimino display name error.\n");
                    break;
            }

            // Draw next tetrimino display
            for (int r = 0; r < NEXT_TETRIMINO_DISPLAY_HEIGHT_IN_TILES; r++){
                for (int c = 0; c < NEXT_TERIMINO_DISPLAY_WIDTH_IN_TILES; c++){
                    drawing_position_x = PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+TILE_WIDTH + r*TILE_WIDTH;
                    drawing_position_y = NEXT_TETRIMINO_DISPLAY_POSITION_Y+c*TILE_HEIGHT;

                    drawTile(drawing_position_x, drawing_position_y, &(next_display->display[r][c]));
                }
            }
            xSemaphoreGive(next_display->lock);
        }
        xSemaphoreGive(next_display->lock);
    }

    tumDrawText(next_text, PLAY_AREA_POSITION_X+PLAY_AREA_WIDTH_IN_TILES*TILE_WIDTH+TILE_WIDTH, 
                STATISTICS_SCORE_POSITION_Y+3*STATISTICS_TEXT_OFFSET, White);
}



/**
 * @brief Draws a "CONTROLS" to the screen.
 */
void drawControlsHeadline(void){
    tumDrawText("CONTROLS", CONTROL_BUTTONS_POSITION_X, STATISTICS_SCORE_POSITION_Y+STATISTICS_TEXT_OFFSET, White);
}

/**
 * @brief Draws the controls for playing tetris to the screen.
 */
void drawPlayingControls(void){
    tumDrawText("A", CONTROL_BUTTONS_POSITION_X, NEXT_TETRIMINO_DISPLAY_POSITION_Y, White);       
    tumDrawText("Move Left", CONTROL_DESCRIPTIONS_POSITION_X, NEXT_TETRIMINO_DISPLAY_POSITION_Y, White);

    tumDrawText("D", CONTROL_BUTTONS_POSITION_X, NEXT_TETRIMINO_DISPLAY_POSITION_Y+CONTROLS_TEXT_OFFSET, White);       
    tumDrawText("Move Right", CONTROL_DESCRIPTIONS_POSITION_X, NEXT_TETRIMINO_DISPLAY_POSITION_Y+CONTROLS_TEXT_OFFSET, White);

    tumDrawText("LEFT", CONTROL_BUTTONS_POSITION_X, NEXT_TETRIMINO_DISPLAY_POSITION_Y+2*CONTROLS_TEXT_OFFSET, White);    
    tumDrawText("Rotate CW", CONTROL_DESCRIPTIONS_POSITION_X, NEXT_TETRIMINO_DISPLAY_POSITION_Y+2*CONTROLS_TEXT_OFFSET, White);

    tumDrawText("RIGHT", CONTROL_BUTTONS_POSITION_X, NEXT_TETRIMINO_DISPLAY_POSITION_Y+3*CONTROLS_TEXT_OFFSET, White);   
    tumDrawText("Rotate CCW", CONTROL_DESCRIPTIONS_POSITION_X, NEXT_TETRIMINO_DISPLAY_POSITION_Y+3*CONTROLS_TEXT_OFFSET, White);
}

/**
 * @brief Draws the controls for state transitions to the screen.
 */
void drawFunctionalityControls(void){

    tumDrawText("R", CONTROL_BUTTONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y, White);    
    tumDrawText("Reset game", CONTROL_DESCRIPTIONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y, White);

    tumDrawText("E", CONTROL_BUTTONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y+CONTROLS_TEXT_OFFSET, White);    
    tumDrawText("Exit to main menu", CONTROL_DESCRIPTIONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y+CONTROLS_TEXT_OFFSET, White);

    tumDrawText("P", CONTROL_BUTTONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y+2*CONTROLS_TEXT_OFFSET, White);    
    tumDrawText("Pause / Resume", CONTROL_DESCRIPTIONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y+2*CONTROLS_TEXT_OFFSET, White);

    if (play_mode.lock){
        if (xSemaphoreTake(play_mode.lock, 0) == pdTRUE){
            if (play_mode.mode == DOUBLE_MODE){
                tumDrawText("M", CONTROL_BUTTONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y+3*CONTROLS_TEXT_OFFSET, White);   
                tumDrawText("Change gen. mode", CONTROL_DESCRIPTIONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y+3*CONTROLS_TEXT_OFFSET, White);
            }
            xSemaphoreGive(play_mode.lock);
        }
        xSemaphoreGive(play_mode.lock);
    }
}

/**
 * @brief Draws the controls for the game over screen to the screen.
 */
void drawGameOverControls(void){
    tumDrawText("R", CONTROL_BUTTONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y, White);    
    tumDrawText("Reset game", CONTROL_DESCRIPTIONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y, White);

    tumDrawText("E", CONTROL_BUTTONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y+CONTROLS_TEXT_OFFSET, White);    
    tumDrawText("Exit to main menu", CONTROL_DESCRIPTIONS_POSITION_X, FUNCTIONALITIY_CONTROLS_POSITION_Y+CONTROLS_TEXT_OFFSET, White);
}



/**
 * @brief Implements the main menu state task. 
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
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
                tumDrawFilledBox(MAIN_MENU_PLAYFIELD_POS_X, PLAY_AREA_POSITION_Y, MAIN_MENU_PLAYFIELD_WIDTH, 
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
                    tumDrawText(high_score_headline_text, SCREEN_WIDTH/2-high_score_headline_text_width/2, HIGH_SCORE_TEXT_POSTION_Y, White);

                if(!tumGetTextSize((char *)first_high_score_text_buffer, &first_high_score_text_buffer_width, NULL))
                    tumDrawText(first_high_score_text_buffer, SCREEN_WIDTH/2-first_high_score_text_buffer_width/2, 
                                HIGH_SCORE_TEXT_POSTION_Y+HIGH_SCORE_TEXT_OFFSET, White);

                if(!tumGetTextSize((char *)second_high_score_text_buffer, &second_high_score_text_buffer_width, NULL))
                    tumDrawText(second_high_score_text_buffer, SCREEN_WIDTH/2-second_high_score_text_buffer_width/2, 
                                HIGH_SCORE_TEXT_POSTION_Y+2*HIGH_SCORE_TEXT_OFFSET, White);

                if(!tumGetTextSize((char *)third_high_score_text_buffer, &third_high_score_text_buffer_width, NULL))
                    tumDrawText(third_high_score_text_buffer, SCREEN_WIDTH/2-third_high_score_text_buffer_width/2, 
                                HIGH_SCORE_TEXT_POSTION_Y+3*HIGH_SCORE_TEXT_OFFSET, White);

                // Draw Text for starting the game
                tumDrawText(press_enter_text, SCREEN_WIDTH/2-press_enter_text_width/2, SCREEN_HEIGHT-CURRENT_STATE_DESCRIPITION_OFFSET_Y, White);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }
        }
    }
}

/**
 * @brief Implements the single playing state task.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vTetrisStateSinglePlaying(void *pvParameters){

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

                drawNextTetrimino(&next_display);

                drawControlsHeadline();
                drawPlayingControls();
                drawFunctionalityControls();

                tumDrawText(text,SCREEN_WIDTH/2-text_width/2, SCREEN_HEIGHT-CURRENT_STATE_DESCRIPITION_OFFSET_Y, Lime);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }
        }
    }
}

/**
 * @brief Implements the single paused state task.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vTetrisStateSinglePaused(void *pvParameters){

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

                drawNextTetrimino(&next_display);

                drawControlsHeadline();
                drawFunctionalityControls();

                tumDrawText(text,SCREEN_WIDTH/2-text_width/2, SCREEN_HEIGHT-CURRENT_STATE_DESCRIPITION_OFFSET_Y, Orange);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }
        }
    }
}

/**
 * @brief Implements the double playing state task.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vTetrisStateDoublePlaying(void *pvParameters){

    char text[50] = "Playing Double-Player Mode";
    int text_width = 0;
    tumGetTextSize((char *) text, &text_width, NULL);

    char mode_text[20] = "CURRENT MODE";
    char mode_buffer[20] = "FAIR";

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

                drawNextTetrimino(&next_display);

                drawControlsHeadline();
                drawFunctionalityControls();
                drawPlayingControls();

                // Draw the current generator mode to the screen
                tumDrawText(mode_text, CONTROL_BUTTONS_POSITION_X, CONTROLS_TEXT_OFFSET, White);
                if (GetGeneratorModeQueue){
                    if (xQueueReceive(GetGeneratorModeQueue, mode_buffer, 0) == pdTRUE){
                        tumDrawText(mode_buffer, CONTROL_BUTTONS_POSITION_X, 2*CONTROLS_TEXT_OFFSET, White);
                    }
                    else{
                        tumDrawText(mode_buffer, CONTROL_BUTTONS_POSITION_X, 2*CONTROLS_TEXT_OFFSET, White);
                    }
                }

                // if generator is still inactive, reset the timer to signal this
                if (generator_mode.lock){
                    if (xSemaphoreTake(generator_mode.lock, 0) == pdTRUE){
                        
                        generator_mode.mode = mode_buffer;
                        if (generator_mode.generator_active == GENERATOR_INACTIVE){
                            if (StateMachineQueue){
                                xSemaphoreGive(generator_mode.lock);
                                if (xTimerIsTimerActive(IsGeneratorRunningTimer) == pdFALSE){
                                    xTimerReset(IsGeneratorRunningTimer, 0);
                                }
                            }
                        }
                        xSemaphoreGive(generator_mode.lock);
                    }
                    xSemaphoreGive(generator_mode.lock);
                }

                tumDrawText(text,SCREEN_WIDTH/2-text_width/2, SCREEN_HEIGHT-CURRENT_STATE_DESCRIPITION_OFFSET_Y, Lime);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }
        }
    }
}

/**
 * @brief Implements the double playing state task.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vTetrisStateDoublePaused(void *pvParameters){

    char text[50] = "Paused Double-Player Mode";
    int text_width = 0;
    tumGetTextSize((char *) text, &text_width, NULL);

    char mode_text[20] = "CURRENT MODE";

    char gen_inactive_one[30] = "The tetris generator";
    char gen_inactive_two[30] = "seems to be inactive.";
    char gen_inactive_three[30] = "Please exit to main menu,";
    char gen_inactive_four[30] = "start the generator and";
    char gen_inactive_five[30] = "re-enter two player mode.";

    int gen_inactive_one_width = 0;
    int gen_inactive_two_width = 0;
    int gen_inactive_three_width = 0;
    int gen_inactive_four_width = 0;
    int gen_inactive_five_width = 0;

    tumGetTextSize((char *) gen_inactive_one, &gen_inactive_one_width, NULL);
    tumGetTextSize((char *) gen_inactive_two, &gen_inactive_two_width, NULL);
    tumGetTextSize((char *) gen_inactive_three, &gen_inactive_three_width, NULL);
    tumGetTextSize((char *) gen_inactive_four, &gen_inactive_four_width, NULL);
    tumGetTextSize((char *) gen_inactive_five, &gen_inactive_five_width, NULL);

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

                drawNextTetrimino(&next_display);

                drawControlsHeadline();
                drawFunctionalityControls();

                tumDrawText(mode_text, CONTROL_BUTTONS_POSITION_X, CONTROLS_TEXT_OFFSET, White);

                // if generator is inactive, display text that says so
                if (generator_mode.lock){
                    if (xSemaphoreTake(generator_mode.lock, 0) == pdTRUE){

                        tumDrawText(generator_mode.mode, CONTROL_BUTTONS_POSITION_X, 2*CONTROLS_TEXT_OFFSET, White);
                        if (generator_mode.generator_active == GENERATOR_INACTIVE){
                            tumDrawText(gen_inactive_one, SCREEN_WIDTH/2-gen_inactive_one_width/2, 
                                        FUNCTIONALITIY_CONTROLS_POSITION_Y+CONTROLS_TEXT_OFFSET, White);
                            tumDrawText(gen_inactive_two, SCREEN_WIDTH/2-gen_inactive_two_width/2, 
                                        FUNCTIONALITIY_CONTROLS_POSITION_Y+2*CONTROLS_TEXT_OFFSET, White);
                            tumDrawText(gen_inactive_three, SCREEN_WIDTH/2-gen_inactive_three_width/2, 
                                        FUNCTIONALITIY_CONTROLS_POSITION_Y+3*CONTROLS_TEXT_OFFSET, White);
                            tumDrawText(gen_inactive_four, SCREEN_WIDTH/2-gen_inactive_four_width/2, 
                                        FUNCTIONALITIY_CONTROLS_POSITION_Y+4*CONTROLS_TEXT_OFFSET, White);
                            tumDrawText(gen_inactive_five, SCREEN_WIDTH/2-gen_inactive_five_width/2, 
                                        FUNCTIONALITIY_CONTROLS_POSITION_Y+5*CONTROLS_TEXT_OFFSET, White);
                        }

                        xSemaphoreGive(generator_mode.lock);
                    }
                    xSemaphoreGive(generator_mode.lock);
                }

                tumDrawText(text, SCREEN_WIDTH/2-text_width/2, SCREEN_HEIGHT-CURRENT_STATE_DESCRIPITION_OFFSET_Y, Orange);

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }
        }
    }
}

/**
 * @brief Implements the game over state task.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
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

                drawControlsHeadline();
                drawGameOverControls();

                vDrawFPS();
                xSemaphoreGive(ScreenLock);
            }
        }
    }
}


/**
 * @brief Callback function for UDP communication, handles all incoming communication from the tetris generator.
 * 
 * @param read_size The size of the incoming transmission in chars.
 * @param buffer Pointer to the content of the transmission.
 * @param args Pointer to arguments of the transmission.
 */
void receiveUDPInput(size_t read_size, char *buffer, void *args){
    char sending_buffer[15] = { 0 };

    BaseType_t xHigherPriorityTaskWoken1 = pdFALSE;
    BaseType_t xHigherPriorityTaskWoken2 = pdFALSE;
    BaseType_t xHigherPriorityTaskWoken3 = pdFALSE;
    BaseType_t xHigherPriorityTaskWoken4 = pdFALSE;
    BaseType_t xHigherPriorityTaskWoken5 = pdFALSE;
    BaseType_t xHigherPriorityTaskWoken6 = pdFALSE;

    printf("Buffer content: %s\n", buffer);
    
    // If a message has been received, the generator is active and the timer to see 
    // if a message has been received in a certain time can be stopped 
    xTimerStopFromISR(IsGeneratorRunningTimer, &xHigherPriorityTaskWoken1);
    printf("Stop generator timer.\n");

    if (xSemaphoreTakeFromISR(HandleUDP, &xHigherPriorityTaskWoken2) == pdTRUE) {

        if (strncmp(buffer, COMMAND_NEXT, (read_size < 4) ? read_size : 4) == 0) {
            if (TetriminoSelectionQueue){
                printf("Sending to spawn task: %c\n", buffer[5]);
                xQueueSendFromISR(TetriminoSelectionQueue, &buffer[5], &xHigherPriorityTaskWoken3);
            }
        }

        if (strncmp(buffer, COMMAND_MODE, (read_size < 4) ? read_size : 4) == 0){
                strncpy(sending_buffer, &buffer[5], read_size-strlen(COMMAND_MODE)-1);
                printf("Sending received mode: %s\n", sending_buffer);

                // if mode=ok has been received, dont send it to the display task
                if (strcmp(sending_buffer, "OK") == 0){
                    printf("Changing mode accepted.\n");
                    xSemaphoreGiveFromISR(GetGeneratorModeSignal, &xHigherPriorityTaskWoken4);
                }
                else{
                    xQueueSendFromISR(GetGeneratorModeQueue, sending_buffer, &xHigherPriorityTaskWoken5);
                }

        }
        xSemaphoreGiveFromISR(HandleUDP, &xHigherPriorityTaskWoken6);

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken1 |
                           xHigherPriorityTaskWoken2 |
                           xHigherPriorityTaskWoken3 |
                           xHigherPriorityTaskWoken4 |
                           xHigherPriorityTaskWoken5 |
                           xHigherPriorityTaskWoken6);
    }
    else{
        fprintf(stderr, "[ERROR] Overlapping UDPHandler call.\n");
    }
}

/**
 * @brief Handles all outgoing UDP communication with the tetris generator.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vUDPControl(void *pvParameters){

    char *addr = NULL; // Loopback
    in_port_t port = UDP_RECEIVE_PORT;

    char change_mode_buffer[15] = { 0 };
    char command[30];

    udp_soc_receive =
        aIOOpenUDPSocket(addr, port, UDP_BUFFER_SIZE, receiveUDPInput, NULL);

    printf("UDP socket opened on port %d\n", port);

    // Initial spawn procedure
    printf("Initial generator request for next tetrimino.\n");
    aIOSocketPut(UDP, NULL, UDP_TRANSMIT_PORT, COMMAND_NEXT, strlen(COMMAND_NEXT));

    while (1) {
        if (DoubleModeNextSignal){
            if (xSemaphoreTake(DoubleModeNextSignal, TETRIMINO_QUEUE_RECEIVE_DELAY) == pdTRUE){
                xTimerReset(IsGeneratorRunningTimer, 0);
                aIOSocketPut(UDP, NULL, UDP_TRANSMIT_PORT, COMMAND_NEXT, strlen(COMMAND_NEXT));
            }
        }
        
        if (GetGeneratorModeSignal){
            if (xSemaphoreTake(GetGeneratorModeSignal, 0) == pdTRUE){
                xTimerReset(IsGeneratorRunningTimer, 0);
                aIOSocketPut(UDP, NULL, UDP_TRANSMIT_PORT, COMMAND_MODE, strlen(COMMAND_MODE));
            }
        }

        if (ChangeGeneratorModeQueue){
            if (xQueueReceive(ChangeGeneratorModeQueue, change_mode_buffer, 0) == pdTRUE){
                printf("Change mode request received: %s\n", change_mode_buffer);
                strcat(command, COMMAND_MODE); 
                strcat(command, "=");
                strcat(command, change_mode_buffer);
                printf("Send command: %s\n", command);

                xTimerReset(IsGeneratorRunningTimer, 0);
                aIOSocketPut(UDP, NULL, UDP_TRANSMIT_PORT, command, strlen(command));

                // resetting the command buffer string
                for (int i = 0; i < strlen(command); i++){
                    command[i] = 0;
                }
            }
        }
    }
}


/**
 * @brief Clears the playfield, spawns new tetriminos and resets the score, both in 
 * single player and double player mode.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
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

                    // Clear tetrimino queue
                    printf("Clear selection queue.\n");
                    xQueueReset(TetriminoSelectionQueue);

                    if (StateMachineQueue){
                        if (play_mode.lock){
                            if (xSemaphoreTake(play_mode.lock, 0) == pdTRUE){
                                
                                if (play_mode.mode == SINGLE_MODE){
                                    xSemaphoreGive(play_mode.lock);
                                    xSemaphoreGive(SpawnSignal);
                                    xQueueSend(StateMachineQueue, &single_playing_signal, 1);
                                }
                                else if (play_mode.mode == DOUBLE_MODE){
                                    xSemaphoreGive(play_mode.lock);
                                    xSemaphoreGive(DoubleModeNextSignal);
                                    xSemaphoreGive(SpawnSignal);
                                    xQueueSend(StateMachineQueue, &double_playing_signal, 1);
                                }
                            }
                            xSemaphoreGive(play_mode.lock);
                        }
                    }
                }
            }
        }
    }
}

/**
 * @brief Changes the starting level according to main menu selection by the user.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
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

/**
 * @brief Changes the play mode of the tetris game, either from single to double or from double to
 * single mode. Only available in the main menu.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vChangePlayMode(void *pvParameters){
    TickType_t last_change = xTaskGetTickCount();

    while(1){
        if(ChangePlayModeSignal){
            if (xSemaphoreTake(ChangePlayModeSignal, portMAX_DELAY) == pdTRUE){

                if (xTaskGetTickCount() - last_change > BUTTON_DEBOUNCE_DELAY){
                    last_change = xTaskGetTickCount();

                    if (play_mode.lock){
                        if (xSemaphoreTake(play_mode.lock, portMAX_DELAY) == pdTRUE){

                            if (play_mode.mode == SINGLE_MODE){
                                play_mode.mode = DOUBLE_MODE;  // change play mode to two-player mode
                                printf("Play mode: %i\n", play_mode.mode);
                            }
                            else if (play_mode.mode == DOUBLE_MODE){
                                play_mode.mode = SINGLE_MODE; // change play mode to single-player mode
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

/**
 * @brief Changes the generator mode sequentially by changing the current_gen_mode struct and requests the next
 * mode via the UDP communication task.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vChangeGeneratorMode(void *pvParameters){

    TickType_t last_change = xTaskGetTickCount();

    while(1){
        if (ChangeGeneratorModeSignal){
            if (xSemaphoreTake(ChangeGeneratorModeSignal, portMAX_DELAY) == pdTRUE){

                if (last_change - xTaskGetTickCount() > BUTTON_DEBOUNCE_DELAY){
                    last_change = xTaskGetTickCount();
                
                    if (generator_mode.lock){
                        if (xSemaphoreTake(generator_mode.lock, portMAX_DELAY) == pdTRUE){
                            
                            if (generator_mode.generator_active == GENERATOR_ACTIVE){

                                if (strcmp(generator_mode.mode, FIRST_GEN_MODE) == 0){
                                    printf("Current mode fair, changing to second mode.\n");
                                    generator_mode.mode = SECOND_GEN_MODE;
                                    xSemaphoreGive(generator_mode.lock);
                                    xQueueSend(ChangeGeneratorModeQueue, &SECOND_GEN_MODE, 0);
                                }

                                else if (strcmp(generator_mode.mode, SECOND_GEN_MODE) == 0){
                                    generator_mode.mode = THIRD_GEN_MODE;
                                    xSemaphoreGive(generator_mode.lock);
                                    xQueueSend(ChangeGeneratorModeQueue, &THIRD_GEN_MODE, 0);
                                }
                                else if (strcmp(generator_mode.mode, THIRD_GEN_MODE) == 0){
                                    generator_mode.mode = FOURTH_GEN_MODE;
                                    xSemaphoreGive(generator_mode.lock);
                                    xQueueSend(ChangeGeneratorModeQueue, &FOURTH_GEN_MODE, 0);
                                }

                                else if (strcmp(generator_mode.mode, FOURTH_GEN_MODE) == 0){
                                    generator_mode.mode = FIFTH_GEN_MODE;
                                    xSemaphoreGive(generator_mode.lock);
                                    xQueueSend(ChangeGeneratorModeQueue, &FIFTH_GEN_MODE, 0);
                                }
                                else if (strcmp(generator_mode.mode, FIFTH_GEN_MODE) == 0){
                                    generator_mode.mode = FIRST_GEN_MODE;
                                    xSemaphoreGive(generator_mode.lock);
                                    xQueueSend(ChangeGeneratorModeQueue, &FIRST_GEN_MODE, 0);
                                }
                            }
                            xSemaphoreGive(generator_mode.lock);
                        }
                        xSemaphoreGive(generator_mode.lock);
                    }
                }
            }
        }
    }

}

/**
 * @brief Callback function for the timer that checks if the tetris generator has answered, If not, this routine is called, 
 * which sets the generator mode to inactive and requests a state transition to the double paused state.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vGeneratorNotRunningRoutine(void *pvParameters){
    if (generator_mode.lock){
        if (xSemaphoreTake(generator_mode.lock, 0) == pdTRUE){
            generator_mode.generator_active = GENERATOR_INACTIVE;
            xSemaphoreGive(generator_mode.lock);
        }
        xSemaphoreGive(generator_mode.lock);
    }

    if (StateMachineQueue){
        printf("Generator inactive, change to double paused.\n");
        xTimerStop(IsGeneratorRunningTimer, 0);
        xQueueSend(StateMachineQueue, &double_paused_signal, 0);
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

    generator_mode.lock = xSemaphoreCreateMutex();
    if (!generator_mode.lock){
        PRINT_ERROR("Failed to create generator modes lock.");
        goto err_generator_modes_lock;
    }

    HandleUDP = xSemaphoreCreateMutex();
    if (!HandleUDP){
        PRINT_ERROR("Failed to create UDP handle.");
        goto err_handle_udp;
    }

    next_display.lock = xSemaphoreCreateMutex();
    if (!next_display.lock){
        PRINT_ERROR("Failed to create next tetrimino display lock.");
        goto err_next_tetrimino_display_lock;
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

    DoubleModeNextSignal = xSemaphoreCreateBinary();
    if (!DoubleModeNextSignal){
        PRINT_ERROR("Failed to create double mode next signal.");
        goto err_double_mode_next_signal;
    }

    GetGeneratorModeSignal = xSemaphoreCreateBinary();
    if (!GetGeneratorModeSignal){
        PRINT_ERROR("Failed to create get generator mode signal.");
        goto err_get_generator_mode_signal;
    }

    ChangeGeneratorModeSignal = xSemaphoreCreateBinary();
    if (!ChangeGeneratorModeSignal){
        PRINT_ERROR("Failed to create change generator mode signal.");
        goto err_change_generator_mode_signal;
    }

    // Messages
    LevelChangingQueue = xQueueCreate(LEVEL_SELECTION_QUEUE_SIZE, sizeof(int));
    if (!LevelChangingQueue){
        PRINT_ERROR("Could not open level selection queue.");
        goto err_level_queue;
    }

    GetGeneratorModeQueue = xQueueCreate(GENERATOR_MODE_QUEUE_SIZE, sizeof(char) * 15);
    if (!GetGeneratorModeQueue){
        PRINT_ERROR("Could not open get generator mode queue.");
        goto err_get_generator_mode_queue;
    }

    ChangeGeneratorModeQueue = xQueueCreate(GENERATOR_MODE_QUEUE_SIZE, sizeof(char) * 15);
    if (!ChangeGeneratorModeQueue){
        PRINT_ERROR("Could not open change generator mode queue.");
        goto err_change_generator_mode_queue;
    }

    IsGeneratorRunningTimer = xTimerCreate("IsGeneratorRunningTimer", IS_GENERATOR_RUNNING_TIMER_PERIOD, pdTRUE, (void*) 0, vGeneratorNotRunningRoutine);
    if (!IsGeneratorRunningTimer){
        PRINT_ERROR("Could not create generator check timer.");
        goto err_is_generator_running_timer;
    }

    // Tasks
    if (xTaskCreate(vMainMenu, "MainMenuTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-2, 
                    &MainMenuTask) != pdPASS) {
        PRINT_TASK_ERROR("MainMenuTask");
        goto err_main_menu_task;
    } 

    if (xTaskCreate(vTetrisStateSinglePlaying, "TetrisStateSinglePlayingTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-2, 
                    &TetrisStateSinglePlayingTask) != pdPASS) {
        PRINT_TASK_ERROR("TetrisStateSinglePlayingTask");
        goto err_tetris_state_single_playing_task;
    } 

    if (xTaskCreate(vTetrisStateSinglePaused, "TetrisStateSinglePausedTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-2, 
                    &TetrisStateSinglePausedTask) != pdPASS) {
        PRINT_TASK_ERROR("TetrisStateSinglePausedTask");
        goto err_tetris_state_single_paused_task;
    } 

    if (xTaskCreate(vTetrisStateDoublePlaying, "TetrisStateDoublePlayingTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-2,
                    &TetrisStateDoublePlayingTask) != pdPASS) {
        PRINT_TASK_ERROR("TetrisStateDoublePlayingTask");
        goto err_tetris_state_double_playing_task;
    }

    if (xTaskCreate(vTetrisStateDoublePaused, "TetrisStateDoublePausedTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-2,
                    &TetrisStateDoublePausedTask) != pdPASS){
        PRINT_TASK_ERROR("TetrisStateDOublePausedTask");
        goto err_tetris_state_double_paused_task;
    }

    if (xTaskCreate(vGameOverScreen, "GameOverScreenTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES-2, 
                    &GameOverScreenTask) != pdPASS) {
        PRINT_TASK_ERROR("GameOverScreenTask");
        goto err_game_over_screen_task;
    }

    if (xTaskCreate(vResetGame, "ResetGameTask", 
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY+4, 
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

    if (xTaskCreate(vChangeGeneratorMode, "ChangeGeneratorModeTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY,
                    &ChangeGeneratorModeTask) != pdPASS){
        PRINT_TASK_ERROR("ChangeGeneratorModeTask");
        goto err_change_generator_mode_task;
    }

    if (xTaskCreate(vUDPControl, "UDPControlTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, mainGENERIC_PRIORITY+4,
                    &UDPControlTask) != pdPASS){
        PRINT_TASK_ERROR("UDPControlTask");
        goto err_udp_control_task;
    }

    vTaskSuspend(MainMenuTask);
    vTaskSuspend(TetrisStateSinglePlayingTask);
    vTaskSuspend(TetrisStateSinglePausedTask);
    vTaskSuspend(TetrisStateDoublePlayingTask);
    vTaskSuspend(TetrisStateDoublePausedTask);
    vTaskSuspend(GameOverScreenTask);

    vTaskSuspend(ResetGameTask);
    vTaskSuspend(ChangeLevelTask);
    vTaskSuspend(ChangePlayModeTask);
    vTaskSuspend(ChangeGeneratorModeTask);
    vTaskSuspend(UDPControlTask);

    statistics = initStatistics(&statistics);
    play_mode = initPlayMode(&play_mode);
    high_scores = initHighScores(&high_scores);
    generator_mode = initGeneratorMode(&generator_mode);
    next_display = initNextTetriminoDisplay(&next_display);

    return 0;

err_udp_control_task:
    vTaskDelete(UDPControlTask);
err_change_generator_mode_task:
    vTaskDelete(ChangeGeneratorModeTask);
err_change_play_mode_task:
    vTaskDelete(ChangePlayModeTask);
err_change_level_task:
    vTaskDelete(ChangeLevelTask);
err_reset_game_task:
    vTaskDelete(ResetGameTask);

err_game_over_screen_task:
    vTaskDelete(GameOverScreenTask);
err_tetris_state_double_paused_task:
    vTaskDelete(TetrisStateDoublePausedTask);
err_tetris_state_double_playing_task:
    vTaskDelete(TetrisStateDoublePlayingTask);
err_tetris_state_single_paused_task:
    vTaskDelete(TetrisStateSinglePausedTask);
err_tetris_state_single_playing_task:
    vTaskDelete(TetrisStateSinglePlayingTask);
err_main_menu_task:
    vTaskDelete(MainMenuTask);

err_is_generator_running_timer:
    xTimerDelete(IsGeneratorRunningTimer, 0);
err_change_generator_mode_queue:
    vQueueDelete(ChangeGeneratorModeQueue);
err_get_generator_mode_queue:
    vQueueDelete(GetGeneratorModeQueue);
err_level_queue:
    vQueueDelete(LevelChangingQueue);

err_change_generator_mode_signal:
    vSemaphoreDelete(ChangeGeneratorModeSignal);
err_get_generator_mode_signal:
    vSemaphoreDelete(GetGeneratorModeSignal);
err_double_mode_next_signal:
    vSemaphoreDelete(DoubleModeNextSignal);
err_change_play_mode_signal:
    vSemaphoreDelete(ChangePlayModeSignal);
err_reset_game_signal:
    vSemaphoreDelete(ResetGameSignal);

err_next_tetrimino_display_lock:
    vSemaphoreDelete(next_display.lock);
err_handle_udp:
    vSemaphoreDelete(HandleUDP);
err_generator_modes_lock:
    vSemaphoreDelete(generator_mode.lock);
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




