/** @file
  Hand the OS the ACPI tables libkrun generated, or else the device tree.

  libkrun can describe the machine with ACPI as well as with the device tree:
  it writes the tables, RSDP first, into a window of guest memory and names
  that window with a `libkrun,acpi-tables` device-tree node. arm64 Linux uses
  one description or the other, never both, so with the node present this
  driver chooses ACPI (which keeps FdtClientDxe from installing the device
  tree as a configuration table) and installs the tables once the ACPI table
  protocol is up. Without the node it chooses the device tree, as before. The
  firmware's own drivers keep reading the device tree either way.

  Copyright (c) 2026, Gustavo Noronha Silva. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Guid/PlatformHasAcpi.h>
#include <Guid/PlatformHasDeviceTree.h>
#include <IndustryStandard/Acpi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/FdtClient.h>

STATIC EFI_PHYSICAL_ADDRESS  mTablesBase;
STATIC VOID                  *mAcpiTableRegistration;

/**
  Install every table the XSDT lists, then the DSDT the FADT points at. The
  ACPI table driver builds its own RSDP and XSDT and links the DSDT into the
  FADT it keeps.
**/
STATIC
EFI_STATUS
InstallTables (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable
  )
{
  EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                   *Xsdt;
  EFI_ACPI_DESCRIPTION_HEADER                   *Table;
  EFI_ACPI_DESCRIPTION_HEADER                   *Dsdt;
  UINT64                                        *Entries;
  UINTN                                         Count;
  UINTN                                         Index;
  UINTN                                         Key;
  EFI_STATUS                                    Status;

  Rsdp = (EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER *)(UINTN)mTablesBase;
  if (Rsdp->Signature != EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER_SIGNATURE) {
    return EFI_NOT_FOUND;
  }

  Xsdt    = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
  Entries = (UINT64 *)(Xsdt + 1);
  Count   = (Xsdt->Length - sizeof (*Xsdt)) / sizeof (UINT64);
  Dsdt    = NULL;

  for (Index = 0; Index < Count; Index++) {
    //
    // The entries follow a 36-byte header, so they are not 8-byte aligned.
    //
    Table  = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)ReadUnaligned64 (&Entries[Index]);
    Status = AcpiTable->InstallAcpiTable (AcpiTable, Table, Table->Length, &Key);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Table->Signature == EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE) {
      Dsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)
             ((EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE *)Table)->XDsdt;
    }
  }

  if (Dsdt == NULL) {
    return EFI_NOT_FOUND;
  }

  return AcpiTable->InstallAcpiTable (AcpiTable, Dsdt, Dsdt->Length, &Key);
}

STATIC
VOID
EFIAPI
OnAcpiTableProtocol (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;
  EFI_STATUS               Status;

  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  mAcpiTableRegistration,
                  (VOID **)&AcpiTable
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  gBS->CloseEvent (Event);

  Status = InstallTables (AcpiTable);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: cannot install the ACPI tables: %r\n", __func__, Status));
  }
}

STATIC
EFI_STATUS
ChooseDescription (
  IN EFI_HANDLE  ImageHandle,
  IN EFI_GUID    *Description
  )
{
  EFI_STATUS  Status;

  //
  // Without a hardware description the OS cannot boot, so tolerate no error.
  //
  Status = gBS->InstallProtocolInterface (
                  &ImageHandle,
                  Description,
                  EFI_NATIVE_INTERFACE,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);
  return Status;
}

EFI_STATUS
EFIAPI
KrunAcpiPlatformEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  FDT_CLIENT_PROTOCOL  *FdtClient;
  CONST UINT64         *Reg;
  UINT32               RegSize;
  UINTN                AddressCells;
  UINTN                SizeCells;
  UINT64               Size;
  EFI_EVENT            Event;
  EFI_STATUS           Status;

  Status = gBS->LocateProtocol (&gFdtClientProtocolGuid, NULL, (VOID **)&FdtClient);
  ASSERT_EFI_ERROR (Status);

  Status = FdtClient->FindCompatibleNodeReg (
                        FdtClient,
                        "libkrun,acpi-tables",
                        (CONST VOID **)&Reg,
                        &AddressCells,
                        &SizeCells,
                        &RegSize
                        );
  if (EFI_ERROR (Status)) {
    return ChooseDescription (ImageHandle, &gEdkiiPlatformHasDeviceTreeGuid);
  }

  if ((AddressCells != 2) || (SizeCells != 2) || (RegSize != 2 * sizeof (UINT64))) {
    DEBUG ((DEBUG_ERROR, "%a: the ACPI window's 'reg' is not one 64-bit range\n", __func__));
    return ChooseDescription (ImageHandle, &gEdkiiPlatformHasDeviceTreeGuid);
  }

  mTablesBase = SwapBytes64 (ReadUnaligned64 (&Reg[0]));
  Size        = SwapBytes64 (ReadUnaligned64 (&Reg[1]));

  //
  // Nothing maps the window yet. Describe it as reserved, so the OS leaves it
  // alone, and map it so the tables can be read from it.
  //
  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeReserved,
                  mTablesBase,
                  Size,
                  EFI_MEMORY_WB | EFI_MEMORY_XP
                  );
  if (!EFI_ERROR (Status)) {
    Status = gDS->SetMemorySpaceAttributes (
                    mTablesBase,
                    Size,
                    EFI_MEMORY_WB | EFI_MEMORY_XP
                    );
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: cannot map the ACPI window at 0x%lx: %r\n",
      __func__,
      mTablesBase,
      Status
      ));
    return ChooseDescription (ImageHandle, &gEdkiiPlatformHasDeviceTreeGuid);
  }

  Event = EfiCreateProtocolNotifyEvent (
            &gEfiAcpiTableProtocolGuid,
            TPL_CALLBACK,
            OnAcpiTableProtocol,
            NULL,
            &mAcpiTableRegistration
            );
  ASSERT (Event != NULL);

  DEBUG ((DEBUG_INFO, "%a: ACPI tables at 0x%lx\n", __func__, mTablesBase));
  return ChooseDescription (ImageHandle, &gEdkiiPlatformHasAcpiGuid);
}
