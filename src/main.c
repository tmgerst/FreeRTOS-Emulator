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

#include "tetris_gameplay.h"
#include "main.h"

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

#define STATE_QUEUE_LENGTH 1

#define STARTING_STATE 0
#define STATE_PLAYING 0
#define STATE_PAUSED 1

#define STATE_CHANGE_DELAY 300

#define NUMBER_OF_STATES 2

#ifdef TRACE_FUNCTIONS
#include "tracer.h"
#endif

const unsigned char next_state_signal = NEXT_TASK;

static TaskHandle_t SequentialStateMachine = NULL;
static TaskHandle_t BufferSwap = NULL;

QueueHandle_t StateMachineQueue = NULL;


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

// State Machine Functionality ###################################

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
                        vTaskSuspend(TetrisStatePausedTask);

                        vTaskResume(TetrisStatePlayingTask);
                        vTaskResume(GenerateTetriminoPermutationsTask);
                        vTaskResume(SpawnTetriminoTask);
                        vTaskResume(MoveTetriminoOneDownTask);
                        vTaskResume(MoveTetriminoToTheRightTask);
                        vTaskResume(MoveTetriminoToTheLeftTask);
                        vTaskResume(RotateTetriminoCWTask);
                        vTaskResume(RotateTetriminoCCWTask);
                        break;

                    case STATE_PAUSED:
                        vTaskSuspend(TetrisStatePlayingTask);
                        vTaskSuspend(GenerateTetriminoPermutationsTask);
                        vTaskSuspend(SpawnTetriminoTask);
                        vTaskSuspend(MoveTetriminoOneDownTask);
                        vTaskSuspend(MoveTetriminoToTheRightTask);
                        vTaskSuspend(MoveTetriminoToTheLeftTask);
                        vTaskSuspend(RotateTetriminoCWTask);
                        vTaskSuspend(RotateTetriminoCCWTask);

                        vTaskResume(TetrisStatePausedTask);
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

    // Message sending
    StateMachineQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateMachineQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_state_queue;
    }

    // Tasks
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

    tetrisInit();

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_bufferswap:
    vTaskDelete(BufferSwap);
err_statemachine:
    vTaskDelete(SequentialStateMachine);

err_state_queue:
    vQueueDelete(StateMachineQueue);

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
