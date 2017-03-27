//
// Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//

#include "X86Emulator.h"

#include <Library/DefaultExceptionHandlerLib.h>

extern CONST UINT64 X86EmulatorThunk[];

VOID
EFIAPI
X86InterpreterSyncExceptionCallback (
  IN     EFI_EXCEPTION_TYPE   ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT   SystemContext
  )
{
  EFI_SYSTEM_CONTEXT_AARCH64  *AArch64Context;
  X86_IMAGE_RECORD            *Record;
  UINTN                       Ec;
  UINTN                       Rt;

  AArch64Context = SystemContext.SystemContextAArch64;

  // instruction permission faults or PC misalignment faults take us to the emu
  Ec = AArch64Context->ESR >> 26;
  if ((Ec == 0x21 && (AArch64Context->ESR & 0x3c) == 0xc) || Ec == 0x22) {
    Record = FindImageRecord (AArch64Context->ELR);
    if (Record != NULL) {
      AArch64Context->X16 = AArch64Context->ELR;
      AArch64Context->X17 = (UINT64)Record;
      AArch64Context->ELR = (UINT64)X86EmulatorThunk;
      return;
    }
  }

  // check whether the exception occurred in the JITed code
  if (AArch64Context->ELR >= (UINTN)static_code_gen_buffer &&
      AArch64Context->ELR < (UINTN)static_code_gen_buffer +
                            EFI_PAGES_TO_SIZE (CODE_GEN_BUFFER_PAGES)) {
    //
    // It looks like we crashed in the JITed code. Check whether we are
    // accessing page 0, and fix up the access in that case.
    //
    if (Ec == 0x25 && AArch64Context->FAR < EFI_PAGE_SIZE) {

      if ((AArch64Context->ESR & BIT24) &&   // Instruction syndrome valid
          !(AArch64Context->ESR & BIT6)) {   // Load instruction

        Rt = (AArch64Context->ESR >> 16) & 0x1f;
        if (Rt != 31) { // ignore wzr/xzr
          (&AArch64Context->X0)[Rt] = 0;
        }
      }
      DEBUG ((DEBUG_WARN,
        "%a: Illegal %a address 0x%lx from X86 code!! Fixing up ...\n",
         __FUNCTION__,
         (AArch64Context->ESR & BIT6) ? "write to" : "read from",
         AArch64Context->FAR));

      AArch64Context->ELR += 4;
      return;
    }
    //
    // We can't handle this exception. Try to produce some meaningful
    // diagnostics regarding the X86 code this maps onto.
    //
    DEBUG ((DEBUG_ERROR, "Exception occurred during emulation:\n"));
    dump_x86_state();
  }
  DefaultExceptionHandler (ExceptionType, SystemContext);
}
