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

void genPulseTrain() {
	/* use Tim15 to generate a clock at sysclk/2 as before
	 * but use the REP counter to stop it after 200 pulses
	 */

	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
	RCC->APB2ENR |= RCC_APB2ENR_TIM15EN;
	RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
	RCC->AHBENR |= RCC_AHBENR_GPIOBEN;

	// tim15 ccr1 outputs on PA2 with AF0
	GPIOA->MODER |= 0x2 << (2 * 2);


	TIM15->PSC = 0;
	TIM15->ARR = 1;

	// set the RCR to only send an update event after 200 update events (confused yet?)
	TIM15->RCR = 200;

	TIM15->EGR = TIM_EGR_UG; // apparently you need to generate an update event to make this kick in

	// do the PWM1 trick with value 1
	TIM15->CCMR1 |= 0x6 << 4;
	TIM15->CCR1 = 1;
	TIM15->CCER |= TIM_CCER_CC1E;

	TIM15->CR1 |= TIM_CR1_OPM;
	TIM15->BDTR |= TIM_BDTR_MOE;

	TIM15->CR1 |= TIM_CR1_CEN;

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
	 * Tim2 is the master timer:
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

	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
	RCC->APB2ENR |= RCC_APB2ENR_TIM15EN;
	RCC->AHBENR |= RCC_AHBENR_GPIOAEN;


	// first lets set up the master timer (TIM2)

	GPIOA->MODER |= (0x2 << (2 * 1)); // A1 alternate function
	GPIOA->AFR[0] |= (0x2 << (4 * 1)); // AF2 == TIM2_CH2

	// count the whole line (1024 long) at the system clock (36 MHz)
	TIM2->PSC = 0;
	TIM2->ARR = 1055;

	// configure Capture/Compare channel 2
	// bits 4:6 are mode
	// 0x6 is PWM1 (high while count is below this value)
	TIM2->CCMR1 |= ((0x6 << 4)) << 8;
	TIM2->CCR2 = 128;
	TIM2->CCER |= TIM_CCER_CC2E;

	// configure CCx1, mode doesn't matter so leave it 0
	TIM2->CCR1 = 216;
	TIM2->CCER |= TIM_CCER_CC1E;

	// CR2_MMS = 0x3 is CC1IF flag set (pulse when CCR1 matches)
	TIM2->CR2 |= 0x3 << 4;

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
	TIM15->DIER |= TIM_DIER_CC1DE;

	// set it to one pulse mode, but because RCR doesn't allow update for 80 overflows, it becomes 80 pulse mode
	TIM15->CR1 |= TIM_CR1_OPM;

	// enable the master outputs
	TIM15->BDTR |= TIM_BDTR_MOE;

	// set up slave mode to be triggered from the output of TIM2
	TIM15->SMCR |= 0x0 << 4; // trigger select 0 is TIM2
	TIM15->SMCR |= 0x6; // slave mode select 6 is 'trigger mode' (start counter at rising edge)

	TIM2->CR1 |= TIM_CR1_CEN; // enable the timer

}

int16_t ccrs[] = {824, 896};
void genHsyncTimers_DMA() {
	/* Tim2 is pixel counter, 0->1023
	 * Tim3 is pixel clock
	 *
	 * Basically, at the start of each line the CCR is waiting for 824, then:
	 * 1) toggle output of the hsync line (low to high)
	 * 2) trigger DMA to change the CCR to be waiting for 896
	 * now it will wait for a count of 896 to:
	 * 1) toggle the output of the hsync line (high to low)
	 * 2) trigger DMA to change the CCR back to 824
	 * then the count will go through 1023 and back to 0, ripe for the next line
	 *
	 * This works fine, but for some reason it's inverted and I can't figure out how to uninvert it
	 * Also, just using 2 timers is a nicer plan anyway, we have plenty of those on board
	 */

	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
	RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
	RCC->AHBENR |= RCC_AHBENR_GPIOBEN;

	GPIOA->MODER |= (0x2 << (2 * 1)); // A1 alternate function
	GPIOA->AFR[0] |= (0x2 << (4 * 1)); // AF2 == TIM2_CH2

	// setup DMA request to change TIM2->CCR2 between 824 and 896 over and over
	// TIM2_CCR2 generates requests on DMA channel 3
	RCC->AHBENR |= RCC_AHBENR_DMA1EN;
	DMA1_Channel3->CMAR = (uint32_t) ccrs;
	DMA1_Channel3->CPAR = (uint32_t) &(TIM2->CCR2);
	DMA1_Channel3->CNDTR = 2;
	DMA1_Channel3->CCR = DMA_CCR_DIR  // set memory -> peripheral
					   | DMA_CCR_MINC // increment memory after tx
					   | DMA_CCR_CIRC // circular around memory (alternate)
					   | (0x1) << 10  // 16 bits in memory
					   | (0x2) << 8;  // 32 bits in peripheral
	DMA1_Channel3->CCR |= DMA_CCR_EN;

	// count the whole line (1024 long) at the system clock (36 MHz)
	TIM2->PSC = 0;
	TIM2->ARR = 1024;

	// configure Capture/Compare channel 2
	// bits 4:6 are mode
	// 0x3 is toggle output on match
	TIM2->CCMR1 |= ((0x3 << 4)) << 8;
	TIM2->CCR2 = 824;

	TIM2->CCER |= TIM_CCER_CC2E;

	// enable DMA after the trigger so it's still in the right order
	TIM2->DIER |= TIM_DIER_CC2DE; // enable DMA request output from CC2

	// configure TIM2 to be the master, outputting the enable signal as TRGO
	// CR2_MMS = 0x1 is enable bit
	TIM2->CR2 |= 0x1 << 4;
	// try setting it to master mode.. not sure it does anything without using the trigger input, but we'll see
	TIM2->SMCR |= TIM_SMCR_MSM;

	// now configure TIM3 the same way as TIM2 for the actual output
	// use CCR1 to output to PB4 (AF1)
	GPIOB->MODER |= (0x2 << (2 * 4)); // B4 AF
	GPIOB->AFR[0] |= (0x1 << (4 * 4)); // B4 AF1 == TIM3_CH1
	TIM3->PSC = 0;
	TIM3->ARR = 1;

	TIM3->CCMR1 |= (0x6 << 4);
	TIM3->CCR1 = 1;
	TIM3->CCER |= TIM_CCER_CC1E;

	// now configure TIM3 to be a slave to TIM2's TRGO
	TIM3->SMCR |= 0x1 << 4; // set trigger select to 1, on TIM3 this corresponds to TIM2
	// slave mode config
	// 0x5 is gated, so enables and disables with the trigger input
	// 0x6 is triggered, so enables on rising edge but does not disable on falling
	TIM3->SMCR |= 0x5;

	TIM3->CR1 |= TIM_CR1_CEN; // turn on slave first then have master ungate it when it is enabled
	TIM2->CR1 |= TIM_CR1_CEN;
}

