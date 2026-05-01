#include "ffu.h"
#include "ufp_base.h"
#include "ufp_proto.h"
#include "usb_transport.h"

typedef struct {
  int help;
  int list_block;
  int packets;
  int flash;
  int usb_ffu;
  int yes;
  int target;
  const CHAR16 *path;
  UINTN path_len;
} OPTIONS;

static uint8_t g_ffu_options[500];
static uint32_t g_mode_data;

static int ch_is_space(CHAR16 ch) {
  return ch == (CHAR16)' ' || ch == (CHAR16)'\t' || ch == (CHAR16)'\r' || ch == (CHAR16)'\n';
}

static CHAR16 ch_lower(CHAR16 ch) {
  if (ch >= (CHAR16)'A' && ch <= (CHAR16)'Z') {
    return (CHAR16)(ch + 32);
  }
  return ch;
}

static int token_is_efi_name(const CHAR16 *t, UINTN len) {
  if (len < 4) {
    return 0;
  }
  return t[len - 4] == (CHAR16)'.' &&
         ch_lower(t[len - 3]) == (CHAR16)'e' &&
         ch_lower(t[len - 2]) == (CHAR16)'f' &&
         ch_lower(t[len - 1]) == (CHAR16)'i';
}

static int token_eq_ascii(const CHAR16 *t, UINTN len, const char *s) {
  UINTN i = 0;
  while (s[i]) {
    if (i >= len || t[i] != (CHAR16)(uint8_t)s[i]) {
      return 0;
    }
    ++i;
  }
  return i == len;
}

static int token_starts_ascii(const CHAR16 *t, UINTN len, const char *s, UINTN *prefix_len) {
  UINTN i = 0;
  while (s[i]) {
    if (i >= len || t[i] != (CHAR16)(uint8_t)s[i]) {
      return 0;
    }
    ++i;
  }
  if (prefix_len) {
    *prefix_len = i;
  }
  return 1;
}

static int parse_uint_token(const CHAR16 *t, UINTN len, int *out) {
  uint32_t value = 0;
  if (!len) {
    return 0;
  }
  for (UINTN i = 0; i < len; ++i) {
    if (t[i] < (CHAR16)'0' || t[i] > (CHAR16)'9') {
      return 0;
    }
    value = value * 10U + (uint32_t)(t[i] - (CHAR16)'0');
  }
  *out = (int)value;
  return 1;
}

static void usage(void) {
  con_puta("UfpFfu ARM64 UEFI app"); con_crlf();
  con_puta("Usage: UfpFfu.efi --info <ffu>"); con_crlf();
  con_puta("       UfpFfu.efi --packets <ffu>"); con_crlf();
  con_puta("       UfpFfu.efi --list-block"); con_crlf();
  con_puta("       UfpFfu.efi --flash <ffu> --target=N [--yes]"); con_crlf();
  con_puta("       UfpFfu.efi --usb-ffu --target=N --yes"); con_crlf();
  con_puta("Without --yes, --flash only validates the FFU write plan against Block I/O."); con_crlf();
}

static EFI_STATUS parse_options(EFI_HANDLE image, OPTIONS *opts) {
  EFI_STATUS st;
  EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
  const CHAR16 *cmd;
  UINTN len;
  UINTN pos = 0;
  UINTN token_index = 0;
  int expect_target = 0;

  memset(opts, 0, sizeof(*opts));
  opts->target = -1;

  st = uefi_bs()->HandleProtocol(image, &gEfiLoadedImageProtocolGuid, (void **)&loaded);
  if (EFI_ERROR(st) || !loaded || !loaded->LoadOptions || !loaded->LoadOptionsSize) {
    opts->help = 1;
    return EFI_SUCCESS;
  }

  cmd = (const CHAR16 *)loaded->LoadOptions;
  len = loaded->LoadOptionsSize / sizeof(CHAR16);
  while (pos < len) {
    const CHAR16 *tok;
    UINTN tok_len;
    int quoted = 0;
    while (pos < len && ch_is_space(cmd[pos])) {
      ++pos;
    }
    if (pos >= len || cmd[pos] == 0) {
      break;
    }
    if (cmd[pos] == (CHAR16)'"') {
      quoted = 1;
      ++pos;
    }
    tok = &cmd[pos];
    while (pos < len && cmd[pos] != 0 && ((quoted && cmd[pos] != (CHAR16)'"') || (!quoted && !ch_is_space(cmd[pos])))) {
      ++pos;
    }
    tok_len = (UINTN)(&cmd[pos] - tok);
    if (quoted && pos < len && cmd[pos] == (CHAR16)'"') {
      ++pos;
    }
    if (!tok_len) {
      continue;
    }
    if (token_index == 0 && token_is_efi_name(tok, tok_len)) {
      ++token_index;
      continue;
    }
    if (expect_target) {
      if (!parse_uint_token(tok, tok_len, &opts->target)) {
        return EFI_INVALID_PARAMETER;
      }
      expect_target = 0;
      ++token_index;
      continue;
    }
    if (token_eq_ascii(tok, tok_len, "--help") || token_eq_ascii(tok, tok_len, "-h")) {
      opts->help = 1;
    } else if (token_eq_ascii(tok, tok_len, "--list-block")) {
      opts->list_block = 1;
    } else if (token_eq_ascii(tok, tok_len, "--packets")) {
      opts->packets = 1;
    } else if (token_eq_ascii(tok, tok_len, "--info")) {
      opts->packets = 0;
      opts->flash = 0;
    } else if (token_eq_ascii(tok, tok_len, "--flash")) {
      opts->flash = 1;
    } else if (token_eq_ascii(tok, tok_len, "--usb-ffu") || token_eq_ascii(tok, tok_len, "--usb")) {
      opts->usb_ffu = 1;
    } else if (token_eq_ascii(tok, tok_len, "--yes")) {
      opts->yes = 1;
    } else if (token_eq_ascii(tok, tok_len, "--target")) {
      expect_target = 1;
    } else {
      UINTN prefix_len = 0;
      if (token_starts_ascii(tok, tok_len, "--target=", &prefix_len)) {
        if (!parse_uint_token(tok + prefix_len, tok_len - prefix_len, &opts->target)) {
          return EFI_INVALID_PARAMETER;
        }
      } else if (!opts->path) {
        opts->path = tok;
        opts->path_len = tok_len;
      }
    }
    ++token_index;
  }
  if (expect_target) {
    return EFI_INVALID_PARAMETER;
  }
  return EFI_SUCCESS;
}

