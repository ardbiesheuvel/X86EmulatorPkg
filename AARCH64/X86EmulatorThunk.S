//
// Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//

	.text
	.global		X86EmulatorThunk
X86EmulatorThunk:
	stp		x29, x30, [sp, #-80]!
	mov		x29, sp

	stp		x0, x1, [sp, #16]
	stp		x2, x3, [sp, #32]
	stp		x4, x5, [sp, #48]
	stp		x6, x7, [sp, #64]

	mov		x0, x16
	add		x1, sp, #16
	mov		x2, x17
	mov		x3, x30
	bl		X86EmulatorVmEntry

	ldp		x29, x30, [sp], #80
	ret
