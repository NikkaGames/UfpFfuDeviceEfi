#ifndef USBFN_H
#define USBFN_H

#include "uefi_min.h"
#include <stdint.h>

#define USBFN_MAX_XFER 0x241000U
#define USBFN_EP0_XFER 512U
#define USBFN_BULK_ENDPOINT_INDEX 1U

typedef enum {
  EfiUsbEndpointControl = 0,
  EfiUsbEndpointIsochronous = 1,
  EfiUsbEndpointBulk = 2,
  EfiUsbEndpointInterrupt = 3
} EFI_USB_ENDPOINT_TYPE;

typedef enum {
  EfiUsbEndpointDirectionHostOut = 0,
  EfiUsbEndpointDirectionHostIn = 1,
  EfiUsbEndpointDirectionDeviceTx = EfiUsbEndpointDirectionHostIn,
  EfiUsbEndpointDirectionDeviceRx = EfiUsbEndpointDirectionHostOut
} EFI_USBFN_ENDPOINT_DIRECTION;

typedef enum {
  EfiUsbUnknownPort = 0,
  EfiUsbStandardDownstreamPort,
  EfiUsbChargingDownstreamPort,
  EfiUsbDedicatedChargingPort,
  EfiUsbInvalidDedicatedChargingPort
} EFI_USBFN_PORT_TYPE;

typedef enum {
  EfiUsbBusSpeedUnknown = 0,
  EfiUsbBusSpeedLow = 1,
  EfiUsbBusSpeedFull = 2,
  EfiUsbBusSpeedHigh = 3,
  EfiUsbBusSpeedSuper = 4
} EFI_USB_BUS_SPEED;

typedef enum {
  EfiUsbMsgNone = 0,
  EfiUsbMsgSetupPacket = 1,
  EfiUsbMsgEndpointStatusChangedRx = 2,
  EfiUsbMsgEndpointStatusChangedTx = 3,
  EfiUsbMsgBusEventDetach = 4,
  EfiUsbMsgBusEventAttach = 5,
  EfiUsbMsgBusEventReset = 6,
  EfiUsbMsgBusEventSuspend = 7,
  EfiUsbMsgBusEventResume = 8,
  EfiUsbMsgBusEventSpeed = 9
} EFI_USBFN_MESSAGE;

typedef enum {
  UsbTransferStatusUnknown = 0,
  UsbTransferStatusComplete = 1,
  UsbTransferStatusAborted = 2
} EFI_USBFN_TRANSFER_STATUS;

typedef struct {
  uint8_t RequestType;
  uint8_t Request;
  uint16_t Value;
  uint16_t Index;
  uint16_t Length;
} USB_SETUP_PACKET;

typedef struct {
  UINTN BytesTransferred;
  uint32_t TransferStatus;
  uint8_t EndpointIndex;
  uint8_t Reserved[3];
} USBFN_TRANSFER_RESULT;

typedef struct {
  uint8_t Length;
  uint8_t DescriptorType;
  uint16_t BcdUSB;
  uint8_t DeviceClass;
  uint8_t DeviceSubClass;
  uint8_t DeviceProtocol;
  uint8_t MaxPacketSize0;
  uint16_t IdVendor;
  uint16_t IdProduct;
  uint16_t BcdDevice;
  uint8_t StrManufacturer;
  uint8_t StrProduct;
  uint8_t StrSerialNumber;
  uint8_t NumConfigurations;
} USB_DEVICE_DESCRIPTOR;

typedef struct {
  uint8_t Length;
  uint8_t DescriptorType;
  uint16_t TotalLength;
  uint8_t NumInterfaces;
  uint8_t ConfigurationValue;
  uint8_t Configuration;
  uint8_t Attributes;
  uint8_t MaxPower;
} USB_CONFIG_DESCRIPTOR;

typedef struct {
  uint8_t Length;
  uint8_t DescriptorType;
  uint8_t InterfaceNumber;
  uint8_t AlternateSetting;
  uint8_t NumEndpoints;
  uint8_t InterfaceClass;
  uint8_t InterfaceSubClass;
  uint8_t InterfaceProtocol;
  uint8_t Interface;
} USB_INTERFACE_DESCRIPTOR;

