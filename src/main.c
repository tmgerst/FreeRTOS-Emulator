#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_Font.h"

#include "AsyncIO.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)

#define RADIUS_FOR_CIRCLE_MOVEMENT 80
#define APPROX_OFFSET_FOR_TEXT_DEFLECTION 15    // Offset to prevent text from going a little bit into the screen wall, 
                                                // since text box is slightly wider than the actual width of the text
#define HARMONIC_MOVEMENT_CONSTANT 1
#define DEBOUNCE_DELAY 150
#define MOUSE_MOVEMENT_DElAY 200
#define OFFSET_ATTENUATION 0.2                  // attenuation for shaking the screen


#define STATE_MACHINE_QUEUE_LENGTH 1
#define NUMBER_OF_STATES 3
#define STATE_CHANGE_DELAY 150

#define STARTING_STATE 0
#define NEXT_STATE 1

#define STATE_EX_TWO 0
#define STATE_EX_THREE 1
#define STATE_EX_FOUR 2

#define FPS_AVERAGE_COUNT 50

#define STACK_SIZE 100
#define FIRST_BLINKING_PERIOD 1000 // in ticks, so milliseconds
#define SECOND_BLINKING_PERIOD 500

#define TIMER_PERIOD 15000

//static TaskHandle_t BigDrawingTask = NULL;
static TaskHandle_t StateMachine = NULL;
static TaskHandle_t BufferSwapTask = NULL;
static TaskHandle_t FirstScreenTask = NULL;
static TaskHandle_t SecondScreenTask = NULL;
static TaskHandle_t ThirdScreenTask = NULL;
static TaskHandle_t BlueBlinkingCircleTask = NULL;
static TaskHandle_t RedBlinkingCircleTask = NULL;
static TaskHandle_t ButtonAHandlerTask = NULL;
static TaskHandle_t ButtonBHandlerTask = NULL;

// Variables needed for static allocation 
static StaticTask_t xIdleTaskTCB;
static StackType_t uxIdleTaskStack[ 1 ];
static StaticTask_t xTimerTaskTCB;
static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];
static StaticTask_t xTaskBuffer;
static StackType_t xStack[ STACK_SIZE ];

static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;
static SemaphoreHandle_t BlueCircleSignal = NULL;
static SemaphoreHandle_t RedCircleSignal = NULL;
static SemaphoreHandle_t ButtonASignal = NULL;

static QueueHandle_t StateMachineQueue = NULL;

static xTimerHandle ResetButtonsABTimer = NULL;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

typedef struct counter {
    int counter_a;
    int counter_b;
    SemaphoreHandle_t lock;
} counter_t;

static counter_t counter = { 0 };

const unsigned char next_state_signal = NEXT_STATE;

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
    /* Pass out a pointer to the StaticTask_t structure in which the Idle task’s
    state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task’s stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize )
{
    /* Pass out a pointer to the StaticTask_t structure in which the Timer
    task’s state will be stored. */
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

    /* Pass out the array that will be used as the Timer task’s stack. */
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;

    /* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configTIMER_TASK_STACK_DEPTH is specified in words, not bytes. */
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

// ------------------- Code for Exercise 2 -----------------------------------------------------------------------------------------------

// Calculates the offset of the mouse position relative to the center of the screen.
coord_t offsetDueToMouse(double attenuation){
    int x_offset = (tumEventGetMouseX() - SCREEN_WIDTH/2) * attenuation;
    int y_offset = (tumEventGetMouseY() - SCREEN_HEIGHT/2) * attenuation;
    coord_t mouse_offset = {x_offset, y_offset};
    return mouse_offset;
}

