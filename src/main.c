#include "stm32f0xx.h"
#include "stm32f0_discovery.h"

#define WIDTH 80
#define HEIGHT 60

// load images
extern char pylogo[];
extern char lenna[];
extern char rickslide[];

// allocate screenbuffer
char screen[HEIGHT][WIDTH + 1];

// flag for the interrupt that triggers when the screen in drawn
char lendflag = 0;

int curLine = HEIGHT; // start at the end of the buffer because the first line will trigger the interrupt, so it will tick over
int lastChange = 0;

/*
 * change the system's clock frequency to 40 MHz
 */
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

/*
 * Update the DMA request address after each line
 */
void TIM3_IRQHandler() {
	GPIOC->ODR |= 0x1;
	TIM3->SR &= ~TIM_SR_CC3IF;
	lastChange++;
	if(lastChange >= 10) {
		// If you don't disable the DMA request, it will immediately do a transfer when enabled
		TIM15->DIER = 0;
		DMA1_Channel5->CCR &= ~DMA_CCR_EN; // disable the DMA channel
		// check if we're in the visible region of the vertical sweep
		// remember that TIM2 also counts at the pixel clock, so multiply the line numbers by 1056
		if((TIM2->CNT > 27 * 1056) && (TIM2->CNT < 627 * 1056)) {
			lastChange = 0;
			curLine += 1;
			if(curLine >= HEIGHT)
				curLine = 0;

			DMA1_Channel5->CMAR = (int)&(screen[curLine]); // change it to a new address
			DMA1_Channel5->CCR |= DMA_CCR_EN; // re-enable the DMA channel
			TIM15->DIER = TIM_DIER_CC1DE;
		}
	}
	GPIOC->ODR &= ~(0x1);
}

/*
 * handler called immediately after the frame has finished drawing
 * just sets a flag because we shouldn't spend a long time processing in an interrupt
 * TODO: could do this as a DMA request, but that's less flexible
 * this sets us up nicely for 'racing the beam'.
 * You have a buffer of 29,568 clock cycles of buffer between this interrupt starting to be triggered and the first pixel being drawn
 * then you have 10 clock cycles per pixel + 256 clock cycles at the end of each line (minus the TIM3 interrupt code)
 * total, there are 663,168 clock cycles until this triggers again
 */
void TIM2_IRQHandler() {
	TIM2->SR &= ~TIM_SR_CC3IF;
	lendflag = 1;
}

/*
 * Setup Tim3 to output the hsync signal to A7
 * Also Tim15 is the pixel clock, and outputs it to A2 for reference
 * Tim15 does a DMA request to copy each pixel to GPIOB so it can drive the DAC
 */
