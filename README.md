# FreeRTOS Emulator - Implementation of ESPL Exercises 2 & 3

Exercises done by: Tim Gerstewitz (GitHub: tmgerst)

## Exercise 3

Some basic explanations and answers to theory questions are given here.

### Exercise 3.1

What is the kernel tick?
> The kernel tick is a tick counter variable that is periodically updated by an interrupt. The interrupt also handles blocking/readying taks.

What is a tickless kernel?
> A tickless kernel stops ther periodic kernel tick during periods when there are no application tasks that are able to execute. The tick count is corrected
when the tick interrupt is restarted. This can be used to save power.

### Exercise 3.2

#### Exercise 3.2.2

The task handling the blue circle signals has a +1 priority compared to the task responsible for the red circle signals.

What happens if the stack size for a statically allocated task is too low?
> The task stack overflows.

### Exercise 3.2.3

First button used:   F (uses a binary semaphore for the corresponding counter task)
Second button used:  G (uses a task notification for the corresponding counter task)

### Exercise 3.2.4

Button used: S

## Exercise 4

The four tick tasks are writing to a mutex-protected 5x15 array in the order that they are called by the scheduler, with the task that executes every tick also writing the tick number into the first column of the array.
The array is then drawn to the screen by a drawing task similar to the ones used in the other two screens.

### Exercise 4.0.3

What can you observe when playing around with the priorities of the tick tasks?
> Tasks are executed according to their priority level, i.e. a task with priority 4 executes before a task with priority 2. Therefore, a task with a higher priority can get ahold of mutexes or send to queues earlier than a task with lower priority. This also holds true when a task blocks on a semaphore given by another task with lower priority: In this case, the lower priority task giving the semaphore is preempted by the higher priority task obtaining it.
> When all tasks have the same priority, they are at first handled by the scheduler in the order they were created, but can be handled in another order after that. In the case of equal priorities it can also be noted that a task that blocks on a semaphore given by another task is always executed after the task that gives the semaphore.
