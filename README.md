# X86EmulatorPkg

This code implements a DXE driver for EDK2/Tianocore that allows UEFI
drivers built for x86_64 aka X64 aka amd64 to be executed on 64-bit
ARM systems (aka AArch64)

The prerequisites are not fully upstream, and may never be, but the
delta is quite small. They can be found here:

	https://github.com/ardbiesheuvel/edk2/tree/x86emu

