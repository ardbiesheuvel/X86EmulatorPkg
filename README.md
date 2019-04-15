# X86EmulatorPkg

This code implements a DXE driver for EDK2/Tianocore that allows UEFI
drivers built for x86_64 aka X64 aka amd64 to be executed on 64-bit
ARM systems (aka AArch64)

All prerequisites in the core code have been merged into the upstream
Tianocore EDK2 repository as of commit 26d60374b87d.

A prebuilt RELEASE binary of this driver is included in the edk2-non-osi
repository at commit 77b5eefd92ae.

## Quick Start

To quickly compile an OVMF version that contains the emulator, run

	$ git clone https://github.com/ardbiesheuvel/edk2.git
	$ cd edk2
	$ git checkout origin/x86emu
	$ git submodule add https://github.com/ardbiesheuvel/X86EmulatorPkg.git
	$ echo "  X86EmulatorPkg/X86Emulator.inf" >> ArmVirtPkg/ArmVirtQemu.dsc
	$ echo "  INF X86EmulatorPkg/X86Emulator.inf" >> ArmVirtPkg/ArmVirtQemuFvMain.fdf.inc
	$ make -C BaseTools
	$ . edksetup.sh
	$ export GCC5_AARCH64_PREFIX=... (if you are on a non-aarch64 system)
	$ build -a AARCH64 -t GCC5 -p ArmVirtPkg/ArmVirtQemu.dsc -b RELEASE

You can then use QEMU to execute it:

	$ qemu-system-aarch64 -M virt -cpu cortex-a57 -m 2G -nographic -bios ./Build/ArmVirtQemu-AARCH64/RELEASE_GCC5/FV/QEMU_EFI.fd

If you see dots on your screen, that is the x86_64 virtio iPXE rom in action!