static EFI_STATUS list_block_devices(void) {
  EFI_STATUS st;
  EFI_HANDLE *handles = 0;
  UINTN count = 0;
  st = uefi_bs()->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, 0, &count, &handles);
  if (EFI_ERROR(st)) {
    con_puta("LocateHandleBuffer(BlockIo) failed: "); con_put_status(st); con_crlf();
    return st;
  }
  con_puta("Block I/O handles: "); con_put_dec(count); con_crlf();
  for (UINTN i = 0; i < count; ++i) {
    EFI_BLOCK_IO_PROTOCOL *bio = 0;
    st = uefi_bs()->HandleProtocol(handles[i], &gEfiBlockIoProtocolGuid, (void **)&bio);
    if (EFI_ERROR(st) || !bio || !bio->Media) {
      continue;
    }
    con_puta("["); con_put_dec(i); con_puta("] ");
    con_puta(bio->Media->MediaPresent ? "present " : "no-media ");
    con_puta(bio->Media->ReadOnly ? "ro " : "rw ");
    con_puta(bio->Media->LogicalPartition ? "partition " : "whole ");
    con_puta("block="); con_put_dec(bio->Media->BlockSize);
    con_puta(" last_lba="); con_put_dec(bio->Media->LastBlock);
    con_crlf();
  }
  uefi_free(handles);
  return EFI_SUCCESS;
}

static EFI_STATUS get_block_device(int target, EFI_BLOCK_IO_PROTOCOL **out) {
  EFI_STATUS st;
  EFI_HANDLE *handles = 0;
  UINTN count = 0;
  if (target < 0 || !out) {
    return EFI_INVALID_PARAMETER;
  }
  st = uefi_bs()->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, 0, &count, &handles);
  if (EFI_ERROR(st)) {
    return st;
  }
  if ((UINTN)target >= count) {
    uefi_free(handles);
    return EFI_NOT_FOUND;
  }
  st = uefi_bs()->HandleProtocol(handles[target], &gEfiBlockIoProtocolGuid, (void **)out);
  uefi_free(handles);
  return st;
}

static uint16_t status_from_efi(EFI_STATUS st) {
  if (!EFI_ERROR(st)) {
    return 0;
  }
  return (uint16_t)(st & 0xffffU);
}

static EFI_STATUS send_fs_short(USB_TRANSPORT *usb, uint16_t status, uint32_t specifier) {
  uint8_t resp[16];
  memset(resp, 0, sizeof(resp));
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'X'; resp[4] = 'F'; resp[5] = 'S';
  wr_be16(resp + 6, status);
  wr_be32(resp + 8, specifier);
  return usb_transport_send(usb, resp, sizeof(resp));
}

static EFI_STATUS send_unknown_response(USB_TRANSPORT *usb) {
  static const uint8_t resp[] = { 'N', 'O', 'K', 'U' };
  return usb_transport_send(usb, resp, sizeof(resp));
}

static EFI_STATUS send_cb_response(USB_TRANSPORT *usb, uint16_t status, uint32_t specifier) {
  uint8_t resp[12];
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'X'; resp[4] = 'C'; resp[5] = 'B';
  wr_be16(resp + 6, status);
  wr_be32(resp + 8, specifier);
  return usb_transport_send(usb, resp, sizeof(resp));
}

static EFI_STATUS send_ext_status8(USB_TRANSPORT *usb, uint8_t app, uint8_t ext, uint16_t status) {
  uint8_t resp[8];
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'X'; resp[4] = app; resp[5] = ext;
  wr_be16(resp + 6, status);
  return usb_transport_send(usb, resp, sizeof(resp));
}

static EFI_STATUS send_ext_blob_response(USB_TRANSPORT *usb, uint8_t app, uint8_t ext, uint16_t status, const uint8_t *data, UINTN data_len) {
  uint8_t stack_resp[64];
  uint8_t *resp = stack_resp;
  EFI_STATUS st;
  UINTN total = 12U + data_len;
  if (total > sizeof(stack_resp)) {
    resp = (uint8_t *)uefi_alloc(total);
    if (!resp) {
      return EFI_OUT_OF_RESOURCES;
    }
  }
  memset(resp, 0, total);
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'X'; resp[4] = app; resp[5] = ext;
  wr_be16(resp + 6, status);
  wr_be32(resp + 8, (uint32_t)data_len);
  if (data_len) {
    memcpy(resp + 12, data, data_len);
  }
  st = usb_transport_send(usb, resp, total);
  if (resp != stack_resp) {
    uefi_free(resp);
  }
  return st;
}

static EFI_STATUS send_fr_response(USB_TRANSPORT *usb, uint32_t param, uint16_t status, const uint8_t *data, UINTN data_len) {
  uint8_t stack_resp[64];
  uint8_t *resp = stack_resp;
  EFI_STATUS st;
  UINTN total = 17U + data_len;
  if (total > sizeof(stack_resp)) {
    resp = (uint8_t *)uefi_alloc(total);
    if (!resp) {
      return EFI_OUT_OF_RESOURCES;
    }
  }
  memset(resp, 0, total);
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'X'; resp[4] = 'F'; resp[5] = 'R';
  wr_be16(resp + 6, status);
  resp[8] = 0;
  wr_be32(resp + 9, param);
  wr_be32(resp + 13, (uint32_t)data_len);
  if (data_len) {
    memcpy(resp + 17, data, data_len);
  }
  st = usb_transport_send(usb, resp, total);
  if (resp != stack_resp) {
    uefi_free(resp);
  }
  return st;
}