// Draws static triangle and text and updates their position according to the mouse movement.
void vDrawStaticItems(coord_t mouse_offset){
        coord_t triangle_points[3] = {{0.5*SCREEN_WIDTH+mouse_offset.x, 0.5*SCREEN_HEIGHT-25+mouse_offset.y},     // 18 as offset: all points of the triangle are distanced
                             {0.5*SCREEN_WIDTH-18+mouse_offset.x, 0.5*SCREEN_HEIGHT+18+mouse_offset.y},           // approximately 25 pixels from the center of the screen
                             {0.5*SCREEN_WIDTH+18+mouse_offset.x, 0.5*SCREEN_HEIGHT+18+mouse_offset.y}} ;         // 25 = sqrt(2a²) --> a ~ 18

        static char not_moving_text[70] = "A circle, a triangle and a square walk into a bar ...";
        static int not_moving_text_width = 0;

        // Get the width of the string on the screen so we can center it
        // Returns 0 if width was successfully obtained
        if (!tumGetTextSize((char *)not_moving_text,
                            &not_moving_text_width, NULL))
            tumDrawText(not_moving_text,
                        SCREEN_WIDTH/2 -
                        not_moving_text_width/2 + mouse_offset.x,
                        0.875*SCREEN_HEIGHT - DEFAULT_FONT_SIZE/2 + mouse_offset.y,
                        Orange);

        tumDrawTriangle(triangle_points, Silver);
}

// Calculates coordinates for circular movement around the center of the screen.
coord_t updateShapePositions(int radius, double radian, coord_t mouse_offset){
    int x_coord = SCREEN_WIDTH/2 + radius * cos(radian) + mouse_offset.x;
    int y_coord = SCREEN_HEIGHT/2 - radius * sin(radian) + mouse_offset.y;
    coord_t new_positions = {x_coord, y_coord};
    return new_positions;
}

// Checks if the moving text has hit a screen boundary. If so, the direction of movement is inverted.
int invertDirectionOnCollision(char* text, coord_t current_position, int offset_per_call, int direction){

    int moving_text_width = 0;
    tumGetTextSize((char *) text, &moving_text_width, NULL);
    int future_position = 0;

    // if text moves to the right: direction = +1
    // if text moves to the left: direction = -1
    if (direction > 0){
        future_position = current_position.x + moving_text_width + offset_per_call; // if text moves to the right, the text width has to be added
    }
    else{
        future_position = current_position.x + offset_per_call; // if text moves to the left, text width is not important
    } 

    // printf("%i\n", future_position);
    
    // if text collides with screen boundaries, invert the direction
    if (future_position > SCREEN_WIDTH-APPROX_OFFSET_FOR_TEXT_DEFLECTION || future_position < APPROX_OFFSET_FOR_TEXT_DEFLECTION){
        direction = direction*(-1);
    }
    return direction;
}

// Calculates the next position for the moving text.
coord_t updateTextPosition(coord_t current_position, int movement_per_call, int direction){
    current_position.x = current_position.x + movement_per_call * direction;
    coord_t new_position = {current_position.x, current_position.y};    
    return new_position;
}

// Checks if left mouse button is pressed. If so, resets the button counters.
void vCheckMouseState(int* button_counter){
    signed char mouse_state_left = tumEventGetMouseLeft();
    if (mouse_state_left != 0) {
        *button_counter = 0;
        *(button_counter+1) = 0;
        *(button_counter+2) = 0;
        *(button_counter+3) = 0;
    }
}

