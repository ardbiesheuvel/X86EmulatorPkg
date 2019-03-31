//
// Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//

#include "X86Emulator.h"
#include "main.h"

STATIC EFI_CPU_ARCH_PROTOCOL      *mCpu;
STATIC EFI_CPU_IO2_PROTOCOL       *mCpuIo2;
STATIC LIST_ENTRY                 mX86ImageList;
STATIC BOOLEAN                    gX86EmulatorIsInitialized;

X86_IMAGE_RECORD*
EFIAPI
FindImageRecord (
  IN  EFI_PHYSICAL_ADDRESS    Address
  )
{
  LIST_ENTRY                  *Entry;
  X86_IMAGE_RECORD            *Record;

  for (Entry = GetFirstNode (&mX86ImageList);
       !IsNull (&mX86ImageList, Entry);
       Entry = GetNextNode (&mX86ImageList, Entry)) {

    Record = BASE_CR (Entry, X86_IMAGE_RECORD, Link);

    if (Address >= Record->ImageBase &&
        Address < Record->ImageBase + Record->ImageSize) {
      return Record;
    }
  }
  return NULL;
}

BOOLEAN
pc_is_native_call (
  IN  UINT64    Pc
  )
{
  return FindImageRecord ((EFI_PHYSICAL_ADDRESS)Pc) == NULL;
}

STATIC
BOOLEAN
EFIAPI
IsX86ImageSupported (
  IN  EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL    *This,
  IN  UINT16                                  ImageType,
  IN  EFI_DEVICE_PATH_PROTOCOL                *DevicePath   OPTIONAL
  )
{
  if (ImageType != EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION &&
      ImageType != EFI_IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER) {
    return FALSE;
  }
  return TRUE;
}

STATIC
EFI_STATUS
EFIAPI
RegisterX86Image (
  IN      EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL    *This,
  IN      EFI_PHYSICAL_ADDRESS                    ImageBase,
  IN      UINT64                                  ImageSize,
  IN  OUT EFI_IMAGE_ENTRY_POINT                   *EntryPoint
  )
{
  X86_IMAGE_RECORD    *Record;

  DEBUG_CODE_BEGIN ();
    PE_COFF_LOADER_IMAGE_CONTEXT  ImageContext;
    EFI_STATUS                    Status;

    ZeroMem (&ImageContext, sizeof (ImageContext));

    ImageContext.Handle    = (VOID *)(UINTN)ImageBase;
    ImageContext.ImageRead = PeCoffLoaderImageReadFromMemory;

    Status = PeCoffLoaderGetImageInfo (&ImageContext);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    ASSERT (ImageContext.Machine == EFI_IMAGE_MACHINE_X64);
    ASSERT (ImageContext.ImageType == EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION ||
            ImageContext.ImageType == EFI_IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER);
  DEBUG_CODE_END ();

  Record = AllocatePool (sizeof *Record);
  if (Record == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Record->ImageBase = ImageBase;
  Record->ImageSize = ImageSize;

  InsertTailList (&mX86ImageList, &Record->Link);

  return mCpu->SetMemoryAttributes (mCpu, ImageBase, ImageSize, EFI_MEMORY_XP);
}

STATIC
EFI_STATUS
EFIAPI
UnregisterX86Image (
  IN  EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL    *This,
  IN  EFI_PHYSICAL_ADDRESS                    ImageBase
  )
{
  X86_IMAGE_RECORD            *Record;
  EFI_STATUS                  Status;

  Record = FindImageRecord (ImageBase);
  if (Record == NULL) {
    return EFI_NOT_FOUND;
  }

  // remove non-exec protection
  Status = mCpu->SetMemoryAttributes (mCpu, Record->ImageBase,
                   Record->ImageSize, 0);

  RemoveEntryList (&Record->Link);
  FreePool (Record);

  return Status;
}

STATIC EDKII_PECOFF_IMAGE_EMULATOR_PROTOCOL   mX86EmulatorProtocol = {
  IsX86ImageSupported,
  RegisterX86Image,
  UnregisterX86Image,
  EDKII_PECOFF_IMAGE_EMULATOR_VERSION,
  EFI_IMAGE_MACHINE_X64
};

UINT8
cpu_inb (
  IN  UINTN     addr
  )
{
  UINT8   Result = -1;

  mCpuIo2->Io.Read(mCpuIo2, EfiCpuIoWidthUint8, addr, 1, &Result);
  return Result;
}

UINT16
cpu_inw (
  IN  UINTN     addr
  )
{
  UINT16  Result = -1;

  mCpuIo2->Io.Read(mCpuIo2, EfiCpuIoWidthUint16, addr, 1, &Result);
  return Result;
}

UINT32
cpu_inl (
  IN  UINTN     addr
  )
{
  UINT32  Result = -1;

  mCpuIo2->Io.Read(mCpuIo2, EfiCpuIoWidthUint32, addr, 1, &Result);
  return Result;
}

VOID
cpu_outb (
  IN  UINTN     addr,
  IN  UINT8     val
  )
{
  mCpuIo2->Io.Write(mCpuIo2, EfiCpuIoWidthUint8, addr, 1, &val);
}

VOID
cpu_outw (
  IN  UINTN     addr,
  IN  UINT16    val
  )
{
  mCpuIo2->Io.Write(mCpuIo2, EfiCpuIoWidthUint16, addr, 1, &val);
}

VOID
cpu_outl (
  IN  UINTN     addr,
  IN  UINT32    val
  )
{
  mCpuIo2->Io.Write(mCpuIo2, EfiCpuIoWidthUint32, addr, 1, &val);
}

UINT64
X86EmulatorVmEntry (
  IN  UINT64              Pc,
  IN  UINT64              *Args,
  IN  X86_IMAGE_RECORD    *Record,
  IN  UINT64              Lr
  )
{
  if (!gX86EmulatorIsInitialized) {
    x86emu_init();
    gX86EmulatorIsInitialized = TRUE;
  }

  return run_x86_func((void*)Pc, (uint64_t *)Args);
}

extern EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL **stdout;
extern EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL **stderr;

UINT8 *code_gen_prologue;

EFI_STATUS
EFIAPI
X86EmulatorDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Alloc;

  InitializeListHead (&mX86ImageList);

  Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesCode,
                  CODE_GEN_BUFFER_PAGES + 1, &Alloc);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  code_gen_prologue = (UINT8 *)(UINTN)Alloc;
  static_code_gen_buffer = code_gen_prologue + EFI_PAGE_SIZE;

  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&mCpu);
  ASSERT_EFI_ERROR(Status);

  Status = gBS->LocateProtocol (&gEfiCpuIo2ProtocolGuid, NULL,
                  (VOID **)&mCpuIo2);
  ASSERT_EFI_ERROR(Status);

  Status = mCpu->RegisterInterruptHandler (mCpu, X86_EMU_EXCEPTION_TYPE,
                   &X86InterpreterSyncExceptionCallback);

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->InstallProtocolInterface (&ImageHandle,
                  &gEdkiiPeCoffImageEmulatorProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mX86EmulatorProtocol);
  if (EFI_ERROR (Status)) {
    mCpu->RegisterInterruptHandler (mCpu, X86_EMU_EXCEPTION_TYPE, NULL);
  }

  stdout = &gST->ConOut;
  stderr = &gST->StdErr;

  return Status;
}
