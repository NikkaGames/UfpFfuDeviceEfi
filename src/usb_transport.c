#include "usb_transport.h"
#include "ufp_base.h"

#define USB_DESC_TYPE_DEVICE 1
#define USB_DESC_TYPE_CONFIG 2
#define USB_DESC_TYPE_STRING 3
#define USB_DESC_TYPE_BOS 15

#define USB_REQ_GET_STATUS 0
#define USB_REQ_CLEAR_FEATURE 1
#define USB_REQ_SET_FEATURE 3
#define USB_REQ_SET_ADDRESS 5
#define USB_REQ_GET_DESCRIPTOR 6
#define USB_REQ_GET_CONFIGURATION 8
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_GET_INTERFACE 10
#define USB_REQ_SET_INTERFACE 11

#define USB_VENDOR_CODE_MS 0xaa

static USB_DEVICE_DESCRIPTOR g_device = {
  18, USB_DESC_TYPE_DEVICE, 0x0200, 0xff, 0, 0, 64,
  0x045e, 0x062a, 0x0100, 1, 2, 3, 1
};

static USB_CONFIG_DESCRIPTOR g_config = {
  9, USB_DESC_TYPE_CONFIG, 32, 1, 1, 0, 0x80, 250
};

static USB_INTERFACE_DESCRIPTOR g_interface = {
  9, 4, 0, 0, 2, 0xff, 0x42, 0x01, 0
};

static USB_ENDPOINT_DESCRIPTOR g_ep_out = {
  7, 5, 0x01, 2, 512, 0
};

static USB_ENDPOINT_DESCRIPTOR g_ep_in = {
  7, 5, 0x81, 2, 512, 0
};

static USB_ENDPOINT_DESCRIPTOR *g_eps[] = { &g_ep_out, &g_ep_in, 0 };
static EFI_USB_INTERFACE_INFO g_if_info = { &g_interface, g_eps };
static void *g_if_table[] = { &g_if_info, 0 };
static EFI_USB_CONFIG_INFO g_cfg_info = { &g_config, g_if_table };
static EFI_USB_CONFIG_INFO *g_cfg_table[] = { &g_cfg_info, 0 };
static EFI_USB_DEVICE_INFO g_dev_info = { &g_device, g_cfg_table };

static const uint8_t g_bos_desc[] = {
  0x05, 0x0f, 0x16, 0x00, 0x01,
  0x07, 0x10, 0x02, 0x02, 0x00, 0x00, 0x00,
  0x0a, 0x10, 0x03, 0x00, 0x0e, 0x00, 0x01, 0x0a, 0xff, 0x07
};

static const uint8_t g_ms_os_string[] = {
  18, 3,
  'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0,
  USB_VENDOR_CODE_MS, 0
};

