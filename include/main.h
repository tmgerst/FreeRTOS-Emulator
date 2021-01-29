#ifndef __MAIN_H__
#define __MAIN_H__

#include "semphr.h"
#include "queue.h"
#include <SDL2/SDL_scancode.h>

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR
#define PRINT_TASK_ERROR(task) PRINT_ERROR("Failed to print task ##task");

extern SemaphoreHandle_t ScreenLock;
extern SemaphoreHandle_t DrawSignal;

extern QueueHandle_t StateMachineQueue;

typedef struct states{
    unsigned char state; 
    SemaphoreHandle_t lock;
} states_t;

extern states_t current_state;

extern const unsigned char main_menu_signal;
extern const unsigned char single_playing_signal;
extern const unsigned char single_paused_signal;
extern const unsigned char double_playing_signal;
extern const unsigned char double_paused_signal;
extern const unsigned char game_over_signal;

void vDrawFPS(void);
unsigned char getCurrentState(states_t* state);

#endif // __MAIN_H__
