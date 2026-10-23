#include "probe_core.h"

static const UINT32 Offsets[PROBE_SAMPLE_COUNT] = {
  0x000000, 0x118128, 0x118234, 0x110100, 0x1fa824, 0x1fa828,
  0x823804, 0x82381c, 0x823820, 0x1183a4, 0x625f04, 0x1fa7c8,
  0x1fa82c, 0x1fa830, 0x824148, 0x8241e0
};

BOOLEAN ProbeCandidate(const UINT32 *Config) {
  return Config != NULL && Config[0] == PROBE_SUPPORTED_ID &&
         (Config[2] >> 24) == 3 && ((Config[3] >> 16) & 0x7fU) == 0;
}

static UINT16 NodeLength(const EFI_DEVICE_PATH_PROTOCOL *Node, UINTN Remaining) {
  UINT16 Length;
  if (Node == NULL || Remaining < sizeof(*Node)) return 0;
  Length = (UINT16)(Node->Length[0] | ((UINT16)Node->Length[1] << 8));
  return Length >= sizeof(*Node) && Length <= Remaining ? Length : 0;
}

BOOLEAN ProbePciRoute(const EFI_DEVICE_PATH_PROTOCOL *Path, UINTN MaxBytes,
                      PROBE_PCI_ROUTE *Route) {
  UINTN Count = 0, Nodes = 0;
  PROBE_PCI_ROUTE Found = {0};
  const ACPI_HID_DEVICE_PATH *Root;
  UINT16 Length;
  if (Path == NULL || Route == NULL) return FALSE;
  Length = NodeLength(Path, MaxBytes);
  if (Length != sizeof(ACPI_HID_DEVICE_PATH) ||
      Path->Type != ACPI_DEVICE_PATH || Path->SubType != ACPI_DP) return FALSE;
  Root = (const ACPI_HID_DEVICE_PATH *)Path;
  if (Root->HID != EISA_PNP_ID(0x0a03) && Root->HID != EISA_PNP_ID(0x0a08))
    return FALSE;
  Found.RootHid = Root->HID;
  Found.RootUid = Root->UID;
  Path = (const EFI_DEVICE_PATH_PROTOCOL *)((const UINT8 *)Path + Length);
  MaxBytes -= Length;
  while (Nodes++ < 64) {
    Length = NodeLength(Path, MaxBytes);
    if (Length == 0) return FALSE;
    if (Path->Type == END_DEVICE_PATH_TYPE) {
      if (Path->SubType != END_ENTIRE_DEVICE_PATH_SUBTYPE ||
          Length != sizeof(EFI_DEVICE_PATH_PROTOCOL) || Count != 2) return FALSE;
      *Route = Found;
      return TRUE;
    }
    if (Count < 2) {
      const PCI_DEVICE_PATH *Pci;
      if (Path->Type != HARDWARE_DEVICE_PATH || Path->SubType != HW_PCI_DP ||
          Length != sizeof(PCI_DEVICE_PATH)) return FALSE;
      Pci = (const PCI_DEVICE_PATH *)Path;
      if (Count == 0) {
        Found.RootPortDevice = Pci->Device;
        Found.RootPortFunction = Pci->Function;
      } else {
        Found.EndpointDevice = Pci->Device;
        Found.EndpointFunction = Pci->Function;
      }
      Count++;
    } else if (Path->Type != ACPI_DEVICE_PATH || Path->SubType != ACPI_ADR_DP ||
               Length < sizeof(ACPI_ADR_DEVICE_PATH) || (Length & 3U) != 0) {
      return FALSE;
    }
    Path = (const EFI_DEVICE_PATH_PROTOCOL *)((const UINT8 *)Path + Length);
    MaxBytes -= Length;
  }
  return FALSE;
}