static EFI_STATUS send_fw_response(USB_TRANSPORT *usb, uint32_t param, uint16_t status) {
  uint8_t resp[17];
  memset(resp, 0, sizeof(resp));
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'X'; resp[4] = 'F'; resp[5] = 'W';
  wr_be16(resp + 6, status);
  resp[8] = 0;
  wr_be32(resp + 9, param);
  return usb_transport_send(usb, resp, sizeof(resp));
}

static void wr_be64_local(uint8_t *p, uint64_t v) {
  wr_be32(p, (uint32_t)(v >> 32));
  wr_be32(p + 4, (uint32_t)v);
}

static UINTN info_add_block(uint8_t *resp, UINTN off, uint8_t id, const uint8_t *data, UINTN data_len) {
  resp[off + 0] = id;
  resp[off + 1] = (uint8_t)(data_len >> 8);
  resp[off + 2] = (uint8_t)data_len;
  memcpy(resp + off + 3, data, data_len);
  resp[10]++;
  return off + 3 + data_len;
}

static UINTN info_add_u32(uint8_t *resp, UINTN off, uint8_t id, uint32_t value) {
  uint8_t data[4];
  wr_be32(data, value);
  return info_add_block(resp, off, id, data, sizeof(data));
}

static EFI_STATUS send_v_info_response(USB_TRANSPORT *usb, EFI_BLOCK_IO_PROTOCOL *bio) {
  uint8_t resp[160];
  uint8_t data[12];
  UINTN off = 11;
  uint32_t write_size = UFP_MAX_PAYLOAD_DATA;
  uint8_t speed = (uint8_t)(usb->speed ? usb->speed : EfiUsbBusSpeedHigh);

  if (usb->max_transfer > 32 && usb->max_transfer - 32 < write_size) {
    write_size = (uint32_t)(usb->max_transfer - 32);
  }

  memset(resp, 0, sizeof(resp));
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'V';
  resp[4] = 0;
  resp[5] = 2;
  resp[6] = 3;
  resp[7] = 3;
  resp[8] = 5;
  resp[9] = 4;
  resp[10] = 0;

  off = info_add_u32(resp, off, 1, (uint32_t)usb->max_transfer);
  off = info_add_u32(resp, off, 2, write_size);
  if (bio && bio->Media) {
    uint64_t sectors = bio->Media->LastBlock + 1U;
    off = info_add_u32(resp, off, 3, sectors > 0xffffffffULL ? 0xffffffffU : (uint32_t)sectors);
  }

  data[0] = 0;
  data[1] = 1;
  off = info_add_block(resp, off, 13, data, 2);

  data[0] = 3;
  data[1] = 0xff;
  data[2] = 0xff;
  data[3] = 0;
  data[4] = 0;
  data[5] = 0;
  data[6] = speed;
  data[7] = 0;
  data[8] = 0xff;
  off = info_add_block(resp, off, 15, data, 9);

  data[0] = 1;
  data[1] = 0;
  data[2] = 0x1f;
  data[3] = 0;
  data[4] = 0;
  off = info_add_block(resp, off, 16, data, 5);

  data[0] = 1;
  data[1] = 0;
  data[2] = 0;
  data[3] = 0;
  data[4] = 1;
  data[5] = 0;
  data[6] = 0;
  data[7] = 0;
  data[8] = 4;
  off = info_add_block(resp, off, 32, data, 9);

  data[0] = 1;
  off = info_add_block(resp, off, 37, data, 1);

  return usb_transport_send(usb, resp, off);
}

