/**
 * @file main.c
 * @author Tim Gerstewitz
 * @date 10th January 2021
 * @brief Source file for the main file of the tetris project. Contains the state machine and of course, the main fucntion.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Event.h"
#include "TUM_Font.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"

#include "AsyncIO.h"

#include "tetris_functionality.h"
#include "tetris_gameplay.h"
#include "main.h"

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

#define STATE_QUEUE_LENGTH 1

#define STARTING_STATE 0

#define MAIN_MENU               0
#define STATE_SINGLE_PLAYING    1
#define STATE_SINGLE_PAUSED     2
#define STATE_DOUBLE_PLAYING    3
#define STATE_DOUBLE_PAUSED     4
#define STATE_GAME_OVER         5

#define STATE_CHANGE_DELAY 150

#define NUMBER_OF_STATES 6

#ifdef TRACE_FUNCTIONS
#include "tracer.h"
#endif

static TaskHandle_t SequentialStateMachine = NULL;
static TaskHandle_t BufferSwap = NULL;

QueueHandle_t StateMachineQueue = NULL;

const unsigned char main_menu_signal = MAIN_MENU;
const unsigned char single_playing_signal = STATE_SINGLE_PLAYING;
const unsigned char single_paused_signal = STATE_SINGLE_PAUSED;
const unsigned char double_playing_signal = STATE_DOUBLE_PLAYING;
const unsigned char double_paused_signal = STATE_DOUBLE_PAUSED;
const unsigned char game_over_signal = STATE_GAME_OVER;


states_t current_state = { 0 };


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



unsigned char getCurrentState(states_t* state){
    return state->state;
}

/**
 * @brief Decides which state is the next to change to, based on queue input by the tasks.
 * 
 * @param state A pointer to hold the state that the state machine will change into.
 * @param queue_input The state that the state machine is supposed to change into.
 */
void changingStateAfterInput(unsigned char *state, unsigned char queue_input){
    *state = queue_input;
    if (*state >= NUMBER_OF_STATES || *state < STARTING_STATE){
        printf("State error. Exiting to main menu.\n");
        *state = STARTING_STATE;
    }
}

/**
 * @brief Implements a non-sequential state machine with six possible states in which different tasks get suspended
 * or blocked. Provides the framework for the tetris game.
 * 
 * @param pvParameters Current resources the RTOS provides.
 */
