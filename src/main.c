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
static aIO_handle_t master_UDP_handle = NULL;

//This is the MASTER

#define MOSI_PORT 1234
#define MISO_PORT 4321

#define IPv4_addr "127.0.0.1"

struct master_args {
	float prev_value;
};

void masterRecv(size_t recv_size, char *buffer, void *args)
{
    float recv_value = *((float *) buffer);

    struct master_args *my_master_args = (struct master_args *) args;

    printf("Previous value was %f, new value is %f\n", my_master_args->prev_value,
            recv_value);

    my_master_args->prev_value = recv_value;
}

void vDemoTask(void *pvParameters)
{
	tumDrawBindThread();

	static int random_value;

	struct master_args my_master_args = { 0 };

	master_UDP_handle =
		aIOOpenUDPSocket(IPv4_addr, MISO_PORT, sizeof(float),
				 masterRecv, (void *)&my_master_args);

	if (master_UDP_handle == NULL) {
		PRINT_ERROR("master UDP socket failed to open");
		exit(EXIT_FAILURE);
	}

	while (1) {
		tumEventFetchEvents(FETCH_EVENT_NONBLOCK);

		// Basic sleep of 1000 milliseconds
		vTaskDelay((TickType_t)1000);

		random_value = rand() % 100 + 1;

		if (aIOSocketPut(UDP, IPv4_addr, MOSI_PORT, (char *)&random_value,
				 sizeof(random_value)))
            PRINT_ERROR("Failed to send from master");
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