EFI_STATUS ProbeSelectionKey(UINTN CandidateCount, EFI_INPUT_KEY Key, UINTN *Selected) {
  if (Selected == NULL || CandidateCount < 2 || CandidateCount > PROBE_MAX_CANDIDATES)
    return EFI_INVALID_PARAMETER;
  if (Key.ScanCode == SCAN_ESC) return EFI_ABORTED;
  if (Key.ScanCode == SCAN_NULL && Key.UnicodeChar >= L'1' &&
      Key.UnicodeChar < L'1' + CandidateCount) {
    *Selected = (UINTN)(Key.UnicodeChar - L'1');
    return EFI_SUCCESS;
  }
  return EFI_NOT_READY;
}

/* SS0 contains eight independent four-bit selectors. Cold samples may vary,
   but the current conservative GA102 policy accepts only locked fields below
   the unlock value 8 and an SS1 low-nibble value of 7. */
BOOLEAN ProbeColdSelectors(UINT32 Ss0, UINT32 Ss1) {
  UINTN I;
  if (Ss1 != 7 || Ss0 == 0xffffffffU || Ss0 == 0x88888888U) return FALSE;
  for (I = 0; I < 8; I++) if (((Ss0 >> (I * 4)) & 0xfU) >= 8) return FALSE;
  return TRUE;
}

BOOLEAN ProbeDecodeTransition(const UINT32 *Initial, const UINT32 *Disabled,
                              const UINT32 *Restored) {
  UINTN I;
  if (!Initial || !Disabled || !Restored || (Initial[1] & 7U) != 7U) return FALSE;
  for (I = 0; I < 64; I++) {
    UINT32 ExpectedDisabled = Initial[I];
    UINT32 ExpectedRestored = Initial[I];
    if (I == 1) {
      ExpectedDisabled &= ~7U;
      ExpectedRestored &= ~4U;
    }
    if (Disabled[I] != ExpectedDisabled || Restored[I] != ExpectedRestored) return FALSE;
  }
  return TRUE;
}

EFI_STATUS ProbeReleaseRelationship(const PROBE_OWNER_RECORD *Records, UINTN Count,
                                    const EFI_GUID *PciGuid, EFI_HANDLE Target,
                                    EFI_HANDLE *Agent, EFI_HANDLE *Child) {
  UINTN I, Drivers = 0, Children = 0;
  if (!Records || !Count || !PciGuid || !Target || !Agent || !Child)
    return EFI_INVALID_PARAMETER;
  *Agent = NULL; *Child = NULL;
  for (I = 0; I < Count; I++) {
    const EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *Entry = &Records[I].Entry;
    UINT32 A = Entry->Attributes;
    BOOLEAN IsPci = TRUE;
    UINTN J;
    const UINT8 *Left = (const UINT8 *)&Records[I].Protocol;
    const UINT8 *Right = (const UINT8 *)PciGuid;
    for (J = 0; J < sizeof(EFI_GUID); J++) if (Left[J] != Right[J]) IsPci = FALSE;
    if (A == EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL ||
        A == EFI_OPEN_PROTOCOL_GET_PROTOCOL || A == EFI_OPEN_PROTOCOL_TEST_PROTOCOL) continue;
    if (!IsPci || !Entry->AgentHandle || Entry->OpenCount != 1) return EFI_ACCESS_DENIED;
    if (A == EFI_OPEN_PROTOCOL_BY_DRIVER && Entry->ControllerHandle == Target) {
      if (Drivers++ || (*Agent && *Agent != Entry->AgentHandle)) return EFI_ACCESS_DENIED;
      *Agent = Entry->AgentHandle;
    } else if (A == EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER &&
               Entry->ControllerHandle && Entry->ControllerHandle != Target) {
      if (Children++ || (*Agent && *Agent != Entry->AgentHandle)) return EFI_ACCESS_DENIED;
      *Agent = Entry->AgentHandle; *Child = Entry->ControllerHandle;
    } else return EFI_ACCESS_DENIED;
  }
  return Drivers == 1 && Children == 1 && *Agent && *Child ? EFI_SUCCESS : EFI_ACCESS_DENIED;
}

