#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "task.h"

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

#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define NUMBER_OF_STATES 2

/* Tetris constants */

#define TILE_WIDTH 20
#define TILE_HEIGHT 20

#define PLAY_AREA_WIDTH_IN_TILES 10
#define PLAY_AREA_HEIGHT_IN_TILES 22
#define PLAY_AREA_POSITION_X 220
#define PLAY_AREA_POSITION_Y -40

#define SPAWN_ROW 2
#define SPAWN_COLUMN 4

#define TETRIMINO_GRID_WIDTH 5
#define TETRIMINO_GRID_HEIGHT 5
#define TETRIMINO_GRID_CENTER 2

/* End of Tetris constants*/

#ifdef TRACE_FUNCTIONS
#include "tracer.h"
#endif

const unsigned char next_state_signal = NEXT_TASK;

static TaskHandle_t SequentialStateMachine = NULL;
static TaskHandle_t BufferSwap = NULL;

static TaskHandle_t StatePlayingControlTask = NULL;
static TaskHandle_t StatePausedControlTask = NULL;

static QueueHandle_t StateMachineQueue = NULL;

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


typedef struct play_area {
    tile_t tiles[PLAY_AREA_HEIGHT_IN_TILES][PLAY_AREA_WIDTH_IN_TILES];
    int size_x;
    int size_y;
} play_area_t;

play_area_t initPlayArea(void){
    play_area_t area;

    for (int r = 0; r < PLAY_AREA_HEIGHT_IN_TILES; r++){
        for(int c = 0; c < PLAY_AREA_WIDTH_IN_TILES; c++){
            area.tiles[r][c] = initTile(Black);
        }
    }

    area.size_x = PLAY_AREA_WIDTH_IN_TILES * TILE_WIDTH;
    area.size_y = PLAY_AREA_HEIGHT_IN_TILES * TILE_HEIGHT;

    return area;
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
    int playfield_row;    // row position of the tetrimino center in the play area 
    int playfield_column;    // column position of the tetrimino center in the play area
} tetrimino_t;

tetrimino_t initTetrimino_T(unsigned int color){
    tetrimino_t new_T_structure;
    new_T_structure.name = 'T';
    new_T_structure.color = color;
    new_T_structure.playfield_row = 0;
    new_T_structure.playfield_column = 0;

    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            new_T_structure.grid[r][c] = 0;
        }
    }

    new_T_structure.grid[TETRIMINO_GRID_CENTER][TETRIMINO_GRID_CENTER-1] = color;
    new_T_structure.grid[TETRIMINO_GRID_CENTER][TETRIMINO_GRID_CENTER] = color;
    new_T_structure.grid[TETRIMINO_GRID_CENTER][TETRIMINO_GRID_CENTER+1] = color;
    new_T_structure.grid[TETRIMINO_GRID_CENTER+1][TETRIMINO_GRID_CENTER] = color;

    return new_T_structure;
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

int tetriminoRowToPlayfieldRow(tetrimino_t* t, int offset){
    return t->playfield_row - TETRIMINO_GRID_CENTER + offset;
}

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

// Checks if the new tetrimino position is valid. Returns 0 if it is valid and -1 if it is not.
int checkNewTetriminoPosition(play_area_t* playfield, tetrimino_t* tetrimino){
    int corresponding_p_row = 0;
    int corresponding_p_column = 0;

    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            if (tetrimino->grid[r][c] != 0){

                corresponding_p_row = tetriminoRowToPlayfieldRow(tetrimino, r);
                corresponding_p_column = tetriminoColumnToPlayfieldColumn(tetrimino, c);
                printf("Computed playfield position: %i %i\n", corresponding_p_row, corresponding_p_column);

                if ((corresponding_p_row < 0) || (corresponding_p_row > PLAY_AREA_HEIGHT_IN_TILES)){
                    printf("Row out of bounds.\n");
                    return -1;
                }

                if ((corresponding_p_column < 0 || (corresponding_p_column > PLAY_AREA_WIDTH_IN_TILES))){
                    printf("Column out of bounds.\n");
                    return -1;
                }

                // Check if the space is already occupied
                if (playfield->tiles[corresponding_p_row][corresponding_p_column].color != 0){
                    printf("Space already occupied.\n");
                    return -1;
                }

            }
        }
    }


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


void moveTetriminoToTheRight(tetrimino_t* tetrimino);

void moveTetriminoToTheLeft(tetrimino_t* tetrimino);

void makeTetriminoFallDownFast(tetrimino_t* tetrimino);

void rotateTetriminoCW(tetrimino_t* tetrimino);

void rotateTetriminoCCW(tetrimino_t* tetrimino);

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
    const TickType_t frameratePeriod = 100;

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
                        break;
                    case STATE_PAUSED:
                        vTaskSuspend(StatePlayingControlTask);
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

    play_area_t playfield = initPlayArea();
    tetrimino_t test = initTetrimino_T(Cyan);

    // Test-Tetrimino spawnen
    setPositionOfTetriminoViaCenter(&test, SPAWN_ROW, SPAWN_COLUMN);
    transferTetriminoColorsToPlayArea(&playfield, &test);

    int counter = 0;
    int pos_test = 0;

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE) {

                tumEventFetchEvents(FETCH_EVENT_BLOCK | FETCH_EVENT_NO_GL_CHECK); // Query events backend for new events, ie. button presses
                xGetButtonInput(); // Update global input    
                xSemaphoreTake(ScreenLock, portMAX_DELAY);    

                tumDrawClear(Gray);

                /* Current game functionality */

                counter++;

                removeTetriminoColorsFromCurrentPosition(&playfield, &test);
                setPositionOfTetriminoViaCenter(&test, SPAWN_ROW+counter, SPAWN_COLUMN);

                pos_test = checkNewTetriminoPosition(&playfield, &test);
                if (pos_test < 0){
                    setPositionOfTetriminoViaCenter(&test, SPAWN_ROW, SPAWN_COLUMN);
                    counter = 0;
                }

                transferTetriminoColorsToPlayArea(&playfield, &test);
                drawPlayArea(&playfield);

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

                tumDrawClear(White);
                tumDrawText(text,SCREEN_WIDTH/2-text_width/2, 5, TUMBlue);

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
        PRINT_ERROR("Failed to create buttons lock");
        goto err_buttons_lock;
    }

    // Message sending
    StateMachineQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateMachineQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_state_queue;
    }

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

    // pongInit();

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_state_paused_control_task:
    vTaskDelete(StatePausedControlTask);
err_state_playing_control_task:
    vTaskDelete(StatePlayingControlTask);
err_bufferswap:
    vTaskDelete(BufferSwap);
err_statemachine:
    vTaskDelete(SequentialStateMachine);

err_state_queue:
    vQueueDelete(StateMachineQueue);

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
