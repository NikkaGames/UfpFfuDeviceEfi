#ifndef USB_TRANSPORT_H
#define USB_TRANSPORT_H

#include "usbfn.h"

typedef struct {
  EFI_USBFN_IO_PROTOCOL *io;
  EFI_USB_BUS_SPEED speed;
  uint32_t configured;
  UINTN max_transfer;
  uint8_t *rx;
  uint8_t *tx;
} USB_TRANSPORT;

EFI_STATUS usb_transport_open(USB_TRANSPORT *usb);
void usb_transport_close(USB_TRANSPORT *usb);
EFI_STATUS usb_transport_recv(USB_TRANSPORT *usb, uint8_t **data, UINTN *len);
EFI_STATUS usb_transport_send(USB_TRANSPORT *usb, const uint8_t *data, UINTN len);

#endif
