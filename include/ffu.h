#ifndef FFU_H
#define FFU_H

#include "uefi_min.h"
#include <stdint.h>

typedef struct {
  uint64_t payload_offset;
  uint64_t descriptor_offset;
  uint32_t byte_count;
  uint32_t block_count;
  uint32_t location_count;
  uint32_t store_index;
} FFU_WRITE_ENTRY;

typedef struct {
  uint64_t header_offset;
  uint64_t payload_base;
  uint64_t payload_size;
  uint64_t descriptor_base;
  uint32_t block_size;
  uint32_t descriptor_count;
  uint32_t entry_start;
  uint32_t entry_count;
} FFU_STORE;

typedef struct {
  uint64_t file_size;
  uint64_t header_size;
  uint64_t payload_base;
  uint32_t chunk_size;
  uint32_t security_catalog_size;
  uint32_t security_hash_size;
  uint32_t image_header_size;
  uint32_t manifest_length;
  uint32_t validation_descriptor_count;
  uint32_t store_count;
  uint32_t entry_count;
  uint64_t total_payload_bytes;
  FFU_STORE *stores;
  FFU_WRITE_ENTRY *entries;
} FFU_PLAN;

typedef struct {
  EFI_FILE_PROTOCOL *file;
  uint64_t size;
} UEFI_FILE_VIEW;

EFI_STATUS file_open_from_image(EFI_HANDLE image, const CHAR16 *path, UINTN path_len, UEFI_FILE_VIEW *view);
void file_close(UEFI_FILE_VIEW *view);
EFI_STATUS file_seek(UEFI_FILE_VIEW *view, uint64_t offset);
EFI_STATUS file_read_exact(UEFI_FILE_VIEW *view, uint64_t offset, void *buffer, UINTN size);
EFI_STATUS file_read_current(UEFI_FILE_VIEW *view, void *buffer, UINTN size);
EFI_STATUS ffu_parse(UEFI_FILE_VIEW *view, FFU_PLAN *plan);
EFI_STATUS ffu_parse_header_bytes(const uint8_t *data, uint64_t size, FFU_PLAN *plan);
void ffu_plan_free(FFU_PLAN *plan);

EFI_STATUS ffu_print_summary(FFU_PLAN *plan);
EFI_STATUS ffu_packet_dry_run(FFU_PLAN *plan);
EFI_STATUS ffu_flash_to_block_io(UEFI_FILE_VIEW *view, FFU_PLAN *plan, EFI_BLOCK_IO_PROTOCOL *bio, int commit);

typedef struct {
  FFU_PLAN plan;
  EFI_BLOCK_IO_PROTOCOL *bio;
  const uint8_t *header;
  uint64_t header_size;
  uint8_t *entry_data;
  uint32_t entry_capacity;
  uint32_t entry_index;
  uint32_t entry_received;
  uint32_t media_block;
  uint32_t media_id;
  uint32_t dry_run;
  uint32_t final_state;
  uint32_t last_sector;
} FFU_STREAM_FLASH;

EFI_STATUS ffu_stream_flash_init(FFU_STREAM_FLASH *ctx, const uint8_t *header, uint64_t header_size, EFI_BLOCK_IO_PROTOCOL *bio, int commit);
void ffu_stream_flash_free(FFU_STREAM_FLASH *ctx);
EFI_STATUS ffu_stream_flash_payload(FFU_STREAM_FLASH *ctx, const uint8_t *data, uint32_t len);
int ffu_stream_flash_done(FFU_STREAM_FLASH *ctx);

#endif