static EFI_STATUS handle_read_param(USB_TRANSPORT *usb, EFI_BLOCK_IO_PROTOCOL *bio, const uint8_t *msg, UINTN len) {
  uint8_t data[512];
  UINTN data_len = 0;
  uint16_t status = 0;
  uint32_t param;
  uint32_t value;
  uint8_t speed;
  uint8_t max_speed;

  if (len < 11) {
    return send_fr_response(usb, 0, 8, 0, 0);
  }
  param = rd_be32(msg + 7);
  memset(data, 0, sizeof(data));

  switch (param) {
    case 0x41545250U: /* ATRP: reset protection support/version. */
      data[0] = 0;
      data[1] = 0;
      data[2] = 0;
      data[3] = 0;
      data[4] = 1;
      data[5] = 0;
      data[6] = 0;
      data[7] = 0;
      data[8] = 1;
      data_len = 9;
      break;
    case 0x46414900U: /* FAI\0: UFP protocol/implementation version. */
      data[0] = 2;
      data[1] = 3;
      data[2] = 3;
      data[3] = 5;
      data[4] = 0;
      data[5] = 4;
      data_len = 6;
      break;
    case 0x44415300U: /* DAS\0: direct async support. */
      data[0] = 0;
      data[1] = 1;
      data_len = 2;
      break;
    case 0x41505054U: /* APPT: app type. */
      data[0] = 1;
      data_len = 1;
      break;
    case 0x4249544CU: /* BITL: BitLocker state. */
      status = 2;
      data[0] = 0;
      data_len = 1;
      break;
    case 0x424E464FU: { /* BNFO: build info string. */
      static const uint8_t build_info[] = "Date:- Time:- Info:-";
      memcpy(data, build_info, sizeof(build_info));
      data_len = sizeof(build_info);
      break;
    }
    case 0x4355464FU: /* CUFO: current UEFI boot option. */
      status = 2;
      data[0] = 0;
      data[1] = 0;
      data_len = 2;
      break;
    case 0x44455300U: /* DES\0: directory entries size. */
      status = 38;
      wr_be64_local(data, 0);
      data_len = 8;
      break;
    case 0x44504900U: /* DPI\0: platform id. */
    case 0x44505200U: /* DPR\0: device properties. */
    case 0x44544900U: /* DTI\0: device target info. */
      status = 2;
      break;
    case 0x44545350U: /* DTSP: data verify speed. */
      wr_be32(data, 0);
      data_len = 4;
      break;
    case 0x44554900U: /* DUI\0: device id. */
      status = 2;
      data_len = 16;
      break;
    case 0x454D4D54U: /* EMMT: eMMC self-test result. */
      status = 9;
      break;
    case 0x454D5300U: /* EMS\0: eMMC sector count. */
      if (bio && bio->Media) {
        uint64_t sectors = bio->Media->LastBlock + 1U;
        wr_be32(data, sectors > 0xffffffffULL ? 0xffffffffU : (uint32_t)sectors);
      } else {
        status = 2;
      }
      data_len = 4;
      break;
    case 0x42460000U: /* BF\0\0: boot-flashing flag. */
      data[0] = 0;
      data_len = 1;
      break;
    case 0x464F0000U: /* FO\0\0: FFU configuration options. */
      memcpy(data, g_ffu_options, sizeof(g_ffu_options));
      data_len = 500;
      break;
    case 0x46530000U: /* FS\0\0: flashing status. */
      wr_be32(data, 3);
      data_len = 4;
      break;
    case 0x465A0000U: /* FZ\0\0: file size. */
      status = 38;
      wr_be64_local(data, 0);
      data_len = 8;
      break;
    case 0x47534253U: /* GSBS: secure boot status. */
      data[0] = 0;
      data_len = 1;
      break;
    case 0x47554656U: /* GUFV: get UEFI variable value. */
    case 0x47555653U: /* GUVS: get UEFI variable size. */
      status = 10;
      break;
    case 0x4C474D52U: /* LGMR: largest memory region. */
      status = 2;
      wr_be64_local(data, 0);
      data_len = 8;
      break;
    case 0x4C5A0000U: /* LZ\0\0: log size. */
      if (len >= 16 && msg[15] != 1 && msg[15] != 2) {
        status = 8;
      }
      wr_be64_local(data, 0);
      data_len = 8;
      break;
    case 0x4D414300U: /* MAC\0: MAC address string. */
      status = 8;
      data_len = 1;
      break;
    case 0x4D4F4445U: /* MODE: transport/application mode. */
      if (len >= 16 && msg[15] != 0) {
        status = 11;
      }
      wr_be32(data, status ? 0 : g_mode_data);
      data_len = 4;
      break;
    case 0x53445300U: /* SDS\0: SD/memory-card sector count. */
      status = 10;
      wr_be32(data, 0);
      data_len = 4;
      break;
    case 0x53465049U: /* SFPI: supported secure-FFU protocol bitmap. */
      value = 0x1f;
      data[0] = 1;
      data[1] = (uint8_t)(value >> 8);
      data[2] = (uint8_t)value;
      data_len = 6;
      break;
    case 0x534D4244U: /* SMBD: SMBIOS data. */
      status = 2;
      wr_be32(data, 0);
      data_len = 4;
      break;
    case 0x534E0000U: /* SN\0\0: serial number GUID. */
      status = 2;
      data_len = 16;
      break;
    case 0x534F534DU: /* SOSM: system memory size. */
      status = 9;
      wr_be64_local(data, 0);
      data_len = 8;
      break;
    case 0x53530000U: /* SS\0\0: summarized security/SFFU/USB capabilities. */
      data[0] = 3;
      data[1] = 0xff;
      data[2] = 0xff;
      data[3] = 0;
      data[4] = 0;
      data[5] = 0;
      data[6] = (uint8_t)(usb->speed ? usb->speed : EfiUsbBusSpeedHigh);
      data[7] = 0;
      data_len = 8;
      break;
    case 0x54454C53U: /* TELS: telemetry log size. */
      wr_be32(data, 0);
      data_len = 4;
      break;
    case 0x54530000U: /* TS\0\0: transport receive transfer size. */
      value = (uint32_t)usb->max_transfer;
      wr_be32(data, value);
      data_len = 4;
      break;
    case 0x55424600U: /* UBF\0: current boot option enumeration. */
    case 0x5545424FU: /* UEBO: enumerate UEFI boot options. */
      status = 2;
      wr_be32(data, 0);
      wr_be32(data + 4, 0);
      data_len = 8;
      break;
    case 0x554B4944U: /* UKID: unlock id. */
    case 0x554B5446U: /* UKTF: unlock-token file list. */
      status = 10;
      break;
    case 0x55534253U: /* USBS: current and maximum USB bus speed. */
      speed = (uint8_t)(usb->speed ? usb->speed : EfiUsbBusSpeedHigh);
      max_speed = speed > EfiUsbBusSpeedHigh ? speed : EfiUsbBusSpeedHigh;
      data[0] = speed;
      data[1] = max_speed;
      data_len = 2;
      break;
    case 0x706D0000U: /* pm\0\0: processor manufacturer. */
      status = 2;
      data_len = 1;
      break;
    case 0x57425300U: /* WBS\0: safe write-buffer payload size. */
      value = UFP_MAX_PAYLOAD_DATA;
      if (usb->max_transfer > 32 && usb->max_transfer - 32 < value) {
        value = (uint32_t)(usb->max_transfer - 32);
      }
      wr_be32(data, value);
      data_len = 4;
      break;
    default:
      status = 11;
      break;
  }

  return send_fr_response(usb, param, status, data, data_len);
}

static EFI_STATUS handle_write_param(USB_TRANSPORT *usb, const uint8_t *msg, UINTN len) {
  uint32_t param;
  uint32_t data_len = 0;
  if (len < 11) {
    return send_fw_response(usb, 0, 8);
  }
  param = rd_be32(msg + 7);
  if (len >= 15) {
    data_len = rd_be32(msg + 11);
  }
  switch (param) {
    case 0x424F434CU: /* BOCL: boot-option optional data. */
    case 0x424F4600U: /* BOF\0: move boot option first. */
    case 0x424F4C00U: /* BOL\0: move boot option last. */
    case 0x4F425500U: /* OBU\0: one-time boot option. */
    case 0x53554656U: /* SUFV: set UEFI variable. */
      return send_fw_response(usb, param, 4);
    case 0x464F0000U: /* FO\0\0: FFU configuration options. */
      if (len < 515 || data_len != 500) {
        return send_fw_response(usb, param, 8);
      }
      memcpy(g_ffu_options, msg + 15, sizeof(g_ffu_options));
      return send_fw_response(usb, param, 0);
    case 0x4C490000U: { /* LI\0\0: log identifier. */
      UINTN actual = 0;
      if (len < 15 || data_len > 200 || len < 15U + data_len) {
        return send_fw_response(usb, param, 8);
      }
      while (actual < data_len && msg[15 + actual] != 0) {
        ++actual;
      }
      if (actual == data_len || actual + 1U != data_len) {
        return send_fw_response(usb, param, 8);
      }
      return send_fw_response(usb, param, 0);
    }
    case 0x4D4F4445U: /* MODE: transport/application mode. */
      if (len < 20 || msg[15] != 0) {
        return send_fw_response(usb, param, 11);
      }
      g_mode_data = rd_be32(msg + 16) ? 1U : 0U;
      return send_fw_response(usb, param, 0);
    default:
      return send_fw_response(usb, param, 11);
  }
}

