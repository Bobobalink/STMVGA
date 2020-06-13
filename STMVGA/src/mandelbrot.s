.syntax unified
.cpu cortex-m0
.fpu softvfp
.thumb

/*
so we're going to do a fully fixed-point implementation of mandelbrot set generation
we have 32 entire bits for each number, but the catch is that that includes the result of a multiply.
To fix this, we need to do a mult with a 64 bit output, shown on this website: https://anjoola.com/32bitmul.html

*/

.data
	.align 4
	xMin: .word -2