void vStateMachine(void *pvParameters){
    unsigned char state_at_the_moment = 0;
    unsigned char queue_input = 0;
    unsigned char change_state = 1;

    // Set initial state
    if (current_state.lock){
        if (xSemaphoreTake(current_state.lock, 0) == pdTRUE){
            current_state.state = STARTING_STATE;
            printf("Initial state: %u\n", current_state.state);
            xSemaphoreGive(current_state.lock);
        }
        xSemaphoreGive(current_state.lock);
    }

    TickType_t last_state_change = xTaskGetTickCount();

    while(1){
        // get current state
        if (current_state.lock){
            if (xSemaphoreTake(current_state.lock, 0) == pdTRUE){
                state_at_the_moment = getCurrentState(&current_state);
                xSemaphoreGive(current_state.lock);
            }
            xSemaphoreGive(current_state.lock);
        }

        if(change_state != 0){
            goto state_handling;
        }

        if (StateMachineQueue){
            if (xQueueReceive(StateMachineQueue, &queue_input, portMAX_DELAY) == pdTRUE){
                if (xTaskGetTickCount() - last_state_change > STATE_CHANGE_DELAY){

                    if (current_state.lock){
                        if (xSemaphoreTake(current_state.lock, portMAX_DELAY) == pdTRUE){
                            printf("Change state.\n");
                            changingStateAfterInput(&(current_state.state), queue_input);
                            state_at_the_moment = getCurrentState(&current_state);
                            change_state = 1;
                            last_state_change = xTaskGetTickCount();
                            xSemaphoreGive(current_state.lock);
                        }
                        xSemaphoreGive(current_state.lock);
                    }
                }
            }
        }

state_handling:
        if(StateMachineQueue){
            if(change_state != 0){
                printf("Current state: %u\n", state_at_the_moment);
                switch(state_at_the_moment){

                    case MAIN_MENU:
                        vTaskSuspend(TetrisStateSinglePlayingTask);
                        vTaskSuspend(TetrisStateSinglePausedTask);
                        vTaskSuspend(TetrisStateDoublePlayingTask);
                        vTaskSuspend(TetrisStateDoublePausedTask);
                        vTaskSuspend(GameOverScreenTask);

                        vTaskSuspend(UDPControlTask);

                        vTaskSuspend(GenerateTetriminoPermutationsTask);
                        vTaskSuspend(SpawnTetriminoTask);
                        vTaskSuspend(MoveTetriminoOneDownTask);
                        vTaskSuspend(MoveTetriminoToTheRightTask);
                        vTaskSuspend(MoveTetriminoToTheLeftTask);
                        vTaskSuspend(RotateTetriminoCWTask);
                        vTaskSuspend(RotateTetriminoCCWTask);
                        vTaskSuspend(ResetGameTask);
                        vTaskSuspend(ChangeGeneratorModeTask);

                        vTaskResume(MainMenuTask);
                        vTaskResume(ChangeLevelTask);
                        vTaskResume(ChangePlayModeTask);
                        break;                        

                    case STATE_SINGLE_PLAYING:
                        vTaskSuspend(MainMenuTask);
                        vTaskSuspend(TetrisStateSinglePausedTask);
                        vTaskSuspend(TetrisStateDoublePlayingTask);
                        vTaskSuspend(TetrisStateDoublePausedTask);
                        vTaskSuspend(GameOverScreenTask);

                        vTaskSuspend(ChangeLevelTask);
                        vTaskSuspend(ChangePlayModeTask);
                        vTaskSuspend(ChangeGeneratorModeTask);
                        vTaskSuspend(UDPControlTask);

                        vTaskResume(GenerateTetriminoPermutationsTask);
                        vTaskResume(SpawnTetriminoTask);
                        vTaskResume(MoveTetriminoOneDownTask);
                        vTaskResume(MoveTetriminoToTheRightTask);
                        vTaskResume(MoveTetriminoToTheLeftTask);
                        vTaskResume(RotateTetriminoCWTask);
                        vTaskResume(RotateTetriminoCCWTask);
                        vTaskResume(ResetGameTask);

                        vTaskResume(TetrisStateSinglePlayingTask);
                        break;

                    case STATE_SINGLE_PAUSED:
                        vTaskSuspend(MainMenuTask);
                        vTaskSuspend(TetrisStateSinglePlayingTask);
                        vTaskSuspend(TetrisStateDoublePlayingTask);
                        vTaskSuspend(TetrisStateDoublePausedTask);
                        vTaskSuspend(GameOverScreenTask);

                        vTaskSuspend(GenerateTetriminoPermutationsTask);
                        vTaskSuspend(SpawnTetriminoTask);
                        vTaskSuspend(MoveTetriminoOneDownTask);
                        vTaskSuspend(MoveTetriminoToTheRightTask);
                        vTaskSuspend(MoveTetriminoToTheLeftTask);
                        vTaskSuspend(RotateTetriminoCWTask);
                        vTaskSuspend(RotateTetriminoCCWTask);

                        vTaskSuspend(ChangeLevelTask);
                        vTaskSuspend(ChangePlayModeTask);
                        vTaskSuspend(ChangeGeneratorModeTask);
                        vTaskSuspend(UDPControlTask);

                        vTaskResume(TetrisStateSinglePausedTask);
                        vTaskResume(ResetGameTask);
                        break;

                    case STATE_DOUBLE_PLAYING:
                        vTaskSuspend(MainMenuTask);
                        vTaskSuspend(TetrisStateSinglePlayingTask);
                        vTaskSuspend(TetrisStateSinglePausedTask);
                        vTaskSuspend(TetrisStateDoublePausedTask);
                        vTaskSuspend(GameOverScreenTask);

                        vTaskSuspend(ChangeLevelTask);
                        vTaskSuspend(ChangePlayModeTask);

                        vTaskSuspend(GenerateTetriminoPermutationsTask);

                        vTaskResume(SpawnTetriminoTask);
                        vTaskResume(MoveTetriminoOneDownTask);
                        vTaskResume(MoveTetriminoToTheRightTask);
                        vTaskResume(MoveTetriminoToTheLeftTask);
                        vTaskResume(RotateTetriminoCWTask);
                        vTaskResume(RotateTetriminoCCWTask);
                        vTaskResume(ResetGameTask);

                        vTaskResume(TetrisStateDoublePlayingTask);
                        vTaskResume(ChangeGeneratorModeTask);
                        vTaskResume(UDPControlTask);
                        break;

                    case STATE_DOUBLE_PAUSED:
                        vTaskSuspend(MainMenuTask);
                        vTaskSuspend(TetrisStateSinglePlayingTask);
                        vTaskSuspend(TetrisStateSinglePausedTask);
                        vTaskSuspend(TetrisStateDoublePlayingTask);
                        vTaskSuspend(GameOverScreenTask);

                        vTaskSuspend(ChangeLevelTask);
                        vTaskSuspend(ChangePlayModeTask);

                        vTaskSuspend(GenerateTetriminoPermutationsTask);
                        vTaskSuspend(SpawnTetriminoTask);
                        vTaskSuspend(MoveTetriminoOneDownTask);
                        vTaskSuspend(MoveTetriminoToTheRightTask);
                        vTaskSuspend(MoveTetriminoToTheLeftTask);
                        vTaskSuspend(RotateTetriminoCWTask);
                        vTaskSuspend(RotateTetriminoCCWTask);

                        vTaskResume(TetrisStateDoublePausedTask);
                        vTaskResume(ResetGameTask);
                        vTaskResume(UDPControlTask);
                        vTaskResume(ChangeGeneratorModeTask);
                        break;                        

                    case STATE_GAME_OVER:
                        vTaskSuspend(MainMenuTask);
                        vTaskSuspend(TetrisStateSinglePlayingTask);
                        vTaskSuspend(TetrisStateSinglePausedTask);
                        vTaskSuspend(TetrisStateDoublePlayingTask);
                        vTaskSuspend(TetrisStateDoublePausedTask);

                        vTaskSuspend(GenerateTetriminoPermutationsTask);
                        vTaskSuspend(SpawnTetriminoTask);
                        vTaskSuspend(MoveTetriminoOneDownTask);
                        vTaskSuspend(MoveTetriminoToTheRightTask);
                        vTaskSuspend(MoveTetriminoToTheLeftTask);
                        vTaskSuspend(RotateTetriminoCWTask);
                        vTaskSuspend(RotateTetriminoCCWTask);

                        vTaskSuspend(ChangeLevelTask);
                        vTaskSuspend(ChangePlayModeTask);
                        vTaskSuspend(ChangeGeneratorModeTask);
                        vTaskSuspend(UDPControlTask);

                        vTaskResume(GameOverScreenTask);
                        vTaskResume(ResetGameTask);
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

    // Semaphores
    current_state.lock = xSemaphoreCreateMutex();
    if (!current_state.lock){
        PRINT_ERROR("Could not create current state lock.");
        goto err_current_state_lock;
    }

    // Message sending
    StateMachineQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateMachineQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_state_queue;
    }

    // Tasks
    if (xTaskCreate(vStateMachine, "SequentialStateMachine",
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

    tetrisFunctionalityInit();
    tetrisGameplayInit();

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_bufferswap:
    vTaskDelete(BufferSwap);
err_statemachine:
    vTaskDelete(SequentialStateMachine);

err_state_queue:
    vQueueDelete(StateMachineQueue);

err_current_state_lock:
    vSemaphoreDelete(current_state.lock);

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
