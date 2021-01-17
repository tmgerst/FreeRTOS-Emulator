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
#define BUTTON_DEBOUNCE_DELAY 100

#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define mainGENERIC_PRIORITY 0

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

TaskHandle_t SpawnTetriminoTask = NULL;
TaskHandle_t MoveTetriminoOneDownTask = NULL;

TaskHandle_t MoveTetriminoToTheRightTask = NULL;
TaskHandle_t MoveTetriminoToTheLeftTask = NULL;

static QueueHandle_t StateMachineQueue = NULL;

SemaphoreHandle_t ScreenLock = NULL;
SemaphoreHandle_t DrawSignal = NULL;

SemaphoreHandle_t SpawnSignal = NULL;

SemaphoreHandle_t ButtonASignal = NULL;
SemaphoreHandle_t ButtonSSignal = NULL;
SemaphoreHandle_t ButtonDSignal = NULL;

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
    SemaphoreHandle_t lock;
} play_area_t;

static play_area_t playfield = { 0 };

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
    int playfield_row;      // row position of the tetrimino center in the play area 
    int playfield_column;    // column position of the tetrimino center in the play area
    SemaphoreHandle_t lock;
} tetrimino_t;

tetrimino_t tetrimino = { 0 };

tetrimino_t initTetrimino(tetrimino_t* t, char name, unsigned int color){
    t->name = name;
    t->color = color;
    t->playfield_row = 0;
    t->playfield_column = 0;

    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            t->grid[r][c] = 0;
        }
    }

    if (t->name == 'T'){
        t->grid[TETRIMINO_GRID_CENTER][TETRIMINO_GRID_CENTER-1] = color;
        t->grid[TETRIMINO_GRID_CENTER][TETRIMINO_GRID_CENTER] = color;
        t->grid[TETRIMINO_GRID_CENTER][TETRIMINO_GRID_CENTER+1] = color;
        t->grid[TETRIMINO_GRID_CENTER+1][TETRIMINO_GRID_CENTER] = color;
    }

    return *t;
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
int checkTetriminoPosition(play_area_t* p, tetrimino_t* t){
    int corresponding_p_row = 0;
    int corresponding_p_column = 0;

    for (int r = 0; r < TETRIMINO_GRID_HEIGHT; r++){
        for (int c = 0; c < TETRIMINO_GRID_WIDTH; c++){
            if (t->grid[r][c] != 0){

                corresponding_p_row = tetriminoRowToPlayfieldRow(t, r);
                corresponding_p_column = tetriminoColumnToPlayfieldColumn(t, c);
                // printf("Computed playfield position: %i %i\n", corresponding_p_row, corresponding_p_column);

                if ((corresponding_p_row < 0) || (corresponding_p_row >= PLAY_AREA_HEIGHT_IN_TILES)){
                    printf("Row out of bounds.\n");
                    return -1;
                }

                if ((corresponding_p_column < 0 || (corresponding_p_column >= PLAY_AREA_WIDTH_IN_TILES))){
                    printf("Column out of bounds.\n");
                    return -1;
                }

                // Check if the space is already occupied
                if (p->tiles[corresponding_p_row][corresponding_p_column].color != 0){
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


void checkForButtonInput(void){

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){

        // if A has been pressed, give binary semaphore
        if(buttons.buttons[KEYCODE(A)]){
            buttons.buttons[KEYCODE(A)] = 0;
            if(ButtonASignal){
                xSemaphoreGive(ButtonASignal); 
                xSemaphoreGive(buttons.lock);
            }
        }
        // if S has been pressed, send binary semaphore
        if (buttons.buttons[KEYCODE(S)]){
            buttons.buttons[KEYCODE(S)] = 0;
            if(ButtonSSignal){
                xSemaphoreGive(ButtonSSignal);
                xSemaphoreGive(buttons.lock);
            }
        }

        // if D has been pressed, send binary semaphore
        if (buttons.buttons[KEYCODE(D)]){
            buttons.buttons[KEYCODE(D)] = 0;
            if(ButtonDSignal){
                xSemaphoreGive(ButtonDSignal);
                xSemaphoreGive(buttons.lock);
            }
        }
    }
    xSemaphoreGive(buttons.lock);
}

void vSpawnTetrimino(void *pvParameters){
    // Hier eine queue einfügen über die das nächste Tetrimino ankommt

    while(1){

        if (SpawnSignal){
            if (xSemaphoreTake(SpawnSignal, 0) == pdTRUE){

                if(tetrimino.lock){
                    if (xSemaphoreTake(tetrimino.lock, 0) == pdTRUE){   

                        tetrimino = initTetrimino(&tetrimino, 'T', Cyan);
                        setPositionOfTetriminoViaCenter(&tetrimino, SPAWN_ROW, SPAWN_COLUMN);

                        if (playfield.lock) {
                            if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){
                                transferTetriminoColorsToPlayArea(&playfield, &tetrimino);
                                xSemaphoreGive(playfield.lock);
                                xSemaphoreGive(tetrimino.lock);
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

    TickType_t last_change = xTaskGetTickCount();

    while(1){
        // Hier muss noch ein Zeitgeber rein, in welchem Intervall das Tetrimino eins runterfallen soll
        if (tetrimino.lock){
            if (xSemaphoreTake(tetrimino.lock, 0) == pdTRUE){

                if (playfield.lock) {
                    if (xSemaphoreTake(playfield.lock, 0) == pdTRUE){

                        backup_row = tetrimino.playfield_row;
                        backup_column = tetrimino.playfield_column;

                        removeTetriminoColorsFromCurrentPosition(&playfield, &tetrimino);
                        setPositionOfTetriminoViaCenter(&tetrimino, backup_row+1, backup_column);
                        flag = checkTetriminoPosition(&playfield, &tetrimino);

                        // Falls Kollision->neu spawnen
                        if (flag != 0) {
                            xSemaphoreGive(SpawnSignal);
                        }
                        else{
                            transferTetriminoColorsToPlayArea(&playfield, &tetrimino);
                            backup_row = 0;
                            backup_row = 0;
                        }

                        xSemaphoreGive(playfield.lock);
                        xSemaphoreGive(tetrimino.lock);
                    }
                    xSemaphoreGive(playfield.lock);
                }
            }
            xSemaphoreGive(tetrimino.lock);
        }
        vTaskDelayUntil(&last_change, 100);
    }
}

void vMoveTetriminoToTheRight(void *pvParameters){
    int backup_row = 0;
    int backup_column = 0;
    int flag = 0;

    TickType_t last_change = xTaskGetTickCount();

    while(1){

        if (ButtonDSignal){
            if (xSemaphoreTake(ButtonDSignal, portMAX_DELAY) == pdTRUE){

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

                                    // Falls Kollision->neu spawnen
                                    if (flag != 0) {
                                        xSemaphoreGive(SpawnSignal);
                                    }
                                    else{
                                        transferTetriminoColorsToPlayArea(&playfield, &tetrimino);
                                        backup_row = 0;
                                        backup_row = 0;
                                    }

                                    xSemaphoreGive(playfield.lock);
                                    xSemaphoreGive(tetrimino.lock);
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

        if (ButtonASignal){
            if (xSemaphoreTake(ButtonASignal, portMAX_DELAY) == pdTRUE){

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

                                    // Falls Kollision->neu spawnen
                                    if (flag != 0) {
                                        xSemaphoreGive(SpawnSignal);
                                    }
                                    else{
                                        transferTetriminoColorsToPlayArea(&playfield, &tetrimino);
                                        backup_row = 0;
                                        backup_row = 0;
                                    }

                                    xSemaphoreGive(playfield.lock);
                                    xSemaphoreGive(tetrimino.lock);
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

void makeTetriminoFallDownFast(void *pvParameters);

void rotateTetriminoCW(void *pvParameters);

void rotateTetriminoCCW(void *pvParameters);

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
    const TickType_t frameratePeriod = 20;

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
                        vTaskResume(SpawnTetriminoTask);
                        vTaskResume(MoveTetriminoOneDownTask);
                        vTaskResume(MoveTetriminoToTheRightTask);
                        vTaskResume(MoveTetriminoToTheLeftTask);
                        break;
                    case STATE_PAUSED:
                        vTaskSuspend(StatePlayingControlTask);
                        vTaskSuspend(SpawnTetriminoTask);
                        vTaskSuspend(MoveTetriminoOneDownTask);
                        vTaskSuspend(MoveTetriminoToTheRightTask);
                        vTaskSuspend(MoveTetriminoToTheLeftTask);

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

    playfield = initPlayArea(&playfield);
    tetrimino = initTetrimino(&tetrimino, 'T', Cyan);

    xSemaphoreGive(SpawnSignal);

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE) {

                tumEventFetchEvents(FETCH_EVENT_BLOCK | FETCH_EVENT_NO_GL_CHECK); // Query events backend for new events, ie. button presses
                xGetButtonInput(); // Update global input    
                xSemaphoreTake(ScreenLock, portMAX_DELAY);    

                tumDrawClear(Gray);

                /* Current game functionality */
                checkForButtonInput();

                if (playfield.lock){
                    if (xSemaphoreTake(playfield.lock, portMAX_DELAY) == pdTRUE){
                        drawPlayArea(&playfield);
                    }
                    xSemaphoreGive(playfield.lock);
                }
                xSemaphoreGive(playfield.lock);

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

    SpawnSignal = xSemaphoreCreateBinary();
    if (!SpawnSignal) {
        PRINT_ERROR("Failed to create spawn signal.");
        goto err_spawn_signal;
    }

    ButtonASignal = xSemaphoreCreateBinary();
    if (!ButtonASignal) {
        PRINT_ERROR("Failed to create button A signal.");
        goto err_button_a_signal;
    }

    ButtonSSignal = xSemaphoreCreateBinary();
    if (!ButtonSSignal) {
        PRINT_ERROR("Failed to create button S signal.");
        goto err_button_s_signal;
    }

    ButtonDSignal = xSemaphoreCreateBinary();
    if (!ButtonDSignal) {
        PRINT_ERROR("Failed to create button D signal.");
        goto err_button_d_signal;
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

    vTaskSuspend(SpawnTetriminoTask);
    vTaskSuspend(MoveTetriminoOneDownTask);
    vTaskSuspend(MoveTetriminoToTheRightTask);
    vTaskSuspend(MoveTetriminoToTheLeftTask);

    // pongInit();

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_move_tetrimino_left:
    vTaskDelete(MoveTetriminoToTheLeftTask);
err_move_tetrimino_right:
    vTaskDelete(MoveTetriminoToTheRightTask);
err_move_one_down_task:
    vTaskDelete(MoveTetriminoOneDownTask);
err_spawn_tetrimino_task:
    vTaskDelete(SpawnTetriminoTask);

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

err_button_d_signal:
    vSemaphoreDelete(ButtonDSignal);
err_button_s_signal:
    vSemaphoreDelete(ButtonSSignal);
err_button_a_signal:
    vSemaphoreDelete(ButtonASignal);
err_spawn_signal:
    vSemaphoreDelete(SpawnSignal);

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
