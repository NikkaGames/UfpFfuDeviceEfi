#ifndef UEFI_MIN_H
#define UEFI_MIN_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t BOOLEAN;
typedef uint16_t CHAR16;
typedef uint64_t UINTN;
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef uint64_t EFI_LBA;
typedef uint64_t EFI_TPL;

#define EFIAPI

#define EFI_SUCCESS 0
#define EFI_ERROR_BIT 0x8000000000000000ULL
#define EFIERR(x) (EFI_ERROR_BIT | (x))
#define EFI_LOAD_ERROR EFIERR(1)
#define EFI_INVALID_PARAMETER EFIERR(2)
#define EFI_UNSUPPORTED EFIERR(3)
#define EFI_BAD_BUFFER_SIZE EFIERR(4)
#define EFI_BUFFER_TOO_SMALL EFIERR(5)
#define EFI_NOT_READY EFIERR(6)
#define EFI_DEVICE_ERROR EFIERR(7)
#define EFI_WRITE_PROTECTED EFIERR(8)
#define EFI_OUT_OF_RESOURCES EFIERR(9)
#define EFI_VOLUME_CORRUPTED EFIERR(10)
#define EFI_VOLUME_FULL EFIERR(11)
#define EFI_NO_MEDIA EFIERR(12)
#define EFI_MEDIA_CHANGED EFIERR(13)
#define EFI_NOT_FOUND EFIERR(14)
#define EFI_ACCESS_DENIED EFIERR(15)
#define EFI_ABORTED EFIERR(21)
#define EFI_END_OF_FILE EFIERR(31)

#define EFI_ERROR(Status) (((EFI_STATUS)(Status) & EFI_ERROR_BIT) != 0)

#define EFI_FILE_MODE_READ  0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE 0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001U

typedef enum {
  AllocateAnyPages,
  AllocateMaxAddress,
  AllocateAddress,
  MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
  EfiReservedMemoryType,
  EfiLoaderCode,
  EfiLoaderData,
  EfiBootServicesCode,
  EfiBootServicesData,
  EfiRuntimeServicesCode,
  EfiRuntimeServicesData,
  EfiConventionalMemory,
  EfiUnusableMemory,
  EfiACPIReclaimMemory,
  EfiACPIMemoryNVS,
  EfiMemoryMappedIO,
  EfiMemoryMappedIOPortSpace,
  EfiPalCode,
  EfiPersistentMemory,
  EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef enum {
  AllHandles,
  ByRegisterNotify,
  ByProtocol
} EFI_LOCATE_SEARCH_TYPE;

typedef struct {
  uint32_t Data1;
  uint16_t Data2;
  uint16_t Data3;
  uint8_t Data4[8];
} EFI_GUID;

typedef struct {
  uint64_t Signature;
  uint32_t Revision;
  uint32_t HeaderSize;
  uint32_t CRC32;
  uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
  EFI_TEXT_RESET Reset;
  EFI_TEXT_STRING OutputString;
  void *TestString;
  void *QueryMode;
  void *SetMode;
  void *SetAttribute;
  void *ClearScreen;
  void *SetCursorPosition;
  void *EnableCursor;
  void *Mode;
};

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;
typedef struct EFI_CONFIGURATION_TABLE EFI_CONFIGURATION_TABLE;

struct EFI_BOOT_SERVICES {
  EFI_TABLE_HEADER Hdr;
  void *RaiseTPL;
  void *RestoreTPL;
  void *AllocatePages;
  void *FreePages;
  void *GetMemoryMap;
  EFI_STATUS (EFIAPI *AllocatePool)(EFI_MEMORY_TYPE PoolType, UINTN Size, void **Buffer);
  EFI_STATUS (EFIAPI *FreePool)(void *Buffer);
  void *CreateEvent;
  void *SetTimer;
  void *WaitForEvent;
  void *SignalEvent;
  void *CloseEvent;
  void *CheckEvent;
  void *InstallProtocolInterface;
  void *ReinstallProtocolInterface;
  void *UninstallProtocolInterface;
  EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface);
  void *Reserved;
  void *RegisterProtocolNotify;
  void *LocateHandle;
  void *LocateDevicePath;
  void *InstallConfigurationTable;
  void *LoadImage;
  void *StartImage;
  void *Exit;
  void *UnloadImage;
  void *ExitBootServices;
  void *GetNextMonotonicCount;
  EFI_STATUS (EFIAPI *Stall)(UINTN Microseconds);
  void *SetWatchdogTimer;
  void *ConnectController;
  void *DisconnectController;
  void *OpenProtocol;
  void *CloseProtocol;
  void *OpenProtocolInformation;
  void *ProtocolsPerHandle;
  EFI_STATUS (EFIAPI *LocateHandleBuffer)(EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID *Protocol, void *SearchKey, UINTN *NoHandles, EFI_HANDLE **Buffer);
  void *LocateProtocol;
  void *InstallMultipleProtocolInterfaces;
  void *UninstallMultipleProtocolInterfaces;
  void *CalculateCrc32;
  void *CopyMem;
  void *SetMem;
  void *CreateEventEx;
};

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *Protocol, void *Registration, void **Interface);