// Increments button counters if one of them is pressed and prints counter values to the screen. Returns a Ticktype that acts as the new value for the last button change.
TickType_t vButtonPresses(int* button_counter, TickType_t last_button_change, coord_t mouse_offset){
    static char button_buffer[40] = { 0 };

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        // increment counter A if time is greater than debounce delay
        if (buttons.buttons[KEYCODE(A)]) {
            if(xTaskGetTickCount()-last_button_change > DEBOUNCE_DELAY) {
                (*button_counter)++;
                last_button_change = xTaskGetTickCount();
            }
        }
        // increment counter B if time is greater than debounce delay
        if (buttons.buttons[KEYCODE(B)]) { 
            if(xTaskGetTickCount()-last_button_change > DEBOUNCE_DELAY) {
                (*(button_counter+1))++;
                last_button_change = xTaskGetTickCount();
            }
        }
        // increment counter C if time is greater than debounce delay
        if (buttons.buttons[KEYCODE(C)]) {
            if(xTaskGetTickCount()-last_button_change > DEBOUNCE_DELAY) {
                (*(button_counter+2))++;
                last_button_change = xTaskGetTickCount();
            }
        }
        // increment counter D if time is greater than debounce delay
        if (buttons.buttons[KEYCODE(D)]) {
            if(xTaskGetTickCount()-last_button_change > DEBOUNCE_DELAY) {
                (*(button_counter+3))++;
                last_button_change = xTaskGetTickCount();
            }
        }

        sprintf(button_buffer, "A: %d | B: %d | C: %d | D: %d",
                *button_counter,
                *(button_counter+1),
                *(button_counter+2),
                *(button_counter+3));
        xSemaphoreGive(buttons.lock);
    }
    tumDrawText(button_buffer, 10+mouse_offset.x, 2*DEFAULT_FONT_SIZE+mouse_offset.y, Black);
    return last_button_change;
}

void vPrintMouseValues(coord_t mouse_offset) {
    static char values[20] = {0};
    int x_position = tumEventGetMouseX();
    int y_position = tumEventGetMouseY();

    sprintf(values, "x-Axis: %5d | y-Axis: %5d", x_position, y_position);
    tumDrawText(values, 10+mouse_offset.x, DEFAULT_FONT_SIZE+mouse_offset.y, Black);
}

void vBigDrawingTask(void *pvParameters){

    char moving_text[50] = "Your advertisement here!";
    int moving_text_width = 0;
    tumGetTextSize((char *) moving_text, &moving_text_width, NULL);

    // Starting positions for moving shapes
    coord_t position_circle = {0.375*SCREEN_WIDTH, SCREEN_HEIGHT/2}; 
    coord_t position_square = {0.625*SCREEN_WIDTH-25, SCREEN_HEIGHT/2};
    coord_t position_moving_text = {SCREEN_WIDTH/2-moving_text_width/2, 0.125*SCREEN_HEIGHT-DEFAULT_FONT_SIZE/2};

    // Initializing the offset values for shaking the screen with the mouse
    coord_t mouse_offset = {0, 0};

    int button_counter[4] = {0, 0, 0, 0}; // Initializing button counter
    TickType_t last_button_change = xTaskGetTickCount();

    // text moves to the right: direction = 1
    // text moves to the left: direction = -1
    int direction = 1;
    int movement_per_call = HARMONIC_MOVEMENT_CONSTANT*2;
    double radian = -M_PI; // start value for circle radian; square radian is this value + M_PI

    tumDrawBindThread();

    while(1){
        tumEventFetchEvents(FETCH_EVENT_NONBLOCK); // Query events backend for new events, ie. button presses
        xGetButtonInput(); // Update global input        

        tumDrawClear(White);

        mouse_offset = offsetDueToMouse(OFFSET_ATTENUATION);

        vPrintMouseValues(mouse_offset);     
        vCheckMouseState(button_counter);
        last_button_change = vButtonPresses(button_counter, last_button_change, mouse_offset);

        vDrawStaticItems(mouse_offset);

        direction = invertDirectionOnCollision(moving_text, position_moving_text, movement_per_call, direction);
        radian = radian - (HARMONIC_MOVEMENT_CONSTANT/(double)100)*M_PI; // update radian for circular movement

        position_circle = updateShapePositions(RADIUS_FOR_CIRCLE_MOVEMENT, radian, mouse_offset);
        position_square = updateShapePositions(RADIUS_FOR_CIRCLE_MOVEMENT, radian+M_PI, mouse_offset); // +M_PI: square should be on opposite side of circle
        position_moving_text = updateTextPosition(position_moving_text, movement_per_call, direction);

        tumDrawCircle(position_circle.x, position_circle.y, 25, TUMBlue); 
        tumDrawFilledBox(position_square.x-25, position_square.y-25, 50, 50, Lime); // -25: offset for square, so that its center is circling with the movement radius
        tumDrawText(moving_text, position_moving_text.x+mouse_offset.x, position_moving_text.y+mouse_offset.y, Orange);

        tumDrawUpdateScreen();
        vTaskDelay((TickType_t)HARMONIC_MOVEMENT_CONSTANT*10);
    }
}