static EFI_STATUS send_legacy_flash_response(USB_TRANSPORT *usb, uint16_t status, uint32_t count, uint32_t specifier) {
  uint8_t resp[18];
  memset(resp, 0, sizeof(resp));
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'F';
  wr_be16(resp + 5, status);
  wr_be32(resp + 10, count);
  wr_be32(resp + 14, specifier);
  return usb_transport_send(usb, resp, sizeof(resp));
}

static EFI_STATUS handle_legacy_flash(USB_TRANSPORT *usb, EFI_BLOCK_IO_PROTOCOL *bio, const uint8_t *msg, UINTN len, int commit) {
  EFI_STATUS st;
  uint16_t status = 0;
  uint32_t specifier = 0;
  uint32_t start = 0;
  uint32_t count = 0;
  uint64_t byte_count = 0;

  if (len < 26) {
    return send_legacy_flash_response(usb, 8, 0, 0);
  }
  start = rd_be32(msg + 11);
  count = rd_be32(msg + 15);
  byte_count = (uint64_t)count * (uint64_t)bio->Media->BlockSize;
  if (count && byte_count / count != bio->Media->BlockSize) {
    status = 8;
    specifier = count;
  } else if (msg[25] != 1) {
    if (len < 64U || (uint64_t)(len - 64U) < byte_count) {
      status = 8;
      specifier = (uint32_t)byte_count;
    } else if (count && (uint64_t)start + count - 1U > bio->Media->LastBlock) {
      status = 8;
      specifier = count;
    } else if (commit && byte_count) {
      st = bio->WriteBlocks(bio, bio->Media->MediaId, start, (UINTN)byte_count, (void *)(msg + 64));
      if (EFI_ERROR(st)) {
        status = status_from_efi(st);
        specifier = (uint32_t)st;
      }
    }
  }
  return send_legacy_flash_response(usb, status, count, specifier);
}

static EFI_STATUS handle_get_gpt(USB_TRANSPORT *usb, EFI_BLOCK_IO_PROTOCOL *bio) {
  EFI_STATUS st;
  uint32_t block_size = bio->Media->BlockSize;
  uint32_t block_count = 0;
  uint32_t payload_len;
  uint16_t status = 0;
  uint8_t *resp;

  if (block_size == 512) {
    block_count = 34;
  } else if (block_size == 4096) {
    block_count = 6;
  } else {
    status = 8;
  }
  payload_len = block_count * block_size;
  resp = (uint8_t *)uefi_alloc((UINTN)payload_len + 8U);
  if (!resp) {
    return EFI_OUT_OF_RESOURCES;
  }
  memset(resp, 0, (UINTN)payload_len + 8U);
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'T';
  if (!status && payload_len) {
    st = bio->ReadBlocks(bio, bio->Media->MediaId, 0, payload_len, resp + 8);
    if (EFI_ERROR(st)) {
      status = 2;
      memset(resp + 8, 0, payload_len);
    }
  }
  wr_be16(resp + 6, status);
  st = usb_transport_send(usb, resp, (UINTN)payload_len + 8U);
  uefi_free(resp);
  return st;
}

static uint32_t efi_status_to_wire32(EFI_STATUS st) {
  uint32_t low = (uint32_t)(st & 0xffffffffU);
  if (EFI_ERROR(st)) {
    return 0x80000000U | low;
  }
  return low;
}

static EFI_STATUS handle_unlock(USB_TRANSPORT *usb, const uint8_t *msg, UINTN len) {
  uint8_t resp[14];
  uint32_t request_version = 0;
  uint32_t token_len = 0;
  uint16_t status = 0;
  EFI_STATUS op_status = EFI_SUCCESS;

  if (len >= 10) {
    request_version = rd_be32(msg + 6);
  }
  if (len >= 18) {
    token_len = rd_be32(msg + 14);
  }
  if (len < 18 || request_version != 2) {
    status = 5121;
    op_status = EFI_UNSUPPORTED;
  } else if ((uint64_t)token_len + 18U > (uint64_t)len) {
    status = 5122;
    op_status = EFI_INVALID_PARAMETER;
  }
  memset(resp, 0, sizeof(resp));
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'X'; resp[4] = 'F'; resp[5] = 'I';
  wr_be16(resp + 8, status);
  wr_be32(resp + 10, efi_status_to_wire32(op_status));
  return usb_transport_send(usb, resp, sizeof(resp));
}

static EFI_STATUS handle_relock(USB_TRANSPORT *usb) {
  uint8_t resp[10];
  memset(resp, 0, sizeof(resp));
  resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'X'; resp[4] = 'F'; resp[5] = 'O';
  wr_be32(resp + 6, 0);
  return usb_transport_send(usb, resp, sizeof(resp));
}

static EFI_STATUS handle_read_empty_log(USB_TRANSPORT *usb, uint8_t ext, uint32_t requested_size) {
  uint16_t status = 0;
  if (requested_size > 0xffe3U || requested_size > usb->max_transfer) {
    status = 48;
  }
  return send_ext_blob_response(usb, 'F', ext, status, 0, 0);
}

