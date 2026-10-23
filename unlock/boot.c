#include "boot.h"
#include "app_io.h"
#include "runtime.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/PartitionInfo.h>
#include <Guid/Gpt.h>

#define GU_BOOT_PATH_LIMIT 4096U

static EFI_GUID LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID PartitionGuid=EFI_PARTITION_INFO_PROTOCOL_GUID;
static EFI_GUID EspGuid=EFI_PART_TYPE_EFI_SYSTEM_PART_GUID;

static BOOLEAN SameGuid(const EFI_GUID *a,const EFI_GUID *b) {
  return !memcmp(a,b,sizeof(*a));
}

static UINTN PathBytes(EFI_DEVICE_PATH_PROTOCOL *path,BOOLEAN *usb) {
  UINTN total=0,nodes=0;*usb=FALSE;
  while(path && nodes++<64 && total+4<=GU_BOOT_PATH_LIMIT) {
    UINTN bytes=path->Length[0]+((UINTN)path->Length[1]<<8);
    if(bytes<4 || bytes>GU_BOOT_PATH_LIMIT-total)return 0;
    if(path->Type==MESSAGING_DEVICE_PATH &&
      (path->SubType==MSG_USB_DP || path->SubType==MSG_USB_CLASS_DP ||
       path->SubType==MSG_USB_WWID_DP))*usb=TRUE;
    total+=bytes;
    if(path->Type==END_DEVICE_PATH_TYPE &&
       path->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE && bytes==4)return total;
    path=(EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)path+bytes);
  }
  return 0;
}

static EFI_DEVICE_PATH_PROTOCOL *FilePath(EFI_HANDLE device,CHAR16 *name) {
  EFI_DEVICE_PATH_PROTOCOL *base=NULL,*result,*node,*end;BOOLEAN usb=FALSE;
  EFI_STATUS status=Gu.bs->HandleProtocol(device,&PathGuid,(VOID **)&base);
  if(EFI_ERROR(status) || !base)return NULL;
  UINTN base_bytes=PathBytes(base,&usb),chars=0;
  while(name[chars])chars++;
  UINTN name_bytes=(chars+1)*sizeof(CHAR16),node_bytes=4+name_bytes;
  if(!base_bytes || usb || node_bytes>0xffff)return NULL;
  UINTN total=base_bytes-4+node_bytes+4;
  status=Gu.bs->AllocatePool(EfiLoaderData,total,(VOID **)&result);
  if(EFI_ERROR(status) || !result)return NULL;
  memcpy(result,base,base_bytes-4);
  node=(EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)result+base_bytes-4);
  node->Type=MEDIA_DEVICE_PATH;node->SubType=MEDIA_FILEPATH_DP;
  node->Length[0]=(UINT8)node_bytes;node->Length[1]=(UINT8)(node_bytes>>8);
  memcpy((UINT8 *)node+4,name,name_bytes);
  end=(EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)node+node_bytes);
  end->Type=END_DEVICE_PATH_TYPE;end->SubType=END_ENTIRE_DEVICE_PATH_SUBTYPE;
  end->Length[0]=4;end->Length[1]=0;
  return result;
}

EFI_STATUS GuBootLoadWindows(EFI_HANDLE image,EFI_HANDLE *child,
                             EFI_DEVICE_PATH_PROTOCOL **allocated_path) {
  static CHAR16 name[]=L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
  EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;EFI_HANDLE *handles=NULL,*selected=NULL;
  EFI_DEVICE_PATH_PROTOCOL *path=NULL;UINTN count=0,matches=0;
  EFI_STATUS status;
  if(!child || !allocated_path)return EFI_INVALID_PARAMETER;
  *child=NULL;*allocated_path=NULL;
  status=Gu.bs->HandleProtocol(image,&LoadedGuid,(VOID **)&loaded);
  if(EFI_ERROR(status) || !loaded)return EFI_UNSUPPORTED;
  status=Gu.bs->LocateHandleBuffer(ByProtocol,&FsGuid,NULL,&count,&handles);
  GuLog("Windows ESP enumeration: status=%#llx handles=%zu\n",
    (unsigned long long)status,(size_t)count);
  if(EFI_ERROR(status) || !handles || !count || count>64)goto out;
  for(UINTN i=0;i<count;i++) {
    EFI_DEVICE_PATH_PROTOCOL *device_path=NULL;EFI_PARTITION_INFO_PROTOCOL *partition=NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_FILE_PROTOCOL *root=NULL,*file=NULL;
    BOOLEAN usb=FALSE;
    if(handles[i]==loaded->DeviceHandle)continue;
    status=Gu.bs->HandleProtocol(handles[i],&PathGuid,(VOID **)&device_path);
    if(EFI_ERROR(status) || !device_path || !PathBytes(device_path,&usb) || usb)continue;
    status=Gu.bs->HandleProtocol(handles[i],&PartitionGuid,(VOID **)&partition);
    if(EFI_ERROR(status) || !partition || partition->Type!=PARTITION_TYPE_GPT ||
       partition->System!=1 || !SameGuid(&partition->Info.Gpt.PartitionTypeGUID,&EspGuid))continue;
    status=Gu.bs->HandleProtocol(handles[i],&FsGuid,(VOID **)&fs);
    if(EFI_ERROR(status) || !fs || EFI_ERROR(fs->OpenVolume(fs,&root)) || !root)continue;
    status=root->Open(root,&file,name,EFI_FILE_MODE_READ,0);
    if(!EFI_ERROR(status) && file) {
      EFI_DEVICE_PATH_PROTOCOL *candidate=FilePath(handles[i],name);
      matches++;
      if(matches==1 && candidate){selected=handles[i];path=candidate;}
      else if(candidate)Gu.bs->FreePool(candidate);
      file->Close(file);
    }
    root->Close(root);
  }
  status=matches==1 && selected && path?EFI_SUCCESS:matches>1?EFI_ABORTED:EFI_NOT_FOUND;
  GuLog("Windows ESP selection: matches=%zu status=%#llx\n",
    (size_t)matches,(unsigned long long)status);
  if(EFI_ERROR(status))goto out;
  status=Gu.bs->LoadImage(TRUE,image,path,NULL,0,child);
  GuLog("Windows Boot Manager LoadImage: status=%#llx child=%#llx\n",
    (unsigned long long)status,(unsigned long long)(UINTN)*child);
  if(EFI_ERROR(status) || !*child) {
    if(!EFI_ERROR(status))status=EFI_LOAD_ERROR;
    goto out;
  }
  *allocated_path=path;path=NULL;
out:
  if(path)Gu.bs->FreePool(path);
  if(handles)Gu.bs->FreePool(handles);
  return status;
}

VOID GuBootFreePath(EFI_DEVICE_PATH_PROTOCOL *path) {
  if(path)Gu.bs->FreePool(path);
}
