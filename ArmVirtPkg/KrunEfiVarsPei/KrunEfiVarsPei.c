/** @file
  Find a VMM-provided, file-backed UEFI variable store in the device tree and
  hand it to the emulated variable driver, so variables outlive the VM.

  libkrun maps the store's file into guest memory and describes it with a
  `libkrun,efi-variable-store` node. Its contents are whatever the file held:
  the variable driver adopts a store whose header is valid and formats one
  that is not (InitEmuNonVolatileVariableStore). Without the node, variables
  stay in RAM for one boot, as before.

  Copyright (c) 2026, Gustavo Noronha Silva. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiPei.h>
#include <Uefi/UefiSpec.h>

#include <Library/ArmMmuLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <libfdt.h>

EFI_STATUS
EFIAPI
KrunEfiVarsPeiEntryPoint (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  VOID          *Fdt;
  INT32         Node;
  INT32         Len;
  CONST UINT64  *Reg;
  UINT64        Base;
  UINT64        Size;
  EFI_STATUS    Status;

  Fdt  = (VOID *)(UINTN)PcdGet64 (PcdDeviceTreeInitialBaseAddress);
  Node = fdt_node_offset_by_compatible (Fdt, -1, "libkrun,efi-variable-store");
  if (Node < 0) {
    DEBUG ((DEBUG_INFO, "%a: no variable store; variables live in RAM\n", __func__));
    return EFI_SUCCESS;
  }

  Reg = fdt_getprop (Fdt, Node, "reg", &Len);
  if ((Reg == NULL) || (Len != 2 * sizeof (UINT64))) {
    DEBUG ((DEBUG_ERROR, "%a: the store's 'reg' is not one 64-bit range\n", __func__));
    return EFI_SUCCESS;
  }

  Base = fdt64_to_cpu (ReadUnaligned64 (&Reg[0]));
  Size = fdt64_to_cpu (ReadUnaligned64 (&Reg[1]));
  if (Size < PcdGet32 (PcdVariableStoreSize)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: the store at 0x%lx is 0x%lx bytes, smaller than 0x%x\n",
      __func__,
      Base,
      Size,
      PcdGet32 (PcdVariableStoreSize)
      ));
    return EFI_SUCCESS;
  }

  //
  // Nothing maps this range yet. Map it as ordinary data memory, then describe
  // it as memory that is allocated for runtime services, so the OS keeps it
  // and maps it for SetVariable() at runtime.
  //
  Status = ArmSetMemoryAttributes (Base, Size, EFI_MEMORY_WB | EFI_MEMORY_XP);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: cannot map the store at 0x%lx: %r\n", __func__, Base, Status));
    return EFI_SUCCESS;
  }

  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_TESTED |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE,
    Base,
    Size
    );
  BuildMemoryAllocationHob (Base, Size, EfiRuntimeServicesData);

  Status = PcdSet64S (PcdEmuVariableNvStoreReserved, Base);
  ASSERT_EFI_ERROR (Status);

  DEBUG ((DEBUG_INFO, "%a: variable store at 0x%lx (0x%lx bytes)\n", __func__, Base, Size));
  return EFI_SUCCESS;
}
