#pragma once
#include "port.h"
#include <Protocol/DevicePath.h>

EFI_STATUS GuBootLoadWindows(EFI_HANDLE Image, EFI_HANDLE *Child,
                             EFI_DEVICE_PATH_PROTOCOL **AllocatedPath);
VOID GuBootFreePath(EFI_DEVICE_PATH_PROTOCOL *Path);
