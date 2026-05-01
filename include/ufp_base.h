#ifndef UFP_BASE_H
#define UFP_BASE_H

#include "uefi_min.h"

void *memcpy(void *dst, const void *src, size_t len);
void *memset(void *dst, int value, size_t len);
int memcmp(const void *a, const void *b, size_t len);

void *uefi_alloc(UINTN size);
void uefi_free(void *ptr);
void uefi_set_system_table(EFI_SYSTEM_TABLE *st);
EFI_SYSTEM_TABLE *uefi_system_table(void);
EFI_BOOT_SERVICES *uefi_bs(void);

void con_puts(const CHAR16 *s);
void con_puta(const char *s);
void con_space(void);
void con_crlf(void);
void con_put_dec(uint64_t value);
void con_put_hex(uint64_t value, unsigned width);
void con_put_status(EFI_STATUS status);

uint16_t rd_le16(const uint8_t *p);
uint32_t rd_le32(const uint8_t *p);
uint64_t rd_le64(const uint8_t *p);
uint16_t rd_be16(const uint8_t *p);
uint32_t rd_be32(const uint8_t *p);
void wr_be16(uint8_t *p, uint16_t v);
void wr_be32(uint8_t *p, uint32_t v);
uint64_t align_up_u64(uint64_t value, uint64_t alignment);

#endif
