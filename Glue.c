//
// Copyright (c) 2017, Linaro, Ltd. <ard.biesheuvel@linaro.org>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//

#include "X86Emulator.h"

#include <Library/CacheMaintenanceLib.h>
#include <Library/PrintLib.h>

VOID
flush_icache_range (
  IN  UINTN   Start,
  IN  UINTN   End
  )
{
  InvalidateInstructionCacheRange ((VOID *)Start, End - Start + 1);
}

VOID
longjmp (
  IN  VOID    *env,
  IN  INT32   val
  )
{
  LongJump((BASE_LIBRARY_JUMP_BUFFER *)env, (UINTN)((val == 0) ? 1 : val));
}

EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL **stdout;
EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL **stderr;

UINTN
AsciiInternalPrint (
  IN  CONST CHAR8                      *Format,
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Console,
  IN  VA_LIST                          Marker
  );

INT32
vfprintf (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL   **stream,
  IN  CONST CHAR8                       *format, 
  VA_LIST                               ap
  )
{
  ASSERT (stream == stdout || stream == stderr);

  return (INT32)AsciiInternalPrint (format, *stream, ap);
}

INT32
fprintf (
  IN  VOID          *stream,
  IN  CONST CHAR8   *format,
  ...)
{
  VA_LIST   Marker;
  INT32     Result;

  VA_START  (Marker, format);
  Result = vfprintf(stream, format, Marker);
  VA_END (Marker);

  return Result;
}

INT32
printf (
  IN  CONST CHAR8   *format,
  ...)
{
  VA_LIST   Marker;
  INT32     Result;

  VA_START  (Marker, format);
  Result = vfprintf(stdout, format, Marker);
  VA_END (Marker);

  return Result;
}

INT32
snprintf (
  OUT CHAR8         *str,
  IN  UINTN         sizeOfBuffer,
  IN  CONST CHAR8   *format,
  ...)
{
  VA_LIST   Marker;
  INT32     NumberOfPrinted;

  VA_START (Marker, format);
  NumberOfPrinted = (INT32)AsciiVSPrint (str, sizeOfBuffer, format, Marker);
  VA_END (Marker);

  return NumberOfPrinted;
}

INT32
strcmp (
  IN  CONST CHAR8   *s1,
  IN  CONST CHAR8   *s2
  )
{
  return AsciiStrCmp (s1, s2);
}

