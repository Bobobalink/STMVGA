/**
  ******************************************************************************
  * @file    main.c
  * @author  Ac6
  * @version V1.0
  * @date    01-December-2013
  * @brief   Default main function.
  ******************************************************************************
*/


#include "stm32f0xx.h"
#include "stm32f0_discovery.h"

void changeClockFreq() {
	// directly lifted from A.3.2 of the Family reference (Page 940)
	if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL)
	{
	 RCC->CFGR &= (uint32_t) (~RCC_CFGR_SW);
	 while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI)
	 {
	 /* For robust implementation, add here time-out management */
	 }
	}
	RCC->CR &= (uint32_t)(~RCC_CR_PLLON);
	while((RCC->CR & RCC_CR_PLLRDY) != 0)
	{
	 /* For robust implementation, add here time-out management */
	}
	RCC->CFGR &= ~RCC_CFGR_PLLMULL; // clear the PLLmul
	// 0x7 is times 9 (for 36 MHz)
	// 0x8 is times 10 (for 40 MHz)
	RCC->CFGR |= 0x8 << 18; // bits 18-21, should be 4 MHz * the setting
	RCC->CR |= RCC_CR_PLLON;
	while((RCC->CR & RCC_CR_PLLRDY) == 0)
	{
	 /* For robust implementation, add here time-out management */
	}
	RCC->CFGR |= (uint32_t) (RCC_CFGR_SW_PLL);
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
	{
	 /* For robust implementation, add here time-out management */
	}
}

void setupHorizontalTimers() {
	/* What we want to generate (in pixel clock units: 40 MHz):
	 * |    800    |  40  |   128   |  88 |
	 *                     _________
	 * ___________________|         |______
	 * |pixels here|
	 *
	 * If we rearrange a little (because it doesn't matter where the timer starts):
	 * |   128   |  88 |    800    |  40  |
	 *  _________
	 * |         |_________________________
	 *                 |pixels here|
	 *
	 * How we generate this: 2 timers
	 * Tim3 is the master timer:
	 *     counts at 40 MHZ, the system clock
	 *     CCx2 is the hsync pulse output:
	 *         PWM mode one, outputs high until 128 pixel clocks then low until restart
	 *     CCx1 is the pixel trigger:
	 * 	       Mode doesn't really matter, its only here to send the master mode output at the compare value
	 *     	   Set compare to 128+88=216.
	 * 	       Set Master Mode Selection to Compare Pulse, so it sends a pulse at 216 pixel clocks
	 * Tim15 is the pixel timer:
	 *     Fires an update event at the actual pixel output clock (so 4 MHz)
	 *     Uses the Repeat counter to output the appropriate number of pixels (80)
	 *     Triggers a DMA request on every update event
	 *     Slave mode triggered from Tim2
	 *
	 */

	// first lets set up the master timer (TIM3)

	GPIOA->MODER |= (0x2 << (2 * 7)); // A7 alternate function
	GPIOA->AFR[0] |= (0x1 << (4 * 7)); // AF1 == TIM3_CH2

	// count the whole line (1056 long) at the system clock (40 MHz)
	TIM3->PSC = 0;
	TIM3->ARR = 1055;

	// configure Capture/Compare channel 2
	// bits 4:6 are mode
	// 0x6 is PWM1 (high while count is below this value)
	TIM3->CCMR1 |= ((0x6 << 4)) << 8;
	TIM3->CCR2 = 128;
	TIM3->CCER |= TIM_CCER_CC2E;

	// configure CCx1, mode doesn't matter so leave it 0
	TIM3->CCR1 = 216;
	TIM3->CCER |= TIM_CCER_CC1E;

	// CR2_MMS = 0x3 is CC1IF flag set (pulse when CCR1 matches)
	TIM3->CR2 |= 0x3 << 4;

	// now set up the pixel TIM15

	// tim15 ccr1 outputs on PA2 with AF0 (just for making sure pixel clock is right)
	GPIOA->MODER |= 0x2 << (2 * 2);

	TIM15->PSC = 0;
	TIM15->ARR = 9; // should create 4MHz update events

	// set the RCR to only send an update event after 80 timer resets
	TIM15->RCR = 80 - 1;

	TIM15->EGR = TIM_EGR_UG; // apparently you need to generate an update event to make this kick in

	// use PWM mode 1 with CCR1 set to 1 to generate a pulse on every reset.
	TIM15->CCMR1 |= 0x6 << 4;
	TIM15->CCR1 = 1;

	TIM15->CCER |= TIM_CCER_CC1E;

	// set up DMA request generation from CCx1 since the update events are inhibited by the RCR
	// triggers DMA channel 1
	TIM15->DIER |= TIM_DIER_CC1DE;

	// set it to one pulse mode, but because RCR doesn't allow update for 80 overflows, it becomes 80 pulse mode
	TIM15->CR1 |= TIM_CR1_OPM;

	// enable the master outputs
	TIM15->BDTR |= TIM_BDTR_MOE;

	// set up slave mode to be triggered from the output of TIM3
	TIM15->SMCR |= 0x1 << 4; // trigger select 1 is TIM3
	TIM15->SMCR |= 0x6; // slave mode select 6 is 'trigger mode' (start counter at rising edge)
}

void setupVerticalTimer() {
	/*
	 * TIM2 is the timer for the vsync signal. We're going to use the same trick as TIM2 to rearrange the signal
	 * into something easily PWM1able.
	 * |    4    |  23 |    600    |   1   | (in units of lines)
	 *  _________
	 * |         |__________________________
	 *                 |pixels here|
	 * The only difference is that the timer counts 1055 times slower (since it counts once per horizontal line)
	 * TODO: I don't actually know what the timing between the hsync and vsync signal should be
	 */

	GPIOA->MODER |= (0x2 << (2 * 1)); // A1 alternate function
	GPIOA->AFR[0] |= (0x2 << (4 * 1)); // AF2 == TIM2_CH2

	// count the whole line (628 long) at the horizontal line clock (1056x slower than the system clock)
	// note that using the prescaler seems to enforce a phase between it and the system clock
	// so we're using the infinite power of the 32 bit timer for this
	TIM2->PSC = 0;
	TIM2->ARR = (628 * 1056) - 1;

	// configure Capture/Compare channel 2
	// bits 4:6 are mode
	// 0x6 is PWM1 (high while count is below this value)
	TIM2->CCMR1 |= (0x6 << 4) << 8;
	TIM2->CCR2 = 4 * 1056;
	TIM2->CCER |= TIM_CCER_CC2E;
}

int main(void) {
	changeClockFreq(); // set the system clock to 40 MHz (the frequency of the VGA clock)

	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
	RCC->APB2ENR |= RCC_APB2ENR_TIM15EN;
	RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
	RCC->AHBENR |= RCC_AHBENR_GPIOBEN;

	setupHorizontalTimers(); // configure TIM3 to generate the HSYNC signal, and TIM15 to trigger DMA requests for signal output
	setupVerticalTimer(); // configure TIM2 to generate the VSYNC signal

	TIM2->CR1 |= TIM_CR1_CEN;
	TIM3->CR1 |= TIM_CR1_CEN;

	for(;;);
}