void synced_T2T3_24MHz() {
	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
	RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
	RCC->AHBENR |= RCC_AHBENR_GPIOBEN;

	GPIOA->MODER |= (0x2 << (2 * 1)); // A1 alternate function
	GPIOA->AFR[0] |= (0x2 << (4 * 1)); // AF2 == TIM2_CH2

	// this *should* be the system clock divided by 2
	TIM2->PSC = 0;
	TIM2->ARR = 1;

    // configure Capture/Compare channel 2
	// bits 4:6 are mode, 0x3 is toggle output on match
	// 0x1 is "set active on match" - when does it set inactive?
	// 0x6 is PWM mode 1 - high when CNT < CCR2
	TIM2->CCMR1 |= ((0x6 << 4)) << 8;
	TIM2->CCR2 = 1;
	TIM2->CCER |= TIM_CCER_CC2E;

	// configure TIM2 to be the master, outputting the enable signal as TRGO
	// CR2_MMS = 0x1 is enable bit
	TIM2->CR2 |= 0x1 << 4;
	// try setting it to master mode.. not sure it does anything without using the trigger input, but we'll see
	TIM2->SMCR |= TIM_SMCR_MSM;

	// now configure TIM3 the same way as TIM2 for the actual output
	// use CCR1 to output to PB4 (AF1)
	GPIOB->MODER |= (0x2 << (2 * 4)); // B4 AF
	GPIOB->AFR[0] |= (0x1 << (4 * 4)); // B4 AF1 == TIM3_CH1
	TIM3->PSC = 0;
	TIM3->ARR = 1;

	TIM3->CCMR1 |= (0x6 << 4);
	TIM3->CCR1 = 1;
	TIM3->CCER |= TIM_CCER_CC1E;

	// now configure TIM3 to be a slave to TIM2's TRGO
	TIM3->SMCR |= 0x1 << 4; // set trigger select to 1, on TIM3 this corresponds to TIM2
	// slave mode config
	// 0x5 is gated, so enables and disables with the trigger input
	// 0x6 is triggered, so enables on rising edge but does not disable on falling
	TIM3->SMCR |= 0x5;

	TIM3->CR1 |= TIM_CR1_CEN; // turn on slave first then have master ungate it when it is enabled
	TIM2->CR1 |= TIM_CR1_CEN;
}

// this is 4 MHz = 12 cycles / 2 loops, 6 cycles/loop
void asmLoop() {
/*
	asm("\
	ldr r0, =#0x48000014 \n\
	movs r1, #0 \n\
	asmLoop_loop: \n\
		str r1, [r0] \n\
		mvn r1, r1 \n\
		b asmLoop_loop \n\
		");
*/
}

// 3.425 MHz = 14 cycles
void tightLoop() {
	register int toggle = 0;
	while(1) {
		GPIOA->ODR = toggle;
		toggle = !toggle;
	}
}

int main(void) {
	changeClockFreq();
	RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
	GPIOA->MODER |= (1 << (2 * 0)); // A0 output

	//genPulseTrain();
	setupHorizontalTimers();

	asm("\
		ldr r0, =#0x48000014 \n\
		movs r1, #0 \n\
		asmLoop_loop: \n\
			str r1, [r0] \n\
			mvn r1, r1 \n\
			b asmLoop_loop \n\
		");
}