EFI_STATUS ProbeCollect(EFI_PCI_IO_PROTOCOL *Pci, EFI_BOOT_SERVICES *Bs,
                       PROBE_RESULT *Result) {
  EFI_STATUS Status;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *Descriptor = NULL;
  UINTN I;
  if (Pci == NULL || Bs == NULL || Result == NULL) return EFI_INVALID_PARAMETER;
  *Result = (PROBE_RESULT){0};
  Status = Pci->Pci.Read(Pci, EfiPciIoWidthUint32, 0, 64, Result->Config);
  if (EFI_ERROR(Status)) return Status;
  if (!ProbeCandidate(Result->Config)) return EFI_UNSUPPORTED;
  Result->MemoryEnabled = (Result->Config[1] & 2) != 0;
  Status = Pci->GetBarAttributes(Pci, 0, NULL, (VOID **)&Descriptor);
  if (!EFI_ERROR(Status)) {
    if (Descriptor == NULL || Descriptor->Desc != ACPI_ADDRESS_SPACE_DESCRIPTOR ||
        Descriptor->Len < sizeof(*Descriptor) - 3 ||
        Descriptor->ResType != ACPI_ADDRESS_SPACE_TYPE_MEM || Descriptor->AddrLen == 0) {
      Status = EFI_COMPROMISED_DATA;
    } else {
      Result->BarBase = Descriptor->AddrRangeMin;
      Result->BarMaximum = Descriptor->AddrRangeMax;
      Result->BarSize = Descriptor->AddrLen;
      Result->BarTranslation = Descriptor->AddrTranslationOffset;
      Result->BarGranularity = Descriptor->AddrSpaceGranularity;
    }
  }
  if (Descriptor != NULL) Bs->FreePool(Descriptor);
  Result->BarStatus = Status;
  for (I = 0; I < PROBE_SAMPLE_COUNT; I++) {
    PROBE_SAMPLE *Sample = &Result->Samples[I];
    Sample->Offset = Offsets[I];
    if (!Result->MemoryEnabled) Sample->Status = EFI_NOT_READY;
    else if (EFI_ERROR(Status)) Sample->Status = Status;
    else if (Result->BarSize < 4 || Offsets[I] > Result->BarSize - 4)
      Sample->Status = EFI_BAD_BUFFER_SIZE;
    else Sample->Status = Pci->Mem.Read(Pci, EfiPciIoWidthUint32, 0,
                                      Offsets[I], 1, &Sample->Value);
  }
  return EFI_SUCCESS;
}

EFI_STATUS ProbeReadProm(EFI_PCI_IO_PROTOCOL *Pci, const PROBE_RESULT *Result,
                        VOID *Buffer, UINTN Size) {
  UINTN Done;
  if (Pci == NULL || Result == NULL || Buffer == NULL || Size == 0 ||
      Size > PROBE_PROM_SIZE || (Size & 3)) return EFI_INVALID_PARAMETER;
  if (!ProbeCandidate(Result->Config)) return EFI_UNSUPPORTED;
  if (!Result->MemoryEnabled) return EFI_NOT_READY;
  if (EFI_ERROR(Result->BarStatus)) return Result->BarStatus;
  if (Result->BarSize < PROBE_PROM_OFFSET ||
      Size > Result->BarSize - PROBE_PROM_OFFSET) return EFI_BAD_BUFFER_SIZE;
  for (Done = 0; Done < Size; ) {
    UINTN Chunk = Size - Done;
    EFI_STATUS Status;
    if (Chunk > 1024) Chunk = 1024;
    Status = Pci->Mem.Read(Pci, EfiPciIoWidthUint32, 0,
                          PROBE_PROM_OFFSET + Done, Chunk / 4, (UINT8 *)Buffer + Done);
    if (EFI_ERROR(Status)) return Status;
    Done += Chunk;
  }
  return EFI_SUCCESS;
}

BOOLEAN ProbeRomUsable(const VOID *Buffer, UINT64 Size) {
  const UINT8 *Bytes = Buffer;
  return Buffer != NULL && Size >= 512 && Size <= PROBE_ROM_LIMIT &&
         Bytes[0] == 0x55 && Bytes[1] == 0xaa;
}