static EFI_STATUS handle_common_ext(USB_TRANSPORT *usb, const uint8_t *msg, UINTN len) {
  uint8_t ext = len > 5 ? msg[5] : 0;
  if (ext == 'B') {
    uint8_t mode = len > 6 ? msg[6] : 0;
    switch (mode) {
      case 'R':
      case 'T':
      case 'U':
      case 'W':
      case 'Z':
        return send_cb_response(usb, 0, 0);
      default:
        return send_cb_response(usb, 8, mode);
    }
  }
  if (ext == 'C') {
    return send_ext_status8(usb, 'C', 'C', 0);
  }
  if (ext == 'E') {
    uint32_t echo_len = 0;
    uint8_t stack_resp[64];
    uint8_t *resp = stack_resp;
    EFI_STATUS st;
    UINTN out_len;
    if (len >= 10) {
      echo_len = rd_be32(msg + 6);
      if ((uint64_t)echo_len > (uint64_t)len - 10U) {
        echo_len = (uint32_t)(len - 10U);
      }
    }
    out_len = 6U + echo_len;
    if (out_len > sizeof(stack_resp)) {
      resp = (uint8_t *)uefi_alloc(out_len);
      if (!resp) {
        return EFI_OUT_OF_RESOURCES;
      }
    }
    resp[0] = 'N'; resp[1] = 'O'; resp[2] = 'K'; resp[3] = 'X'; resp[4] = 'C'; resp[5] = 'E';
    if (echo_len) {
      memcpy(resp + 6, msg + 10, echo_len);
    }
    st = usb_transport_send(usb, resp, out_len);
    if (resp != stack_resp) {
      uefi_free(resp);
    }
    return st;
  }
  if (ext == 'M') {
    uint16_t display_id = len >= 8 ? rd_be16(msg + 6) : 0xffffU;
    return send_ext_status8(usb, 'C', 'M', display_id < 5 ? 0 : 11);
  }
  if (ext == 'D' || ext == 'F' || ext == 'P') {
    return send_ext_blob_response(usb, 'C', ext, 38, 0, 0);
  }
  return send_unknown_response(usb);
}