// ----------------------------------- End of Code for Exercise 2 -----------------------------------------------------------------------

// ----------------------------------- Code for Exercise 3 ------------------------------------------------------------------------------

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
        *state = STATE_EX_TWO;
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
                    case STATE_EX_TWO:
                        vTaskSuspend(SecondScreenTask);
                        vTaskSuspend(ThirdScreenTask);

                        vTaskResume(FirstScreenTask);
                        // vTaskResume(BlueBlinkingCircleTask);
                        // vTaskResume(RedBlinkingCircleTask);
                        break;
                    case STATE_EX_THREE:
                        // vTaskSuspend(RedBlinkingCircleTask);
                        // vTaskSuspend(BlueBlinkingCircleTask);
                        vTaskSuspend(FirstScreenTask);
                        vTaskSuspend(ThirdScreenTask);

                        vTaskResume(SecondScreenTask);
                        break;
                    case STATE_EX_FOUR:
                        // vTaskSuspend(RedBlinkingCircleTask);
                        // vTaskSuspend(BlueBlinkingCircleTask);
                        vTaskSuspend(FirstScreenTask);
                        vTaskSuspend(SecondScreenTask);

                        vTaskResume(ThirdScreenTask);
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

void vSwapBuffers(void *pvParameters)
{
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t frameratePeriod = 10;

    tumDrawBindThread(); // Setup Rendering handle with correct GL context

    while (1) {
        if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
            tumDrawUpdateScreen();
            tumEventFetchEvents(FETCH_EVENT_BLOCK);
            xSemaphoreGive(ScreenLock);
            xSemaphoreGive(DrawSignal);
            vTaskDelayUntil(&xLastWakeTime,
                            pdMS_TO_TICKS(frameratePeriod));
        }
    }
}

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

    if (average_count < FPS_AVERAGE_COUNT) {
        average_count++;
    }
    else {
        periods_total -= periods[index];
    }

    xLastWakeTime = xTaskGetTickCount();

    if (prevWakeTime != xLastWakeTime) {
        periods[index] = configTICK_RATE_HZ / (xLastWakeTime - prevWakeTime);
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

    fps = periods_total / average_count;

    sprintf(str, "FPS: %2d", fps);

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        tumDrawText(str, SCREEN_WIDTH - text_width - 10, DEFAULT_FONT_SIZE, Black);
}

void vCheckForButtonInput(void){
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){

        // if A has been pressed, give binary semaphore
        if(buttons.buttons[KEYCODE(A)]){
            buttons.buttons[KEYCODE(A)] = 0;
            if(ButtonASignal){
                xSemaphoreGive(ButtonASignal); 
                xSemaphoreGive(buttons.lock);
            }
        }
        // if B has been pressed, send task notification
        if (buttons.buttons[KEYCODE(B)]){
            buttons.buttons[KEYCODE(B)] = 0;
            xTaskNotifyGive(ButtonBHandlerTask);
            xSemaphoreGive(buttons.lock);
        }
    }
    xSemaphoreGive(buttons.lock);
}