typedef struct {
  uint8_t Length;
  uint8_t DescriptorType;
  uint8_t EndpointAddress;
  uint8_t Attributes;
  uint16_t MaxPacketSize;
  uint8_t Interval;
} USB_ENDPOINT_DESCRIPTOR;

typedef struct {
  USB_CONFIG_DESCRIPTOR *ConfigDescriptor;
  void **InterfaceInfoTable;
} EFI_USB_CONFIG_INFO;

typedef struct {
  USB_INTERFACE_DESCRIPTOR *InterfaceDescriptor;
  USB_ENDPOINT_DESCRIPTOR **EndpointDescriptorTable;
} EFI_USB_INTERFACE_INFO;

typedef struct {
  USB_DEVICE_DESCRIPTOR *DeviceDescriptor;
  EFI_USB_CONFIG_INFO **ConfigInfoTable;
} EFI_USB_DEVICE_INFO;

typedef struct EFI_USBFN_IO_PROTOCOL EFI_USBFN_IO_PROTOCOL;
struct EFI_USBFN_IO_PROTOCOL {
  uint32_t Revision;
  EFI_STATUS (EFIAPI *DetectPort)(EFI_USBFN_IO_PROTOCOL *This, EFI_USBFN_PORT_TYPE *PortType);
  EFI_STATUS (EFIAPI *ConfigureEnableEndpoints)(EFI_USBFN_IO_PROTOCOL *This, EFI_USB_DEVICE_INFO *DeviceInfo);
  EFI_STATUS (EFIAPI *GetEndpointMaxPacketSize)(EFI_USBFN_IO_PROTOCOL *This, EFI_USB_ENDPOINT_TYPE EndpointType, EFI_USB_BUS_SPEED BusSpeed, UINTN *MaxPacketSize);
  EFI_STATUS (EFIAPI *GetDeviceInfo)(EFI_USBFN_IO_PROTOCOL *This, uint32_t Id, UINTN *BufferSize, void *Buffer);
  EFI_STATUS (EFIAPI *GetVendorIdProductId)(EFI_USBFN_IO_PROTOCOL *This, uint16_t *Vid, uint16_t *Pid);
  EFI_STATUS (EFIAPI *AbortTransfer)(EFI_USBFN_IO_PROTOCOL *This, uint8_t EndpointIndex, EFI_USBFN_ENDPOINT_DIRECTION Direction);
  EFI_STATUS (EFIAPI *GetEndpointStallState)(EFI_USBFN_IO_PROTOCOL *This, uint8_t EndpointIndex, EFI_USBFN_ENDPOINT_DIRECTION Direction, BOOLEAN *State);
  EFI_STATUS (EFIAPI *SetEndpointStallState)(EFI_USBFN_IO_PROTOCOL *This, uint8_t EndpointIndex, EFI_USBFN_ENDPOINT_DIRECTION Direction, BOOLEAN State);
  EFI_STATUS (EFIAPI *EventHandler)(EFI_USBFN_IO_PROTOCOL *This, EFI_USBFN_MESSAGE *Message, UINTN *PayloadSize, void *Payload);
  EFI_STATUS (EFIAPI *Transfer)(EFI_USBFN_IO_PROTOCOL *This, uint8_t EndpointIndex, EFI_USBFN_ENDPOINT_DIRECTION Direction, UINTN *BufferSize, void *Buffer);
  EFI_STATUS (EFIAPI *GetMaxTransferSize)(EFI_USBFN_IO_PROTOCOL *This, UINTN *MaxTransferSize);
  EFI_STATUS (EFIAPI *AllocateTransferBuffer)(EFI_USBFN_IO_PROTOCOL *This, UINTN Size, void **Buffer);
  EFI_STATUS (EFIAPI *FreeTransferBuffer)(EFI_USBFN_IO_PROTOCOL *This, void *Buffer);
  EFI_STATUS (EFIAPI *StartController)(EFI_USBFN_IO_PROTOCOL *This);
  EFI_STATUS (EFIAPI *StopController)(EFI_USBFN_IO_PROTOCOL *This);
};

extern EFI_GUID gEfiUsbFnIoProtocolGuid;

#endif