void setupHorizontalTimers() {
	/*
	 * What we want to generate (in pixel clock units: 40 MHz):
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
	 * Note: It is important that the pixel lines are all LOW when we are outside of the pixel region
	 * At least some monitors (like mine) use that as an important sync clue.
	 * As such, we will use an extra pixel at the end of each sync line to reset the line to 0
	 *
	 * How we generate this: 2 timers
	 *
	 * Tim3 is the master timer:
	 *     counts at 40 MHZ, the system clock
	 *     CCx2 is the hsync pulse output:
	 *         PWM mode one, outputs high until 128 pixel clocks then low until restart
	 *     CCx1 is the pixel trigger:
	 * 	       Mode doesn't really matter, its only here to send the master mode output at the compare value
	 *     	   Set compare to 128+88=216.
	 * 	       Set Master Mode Selection to Compare Pulse, so it sends a pulse at 216 pixel clocks
	 * 	   CCx3 triggers an interrupt right after the pixels finish outputting
	 * 	       This is primarily to keep the DMA address up to date, but might also be good for synchronizing screen updates
	 *
	 * Tim15 is the pixel timer:
	 *     Fires an update event at the actual pixel output clock (so 4 MHz)
	 *     Uses the Repeat counter to output the appropriate number of pixels (80)
	 *     Triggers a DMA request channel 5 on every update event
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
	// should be 216, but again it's kinda slow so add a fudge factor to make the timings work
	TIM3->CCR1 = 216 - 7;
	TIM3->CCER |= TIM_CCER_CC1E;

	// CR2_MMS = 0x3 is CC1IF flag set (pulse when CCx1 matches)
	TIM3->CR2 |= 0x3 << 4;

	// configure CCx3 for the end of the pixel output region
	// should be 1016, but it takes several clock cycles for the interrupt to actually trigger, so use a fudge factor for more time before the next line starts
	// again, mode doesn't matter
	TIM3->CCR3 = 1016 - 32;
	TIM3->CCER |= TIM_CCER_CC3E;

	TIM3->DIER |= TIM_DIER_CC3IE; // enable the interrupt on CCx3 so that we can get the max number of cycles after the pixels are done
	NVIC->ISER[0] |= 1 << TIM3_IRQn; // enable the interrupt for real
	NVIC_SetPriority(TIM3_IRQn, 3); // set it to the highest priority

	// now set up the pixel TIM15

	// tim15 ccr1 outputs on PA2 with AF0 (just for making sure pixel clock is right)
	GPIOA->MODER |= 0x2 << (2 * 2);

	TIM15->PSC = 0;
	TIM15->ARR = 9; // should create 4MHz update events

	// set the RCR to only send an update event after 81 timer resets (80 screen pixels + 1 edge pixel)
	TIM15->RCR = 81 - 1;

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

	// set up DMA channel 5:
	// transfers one byte from memory to GPIOB->ODR, increment 80 times then circle
	DMA1_Channel5->CCR |=
			DMA_CCR_PL |       // set it to the highest priority
			DMA_CCR_MINC |     // increment memory address
			DMA_CCR_CIRC |     // enable circular mode
			DMA_CCR_DIR;       // transfer memory -> peripheral
	DMA1_Channel5->CNDTR = 81; // transfer 81 elements before circling back
	DMA1_Channel5->CPAR = (int) &(GPIOB->ODR);
	DMA1_Channel5->CMAR = (int) screen;

	// set up GPIOB to output on pins 0..7
	for(int i = 0; i < 8; i++)
		GPIOB->MODER |= (0x1 << 2 * i);
}

/*
 * Setup Tim2 to drive A1 to be the vsync signal
 */
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

	// add a screen refresh interrupt that triggers immediately after the visible region of the screen is drawn
	TIM2->CCR3 = 1056 * 627;
	TIM2->CCER |= TIM_CCER_CC3E;
	TIM2->DIER |= TIM_DIER_CC3IE;
	NVIC->ISER[0] |= 1 << TIM2_IRQn;
	NVIC_SetPriority(TIM2_IRQn, 0); // set it to low priority
}

int main(void) {
	changeClockFreq(); // set the system clock to 40 MHz (the frequency of the VGA clock)

	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
	RCC->APB2ENR |= RCC_APB2ENR_TIM15EN;
	RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
	RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
	RCC->AHBENR |= RCC_AHBENR_DMA1EN;
	RCC->AHBENR |= RCC_APB2ENR_SYSCFGEN;

	RCC->AHBENR |= RCC_AHBENR_GPIOCEN;
	GPIOC->MODER |= 0x1;

	setupHorizontalTimers(); // configure TIM3 to generate the HSYNC signal, and TIM15 to trigger DMA requests for signal output
	setupVerticalTimer(); // configure TIM2 to generate the VSYNC signal

	// load the right edge fake pixels with 0. They must always remain ZERO
	for(int y = 0; y < HEIGHT; y++) {
		screen[y][WIDTH] = 0;
	}

	TIM2->CR1 |= TIM_CR1_CEN;
	TIM3->CR1 |= TIM_CR1_CEN;

	for(;;) {
		asm("wfi"); // wait for an interrupt to be triggered
		if(lendflag) { // if we just finished drawing a frame
			for(int y = 0; y < HEIGHT; y++) {
				for(int x = 0; x < WIDTH; x++) {
					screen[y][x] = rickslide[y * WIDTH + x];
				}
			}
			lendflag = 0; // we're done drawing the frame
		}
	}
}