static const uint8_t g_ms_compat_id[] = {
  0x28, 0x00, 0x00, 0x00, 0x00, 0x01, 0x04, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x01, 'W', 'I', 'N', 'U', 'S', 'B',
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

static uint8_t g_ms_ext_props[142];
static int g_ms_ext_props_ready;

static void put_le16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void put_utf16_ascii(uint8_t *p, const char *s, UINTN chars_with_nul) {
  UINTN i = 0;
  while (i < chars_with_nul) {
    uint8_t ch = s[i] ? (uint8_t)s[i] : 0;
    p[2 * i] = ch;
    p[2 * i + 1] = 0;
    if (!s[i]) {
      ++i;
      break;
    }
    ++i;
  }
  while (i < chars_with_nul) {
    p[2 * i] = 0;
    p[2 * i + 1] = 0;
    ++i;
  }
}

static const uint8_t *ms_ext_props(UINTN *len) {
  if (!g_ms_ext_props_ready) {
    const char *name = "DeviceInterfaceGUID";
    const char *guid = "{8FE6D4D7-49DD-41E7-9486-49AFC6BFE475}";
    memset(g_ms_ext_props, 0, sizeof(g_ms_ext_props));
    put_le32(g_ms_ext_props + 0, sizeof(g_ms_ext_props));
    put_le16(g_ms_ext_props + 4, 0x0100);
    put_le16(g_ms_ext_props + 6, 0x0005);
    put_le16(g_ms_ext_props + 8, 1);
    put_le32(g_ms_ext_props + 10, 132);
    put_le32(g_ms_ext_props + 14, 1);
    put_le16(g_ms_ext_props + 18, 40);
    put_utf16_ascii(g_ms_ext_props + 20, name, 20);
    put_le32(g_ms_ext_props + 60, 78);
    put_utf16_ascii(g_ms_ext_props + 64, guid, 39);
    g_ms_ext_props_ready = 1;
  }
  *len = sizeof(g_ms_ext_props);
  return g_ms_ext_props;
}

static UINTN make_string_desc(uint8_t *out, const char *s) {
  UINTN n = 0;
  while (s[n] && n < 60) {
    ++n;
  }
  out[0] = (uint8_t)(2 + n * 2);
  out[1] = USB_DESC_TYPE_STRING;
  for (UINTN i = 0; i < n; ++i) {
    out[2 + i * 2] = (uint8_t)s[i];
    out[3 + i * 2] = 0;
  }
  return out[0];
}

static UINTN make_config_desc(uint8_t *out) {
  memcpy(out, &g_config, 9);
  memcpy(out + 9, &g_interface, 9);
  memcpy(out + 18, &g_ep_out, 7);
  memcpy(out + 25, &g_ep_in, 7);
  return 32;
}

static EFI_STATUS usb_send_control(USB_TRANSPORT *usb, const void *data, UINTN len, UINTN req_len) {
  UINTN send_len = len < req_len ? len : req_len;
  return usb->io->Transfer(usb->io, 0, EfiUsbEndpointDirectionHostIn, &send_len, (void *)data);
}

static EFI_STATUS usb_status_in(USB_TRANSPORT *usb) {
  UINTN z = 0;
  return usb->io->Transfer(usb->io, 0, EfiUsbEndpointDirectionHostIn, &z, usb->tx);
}

static EFI_STATUS usb_status_out(USB_TRANSPORT *usb) {
  UINTN z = 0;
  return usb->io->Transfer(usb->io, 0, EfiUsbEndpointDirectionHostOut, &z, usb->rx);
}

static EFI_STATUS usb_stall_ep0(USB_TRANSPORT *usb, const USB_SETUP_PACKET *setup) {
  EFI_USBFN_ENDPOINT_DIRECTION dir = (setup->RequestType & 0x80) ? EfiUsbEndpointDirectionHostIn : EfiUsbEndpointDirectionHostOut;
  return usb->io->SetEndpointStallState(usb->io, 0, dir, 1);
}

static EFI_STATUS handle_setup(USB_TRANSPORT *usb, const USB_SETUP_PACKET *setup) {
  uint8_t dtype = (uint8_t)(setup->Value >> 8);
  uint8_t dindex = (uint8_t)setup->Value;
  uint8_t tmp[128];
  UINTN len;

  if ((setup->RequestType & 0x60) == 0 && setup->Request == USB_REQ_GET_DESCRIPTOR) {
    if (dtype == USB_DESC_TYPE_DEVICE) {
      return usb_send_control(usb, &g_device, 18, setup->Length);
    }
    if (dtype == USB_DESC_TYPE_CONFIG) {
      len = make_config_desc(tmp);
      return usb_send_control(usb, tmp, len, setup->Length);
    }
    if (dtype == USB_DESC_TYPE_BOS) {
      return usb_send_control(usb, g_bos_desc, sizeof(g_bos_desc), setup->Length);
    }
    if (dtype == USB_DESC_TYPE_STRING) {
      if (dindex == 0) {
        static const uint8_t lang[] = {4, 3, 0x09, 0x04};
        return usb_send_control(usb, lang, sizeof(lang), setup->Length);
      }
      if (dindex == 1) {
        len = make_string_desc(tmp, "Microsoft");
        return usb_send_control(usb, tmp, len, setup->Length);
      }
      if (dindex == 2) {
        len = make_string_desc(tmp, "UFP FFU Flash");
        return usb_send_control(usb, tmp, len, setup->Length);
      }
      if (dindex == 3) {
        len = make_string_desc(tmp, "0001");
        return usb_send_control(usb, tmp, len, setup->Length);
      }
      if (dindex == 0xee) {
        return usb_send_control(usb, g_ms_os_string, sizeof(g_ms_os_string), setup->Length);
      }
    }
    return usb_stall_ep0(usb, setup);
  }

  if ((setup->RequestType & 0x60) == 0x40 && setup->Request == USB_VENDOR_CODE_MS) {
    if (setup->Index == 4) {
      return usb_send_control(usb, g_ms_compat_id, sizeof(g_ms_compat_id), setup->Length);
    }
    if (setup->Index == 5) {
      const uint8_t *props = ms_ext_props(&len);
      return usb_send_control(usb, props, len, setup->Length);
    }
    return usb_stall_ep0(usb, setup);
  }

  if ((setup->RequestType & 0x60) == 0) {
    switch (setup->Request) {
      case USB_REQ_SET_CONFIGURATION:
        usb->configured = setup->Value ? 1U : 0U;
        return usb_status_in(usb);
      case USB_REQ_GET_CONFIGURATION:
        tmp[0] = (uint8_t)(usb->configured ? 1 : 0);
        return usb_send_control(usb, tmp, 1, setup->Length);
      case USB_REQ_GET_STATUS:
        tmp[0] = 0;
        tmp[1] = 0;
        return usb_send_control(usb, tmp, 2, setup->Length);
      case USB_REQ_SET_ADDRESS:
      case USB_REQ_CLEAR_FEATURE:
      case USB_REQ_SET_FEATURE:
      case USB_REQ_SET_INTERFACE:
        return usb_status_in(usb);
      case USB_REQ_GET_INTERFACE:
        tmp[0] = 0;
        return usb_send_control(usb, tmp, 1, setup->Length);
      default:
        break;
    }
  }
  if ((setup->RequestType & 0x80) == 0) {
    return usb_status_out(usb);
  }
  return usb_stall_ep0(usb, setup);
}

EFI_STATUS usb_transport_open(USB_TRANSPORT *usb) {
  EFI_STATUS st;
  EFI_LOCATE_PROTOCOL locate;
  EFI_USBFN_PORT_TYPE port = EfiUsbUnknownPort;
  if (!usb) {
    return EFI_INVALID_PARAMETER;
  }
  memset(usb, 0, sizeof(*usb));
  locate = (EFI_LOCATE_PROTOCOL)uefi_bs()->LocateProtocol;
  if (!locate) {
    return EFI_UNSUPPORTED;
  }
  st = locate(&gEfiUsbFnIoProtocolGuid, 0, (void **)&usb->io);
  if (EFI_ERROR(st) || !usb->io) {
    return st;
  }
  if (usb->io->DetectPort) {
    usb->io->DetectPort(usb->io, &port);
  }
  usb->max_transfer = USBFN_MAX_XFER;
  if (usb->io->GetMaxTransferSize) {
    UINTN reported = 0;
    if (!EFI_ERROR(usb->io->GetMaxTransferSize(usb->io, &reported)) && reported && reported < usb->max_transfer) {
      usb->max_transfer = reported;
    }
  }
  if (usb->io->AllocateTransferBuffer) {
    st = usb->io->AllocateTransferBuffer(usb->io, usb->max_transfer, (void **)&usb->rx);
    if (EFI_ERROR(st)) {
      return st;
    }
    st = usb->io->AllocateTransferBuffer(usb->io, usb->max_transfer, (void **)&usb->tx);
    if (EFI_ERROR(st)) {
      usb_transport_close(usb);
      return st;
    }
  } else {
    usb->rx = (uint8_t *)uefi_alloc(usb->max_transfer);
    usb->tx = (uint8_t *)uefi_alloc(usb->max_transfer);
    if (!usb->rx || !usb->tx) {
      usb_transport_close(usb);
      return EFI_OUT_OF_RESOURCES;
    }
  }
  if (usb->io->StartController) {
    st = usb->io->StartController(usb->io);
    if (EFI_ERROR(st)) {
      usb_transport_close(usb);
      return st;
    }
  }
  st = usb->io->ConfigureEnableEndpoints(usb->io, &g_dev_info);
  if (EFI_ERROR(st)) {
    usb_transport_close(usb);
    return st;
  }
  con_puta("USB function transport active. Waiting for host enumeration."); con_crlf();
  return EFI_SUCCESS;
}

void usb_transport_close(USB_TRANSPORT *usb) {
  if (!usb) {
    return;
  }
  if (usb->io) {
    if (usb->io->AbortTransfer) {
      usb->io->AbortTransfer(usb->io, USBFN_BULK_ENDPOINT_INDEX, EfiUsbEndpointDirectionHostOut);
      usb->io->AbortTransfer(usb->io, USBFN_BULK_ENDPOINT_INDEX, EfiUsbEndpointDirectionHostIn);
    }
    if (usb->io->StopController) {
      usb->io->StopController(usb->io);
    }
    if (usb->io->FreeTransferBuffer) {
      if (usb->rx) {
        usb->io->FreeTransferBuffer(usb->io, usb->rx);
      }
      if (usb->tx) {
        usb->io->FreeTransferBuffer(usb->io, usb->tx);
      }
    } else {
      uefi_free(usb->rx);
      uefi_free(usb->tx);
    }
  }
  memset(usb, 0, sizeof(*usb));
}

static EFI_STATUS pump_until(USB_TRANSPORT *usb, int want_rx, int want_tx, UINTN *bytes) {
  EFI_STATUS st;
  EFI_USBFN_MESSAGE msg;
  USB_SETUP_PACKET setup;
  USBFN_TRANSFER_RESULT result;
  UINTN payload_size;
  UINTN rx_size;

  if (want_rx) {
    rx_size = usb->max_transfer;
    st = usb->io->Transfer(usb->io, USBFN_BULK_ENDPOINT_INDEX, EfiUsbEndpointDirectionHostOut, &rx_size, usb->rx);
    if (EFI_ERROR(st)) {
      return st;
    }
  }
  while (1) {
    msg = EfiUsbMsgNone;
    payload_size = sizeof(result);
    memset(&result, 0, sizeof(result));
    st = usb->io->EventHandler(usb->io, &msg, &payload_size, &result);
    if (EFI_ERROR(st)) {
      return st;
    }
    if (msg == EfiUsbMsgSetupPacket) {
      payload_size = sizeof(setup);
      memcpy(&setup, &result, sizeof(setup));
      handle_setup(usb, &setup);
    } else if (msg == EfiUsbMsgEndpointStatusChangedRx && want_rx) {
      if (result.EndpointIndex == USBFN_BULK_ENDPOINT_INDEX) {
        if (result.TransferStatus == UsbTransferStatusComplete) {
          *bytes = result.BytesTransferred;
          return EFI_SUCCESS;
        }
        return EFI_ABORTED;
      }
    } else if (msg == EfiUsbMsgEndpointStatusChangedTx && want_tx) {
      if (result.EndpointIndex == USBFN_BULK_ENDPOINT_INDEX) {
        if (result.TransferStatus == UsbTransferStatusComplete) {
          return EFI_SUCCESS;
        }
        return EFI_ABORTED;
      }
    } else if (msg == EfiUsbMsgBusEventSpeed) {
      usb->speed = (EFI_USB_BUS_SPEED)result.BytesTransferred;
    } else if (msg == EfiUsbMsgBusEventDetach || msg == EfiUsbMsgBusEventReset) {
      usb->configured = 0;
    }
  }
}

EFI_STATUS usb_transport_recv(USB_TRANSPORT *usb, uint8_t **data, UINTN *len) {
  EFI_STATUS st;
  UINTN got = 0;
  if (!usb || !data || !len) {
    return EFI_INVALID_PARAMETER;
  }
  st = pump_until(usb, 1, 0, &got);
  if (EFI_ERROR(st)) {
    return st;
  }
  *data = usb->rx;
  *len = got;
  return EFI_SUCCESS;
}

EFI_STATUS usb_transport_send(USB_TRANSPORT *usb, const uint8_t *data, UINTN len) {
  EFI_STATUS st;
  UINTN send_len = len;
  UINTN ignored = 0;
  if (!usb || !data || len > usb->max_transfer) {
    return EFI_INVALID_PARAMETER;
  }
  memcpy(usb->tx, data, len);
  st = usb->io->Transfer(usb->io, USBFN_BULK_ENDPOINT_INDEX, EfiUsbEndpointDirectionHostIn, &send_len, usb->tx);
  if (EFI_ERROR(st)) {
    return st;
  }
  return pump_until(usb, 0, 1, &ignored);
}