static EFI_STATUS usb_secure_flash_loop(EFI_BLOCK_IO_PROTOCOL *bio, int commit) {
  EFI_STATUS st;
  USB_TRANSPORT usb;
  FFU_STREAM_FLASH flash;
  uint8_t *header = 0;
  uint64_t header_size = 0;
  uint64_t header_cap = 0;
  int flash_ready = 0;
  uint32_t async_write_status = 0;
  uint32_t async_last_sector = 0xffffffffU;
  uint16_t async_ufp_status = 0;
  uint8_t resp[64];

  memset(&flash, 0, sizeof(flash));
  st = usb_transport_open(&usb);
  if (EFI_ERROR(st)) {
    con_puta("USB function init failed: "); con_put_status(st); con_crlf();
    return st;
  }

  while (1) {
    uint8_t *msg = 0;
    UINTN len = 0;
    st = usb_transport_recv(&usb, &msg, &len);
    if (EFI_ERROR(st)) {
      con_puta("USB receive failed: "); con_put_status(st); con_crlf();
      break;
    }
    if (len < 4) {
      continue;
    }
    if (msg[0] != 'N' || msg[1] != 'O' || msg[2] != 'K') {
      send_unknown_response(&usb);
      continue;
    }
    if (msg[3] == 'I') {
      static const uint8_t hello[] = { 'N', 'O', 'K', 'I' };
      usb_transport_send(&usb, hello, sizeof(hello));
      continue;
    }
    if (msg[3] == 'V') {
      send_v_info_response(&usb, bio);
      continue;
    }
    if (msg[3] == 'R' || msg[3] == 'M' || msg[3] == 'Z') {
      st = EFI_SUCCESS;
      break;
    }
    if (msg[3] == 'S' || msg[3] == 'N') {
      continue;
    }
    if (msg[3] == 'T') {
      handle_get_gpt(&usb, bio);
      continue;
    }
    if (msg[3] == 'F') {
      handle_legacy_flash(&usb, bio, msg, len, commit);
      continue;
    }
    if (len < 6 || msg[3] != 'X') {
      send_unknown_response(&usb);
      continue;
    }
    if (msg[4] == 'C') {
      uint8_t mode = (msg[5] == 'B' && len > 6) ? msg[6] : 0;
      handle_common_ext(&usb, msg, len);
      if (msg[5] == 'B' && mode == 'R') {
        con_puta("USB host requested reboot/switch. Exiting app."); con_crlf();
        st = EFI_SUCCESS;
        break;
      }
      continue;
    }
    if (msg[4] == 'F' && msg[5] == 'R') {
      handle_read_param(&usb, bio, msg, len);
      continue;
    }
    if (msg[4] == 'F' && msg[5] == 'W') {
      handle_write_param(&usb, msg, len);
      continue;
    }
    if (msg[4] == 'F' && msg[5] == 'I') {
      handle_unlock(&usb, msg, len);
      continue;
    }
    if (msg[4] == 'F' && msg[5] == 'O') {
      handle_relock(&usb);
      continue;
    }
    if (msg[4] == 'F' && msg[5] == 'T') {
      uint32_t requested = len >= 28 ? rd_be32(msg + 24) : 0;
      handle_read_empty_log(&usb, 'T', requested);
      continue;
    }
    if (msg[4] == 'F' && msg[5] == 'X') {
      uint32_t requested = len >= 11 ? rd_be32(msg + 7) : 0;
      handle_read_empty_log(&usb, 'X', requested);
      continue;
    }
    if (msg[4] == 'F' && msg[5] == 'F') {
      if (len >= 7 && msg[6] == 'S') {
        uint16_t start_status = 0;
        if (len < 10 || msg[8] != 1 || msg[9] != 0) {
          start_status = 8;
        } else {
          async_write_status = 0;
          async_last_sector = 0xffffffffU;
          async_ufp_status = 0;
        }
        size_t out = ufp_build_async_start_response(resp, sizeof(resp), start_status);
        usb_transport_send(&usb, resp, out);
      } else if (len >= 64 && msg[6] == 'W') {
        uint32_t start = rd_be32(msg + 12);
        uint32_t count = rd_be32(msg + 16);
        uint64_t data_len = (uint64_t)count * (uint64_t)bio->Media->BlockSize;
        size_t out = ufp_build_async_write_status(resp, sizeof(resp), count, start, async_write_status, async_last_sector, async_ufp_status);
        usb_transport_send(&usb, resp, out);
        if (msg[25] != 1) {
          if (count && data_len / count != bio->Media->BlockSize) {
            async_write_status = 0;
            async_ufp_status = 8;
          } else if ((uint64_t)(len - 64U) < data_len) {
            async_write_status = 0;
            async_ufp_status = 8;
          } else if (count && (uint64_t)start + count - 1U > bio->Media->LastBlock) {
            async_write_status = 0;
            async_ufp_status = 8;
          } else if (commit && data_len) {
            st = bio->WriteBlocks(bio, bio->Media->MediaId, start, (UINTN)data_len, msg + 64);
            async_write_status = (uint32_t)st;
            async_ufp_status = EFI_ERROR(st) ? status_from_efi(st) : 0;
            if (!EFI_ERROR(st) && count) {
              async_last_sector = start + count - 1U;
            }
          } else {
            async_write_status = 0;
            async_ufp_status = 0;
            if (count) {
              async_last_sector = start + count - 1U;
            }
          }
        }
      } else if (len >= 7 && msg[6] == 'E') {
        size_t out = ufp_build_async_end_response(resp, sizeof(resp), async_write_status, async_last_sector, async_ufp_status);
        usb_transport_send(&usb, resp, out);
        async_write_status = 0;
        async_last_sector = 0xffffffffU;
        async_ufp_status = 0;
      } else {
        send_unknown_response(&usb);
      }
      continue;
    }
    if (msg[4] != 'F' || msg[5] != 'S') {
      send_unknown_response(&usb);
      continue;
    }
    if (len < 16) {
      send_fs_short(&usb, 8, (uint32_t)len);
      continue;
    }

    {
      uint16_t protocol = rd_be16(msg + 6);
      uint8_t progress = msg[8];
      uint32_t subblock = rd_be32(msg + 12);

      if ((protocol & 0x1fU) == 0 || (protocol & ~0x1fU) != 0) {
        send_fs_short(&usb, 8, protocol);
        continue;
      }
      if (msg[11] != 1) {
        send_fs_short(&usb, 15, msg[11]);
        continue;
      }

      if (subblock == UFP_SEC_SUBBLOCK_HEADER_V1 || subblock == UFP_SEC_SUBBLOCK_HEADER_V2) {
        uint32_t chunk_len;
        uint32_t file_offset;
        uint32_t total_len;
        uint32_t data_offset;
        uint8_t options;
        uint8_t erase_options;
        uint64_t need;
        uint64_t complete_len;

        if (subblock == UFP_SEC_SUBBLOCK_HEADER_V1) {
          uint32_t subblock_len;
          if (len < 32) {
            send_fs_short(&usb, 8, (uint32_t)len);
            continue;
          }
          subblock_len = rd_be32(msg + 16);
          chunk_len = rd_be32(msg + 24);
          if (subblock_len != chunk_len + 12U) {
            send_fs_short(&usb, 16, subblock_len);
            continue;
          }
          file_offset = 0;
          total_len = chunk_len;
          data_offset = 32;
          options = msg[28];
          erase_options = msg[29];
        } else {
          uint32_t absolute_header_offset;
          if (len < 60) {
            send_fs_short(&usb, 8, (uint32_t)len);
            continue;
          }
          absolute_header_offset = rd_be32(msg + 20);
          (void)absolute_header_offset;
          total_len = rd_be32(msg + 24);
          file_offset = rd_be32(msg + 29);
          chunk_len = rd_be32(msg + 33);
          data_offset = 60;
          options = msg[28];
          erase_options = msg[37];
        }

        if ((options & 0xc0U) != 0) {
          send_fs_short(&usb, 8, options);
          continue;
        }
        if (erase_options != 0) {
          send_fs_short(&usb, 8, erase_options);
          continue;
        }
        if (chunk_len > UFP_MAX_HEADER_DATA || len < (UINTN)data_offset + chunk_len) {
          send_fs_short(&usb, 16, chunk_len);
          continue;
        }
        need = (uint64_t)file_offset + chunk_len;
        if (need < file_offset || (total_len && need > total_len)) {
          send_fs_short(&usb, 16, chunk_len);
          continue;
        }
        complete_len = total_len ? total_len : need;
        if (complete_len == 0) {
          complete_len = need;
        }
        if (need > header_cap || complete_len > header_cap) {
          uint64_t cap_need = complete_len > need ? complete_len : need;
          uint64_t new_cap = align_up_u64(cap_need ? cap_need : 1, 1024U * 1024U);
          uint8_t *new_header;
          if (new_cap > 128ULL * 1024ULL * 1024ULL) {
            send_fs_short(&usb, 4, (uint32_t)new_cap);
            continue;
          }
          new_header = (uint8_t *)uefi_alloc((UINTN)new_cap);
          if (!new_header) {
            send_fs_short(&usb, 1, (uint32_t)new_cap);
            continue;
          }
          memset(new_header, 0, (UINTN)new_cap);
          if (header) {
            memcpy(new_header, header, (size_t)header_size);
            uefi_free(header);
          }
          header = new_header;
          header_cap = new_cap;
        }
        memcpy(header + file_offset, msg + data_offset, chunk_len);
        if (need > header_size) {
          header_size = need;
        }
        if (complete_len && header_size < complete_len) {
          send_fs_short(&usb, 0, 0);
          continue;
        }
        if (!flash_ready) {
          st = ffu_stream_flash_init(&flash, header, complete_len ? complete_len : header_size, bio, commit);
          if (!EFI_ERROR(st)) {
            flash_ready = 1;
            con_puta("Received complete FFU header over USB."); con_crlf();
            ffu_print_summary(&flash.plan);
          } else {
            con_puta("FFU header parse failed: "); con_put_status(st); con_crlf();
            send_fs_short(&usb, status_from_efi(st), (uint32_t)st);
            continue;
          }
        }
        send_fs_short(&usb, 0, 0);
        continue;
      }

      if (subblock == UFP_SEC_SUBBLOCK_PAYLOAD_V1 ||
          subblock == UFP_SEC_SUBBLOCK_PAYLOAD_V2 ||
          subblock == UFP_SEC_SUBBLOCK_PAYLOAD_V3) {
        uint32_t data_len;
        uint32_t subblock_len;
        uint32_t data_offset;
        uint32_t overhead;
        uint16_t protocol_mask;
        uint16_t ufp_status = 0;
        uint32_t specifier = 0;
        uint8_t final_state = 0;
        uint8_t options;

        if (len < 25) {
          send_fs_short(&usb, 8, (uint32_t)len);
          continue;
        }
        subblock_len = rd_be32(msg + 16);
        data_len = rd_be32(msg + 20);
        options = msg[24];
        if (subblock == UFP_SEC_SUBBLOCK_PAYLOAD_V1) {
          data_offset = 28;
          overhead = 8;
          protocol_mask = 0x0003;
        } else if (subblock == UFP_SEC_SUBBLOCK_PAYLOAD_V2) {
          data_offset = 32;
          overhead = 12;
          protocol_mask = 0x000c;
        } else {
          data_offset = 64;
          overhead = 44;
          protocol_mask = 0x0010;
        }
        if (subblock_len != data_len + overhead) {
          send_fs_short(&usb, 16, subblock_len);
          continue;
        }
        if ((options & 0xfeU) != 0) {
          send_fs_short(&usb, 8, options);
          continue;
        }
        if ((protocol & protocol_mask) == 0) {
          send_fs_short(&usb, 8, protocol);
          continue;
        }
        if (data_len > UFP_MAX_PAYLOAD_DATA || len < (UINTN)data_offset + data_len) {
          send_fs_short(&usb, 16, data_len);
          continue;
        }
        if (!flash_ready) {
          ufp_status = 6;
        } else if (data_len) {
          st = ffu_stream_flash_payload(&flash, msg + data_offset, data_len);
          if (EFI_ERROR(st)) {
            ufp_status = status_from_efi(st);
            specifier = (uint32_t)st;
          }
        }
        if (progress == 100) {
          final_state = ffu_stream_flash_done(&flash) ? 1 : 0;
          if (!final_state && !ufp_status) {
            ufp_status = 8;
          }
        }
        {
          size_t out = ufp_build_secure_status_response(resp, sizeof(resp), ufp_status, specifier, 0xffffffffU, flash.last_sector, final_state);
          usb_transport_send(&usb, resp, out);
        }
        if (final_state == 1 && !ufp_status) {
          con_puta("USB FFU flashing completed."); con_crlf();
          if (commit && bio->FlushBlocks) {
            bio->FlushBlocks(bio);
          }
          st = EFI_SUCCESS;
          break;
        }
        continue;
      }

      send_fs_short(&usb, 14, subblock);
      continue;
    }
  }
  ffu_stream_flash_free(&flash);
  uefi_free(header);
  usb_transport_close(&usb);
  return st;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS st;
  OPTIONS opts;
  UEFI_FILE_VIEW view;
  FFU_PLAN plan;

  uefi_set_system_table(SystemTable);
  con_puta("UfpFfu: recovered UFP FFU protocol app"); con_crlf();

  st = parse_options(ImageHandle, &opts);
  if (EFI_ERROR(st)) {
    con_puta("Argument parse failed: "); con_put_status(st); con_crlf();
    usage();
    return st;
  }
  if (opts.help) {
    usage();
    return EFI_SUCCESS;
  }
  if (opts.list_block) {
    return list_block_devices();
  }
  if (opts.usb_ffu) {
    EFI_BLOCK_IO_PROTOCOL *bio = 0;
    if (opts.target < 0 || !opts.yes) {
      con_puta("--usb-ffu requires --target=N --yes."); con_crlf();
      return EFI_INVALID_PARAMETER;
    }
    st = get_block_device(opts.target, &bio);
    if (EFI_ERROR(st)) {
      con_puta("Target Block I/O handle not found: "); con_put_status(st); con_crlf();
      return st;
    }
    return usb_secure_flash_loop(bio, opts.yes);
  }
  if (!opts.path || !opts.path_len) {
    usage();
    return EFI_INVALID_PARAMETER;
  }

  memset(&view, 0, sizeof(view));
  memset(&plan, 0, sizeof(plan));

  st = file_open_from_image(ImageHandle, opts.path, opts.path_len, &view);
  if (EFI_ERROR(st)) {
    con_puta("Open FFU failed: "); con_put_status(st); con_crlf();
    return st;
  }

  st = ffu_parse(&view, &plan);
  if (EFI_ERROR(st)) {
    con_puta("FFU parse failed: "); con_put_status(st); con_crlf();
    file_close(&view);
    return st;
  }

  ffu_print_summary(&plan);

  if (opts.packets) {
    ffu_packet_dry_run(&plan);
  }

  if (opts.flash) {
    EFI_BLOCK_IO_PROTOCOL *bio = 0;
    if (opts.target < 0) {
      con_puta("--flash requires --target=N. Use --list-block first."); con_crlf();
      st = EFI_INVALID_PARAMETER;
    } else {
      st = get_block_device(opts.target, &bio);
      if (EFI_ERROR(st)) {
        con_puta("Target Block I/O handle not found: "); con_put_status(st); con_crlf();
      } else {
        st = ffu_flash_to_block_io(&view, &plan, bio, opts.yes);
        if (EFI_ERROR(st)) {
          con_puta("Flash plan failed: "); con_put_status(st); con_crlf();
        } else if (opts.yes) {
          con_puta("Flash completed."); con_crlf();
        } else {
          con_puta("Dry-run completed. Re-run with --yes to write."); con_crlf();
        }
      }
    }
  }

  ffu_plan_free(&plan);
  file_close(&view);
  return st;
}
