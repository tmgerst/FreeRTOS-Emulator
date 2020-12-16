# FreeRTOS Emulator - Implementation of ESPL Exercises 2 & 3

Exercises done by: Tim Gerstewitz (GitHub: tmgerst)

## Exercise 3

Explanations and answers to theory questions.

### Exercise 3.1

What is the kernel tick?
> The kernel tick is a tick counter variable that is periodically updated by an interrupt. The interrupt also handles blocking/readying taks.

What is a tickless kernel?
> A tickless kernel stops ther periodic kernel tick during periods when there are no application tasks that are able to execute. The tick count is corrected
when the tick interrupt is restarted. This can be used to save power.