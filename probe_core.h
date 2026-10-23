#pragma once
#include <Uefi.h>
#include <Protocol/PciIo.h>
#include <IndustryStandard/Acpi10.h>

#define PROBE_SAMPLE_COUNT 16
#define PROBE_PROM_OFFSET 0x00300000ULL
#define PROBE_PROM_SIZE 0x00100000U
#define PROBE_ROM_LIMIT 0x01000000U
#define PROBE_MAX_CANDIDATES 9U

/* The probe is portable across RTX 3080 Ti board vendors and PCI layouts, but
   intentionally refuses every other GPU family before any MMIO is sampled. */
#define PROBE_SUPPORTED_ID 0x220810deU

typedef struct {
  UINT32 Offset;
  UINT32 Value;
  EFI_STATUS Status;
} PROBE_SAMPLE;

typedef struct {
  UINT32 Config[64];
  UINT64 BarBase;
  UINT64 BarMaximum;
  UINT64 BarSize;
  UINT64 BarTranslation;
  UINT64 BarGranularity;
  EFI_STATUS BarStatus;
  BOOLEAN MemoryEnabled;
  PROBE_SAMPLE Samples[PROBE_SAMPLE_COUNT];
} PROBE_RESULT;

typedef struct {
  UINT32 RootHid;
  UINT32 RootUid;
  UINT8 RootPortDevice;
  UINT8 RootPortFunction;
  UINT8 EndpointDevice;
  UINT8 EndpointFunction;
} PROBE_PCI_ROUTE;

typedef struct {
  EFI_GUID Protocol;
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY Entry;
} PROBE_OWNER_RECORD;

BOOLEAN ProbeCandidate(const UINT32 *Config);
BOOLEAN ProbePciRoute(const EFI_DEVICE_PATH_PROTOCOL *Path, UINTN MaxBytes,
                      PROBE_PCI_ROUTE *Route);
EFI_STATUS ProbeSelectionKey(UINTN CandidateCount, EFI_INPUT_KEY Key, UINTN *Selected);
BOOLEAN ProbeColdSelectors(UINT32 Ss0, UINT32 Ss1);
BOOLEAN ProbeDecodeTransition(const UINT32 *Initial, const UINT32 *Disabled,
                              const UINT32 *Restored);
EFI_STATUS ProbeReleaseRelationship(const PROBE_OWNER_RECORD *Records, UINTN Count,
                                    const EFI_GUID *PciGuid, EFI_HANDLE Target,
                                    EFI_HANDLE *Agent, EFI_HANDLE *Child);
EFI_STATUS ProbeCollect(EFI_PCI_IO_PROTOCOL *Pci, EFI_BOOT_SERVICES *Bs,
                       PROBE_RESULT *Result);
EFI_STATUS ProbeReadProm(EFI_PCI_IO_PROTOCOL *Pci, const PROBE_RESULT *Result,
                        VOID *Buffer, UINTN Size);
BOOLEAN ProbeRomUsable(const VOID *Buffer, UINT64 Size);