void vFirstScreenTask(void *pvParameter){  

    char first_text[30] = "Blinking Circles";
    int first_text_width = 0;
    tumGetTextSize((char *) first_text, &first_text_width, NULL); 

    char str[30] = {0};

    coord_t position_blue_circle = {0.25*SCREEN_WIDTH, 0.5*SCREEN_HEIGHT};
    coord_t position_red_circle = {0.75*SCREEN_WIDTH, 0.5*SCREEN_HEIGHT};

    unsigned char red_circle_drawn = 1;
    unsigned char blue_circle_drawn = 1;

    // int time_measured = 0;
    // TickType_t last_change = xTaskGetTickCount();

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE) {

                tumEventFetchEvents(FETCH_EVENT_BLOCK | FETCH_EVENT_NO_GL_CHECK);
                xGetButtonInput(); // Update global button data
                xSemaphoreTake(ScreenLock, portMAX_DELAY);

                // Draw white screen and headline
                tumDrawClear(White);
                if (!tumGetTextSize((char *)first_text, &first_text_width, NULL)){
                    tumDrawText(first_text, SCREEN_WIDTH/2 - first_text_width/2, SCREEN_HEIGHT/8 - DEFAULT_FONT_SIZE/2, TUMBlue);
                }

                // Check for input from either button A or button B
                vCheckForButtonInput();
                if(xSemaphoreTake(counter.lock, 0) == pdTRUE){
                    sprintf(str, "A: %3d | B: %3d", counter.counter_a, counter.counter_b);
                }
                xSemaphoreGive(counter.lock);
                tumDrawText(str, SCREEN_WIDTH-100, SCREEN_HEIGHT-DEFAULT_FONT_SIZE-5, Black);   // Draw button counters

                // Check for input from blue circle task and interpret it to draw or not draw the circle
                if (BlueCircleSignal){
                    if (xSemaphoreTake(BlueCircleSignal, 0)){
                        if (blue_circle_drawn == 0){
                            blue_circle_drawn = 1;
                        }
                        else{
                            blue_circle_drawn = 0;
                        }
                    }
                }

                // Check for input from red circle task and interpret it to draw or not draw the circle
                if (RedCircleSignal){
                    if (xSemaphoreTake(RedCircleSignal, 0)){
                        if(red_circle_drawn == 0){
                            red_circle_drawn = 1;
                        }
                        else{
                            red_circle_drawn = 0;
                        }
                    }
                }

                // if flags are set accordingly, draw the circles to the screen
                if (blue_circle_drawn == 0){
                    tumDrawCircle(position_blue_circle.x, position_blue_circle.y, 25, TUMBlue);
                }
                if (red_circle_drawn == 0){
                    tumDrawCircle(position_red_circle.x, position_red_circle.y, 25, Red);
                    /* if(time_measured == 0){
                        printf("Since last time: %i\n", xTaskGetTickCount() - last_change);
                        last_change = xTaskGetTickCount();
                        time_measured = 1;                        
                    }
                    */
                }
                /* else{
                    time_measured = 0;
                } */
                
                vDrawFPS();
                xSemaphoreGive(ScreenLock);
                vCheckForStateInput();
            }
        }
    }
}

void vSecondScreenTask(void *pvParameters){

    char second_text[30] = "Second Task";
    int second_text_width = 0;
    tumGetTextSize((char *) second_text, &second_text_width, NULL); 

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE) {

                tumEventFetchEvents(FETCH_EVENT_BLOCK | FETCH_EVENT_NO_GL_CHECK);
                xGetButtonInput(); // Update global button data
                xSemaphoreTake(ScreenLock, portMAX_DELAY);
                
                tumDrawClear(White);

                if (!tumGetTextSize((char *)second_text, &second_text_width, NULL)){
                    tumDrawText(second_text, SCREEN_WIDTH/2 - second_text_width/2, SCREEN_HEIGHT/2 - DEFAULT_FONT_SIZE/2, Lime);
                }

                xSemaphoreGive(ScreenLock);
                vCheckForStateInput();
            }
        }
    }
}

void vThirdScreenTask(void *pvParameters){

    char third_text[30] = "Third Task";
    int third_text_width = 0;
    tumGetTextSize((char *) third_text, &third_text_width, NULL); 

    while(1){
        if (DrawSignal){
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) == pdTRUE) {

                tumEventFetchEvents(FETCH_EVENT_BLOCK | FETCH_EVENT_NO_GL_CHECK);
                xGetButtonInput(); // Update global button data
                xSemaphoreTake(ScreenLock, portMAX_DELAY);
                
                tumDrawClear(White);

                if (!tumGetTextSize((char *)third_text, &third_text_width, NULL)){
                    tumDrawText(third_text, SCREEN_WIDTH/2 - third_text_width/2, SCREEN_HEIGHT/2 - DEFAULT_FONT_SIZE/2, Orange);
                }

                xSemaphoreGive(ScreenLock);
                vCheckForStateInput();
            }
        }
    }
}

