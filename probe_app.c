#include "probe_core.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/FileInfo.h>

static EFI_GUID PciGuid = EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID LoadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID FsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
static EFI_SYSTEM_TABLE *St;
static CHAR8 Log[32768];
static UINTN Used;
static CHAR8 Profile[2048];
static UINTN ProfileUsed;
/* UEFI may place the image anywhere.  Keep one absolute self-reference so the
   PE contains a DIR64 base-relocation entry even when all code is RIP-relative. */
static VOID * volatile RelocationAnchor = (VOID *)&RelocationAnchor;

static VOID Text(const CHAR8 *S) {
  while (*S && Used + 1 < sizeof(Log)) Log[Used++] = *S++;
  Log[Used] = 0;
}
static VOID Hex(const CHAR8 *Key, UINT64 Value) {
  static const CHAR8 Digits[] = "0123456789abcdef";
  INTN I;
  Text(Key); Text("=0x");
  for (I = 60; I >= 0; I -= 4) {
    CHAR8 S[2] = {Digits[(Value >> I) & 15], 0}; Text(S);
  }
  Text("\r\n");
}
static VOID ProfileText(const CHAR8 *S) {
  while (*S && ProfileUsed + 1 < sizeof(Profile)) Profile[ProfileUsed++] = *S++;
  Profile[ProfileUsed] = 0;
}
static VOID ProfileHex(const CHAR8 *Key, UINT64 Value) {
  static const CHAR8 Digits[] = "0123456789abcdef";
  INTN I;
  ProfileText(Key); ProfileText("=0x");
  for (I = 60; I >= 0; I -= 4) {
    CHAR8 S[2] = {Digits[(Value >> I) & 15], 0}; ProfileText(S);
  }
  ProfileText("\r\n");
}
static VOID Say(CHAR16 *S) {
  if (St->ConOut && St->ConOut->OutputString) St->ConOut->OutputString(St->ConOut, S);
}
static VOID SayHex(UINT64 Value, UINTN Digits) {
  static const CHAR16 HexDigits[] = L"0123456789abcdef";
  CHAR16 Buffer[17];
  UINTN I;
  if (Digits > 16) Digits = 16;
  for (I = 0; I < Digits; I++)
    Buffer[I] = HexDigits[(Value >> ((Digits - I - 1) * 4)) & 15];
  Buffer[Digits] = 0; Say(Buffer);
}
static VOID ShowLog(VOID) {
  CHAR16 Line[128];
  UINTN I = 0;
  while (I < Used) {
    UINTN N = 0;
    while (I < Used && N < 127) Line[N++] = (CHAR16)(UINT8)Log[I++];
    Line[N] = 0; Say(Line);
  }
}

/* Bounded walk of firmware-owned device-path nodes, without opening controllers. */
static BOOLEAN PathHasUsb(EFI_HANDLE Handle, BOOLEAN Record) {
  EFI_DEVICE_PATH_PROTOCOL *Path = NULL;
  UINTN Total = 0, Nodes = 0;
  BOOLEAN Usb = FALSE;
  EFI_STATUS Status = St->BootServices->HandleProtocol(Handle, &PathGuid, (VOID **)&Path);
  if (Record) Hex("device_path_status", Status);
  if (EFI_ERROR(Status) || Path == NULL) return FALSE;
  while (Nodes++ < 64 && Total + 4 <= 4096) {
    UINTN Length = Path->Length[0] | ((UINTN)Path->Length[1] << 8);
    UINTN I;
    if (Length < 4 || Length > 4096 - Total) break;
    if (Path->Type == MESSAGING_DEVICE_PATH &&
        (Path->SubType == MSG_USB_DP || Path->SubType == MSG_USB_CLASS_DP ||
         Path->SubType == MSG_USB_WWID_DP)) Usb = TRUE;
    if (Record) {
      static const CHAR8 Digits[] = "0123456789abcdef";
      Text("path_node=");
      for (I = 0; I < Length; I++) {
        UINT8 B = ((UINT8 *)Path)[I];
        CHAR8 Pair[3] = {Digits[B >> 4], Digits[B & 15], 0}; Text(Pair);
      }
      Text("\r\n");
    }
    if (Path->Type == END_DEVICE_PATH_TYPE)
      return Usb && Length == 4 && Path->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE;
    Total += Length;
    Path = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)Path + Length);
  }
  return FALSE; /* Malformed or unterminated path is never an output destination. */
}

