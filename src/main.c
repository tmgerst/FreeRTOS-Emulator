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

void vDrawStaticItems(void){
        coord_t triangle_points[3] = {{0.5*SCREEN_WIDTH, 0.5*SCREEN_HEIGHT-25},     // 18 as offset: all points of the triangle are distanced
                             {0.5*SCREEN_WIDTH-18, 0.5*SCREEN_HEIGHT+18},           // approximately 25 pixels from the center of the screen
                             {0.5*SCREEN_WIDTH+18, 0.5*SCREEN_HEIGHT+18}} ;         // 25 = sqrt(2a²) --> a ~ 18

        static char not_moving_text[70] = "A circle, a triangle and a square walk into a bar ...";
        static int not_moving_text_width = 0;

        // Get the width of the string on the screen so we can center it
        // Returns 0 if width was successfully obtained
        if (!tumGetTextSize((char *)not_moving_text,
                            &not_moving_text_width, NULL))
            tumDrawText(not_moving_text,
                        SCREEN_WIDTH / 2 -
                        not_moving_text_width / 2,
                        0.875*SCREEN_HEIGHT - DEFAULT_FONT_SIZE / 2,
                        Orange);

        tumDrawTriangle(triangle_points, Silver);
}

// Calculates coordinates for circular movement around the center of the screen
coord_t update_shape_positions(int radius, double radian){
    int x_coord = SCREEN_WIDTH/2 + radius * cos(radian);
    int y_coord = SCREEN_HEIGHT/2 - radius * sin(radian);
    coord_t new_positions = {x_coord, y_coord};
    return new_positions;
}

// Checks if the moving text has hit a screen boundary. If so, the direction of movement is inverted.
int invert_direction_on_collision(char* text, coord_t current_position, int offset_per_call, int direction){

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
    if (future_position > SCREEN_WIDTH || future_position < 15){
        direction = direction*(-1);
    }
    return direction;
}

coord_t update_text_position(coord_t current_position, int offset_per_call, int direction){
    current_position.x = current_position.x + offset_per_call*direction;
    coord_t new_position = {current_position.x, current_position.y};    
    return new_position;
}

void vButtonPresses(int* button_counter){
    static char button_buffer[40] = { 0 };

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {

        if (buttons.buttons[KEYCODE(A)]) { (*button_counter)++; }
        if (buttons.buttons[KEYCODE(B)]) { (*(button_counter+1))++; }
        if (buttons.buttons[KEYCODE(C)]) { (*(button_counter+2))++; }
        if (buttons.buttons[KEYCODE(D)]) { (*(button_counter+3))++; }

        sprintf(button_buffer, "A: %d | B: %d | C: %d | D: %d",
                *button_counter,
                *(button_counter+1),
                *(button_counter+2),
                *(button_counter+3));
        xSemaphoreGive(buttons.lock);

        tumDrawText(button_buffer, 10, DEFAULT_FONT_SIZE, Black);
    }
}

void vBigDrawingTask(void *pvParameters){

    char moving_text[50] = "Your advertisement here!";
    int moving_text_width = 0;
    tumGetTextSize((char *) moving_text, &moving_text_width, NULL);

    // Starting positions for moving shapes
    coord_t position_circle = {0.375*SCREEN_WIDTH, SCREEN_HEIGHT/2}; 
    coord_t position_square = {0.625*SCREEN_WIDTH-25, SCREEN_HEIGHT/2};
    coord_t position_moving_text = {SCREEN_WIDTH/2-moving_text_width/2, 0.125*SCREEN_HEIGHT-DEFAULT_FONT_SIZE/2};

    int button_counter[4] = {0, 0, 0, 0}; // Initializing button counter

    // text moves to the right: direction = 1
    // text moves to the left: direction = -1
    int direction = 1;
    int offset_per_call = 10;
    double radian = -M_PI; // start value for circle radian; square radian is this value + M_PI

    tumDrawBindThread();

    while(1){
        tumEventFetchEvents(FETCH_EVENT_NONBLOCK); // Query events backend for new events, ie. button presses
        xGetButtonInput(); // Update global input        

        tumDrawClear(White);
        vDrawStaticItems();

        direction = invert_direction_on_collision(moving_text, position_moving_text, offset_per_call, direction);
        radian = radian - 0.05*M_PI; // update radian for circular movement

        position_circle = update_shape_positions(RADIUS_FOR_CIRCLE_MOVEMENT, radian);
        position_square = update_shape_positions(RADIUS_FOR_CIRCLE_MOVEMENT, radian+M_PI); // +M_PI: square should be on opposite side of circle
        position_moving_text = update_text_position(position_moving_text, offset_per_call, direction);

        tumDrawCircle(position_circle.x, position_circle.y, 25, TUMBlue); 
        tumDrawFilledBox(position_square.x-25, position_square.y-25, 50, 50, Lime); // -25: offset for square, so that its center is circling with the movement radius
        tumDrawText(moving_text, position_moving_text.x, position_moving_text.y, Orange);

        vButtonPresses(button_counter);

        tumDrawUpdateScreen();
        vTaskDelay((TickType_t)50);
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
