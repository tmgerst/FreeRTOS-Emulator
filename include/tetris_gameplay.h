#ifndef __TETRIS_GAMEPLAY_H__
#define __TETRIS_GAMEPLAY_H__

#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)

extern TaskHandle_t MainMenuTask;
extern TaskHandle_t TetrisStatePlayingTask;
extern TaskHandle_t TetrisStatePausedTask;
extern TaskHandle_t GameOverScreenTask;

extern TaskHandle_t GenerateTetriminoPermutationsTask;
extern TaskHandle_t SpawnTetriminoTask;
extern TaskHandle_t MoveTetriminoOneDownTask;
extern TaskHandle_t MoveTetriminoToTheRightTask;
extern TaskHandle_t MoveTetriminoToTheLeftTask;
extern TaskHandle_t RotateTetriminoCWTask;
extern TaskHandle_t RotateTetriminoCCWTask;

extern TaskHandle_t ResetGameTask;
extern TaskHandle_t ChangeLevelTask;
extern TaskHandle_t ChangePlayModeTask;

int tetrisInit(void);

#endif // __TETRIS_GAMEPLAY_H__