void vBlueBlinkingCircle(void *pvParameters){

    TickType_t blue_last_wake_period = xTaskGetTickCount();
    
    while(1){
        if (BlueCircleSignal){
            vTaskDelayUntil(&blue_last_wake_period, FIRST_BLINKING_PERIOD/2);
            xSemaphoreGive(BlueCircleSignal);
        }
        vCheckForStateInput();
    }
}

void vRedBlinkingCircle(void *pvParameters){

    TickType_t red_last_wake_time = xTaskGetTickCount();
    
    while(1){
        if(RedCircleSignal){
            vTaskDelayUntil(&red_last_wake_time, SECOND_BLINKING_PERIOD/2);
            xSemaphoreGive(RedCircleSignal);
        }
        vCheckForStateInput();
    }    
}

void vButtonAHandler(void *pvParameters){
    TickType_t last_change = xTaskGetTickCount();

    while(1){
        if (ButtonASignal){
            xSemaphoreTake(ButtonASignal, portMAX_DELAY); // Blocking on binary semaphore
            if(xTaskGetTickCount() - last_change > DEBOUNCE_DELAY){ // Debounce button
                if(xSemaphoreTake(counter.lock, 0) == pdTRUE){
                    counter.counter_a++;
                }
                last_change = xTaskGetTickCount();
                xSemaphoreGive(counter.lock);
            }
        }
        vCheckForStateInput();
    }
}

void vButtonBHandler(void *pvParameters){
    TickType_t last_change = xTaskGetTickCount();

    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // Blocking on task notification
        if(xTaskGetTickCount() - last_change > DEBOUNCE_DELAY){ // Debounce button
            if(xSemaphoreTake(counter.lock, 0) == pdTRUE){
                counter.counter_b++;
            }
            last_change = xTaskGetTickCount();
            xSemaphoreGive(counter.lock);
        }
        vCheckForStateInput();
    }
}

void vResetButtonsAB(void *pvParameters){
    printf("Entering callback function.\n");
    if(xSemaphoreTake(counter.lock, 0) == pdTRUE){
        printf("Resetting button values.\n");
        counter.counter_a = 0;
        counter.counter_b = 0;
    }
    xSemaphoreGive(counter.lock);
}

