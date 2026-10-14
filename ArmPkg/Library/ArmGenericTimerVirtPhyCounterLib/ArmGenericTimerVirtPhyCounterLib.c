/** @file
  ArmGenericTimerCounterLib that uses the virtual timer at EL1 and the
  physical timer at EL2.

  Apple's Hypervisor.framework starts a guest at EL2 when it offers nested
  virtualization, and there the EL1 virtual timer never fires: CNTV_CTL_EL0
  never reports ISTATUS, so a firmware driving it gets no timer interrupts and
  no UEFI timer events. The EL1 physical timer is delivered at EL2. At EL1,
  where the physical timer's registers trap to the VMM, the virtual timer
  remains the right one.

  The counter and the comparator always come from the same timer, since
  TimerDxe computes compare values from ArmGenericTimerGetSystemCount ().

  Copyright (c) 2011 - 2014, ARM Ltd. All rights reserved.<BR>
  Copyright (c) 2014, Linaro Ltd. All rights reserved.<BR>
  Copyright (c) 2026, Gustavo Noronha Silva. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/ArmGenericTimerCounterLib.h>
#include <Library/ArmLib.h>

STATIC
BOOLEAN
UsePhysicalTimer (
  VOID
  )
{
  return ArmReadCurrentEL () == AARCH64_EL2;
}

VOID
EFIAPI
ArmGenericTimerEnableTimer (
  VOID
  )
{
  UINTN  TimerCtrlReg;

  TimerCtrlReg  = ArmGenericTimerGetTimerCtrlReg ();
  TimerCtrlReg |= ARM_ARCH_TIMER_ENABLE;
  ArmGenericTimerSetTimerCtrlReg (TimerCtrlReg);
}

VOID
EFIAPI
ArmGenericTimerReenableTimer (
  VOID
  )
{
}

VOID
EFIAPI
ArmGenericTimerDisableTimer (
  VOID
  )
{
  UINTN  TimerCtrlReg;

  TimerCtrlReg  = ArmGenericTimerGetTimerCtrlReg ();
  TimerCtrlReg &= ~ARM_ARCH_TIMER_ENABLE;
  ArmGenericTimerSetTimerCtrlReg (TimerCtrlReg);
}

VOID
EFIAPI
ArmGenericTimerSetTimerFreq (
  IN   UINTN  FreqInHz
  )
{
  ArmWriteCntFrq (FreqInHz);
}

UINTN
EFIAPI
ArmGenericTimerGetTimerFreq (
  VOID
  )
{
  return ArmReadCntFrq ();
}

UINTN
EFIAPI
ArmGenericTimerGetTimerVal (
  VOID
  )
{
  return UsePhysicalTimer () ? ArmReadCntpTval () : ArmReadCntvTval ();
}

VOID
EFIAPI
ArmGenericTimerSetTimerVal (
  IN   UINTN  Value
  )
{
  if (UsePhysicalTimer ()) {
    ArmWriteCntpTval (Value);
  } else {
    ArmWriteCntvTval (Value);
  }
}

UINT64
EFIAPI
ArmGenericTimerGetSystemCount (
  VOID
  )
{
  return UsePhysicalTimer () ? ArmReadCntPct () : ArmReadCntvCt ();
}

UINTN
EFIAPI
ArmGenericTimerGetTimerCtrlReg (
  VOID
  )
{
  return UsePhysicalTimer () ? ArmReadCntpCtl () : ArmReadCntvCtl ();
}

VOID
EFIAPI
ArmGenericTimerSetTimerCtrlReg (
  UINTN  Value
  )
{
  if (UsePhysicalTimer ()) {
    ArmWriteCntpCtl (Value);
  } else {
    ArmWriteCntvCtl (Value);
  }
}

UINT64
EFIAPI
ArmGenericTimerGetCompareVal (
  VOID
  )
{
  return UsePhysicalTimer () ? ArmReadCntpCval () : ArmReadCntvCval ();
}

VOID
EFIAPI
ArmGenericTimerSetCompareVal (
  IN   UINT64  Value
  )
{
  if (UsePhysicalTimer ()) {
    ArmWriteCntpCval (Value);
  } else {
    ArmWriteCntvCval (Value);
  }
}