struct EFI_RUNTIME_SERVICES {
  EFI_TABLE_HEADER Hdr;
  void *GetTime;
  void *SetTime;
  void *GetWakeupTime;
  void *SetWakeupTime;
  void *SetVirtualAddressMap;
  void *ConvertPointer;
  EFI_STATUS (EFIAPI *GetVariable)(CHAR16 *VariableName, EFI_GUID *VendorGuid, uint32_t *Attributes, UINTN *DataSize, void *Data);
  void *GetNextVariableName;
  EFI_STATUS (EFIAPI *SetVariable)(CHAR16 *VariableName, EFI_GUID *VendorGuid, uint32_t Attributes, UINTN DataSize, void *Data);
  void *GetNextHighMonotonicCount;
  void *ResetSystem;
  void *UpdateCapsule;
  void *QueryCapsuleCapabilities;
  void *QueryVariableInfo;
};

typedef struct {
  EFI_TABLE_HEADER Hdr;
  CHAR16 *FirmwareVendor;
  uint32_t FirmwareRevision;
  EFI_HANDLE ConsoleInHandle;
  void *ConIn;
  EFI_HANDLE ConsoleOutHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
  EFI_HANDLE StandardErrorHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
  EFI_RUNTIME_SERVICES *RuntimeServices;
  EFI_BOOT_SERVICES *BootServices;
  UINTN NumberOfTableEntries;
  EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
  uint32_t Revision;
  EFI_HANDLE ParentHandle;
  EFI_SYSTEM_TABLE *SystemTable;
  EFI_HANDLE DeviceHandle;
  void *FilePath;
  void *Reserved;
  uint32_t LoadOptionsSize;
  void *LoadOptions;
  void *ImageBase;
  uint64_t ImageSize;
  EFI_MEMORY_TYPE ImageCodeType;
  EFI_MEMORY_TYPE ImageDataType;
  EFI_STATUS (EFIAPI *Unload)(EFI_HANDLE ImageHandle);
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle, CHAR16 *FileName, uint64_t OpenMode, uint64_t Attributes);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, void *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_WRITE)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, void *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_POSITION)(EFI_FILE_PROTOCOL *This, uint64_t *Position);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION)(EFI_FILE_PROTOCOL *This, uint64_t Position);

struct EFI_FILE_PROTOCOL {
  uint64_t Revision;
  EFI_FILE_OPEN Open;
  EFI_FILE_CLOSE Close;
  void *Delete;
  EFI_FILE_READ Read;
  EFI_FILE_WRITE Write;
  EFI_FILE_GET_POSITION GetPosition;
  EFI_FILE_SET_POSITION SetPosition;
  void *GetInfo;
  void *SetInfo;
  void *Flush;
  void *OpenEx;
  void *ReadEx;
  void *WriteEx;
  void *FlushEx;
};

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
  uint64_t Revision;
  EFI_STATUS (EFIAPI *OpenVolume)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);
};

typedef struct {
  uint32_t MediaId;
  BOOLEAN RemovableMedia;
  BOOLEAN MediaPresent;
  BOOLEAN LogicalPartition;
  BOOLEAN ReadOnly;
  BOOLEAN WriteCaching;
  uint32_t BlockSize;
  uint32_t IoAlign;
  EFI_LBA LastBlock;
  EFI_LBA LowestAlignedLba;
  uint32_t LogicalBlocksPerPhysicalBlock;
  uint32_t OptimalTransferLengthGranularity;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL EFI_BLOCK_IO_PROTOCOL;
struct EFI_BLOCK_IO_PROTOCOL {
  uint64_t Revision;
  EFI_BLOCK_IO_MEDIA *Media;
  EFI_STATUS (EFIAPI *Reset)(EFI_BLOCK_IO_PROTOCOL *This, BOOLEAN ExtendedVerification);
  EFI_STATUS (EFIAPI *ReadBlocks)(EFI_BLOCK_IO_PROTOCOL *This, uint32_t MediaId, EFI_LBA LBA, UINTN BufferSize, void *Buffer);
  EFI_STATUS (EFIAPI *WriteBlocks)(EFI_BLOCK_IO_PROTOCOL *This, uint32_t MediaId, EFI_LBA LBA, UINTN BufferSize, void *Buffer);
  EFI_STATUS (EFIAPI *FlushBlocks)(EFI_BLOCK_IO_PROTOCOL *This);
};

extern EFI_GUID gEfiLoadedImageProtocolGuid;
extern EFI_GUID gEfiSimpleFileSystemProtocolGuid;
extern EFI_GUID gEfiBlockIoProtocolGuid;

#endif
