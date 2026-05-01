#include "ufp_base.h"

EFI_GUID gEfiLoadedImageProtocolGuid = {0x5B1B31A1, 0x9562, 0x11D2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
EFI_GUID gEfiSimpleFileSystemProtocolGuid = {0x0964E5B22, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
EFI_GUID gEfiBlockIoProtocolGuid = {0x964E5B21, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
EFI_GUID gEfiUsbFnIoProtocolGuid = {0x32D2963A, 0xFE5D, 0x4F30, {0xB6, 0x33, 0x6E, 0x5D, 0xC5, 0x58, 0x03, 0xCC}};

static EFI_SYSTEM_TABLE *g_st;

void *memcpy(void *dst, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  while (len--) {
    *d++ = *s++;
  }
  return dst;
}

void *memset(void *dst, int value, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  while (len--) {
    *d++ = (uint8_t)value;
  }
  return dst;
}

int memcmp(const void *a, const void *b, size_t len) {
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  while (len--) {
    if (*pa != *pb) {
      return (int)*pa - (int)*pb;
    }
    ++pa;
    ++pb;
  }
  return 0;
}

void uefi_set_system_table(EFI_SYSTEM_TABLE *st) {
  g_st = st;
}

EFI_SYSTEM_TABLE *uefi_system_table(void) {
  return g_st;
}

EFI_BOOT_SERVICES *uefi_bs(void) {
  return g_st ? g_st->BootServices : 0;
}

EFI_RUNTIME_SERVICES *uefi_rs(void) {
  return g_st ? g_st->RuntimeServices : 0;
}

void *uefi_alloc(UINTN size) {
  void *ptr = 0;
  if (!size) {
    size = 1;
  }
  if (!g_st || !g_st->BootServices) {
    return 0;
  }
  if (EFI_ERROR(g_st->BootServices->AllocatePool(EfiLoaderData, size, &ptr))) {
    return 0;
  }
  memset(ptr, 0, (size_t)size);
  return ptr;
}

void uefi_free(void *ptr) {
  if (ptr && g_st && g_st->BootServices) {
    g_st->BootServices->FreePool(ptr);
  }
}

void con_puts(const CHAR16 *s) {
  if (g_st && g_st->ConOut && s) {
    g_st->ConOut->OutputString(g_st->ConOut, (CHAR16 *)s);
  }
}

void con_puta(const char *s) {
  CHAR16 tmp[96];
  UINTN n = 0;
  if (!s) {
    return;
  }
  while (*s) {
    n = 0;
    while (*s && n + 1 < (sizeof(tmp) / sizeof(tmp[0]))) {
      tmp[n++] = (CHAR16)(uint8_t)*s++;
    }
    tmp[n] = 0;
    con_puts(tmp);
  }
}

void con_space(void) {
  con_puts((const CHAR16 *)L" ");
}

void con_crlf(void) {
  con_puts((const CHAR16 *)L"\r\n");
}

void con_put_dec(uint64_t value) {
  CHAR16 buf[32];
  UINTN pos = sizeof(buf) / sizeof(buf[0]);
  buf[--pos] = 0;
  if (!value) {
    buf[--pos] = (CHAR16)'0';
  } else {
    while (value && pos) {
      buf[--pos] = (CHAR16)('0' + (value % 10));
      value /= 10;
    }
  }
  con_puts(&buf[pos]);
}

void con_put_hex(uint64_t value, unsigned width) {
  CHAR16 buf[19];
  static const char hex[] = "0123456789abcdef";
  unsigned n = width;
  if (!n || n > 16) {
    n = 16;
  }
  buf[0] = (CHAR16)'0';
  buf[1] = (CHAR16)'x';
  for (unsigned i = 0; i < n; ++i) {
    unsigned shift = (n - 1 - i) * 4;
    buf[2 + i] = (CHAR16)hex[(value >> shift) & 0xf];
  }
  buf[2 + n] = 0;
  con_puts(buf);
}

void con_put_status(EFI_STATUS status) {
  con_put_hex(status, 16);
}

uint16_t rd_le16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t rd_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t rd_le64(const uint8_t *p) {
  return (uint64_t)rd_le32(p) | ((uint64_t)rd_le32(p + 4) << 32);
}

uint16_t rd_be16(const uint8_t *p) {
  return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

uint32_t rd_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void wr_be16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

void wr_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

uint64_t align_up_u64(uint64_t value, uint64_t alignment) {
  uint64_t rem;
  if (!alignment) {
    return value;
  }
  rem = value % alignment;
  return rem ? value + alignment - rem : value;
}
