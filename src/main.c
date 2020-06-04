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

static TaskHandle_t DemoTask = NULL;
static aIO_handle_t slave_UDP_handle = NULL;

//This is the SLAVE

#define MOSI_PORT 1234
#define MISO_PORT 4321

#define IPv4_addr "127.0.0.1"

struct slave_args {
    int prev_value;
};

void slaveRecv(size_t recv_size, char *buffer, void *args)
{
    int recv_value = *((int *) buffer);
    float sqrt_value = sqrtf(recv_value);

    struct slave_args *my_slave_args = (struct slave_args *) args;

    printf("Prev sent value was %d, new sqrt is %d\n", my_slave_args->prev_value, 
            recv_value);

    my_slave_args->prev_value = recv_value;

    printf("Received %d, sqrt is %f\n", recv_value, sqrt_value);

   if(aIOSocketPut(UDP, IPv4_addr, MISO_PORT, (char *)&sqrt_value, sizeof(sqrt_value)))
       PRINT_ERROR("Failed to send sqrt to master");
}

void vDemoTask(void *pvParameters)
{
    tumDrawBindThread();

    struct slave_args my_slave_args = {0};

    slave_UDP_handle = aIOOpenUDPSocket(IPv4_addr, MOSI_PORT, sizeof(int), 
            slaveRecv, (void *) &my_slave_args);

    if(slave_UDP_handle == NULL){
        PRINT_ERROR("Slave UDP socket failed to open");
        exit(EXIT_FAILURE);
    }

    while (1) {
        tumEventFetchEvents(FETCH_EVENT_NONBLOCK);

        // Basic sleep of 1000 milliseconds
        vTaskDelay((TickType_t)1000);
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

    if (xTaskCreate(vDemoTask, "DemoTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &DemoTask) != pdPASS) {
        goto err_demotask;
    }

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_demotask:
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