/* Only a USB-loaded image with our existing marker directory can create files. */
static EFI_FILE_PROTOCOL *OpenOutput(EFI_HANDLE Image) {
  EFI_LOADED_IMAGE_PROTOCOL *Loaded = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
  EFI_FILE_PROTOCOL *Root = NULL, *Parent = NULL, *Run = NULL;
  EFI_STATUS Status;
  UINTN Slot;
  union { EFI_FILE_INFO Info; UINT8 Bytes[512]; } Metadata;
  UINTN MetadataSize = sizeof(Metadata);
  Status = St->BootServices->HandleProtocol(Image, &LoadedGuid, (VOID **)&Loaded);
  Hex("loaded_image_status", Status);
  if (EFI_ERROR(Status) || Loaded == NULL) return NULL;
  if (!PathHasUsb(Loaded->DeviceHandle, TRUE)) {
    Text("output=disabled: loaded image device is not a verified USB path\r\n");
    return NULL;
  }
  Status = St->BootServices->HandleProtocol(Loaded->DeviceHandle, &FsGuid, (VOID **)&Fs);
  Hex("usb_filesystem_status", Status);
  if (EFI_ERROR(Status) || Fs == NULL) return NULL;
  Status = Fs->OpenVolume(Fs, &Root);
  Hex("usb_open_volume_status", Status);
  if (EFI_ERROR(Status) || Root == NULL) return NULL;
  Status = Root->Open(Root, &Parent, L"\\RTX3080TI-PROBE",
                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  Root->Close(Root);
  Hex("probe_directory_status", Status);
  if (EFI_ERROR(Status) || Parent == NULL) return NULL;
  Status = Parent->GetInfo(Parent, &FileInfoGuid, &MetadataSize, &Metadata);
  if (EFI_ERROR(Status) || MetadataSize < SIZE_OF_EFI_FILE_INFO ||
      !(Metadata.Info.Attribute & EFI_FILE_DIRECTORY)) {
    Text("output=disabled: marker is not a verified directory\r\n");
    Parent->Close(Parent); return NULL;
  }
  for (Slot = 0; Slot < 100; Slot++) {
    CHAR16 Name[] = L"RUN00";
    Name[3] = (CHAR16)('0' + Slot / 10); Name[4] = (CHAR16)('0' + Slot % 10);
    Status = Parent->Open(Parent, &Run, Name, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(Status)) { Run->Close(Run); Run = NULL; continue; }
    if (Status != EFI_NOT_FOUND) break;
    Status = Parent->Open(Parent, &Run, Name,
      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
    Hex("run_directory_status", Status); Hex("run_slot", Slot);
    if (EFI_ERROR(Status)) Run = NULL;
    break;
  }
  Parent->Close(Parent);
  return Run;
}

static EFI_STATUS Save(EFI_FILE_PROTOCOL *Directory, CHAR16 *Name,
                       VOID *Data, UINTN Size) {
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_STATUS Status;
  UINTN Written = Size;
  if (!Directory) return EFI_NOT_FOUND;
  Status = Directory->Open(Directory, &File, Name, EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR(Status)) { File->Close(File); return EFI_ALREADY_STARTED; }
  if (Status != EFI_NOT_FOUND) return Status;
  Status = Directory->Open(Directory, &File, Name,
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR(Status)) return Status;
  Status = File->Write(File, &Written, Data);
  if (!EFI_ERROR(Status) && Written != Size) Status = EFI_VOLUME_FULL;
  if (!EFI_ERROR(Status)) Status = File->Flush(File);
  File->Close(File);
  return Status;
}

static VOID RecordGop(VOID) {
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0, I;
  EFI_STATUS Status = St->BootServices->LocateHandleBuffer(ByProtocol, &GopGuid,
                                                        NULL, &Count, &Handles);
  Hex("gop_enumeration_status", Status);
  if (EFI_ERROR(Status)) return;
  for (I = 0; I < Count; I++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    Status = St->BootServices->HandleProtocol(Handles[I], &GopGuid, (VOID **)&Gop);
    if (EFI_ERROR(Status) || !Gop || !Gop->Mode) continue;
    Text("[GOP]\r\n"); Hex("index", I);
    Hex("mode", Gop->Mode->Mode);
    Hex("framebuffer_base", Gop->Mode->FrameBufferBase);
    Hex("framebuffer_size", Gop->Mode->FrameBufferSize);
    if (Gop->Mode->Info) {
      Hex("width", Gop->Mode->Info->HorizontalResolution);
      Hex("height", Gop->Mode->Info->VerticalResolution);
      Hex("pixels_per_scanline", Gop->Mode->Info->PixelsPerScanLine);
      Hex("pixel_format", Gop->Mode->Info->PixelFormat);
    }
    PathHasUsb(Handles[I], TRUE);
  }
  if (Handles) St->BootServices->FreePool(Handles);
}

typedef struct {
  EFI_HANDLE Handle;
  EFI_PCI_IO_PROTOCOL *Pci;
  UINT32 Config[64];
  UINTN Segment, Bus, Device, Function;
  PROBE_PCI_ROUTE Route;
  BOOLEAN RouteValid;
} PROBE_CANDIDATE_RECORD;

static BOOLEAN CandidateRoute(EFI_HANDLE Handle, PROBE_PCI_ROUTE *Route) {
  EFI_DEVICE_PATH_PROTOCOL *Path = NULL;
  EFI_STATUS Status = St->BootServices->HandleProtocol(Handle, &PathGuid, (VOID **)&Path);
  return !EFI_ERROR(Status) && Path && ProbePciRoute(Path, 4096, Route);
}

static VOID ShowCandidate(UINTN Index, const PROBE_CANDIDATE_RECORD *Candidate) {
  CHAR16 Number[2] = {(CHAR16)('1' + Index), 0};
  Say(L"  ["); Say(Number); Say(L"] ");
  SayHex(Candidate->Segment, 4); Say(L":"); SayHex(Candidate->Bus, 2);
  Say(L":"); SayHex(Candidate->Device, 2); Say(L"."); SayHex(Candidate->Function, 1);
  Say(L"  device="); SayHex(Candidate->Config[0] >> 16, 4);
  Say(L" subsystem="); SayHex(Candidate->Config[11], 8);
  Say(Candidate->RouteValid ? L"\r\n" : L"  [unsupported device path]\r\n");
}

static EFI_STATUS SelectCandidate(PROBE_CANDIDATE_RECORD *Candidates, UINTN Count,
                                  UINTN *Selected) {
  UINTN I;
  EFI_INPUT_KEY Key;
  if (!Candidates || !Selected || Count == 0 || Count > PROBE_MAX_CANDIDATES)
    return EFI_INVALID_PARAMETER;
  Say(L"\r\nSupported RTX 3080 Ti candidates:\r\n");
  for (I = 0; I < Count; I++) ShowCandidate(I, &Candidates[I]);
  if (Count == 1) {
    if (!Candidates[0].RouteValid) return EFI_UNSUPPORTED;
    Say(L"One candidate found; selected automatically.\r\n");
    *Selected = 0; return EFI_SUCCESS;
  }
  if (!St->ConIn || !St->ConIn->ReadKeyStroke) return EFI_UNSUPPORTED;
  Say(L"Press 1-9 to select a GPU, or Esc to cancel (30 seconds).\r\n");
  for (I = 0; I < 300; I++) {
    EFI_STATUS Status = St->ConIn->ReadKeyStroke(St->ConIn, &Key);
    if (!EFI_ERROR(Status)) {
      Status = ProbeSelectionKey(Count, Key, Selected);
      if (Status == EFI_ABORTED) return Status;
      if (!EFI_ERROR(Status))
        return Candidates[*Selected].RouteValid ? EFI_SUCCESS : EFI_UNSUPPORTED;
    } else if (Status != EFI_NOT_READY) return Status;
    St->BootServices->Stall(100000);
  }
  return EFI_TIMEOUT;
}

static BOOLEAN SampleValue(const PROBE_RESULT *Result, UINT32 Offset, UINT32 *Value) {
  UINTN I;
  for (I = 0; I < PROBE_SAMPLE_COUNT; I++) {
    if (Result->Samples[I].Offset == Offset && !EFI_ERROR(Result->Samples[I].Status)) {
      *Value = Result->Samples[I].Value; return TRUE;
    }
  }
  return FALSE;
}

static EFI_STATUS MakeProfile(const PROBE_CANDIDATE_RECORD *Candidate,
                              const PROBE_RESULT *Result) {
  UINT32 Chip, Fb, Ss0, Ss1;
  if (!Candidate || !Result || !Candidate->RouteValid ||
      Candidate->Route.EndpointDevice != Candidate->Device ||
      Candidate->Route.EndpointFunction != Candidate->Function ||
      EFI_ERROR(Result->BarStatus) || Result->BarTranslation ||
      !SampleValue(Result, 0x000000, &Chip) ||
      !SampleValue(Result, 0x1183a4, &Fb) ||
      !SampleValue(Result, 0x82381c, &Ss0) ||
      !SampleValue(Result, 0x823820, &Ss1)) return EFI_UNSUPPORTED;
  ProfileUsed = 0; Profile[0] = 0;
  ProfileText("format=rtx3080ti-probe-v1\r\n");
  ProfileHex("identity", Candidate->Config[0]);
  ProfileHex("class_revision", Candidate->Config[2]);
  ProfileHex("subsystem", Candidate->Config[11]);
  ProfileHex("segment", Candidate->Segment); ProfileHex("bus", Candidate->Bus);
  ProfileHex("device", Candidate->Device); ProfileHex("function", Candidate->Function);
  ProfileHex("root_hid", Candidate->Route.RootHid);
  ProfileHex("root_uid", Candidate->Route.RootUid);
  ProfileHex("root_port_device", Candidate->Route.RootPortDevice);
  ProfileHex("root_port_function", Candidate->Route.RootPortFunction);
  ProfileHex("bar0_base", Result->BarBase); ProfileHex("bar0_maximum", Result->BarMaximum);
  ProfileHex("bar0_size", Result->BarSize);
  ProfileHex("bar0_translation", Result->BarTranslation);
  ProfileHex("bar0_granularity", Result->BarGranularity);
  ProfileHex("chip_id", Chip); ProfileHex("fb_mb", Fb);
  ProfileHex("cold_ss0", Ss0); ProfileHex("cold_ss1", Ss1);
  ProfileHex("prom_size", PROBE_PROM_SIZE); ProfileHex("gpu_writes", 0);
  return ProfileUsed && ProfileUsed + 1 < sizeof(Profile) ? EFI_SUCCESS : EFI_BUFFER_TOO_SMALL;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE Image, EFI_SYSTEM_TABLE *Table) {
  EFI_BOOT_SERVICES *Bs;
  EFI_HANDLE *Handles = NULL, SelectedHandle = NULL;
  EFI_PCI_IO_PROTOCOL *Selected = NULL;
  PROBE_CANDIDATE_RECORD Candidates[PROBE_MAX_CANDIDATES];
  EFI_FILE_PROTOCOL *Output;
  UINTN Count = 0, I, CandidateCount = 0, SelectedIndex = 0;
  EFI_STATUS Status;
  PROBE_RESULT Result;
  if (RelocationAnchor != (VOID *)&RelocationAnchor) return EFI_LOAD_ERROR;
  St = Table; Bs = St->BootServices; Used = ProfileUsed = 0; Log[0] = Profile[0] = 0;
  Say(L"\r\nRTX 3080 Ti read-only profile probe v0.2\r\n");
  Say(L"Collects selected GPU state. This does not unlock or write to the GPU.\r\n");
  Text("RTX3080Ti read-only profile probe v0.2\r\n");
  Text("Supported PCI identity=0x220810de; subsystem and PCI location are discovered\r\n");
  Output = OpenOutput(Image);
  Status = Bs->LocateHandleBuffer(ByProtocol, &PciGuid, NULL, &Count, &Handles);
  Hex("pci_enumeration_status", Status);
  if (!EFI_ERROR(Status)) {
    for (I = 0; I < Count; I++) {
      EFI_PCI_IO_PROTOCOL *Pci = NULL;
      UINT32 Config[64];
      UINTN Segment = 0, Bus = 0, Device = 0, Function = 0;
      Status = Bs->HandleProtocol(Handles[I], &PciGuid, (VOID **)&Pci);
      if (EFI_ERROR(Status) || Pci == NULL) continue;
      Status = Pci->Pci.Read(Pci, EfiPciIoWidthUint32, 0, 64, Config);
      if (EFI_ERROR(Status) || (Config[0] & 0xffff) != 0x10de) continue;
      Text("[NVIDIA PCI]\r\n");
      Status = Pci->GetLocation(Pci, &Segment, &Bus, &Device, &Function);
      Hex("location_status", Status); Hex("segment", Segment); Hex("bus", Bus);
      Hex("device", Device); Hex("function", Function);
      Hex("identity", Config[0]); Hex("class_revision", Config[2]);
      Hex("subsystem", Config[11]);
      if (!EFI_ERROR(Status) && ProbeCandidate(Config)) {
        if (CandidateCount >= PROBE_MAX_CANDIDATES) {
          Text("candidate_limit_exceeded=1\r\n"); CandidateCount++; continue;
        }
        Candidates[CandidateCount].Handle = Handles[I];
        Candidates[CandidateCount].Pci = Pci;
        Candidates[CandidateCount].Segment = Segment; Candidates[CandidateCount].Bus = Bus;
        Candidates[CandidateCount].Device = Device; Candidates[CandidateCount].Function = Function;
        for (UINTN J = 0; J < 64; J++) Candidates[CandidateCount].Config[J] = Config[J];
        Candidates[CandidateCount].RouteValid = CandidateRoute(Handles[I], &Candidates[CandidateCount].Route);
        Hex("candidate_index", CandidateCount); Hex("candidate_route_valid", Candidates[CandidateCount].RouteValid);
        CandidateCount++;
      }
    }
  }
  if (Handles) Bs->FreePool(Handles);
  Hex("candidate_count", CandidateCount);
  RecordGop();
  Status = CandidateCount <= PROBE_MAX_CANDIDATES ?
    SelectCandidate(Candidates, CandidateCount, &SelectedIndex) : EFI_UNSUPPORTED;
  Hex("selection_status", Status);
  if (!EFI_ERROR(Status)) {
    PROBE_CANDIDATE_RECORD *Choice = &Candidates[SelectedIndex];
    Selected = Choice->Pci; SelectedHandle = Choice->Handle;
    Hex("selected_index", SelectedIndex); Hex("selected_segment", Choice->Segment);
    Hex("selected_bus", Choice->Bus); Hex("selected_device", Choice->Device);
    Hex("selected_function", Choice->Function);
    Say(L"Reading the selected RTX 3080 Ti existing state...\r\n");
    Text("[TARGET]\r\n"); PathHasUsb(SelectedHandle, TRUE);
    Status = ProbeCollect(Selected, Bs, &Result);
    Hex("collect_status", Status);
    if (!EFI_ERROR(Status)) {
      for (I = 0; I < 64; I++) { Hex("config_offset", I * 4); Hex("value", Result.Config[I]); }
      Hex("bar0_status", Result.BarStatus); Hex("bar0_base", Result.BarBase);
      Hex("bar0_maximum", Result.BarMaximum); Hex("bar0_length", Result.BarSize);
      Hex("bar0_translation", Result.BarTranslation); Hex("bar0_granularity", Result.BarGranularity);
      Hex("memory_decode_enabled", Result.MemoryEnabled);
      for (I = 0; I < PROBE_SAMPLE_COUNT; I++) {
        PROBE_SAMPLE *Sample = &Result.Samples[I];
        Hex("register", Sample->Offset); Hex("status", Sample->Status); Hex("value", Sample->Value);
        if (!EFI_ERROR(Sample->Status) &&
            (Sample->Value == 0xffffffff || (Sample->Value & 0xffff0000U) == 0xbadf0000U))
          Text("interpretation=inaccessible, not a valid measurement\r\n");
      }
      Hex("rom_image_size", Selected->RomSize);
      if (Output && ProbeRomUsable(Selected->RomImage, Selected->RomSize)) {
        Status = Save(Output, L"ROMIMAGE.BIN", Selected->RomImage, (UINTN)Selected->RomSize);
        Hex("rom_image_save_status", Status);
      } else Text("rom_image=not saved (no writable USB or missing/invalid firmware ROM copy)\r\n");
      if (Output && Result.MemoryEnabled && !EFI_ERROR(Result.BarStatus) &&
          Result.BarSize >= PROBE_PROM_OFFSET + PROBE_PROM_SIZE) {
        VOID *Prom = NULL;
        Status = Bs->AllocatePool(EfiLoaderData, PROBE_PROM_SIZE, &Prom);
        Hex("prom_allocation_status", Status);
        if (!EFI_ERROR(Status) && Prom) {
          Say(L"Reading the 1 MiB PROM window; no ROM-enable writes are used.\r\n");
          Status = ProbeReadProm(Selected, &Result, Prom, PROBE_PROM_SIZE);
          Hex("prom_read_status", Status);
          if (!EFI_ERROR(Status) && ProbeRomUsable(Prom, PROBE_PROM_SIZE)) {
            Status = Save(Output, L"PROM.BIN", Prom, PROBE_PROM_SIZE);
            Hex("prom_save_status", Status);
            if (!EFI_ERROR(Status)) {
              EFI_STATUS ProfileStatus = MakeProfile(Choice, &Result);
              Hex("profile_build_status", ProfileStatus);
              if (!EFI_ERROR(ProfileStatus)) {
                ProfileStatus = Save(Output, L"PROFILE.CFG", Profile, ProfileUsed);
                Hex("profile_save_status", ProfileStatus);
              }
            }
          } else Text("prom=not saved (read error or no 55aa ROM signature)\r\n");
          Bs->FreePool(Prom);
        }
      }
    }
  } else {
    Text("target=not selected; no GPU MMIO or ROM reads attempted\r\n");
    Say(L"No supported GPU selected. No GPU MMIO or ROM reads were attempted.\r\n");
  }
  Text("GPU writes=none; GPU resets=none; boot-variable changes=none\r\n");
  Status = Save(Output, L"REPORT.TXT", Log, Used);
  if (Output) Output->Close(Output);
  if (EFI_ERROR(Status)) {
    Say(L"Could not save report. Diagnostic details follow:\r\n");
    Hex("report_save_status", Status); ShowLog();
  } else Say(L"Saved under USB \\RTX3080TI-PROBE\\RUNxx (report, PROM and profile when valid).\r\n");
  Say(L"Returning in 15 seconds. Choose Windows Boot Manager if a boot menu appears.\r\n");
  Bs->Stall(15000000);
  return EFI_SUCCESS;
}
