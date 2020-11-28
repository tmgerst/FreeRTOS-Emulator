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
#define DEBOUNCE_DELAY 200
#define MOUSE_MOVEMENT_DElAY 200
#define OFFSET_ATTENUATION 0.2                  // attenuation for shaking the screen

// static TaskHandle_t DemoTask = NULL;
static TaskHandle_t BigDrawingTask = NULL;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

void vDemoTask(void *pvParameters)
{
    // structure to store time retrieved from Linux kernel
    static struct timespec the_time;
    static char our_time_string[100];
    static int our_time_strings_width = 0;

    // Needed such that Gfx library knows which thread controlls drawing
    // Only one thread can call tumDrawUpdateScreen while and thread can call
    // the drawing functions to draw objects. This is a limitation of the SDL
    // backend.
    tumDrawBindThread();

    while (1) {
        tumEventFetchEvents(FETCH_EVENT_NONBLOCK); // Query events backend for new events, ie. button presses
        xGetButtonInput(); // Update global input

        // `buttons` is a global shared variable and as such needs to be
        // guarded with a mutex, mutex must be obtained before accessing the
        // resource and given back when you're finished. If the mutex is not
        // given back then no other task can access the reseource.
        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
            if (buttons.buttons[KEYCODE(
                                    Q)]) { // Equiv to SDL_SCANCODE_Q
                exit(EXIT_SUCCESS);
            }
            xSemaphoreGive(buttons.lock);
        }

        tumDrawClear(White); // Clear screen

        clock_gettime(CLOCK_REALTIME,
                      &the_time); // Get kernel real time

        // Format our string into our char array
        sprintf(our_time_string,
                "There has been %ld seconds since the Epoch. Press Q to quit",
                (long int)the_time.tv_sec);

        // Get the width of the string on the screen so we can center it
        // Returns 0 if width was successfully obtained
        if (!tumGetTextSize((char *)our_time_string,
                            &our_time_strings_width, NULL))
            tumDrawText(our_time_string,
                        SCREEN_WIDTH / 2 -
                        our_time_strings_width / 2,
                        SCREEN_HEIGHT / 2 - DEFAULT_FONT_SIZE / 2,
                        TUMBlue);

        tumDrawUpdateScreen(); // Refresh the screen to draw string

        // Basic sleep of 1000 milliseconds
        vTaskDelay((TickType_t)1000);
    }
}

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

    // Demo Task not needed currently
    /*
    if (xTaskCreate(vDemoTask, "DemoTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &DemoTask) != pdPASS) {
        goto err_demotask;
    }
    */

    
    if (xTaskCreate(vBigDrawingTask, "BigDrawingTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &BigDrawingTask) != pdPASS) {
        goto err_big_drawing;
    }

    vTaskStartScheduler();

    return EXIT_SUCCESS;

// err_demotask:
err_big_drawing:
    vTaskDelete(vBigDrawingTask);
    vSemaphoreDelete(buttons.lock);
err_buttons_lock:
    tumSoundExit();
err_init_audio:
    tumEventExit();
err_init_events:
    tumDrawExit();
err_init_drawing:
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