int main(int argc, char *argv[])
{
    char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);

    printf("Initializing: ");

    if (tumDrawInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize drawing");
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

    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock");
        goto err_buttons_lock;
    }

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

    counter.lock = xSemaphoreCreateMutex(); // Locking of button counter variables
    if (!counter.lock) {
        PRINT_ERROR("Failed to create counter lock.");
        goto err_counter_lock;
    }

    BlueCircleSignal = xSemaphoreCreateBinary();
    if (!BlueCircleSignal) {
        PRINT_ERROR("Failed to create blue circle signal.");
        goto err_blue_circle_signal;
    }

    RedCircleSignal = xSemaphoreCreateBinary();
    if (!RedCircleSignal) {
        PRINT_ERROR("Failed to create red circle signal.");
        goto err_red_circle_signal;
    }

    ButtonASignal = xSemaphoreCreateBinary(); // Semaphore for signaling that button A is pressed
    if(!ButtonASignal){
        PRINT_ERROR("Failed to create button A signal.");
        goto err_button_a_signal;        
    }

    // Message sending
    StateMachineQueue = xQueueCreate(STATE_MACHINE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateMachineQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_state_machine_queue;
    }

    // Timers
    ResetButtonsABTimer = xTimerCreate("ResetButtonsABTimer", TIMER_PERIOD, pdTRUE, (void*) 0, vResetButtonsAB);
    if(!ResetButtonsABTimer){
        PRINT_ERROR("Could not create timer.");
        goto err_reset_buttons_ab_timer;
    }
    
    // ----------------------------------------Tasks----------------------------------------------------

    if (xTaskCreate(vSequentialStateMachine, "StateMachine", mainGENERIC_STACK_SIZE * 2, NULL,
                    configMAX_PRIORITIES-1, &StateMachine) != pdPASS) {
        goto err_state_machine;
    }

    if (xTaskCreate(vSwapBuffers, "BufferSwapTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    configMAX_PRIORITIES, &BufferSwapTask) != pdPASS) {
        goto err_buffer_swap_task;
    }

    // Blinking Circles
    if (xTaskCreate(vBlueBlinkingCircle, "BlueBlinkingCircleTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY+1, &BlueBlinkingCircleTask) != pdPASS) {
        goto err_blue_blinking_circle_task;
    }

    RedBlinkingCircleTask = xTaskCreateStatic(vRedBlinkingCircle, "RedBlinkingCircleTask", STACK_SIZE, NULL, 
                    mainGENERIC_PRIORITY, xStack, &xTaskBuffer);
    if (!RedBlinkingCircleTask) {
        goto err_red_blinking_circle_task;
    }

    // Button Handler Functions
    if (xTaskCreate(vButtonAHandler, "ButtonAHandlingTask", mainGENERIC_STACK_SIZE*2, NULL,
                    mainGENERIC_PRIORITY+1, &ButtonAHandlerTask) != pdPASS) {
        goto err_button_a_handler_task;
    }

    if (xTaskCreate(vButtonBHandler, "ButtonBHandlingTask", mainGENERIC_STACK_SIZE*2, NULL,
                    mainGENERIC_PRIORITY, &ButtonBHandlerTask) != pdPASS) {
        goto err_button_b_handler_task;
    } 
    
    // Screen drawing functions
    if (xTaskCreate(vFirstScreenTask, "FirstScreenTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY+2, &FirstScreenTask) != pdPASS) {
        goto err_first_screen_task;
    }

    if (xTaskCreate(vSecondScreenTask, "SecondScreenTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &SecondScreenTask) != pdPASS) {
        goto err_second_screen_task;
    }

    if (xTaskCreate(vThirdScreenTask, "ThirdScreenTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &ThirdScreenTask) != pdPASS) {
        goto err_third_screen_task;
    }

    xTimerStart(ResetButtonsABTimer, 0);    // timer starts once the scheduler starts, but can be called here nonetheless

    vTaskStartScheduler();

    return EXIT_SUCCESS;

// Exits in reverse order to init

// Deleting screen tasks
err_third_screen_task:
    vTaskDelete(ThirdScreenTask);
err_second_screen_task:
    vTaskDelete(SecondScreenTask);
err_first_screen_task:
    vTaskDelete(FirstScreenTask);

// Deleting other tasks
err_button_b_handler_task:
    vTaskDelete(ButtonBHandlerTask);
err_button_a_handler_task:
    vTaskDelete(ButtonAHandlerTask);
err_red_blinking_circle_task:
    vTaskDelete(RedBlinkingCircleTask);
err_blue_blinking_circle_task:
    vTaskDelete(BlueBlinkingCircleTask);
err_buffer_swap_task:
    vTaskDelete(BufferSwapTask);
err_state_machine:
    vTaskDelete(StateMachine);
err_state_machine_queue:
    vQueueDelete(StateMachineQueue);

// Deleting timers
err_reset_buttons_ab_timer:
    xTimerDelete(ResetButtonsABTimer, 0);

// Deleting semaphores
err_button_a_signal:
    vSemaphoreDelete(ButtonASignal);
err_red_circle_signal:
    vSemaphoreDelete(RedCircleSignal);
err_blue_circle_signal:
    vSemaphoreDelete(BlueCircleSignal);
err_counter_lock:
    vSemaphoreDelete(counter.lock);
err_screen_lock:
    vSemaphoreDelete(ScreenLock);
err_draw_signal:
    vSemaphoreDelete(DrawSignal);
err_buttons_lock:
    vSemaphoreDelete(buttons.lock);

// Exit other stuff
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
