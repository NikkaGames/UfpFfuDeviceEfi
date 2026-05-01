#include "ffu.h"
#include "ufp_base.h"
#include "ufp_proto.h"

#define FFU_SECURITY_HEADER_SIZE 32U
#define FFU_IMAGE_HEADER_V1_SIZE 24U
#define FFU_IMAGE_HEADER_V2_SIZE 28U
#define FFU_STORE_HEADER_V1_SIZE 248U
#define FFU_STORE_HEADER_V2_BASE_SIZE 262U

static EFI_STATUS append_store(FFU_PLAN *plan, uint32_t *cap, const FFU_STORE *store) {
  FFU_STORE *next;
  if (plan->store_count >= *cap) {
    uint32_t new_cap = *cap ? *cap * 2U : 2U;
    next = (FFU_STORE *)uefi_alloc((UINTN)new_cap * sizeof(FFU_STORE));
    if (!next) {
      return EFI_OUT_OF_RESOURCES;
    }
    if (plan->stores) {
      memcpy(next, plan->stores, (size_t)plan->store_count * sizeof(FFU_STORE));
      uefi_free(plan->stores);
    }
    plan->stores = next;
    *cap = new_cap;
  }
  plan->stores[plan->store_count++] = *store;
  return EFI_SUCCESS;
}

static EFI_STATUS append_entry(FFU_PLAN *plan, uint32_t *cap, const FFU_WRITE_ENTRY *entry) {
  FFU_WRITE_ENTRY *next;
  if (plan->entry_count >= *cap) {
    uint32_t new_cap = *cap ? *cap * 2U : 64U;
    next = (FFU_WRITE_ENTRY *)uefi_alloc((UINTN)new_cap * sizeof(FFU_WRITE_ENTRY));
    if (!next) {
      return EFI_OUT_OF_RESOURCES;
    }
    if (plan->entries) {
      memcpy(next, plan->entries, (size_t)plan->entry_count * sizeof(FFU_WRITE_ENTRY));
      uefi_free(plan->entries);
    }
    plan->entries = next;
    *cap = new_cap;
  }
  plan->entries[plan->entry_count++] = *entry;
  return EFI_SUCCESS;
}

static int sig_eq(const uint8_t *p, const char *s, UINTN len) {
  for (UINTN i = 0; i < len; ++i) {
    if (p[i] != (uint8_t)s[i]) {
      return 0;
    }
  }
  return 1;
}

static EFI_STATUS mem_read_exact(const uint8_t *data, uint64_t size, uint64_t offset, void *buffer, UINTN len) {
  if (!data || !buffer || offset > size || (uint64_t)len > size - offset) {
    return EFI_END_OF_FILE;
  }
  memcpy(buffer, data + offset, (size_t)len);
  return EFI_SUCCESS;
}

static EFI_STATUS make_open_path(const CHAR16 *path, UINTN path_len, CHAR16 **out) {
  UINTN start = 0;
  UINTN len;
  CHAR16 *copy;
  if (!path || !path_len || !out) {
    return EFI_INVALID_PARAMETER;
  }
  if (path_len >= 3 && path[2] == (CHAR16)':') {
    start = 3;
  }
  len = path_len - start;
  copy = (CHAR16 *)uefi_alloc((len + 1) * sizeof(CHAR16));
  if (!copy) {
    return EFI_OUT_OF_RESOURCES;
  }
  for (UINTN i = 0; i < len; ++i) {
    CHAR16 ch = path[start + i];
    copy[i] = (ch == (CHAR16)'/') ? (CHAR16)'\\' : ch;
  }
  copy[len] = 0;
  *out = copy;
  return EFI_SUCCESS;
}

EFI_STATUS file_open_from_image(EFI_HANDLE image, const CHAR16 *path, UINTN path_len, UEFI_FILE_VIEW *view) {
  EFI_STATUS st;
  EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
  EFI_FILE_PROTOCOL *root = 0;
  EFI_FILE_PROTOCOL *file = 0;
  CHAR16 *open_path = 0;
  uint64_t size = 0;

  if (!view) {
    return EFI_INVALID_PARAMETER;
  }
  memset(view, 0, sizeof(*view));

  st = make_open_path(path, path_len, &open_path);
  if (EFI_ERROR(st)) {
    return st;
  }

  st = uefi_bs()->HandleProtocol(image, &gEfiLoadedImageProtocolGuid, (void **)&loaded);
  if (EFI_ERROR(st)) {
    uefi_free(open_path);
    return st;
  }
  st = uefi_bs()->HandleProtocol(loaded->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void **)&fs);
  if (EFI_ERROR(st)) {
    uefi_free(open_path);
    return st;
  }
  st = fs->OpenVolume(fs, &root);
  if (EFI_ERROR(st)) {
    uefi_free(open_path);
    return st;
  }
  st = root->Open(root, &file, open_path, EFI_FILE_MODE_READ, 0);
  root->Close(root);
  uefi_free(open_path);
  if (EFI_ERROR(st)) {
    return st;
  }

  st = file->SetPosition(file, UINT64_MAX);
  if (!EFI_ERROR(st)) {
    st = file->GetPosition(file, &size);
  }
  if (!EFI_ERROR(st)) {
    st = file->SetPosition(file, 0);
  }
  if (EFI_ERROR(st)) {
    file->Close(file);
    return st;
  }

  view->file = file;
  view->size = size;
  return EFI_SUCCESS;
}

void file_close(UEFI_FILE_VIEW *view) {
  if (view && view->file) {
    view->file->Close(view->file);
    view->file = 0;
    view->size = 0;
  }
}

EFI_STATUS file_seek(UEFI_FILE_VIEW *view, uint64_t offset) {
  if (!view || !view->file) {
    return EFI_INVALID_PARAMETER;
  }
  return view->file->SetPosition(view->file, offset);
}

EFI_STATUS file_read_current(UEFI_FILE_VIEW *view, void *buffer, UINTN size) {
  EFI_STATUS st;
  UINTN got = size;
  if (!view || !view->file || !buffer) {
    return EFI_INVALID_PARAMETER;
  }
  st = view->file->Read(view->file, &got, buffer);
  if (EFI_ERROR(st)) {
    return st;
  }
  return got == size ? EFI_SUCCESS : EFI_END_OF_FILE;
}

EFI_STATUS file_read_exact(UEFI_FILE_VIEW *view, uint64_t offset, void *buffer, UINTN size) {
  EFI_STATUS st = file_seek(view, offset);
  if (EFI_ERROR(st)) {
    return st;
  }
  return file_read_current(view, buffer, size);
}

void ffu_plan_free(FFU_PLAN *plan) {
  if (!plan) {
    return;
  }
  uefi_free(plan->stores);
  uefi_free(plan->entries);
  memset(plan, 0, sizeof(*plan));
}

EFI_STATUS ffu_parse(UEFI_FILE_VIEW *view, FFU_PLAN *plan) {
  EFI_STATUS st;
  uint8_t sec[FFU_SECURITY_HEADER_SIZE];
  uint8_t img[FFU_IMAGE_HEADER_V2_SIZE];
  uint32_t store_cap = 0;
  uint32_t entry_cap = 0;
  uint64_t cursor;
  uint64_t payload_cursor;
  uint32_t desired_store_count = 1;

  if (!view || !plan) {
    return EFI_INVALID_PARAMETER;
  }
  memset(plan, 0, sizeof(*plan));
  plan->file_size = view->size;

  if (view->size < FFU_SECURITY_HEADER_SIZE) {
    return EFI_VOLUME_CORRUPTED;
  }
  st = file_read_exact(view, 0, sec, sizeof(sec));
  if (EFI_ERROR(st)) {
    return st;
  }
  if (rd_le32(sec) != FFU_SECURITY_HEADER_SIZE || !sig_eq(sec + 4, "SignedImage ", 12)) {
    return EFI_VOLUME_CORRUPTED;
  }
  if (rd_le32(sec + 20) != 0x800cU) {
    return EFI_UNSUPPORTED;
  }
  plan->chunk_size = rd_le32(sec + 16) << 10;
  plan->security_catalog_size = rd_le32(sec + 24);
  plan->security_hash_size = rd_le32(sec + 28);
  if (!plan->chunk_size || !plan->security_catalog_size || !plan->security_hash_size || (plan->security_hash_size & 31U) != 0) {
    return EFI_VOLUME_CORRUPTED;
  }
  cursor = align_up_u64((uint64_t)FFU_SECURITY_HEADER_SIZE + plan->security_catalog_size + plan->security_hash_size, plan->chunk_size);

  st = file_read_exact(view, cursor, img, FFU_IMAGE_HEADER_V1_SIZE);
  if (EFI_ERROR(st)) {
    return st;
  }
  plan->image_header_size = rd_le32(img);
  if ((plan->image_header_size != FFU_IMAGE_HEADER_V1_SIZE && plan->image_header_size != FFU_IMAGE_HEADER_V2_SIZE) ||
      !sig_eq(img + 4, "ImageFlash  ", 12)) {
    return EFI_VOLUME_CORRUPTED;
  }
  if (plan->image_header_size == FFU_IMAGE_HEADER_V2_SIZE) {
    st = file_read_exact(view, cursor + FFU_IMAGE_HEADER_V1_SIZE, img + FFU_IMAGE_HEADER_V1_SIZE, 4);
    if (EFI_ERROR(st)) {
      return st;
    }
  }
  plan->manifest_length = rd_le32(img + 16);
  if (!plan->manifest_length || !rd_le32(img + 20)) {
    return EFI_VOLUME_CORRUPTED;
  }
  plan->validation_descriptor_count = plan->image_header_size == FFU_IMAGE_HEADER_V2_SIZE ? rd_le32(img + 24) : 0;
  cursor += plan->image_header_size + plan->manifest_length;

  for (uint32_t i = 0; i < plan->validation_descriptor_count; ++i) {
    uint8_t vd[28];
    uint64_t skip = 0;
    st = file_read_exact(view, cursor, vd, sizeof(vd));
    if (EFI_ERROR(st)) {
      return st;
    }
    for (uint32_t j = 0; j < 7; ++j) {
      skip += rd_le32(vd + j * 4U);
    }
    cursor += sizeof(vd) + skip;
  }

  while (plan->store_count < desired_store_count) {
    uint8_t sh[FFU_STORE_HEADER_V2_BASE_SIZE];
    FFU_STORE store;
    uint64_t header_offset;
    uint64_t descriptor_cursor;
    uint64_t rel_payload = 0;
    uint32_t store_index = plan->store_count;
    uint16_t marker;
    uint32_t block_size;
    uint32_t desc_count;
    uint64_t payload_size = 0;

    cursor = align_up_u64(cursor, plan->chunk_size);
    header_offset = cursor;
    st = file_read_exact(view, cursor, sh, FFU_STORE_HEADER_V1_SIZE);
    if (EFI_ERROR(st)) {
      return st;
    }
    cursor += FFU_STORE_HEADER_V1_SIZE;

    marker = rd_le16(sh + 4);
    if (marker == 2) {
      uint16_t device_path_len;
      st = file_read_exact(view, cursor, sh + FFU_STORE_HEADER_V1_SIZE, FFU_STORE_HEADER_V2_BASE_SIZE - FFU_STORE_HEADER_V1_SIZE);
      if (EFI_ERROR(st)) {
        return st;
      }
      cursor += FFU_STORE_HEADER_V2_BASE_SIZE - FFU_STORE_HEADER_V1_SIZE;
      desired_store_count = rd_le16(sh + 248);
      payload_size = rd_le64(sh + 252);
      device_path_len = rd_le16(sh + 260);
      if (!desired_store_count || !device_path_len) {
        return EFI_VOLUME_CORRUPTED;
      }
      cursor += (uint64_t)device_path_len * 2U;
    }

    if (rd_le32(sh) != 0 || rd_le16(sh + 8) != 2 || rd_le16(sh + 10) != 0) {
      return EFI_VOLUME_CORRUPTED;
    }
    block_size = rd_le32(sh + 204);
    desc_count = rd_le32(sh + 208);
    if (!block_size || block_size > UFP_MAX_PAYLOAD_DATA || !desc_count) {
      return EFI_VOLUME_CORRUPTED;
    }

    memset(&store, 0, sizeof(store));
    store.header_offset = header_offset;
    store.descriptor_base = cursor;
    store.block_size = block_size;
    store.descriptor_count = desc_count;
    store.payload_size = payload_size;
    store.entry_start = plan->entry_count;

    descriptor_cursor = cursor;
    for (uint32_t d = 0; d < desc_count; ++d) {
      uint8_t wh[8];
      FFU_WRITE_ENTRY entry;
      uint32_t loc_count;
      uint32_t block_count;
      uint64_t byte_count;
      st = file_read_exact(view, descriptor_cursor, wh, sizeof(wh));
      if (EFI_ERROR(st)) {
        return st;
      }
      loc_count = rd_le32(wh);
      block_count = rd_le32(wh + 4);
      if (!loc_count || !block_count) {
        return EFI_VOLUME_CORRUPTED;
      }
      byte_count = (uint64_t)block_size * block_count;
      if (byte_count > 0xffffffffULL) {
        return EFI_UNSUPPORTED;
      }
      memset(&entry, 0, sizeof(entry));
      entry.payload_offset = rel_payload;
      entry.descriptor_offset = descriptor_cursor;
      entry.byte_count = (uint32_t)byte_count;
      entry.block_count = block_count;
      entry.location_count = loc_count;
      entry.store_index = store_index;
      st = append_entry(plan, &entry_cap, &entry);
      if (EFI_ERROR(st)) {
        return st;
      }
      rel_payload += byte_count;
      descriptor_cursor += 8ULL + (uint64_t)loc_count * 8ULL;
    }
    store.entry_count = desc_count;
    if (!store.payload_size) {
      store.payload_size = rel_payload;
    }
    st = append_store(plan, &store_cap, &store);
    if (EFI_ERROR(st)) {
      return st;
    }
    cursor = descriptor_cursor;
  }

  payload_cursor = cursor;
  for (uint32_t s = 0; s < plan->store_count; ++s) {
    FFU_STORE *store = &plan->stores[s];
    payload_cursor = align_up_u64(payload_cursor, store->block_size);
    store->payload_base = payload_cursor;
    if (!plan->payload_base) {
      plan->payload_base = payload_cursor;
    }
    for (uint32_t e = 0; e < store->entry_count; ++e) {
      FFU_WRITE_ENTRY *entry = &plan->entries[store->entry_start + e];
      entry->payload_offset += payload_cursor;
      plan->total_payload_bytes += entry->byte_count;
    }
    payload_cursor += store->payload_size;
  }
  plan->header_size = plan->payload_base;
  if (!plan->header_size || plan->header_size > view->size) {
    return EFI_VOLUME_CORRUPTED;
  }
  return EFI_SUCCESS;
}

EFI_STATUS ffu_parse_header_bytes(const uint8_t *data, uint64_t size, FFU_PLAN *plan) {
  EFI_STATUS st;
  uint8_t sec[FFU_SECURITY_HEADER_SIZE];
  uint8_t img[FFU_IMAGE_HEADER_V2_SIZE];
  uint32_t store_cap = 0;
  uint32_t entry_cap = 0;
  uint64_t cursor;
  uint64_t payload_cursor;
  uint32_t desired_store_count = 1;

  if (!data || !plan) {
    return EFI_INVALID_PARAMETER;
  }
  memset(plan, 0, sizeof(*plan));
  plan->file_size = size;
  if (size < FFU_SECURITY_HEADER_SIZE) {
    return EFI_END_OF_FILE;
  }
  st = mem_read_exact(data, size, 0, sec, sizeof(sec));
  if (EFI_ERROR(st)) {
    return st;
  }
  if (rd_le32(sec) != FFU_SECURITY_HEADER_SIZE || !sig_eq(sec + 4, "SignedImage ", 12)) {
    return EFI_VOLUME_CORRUPTED;
  }
  if (rd_le32(sec + 20) != 0x800cU) {
    return EFI_UNSUPPORTED;
  }
  plan->chunk_size = rd_le32(sec + 16) << 10;
  plan->security_catalog_size = rd_le32(sec + 24);
  plan->security_hash_size = rd_le32(sec + 28);
  if (!plan->chunk_size || !plan->security_catalog_size || !plan->security_hash_size || (plan->security_hash_size & 31U) != 0) {
    return EFI_VOLUME_CORRUPTED;
  }
  cursor = align_up_u64((uint64_t)FFU_SECURITY_HEADER_SIZE + plan->security_catalog_size + plan->security_hash_size, plan->chunk_size);
  if (cursor + FFU_IMAGE_HEADER_V1_SIZE > size) {
    return EFI_END_OF_FILE;
  }

  st = mem_read_exact(data, size, cursor, img, FFU_IMAGE_HEADER_V1_SIZE);
  if (EFI_ERROR(st)) {
    return st;
  }
  plan->image_header_size = rd_le32(img);
  if ((plan->image_header_size != FFU_IMAGE_HEADER_V1_SIZE && plan->image_header_size != FFU_IMAGE_HEADER_V2_SIZE) ||
      !sig_eq(img + 4, "ImageFlash  ", 12)) {
    return EFI_VOLUME_CORRUPTED;
  }
  if (plan->image_header_size == FFU_IMAGE_HEADER_V2_SIZE) {
    st = mem_read_exact(data, size, cursor + FFU_IMAGE_HEADER_V1_SIZE, img + FFU_IMAGE_HEADER_V1_SIZE, 4);
    if (EFI_ERROR(st)) {
      return st;
    }
  }
  plan->manifest_length = rd_le32(img + 16);
  if (!plan->manifest_length || !rd_le32(img + 20)) {
    return EFI_VOLUME_CORRUPTED;
  }
  plan->validation_descriptor_count = plan->image_header_size == FFU_IMAGE_HEADER_V2_SIZE ? rd_le32(img + 24) : 0;
  cursor += plan->image_header_size + plan->manifest_length;
  if (cursor > size) {
    return EFI_END_OF_FILE;
  }

  for (uint32_t i = 0; i < plan->validation_descriptor_count; ++i) {
    uint8_t vd[28];
    uint64_t skip = 0;
    st = mem_read_exact(data, size, cursor, vd, sizeof(vd));
    if (EFI_ERROR(st)) {
      return st;
    }
    for (uint32_t j = 0; j < 7; ++j) {
      skip += rd_le32(vd + j * 4U);
    }
    cursor += sizeof(vd) + skip;
    if (cursor > size) {
      return EFI_END_OF_FILE;
    }
  }

  while (plan->store_count < desired_store_count) {
    uint8_t sh[FFU_STORE_HEADER_V2_BASE_SIZE];
    FFU_STORE store;
    uint64_t header_offset;
    uint64_t descriptor_cursor;
    uint64_t rel_payload = 0;
    uint32_t store_index = plan->store_count;
    uint16_t marker;
    uint32_t block_size;
    uint32_t desc_count;
    uint64_t payload_size = 0;

    cursor = align_up_u64(cursor, plan->chunk_size);
    header_offset = cursor;
    st = mem_read_exact(data, size, cursor, sh, FFU_STORE_HEADER_V1_SIZE);
    if (EFI_ERROR(st)) {
      return st;
    }
    cursor += FFU_STORE_HEADER_V1_SIZE;

    marker = rd_le16(sh + 4);
    if (marker == 2) {
      uint16_t device_path_len;
      st = mem_read_exact(data, size, cursor, sh + FFU_STORE_HEADER_V1_SIZE, FFU_STORE_HEADER_V2_BASE_SIZE - FFU_STORE_HEADER_V1_SIZE);
      if (EFI_ERROR(st)) {
        return st;
      }
      cursor += FFU_STORE_HEADER_V2_BASE_SIZE - FFU_STORE_HEADER_V1_SIZE;
      desired_store_count = rd_le16(sh + 248);
      payload_size = rd_le64(sh + 252);
      device_path_len = rd_le16(sh + 260);
      if (!desired_store_count || !device_path_len) {
        return EFI_VOLUME_CORRUPTED;
      }
      cursor += (uint64_t)device_path_len * 2U;
      if (cursor > size) {
        return EFI_END_OF_FILE;
      }
    }

    if (rd_le32(sh) != 0 || rd_le16(sh + 8) != 2 || rd_le16(sh + 10) != 0) {
      return EFI_VOLUME_CORRUPTED;
    }
    block_size = rd_le32(sh + 204);
    desc_count = rd_le32(sh + 208);
    if (!block_size || block_size > UFP_MAX_PAYLOAD_DATA || !desc_count) {
      return EFI_VOLUME_CORRUPTED;
    }

    memset(&store, 0, sizeof(store));
    store.header_offset = header_offset;
    store.descriptor_base = cursor;
    store.block_size = block_size;
    store.descriptor_count = desc_count;
    store.payload_size = payload_size;
    store.entry_start = plan->entry_count;

    descriptor_cursor = cursor;
    for (uint32_t d = 0; d < desc_count; ++d) {
      uint8_t wh[8];
      FFU_WRITE_ENTRY entry;
      uint32_t loc_count;
      uint32_t block_count;
      uint64_t byte_count;
      st = mem_read_exact(data, size, descriptor_cursor, wh, sizeof(wh));
      if (EFI_ERROR(st)) {
        return st;
      }
      loc_count = rd_le32(wh);
      block_count = rd_le32(wh + 4);
      if (!loc_count || !block_count) {
        return EFI_VOLUME_CORRUPTED;
      }
      if (descriptor_cursor + 8ULL + (uint64_t)loc_count * 8ULL > size) {
        return EFI_END_OF_FILE;
      }
      byte_count = (uint64_t)block_size * block_count;
      if (byte_count > 0xffffffffULL) {
        return EFI_UNSUPPORTED;
      }
      memset(&entry, 0, sizeof(entry));
      entry.payload_offset = rel_payload;
      entry.descriptor_offset = descriptor_cursor;
      entry.byte_count = (uint32_t)byte_count;
      entry.block_count = block_count;
      entry.location_count = loc_count;
      entry.store_index = store_index;
      st = append_entry(plan, &entry_cap, &entry);
      if (EFI_ERROR(st)) {
        return st;
      }
      rel_payload += byte_count;
      descriptor_cursor += 8ULL + (uint64_t)loc_count * 8ULL;
    }
    store.entry_count = desc_count;
    if (!store.payload_size) {
      store.payload_size = rel_payload;
    }
    st = append_store(plan, &store_cap, &store);
    if (EFI_ERROR(st)) {
      return st;
    }
    cursor = descriptor_cursor;
  }

  payload_cursor = cursor;
  for (uint32_t s = 0; s < plan->store_count; ++s) {
    FFU_STORE *store = &plan->stores[s];
    payload_cursor = align_up_u64(payload_cursor, store->block_size);
    store->payload_base = payload_cursor;
    if (!plan->payload_base) {
      plan->payload_base = payload_cursor;
    }
    for (uint32_t e = 0; e < store->entry_count; ++e) {
      FFU_WRITE_ENTRY *entry = &plan->entries[store->entry_start + e];
      entry->payload_offset += payload_cursor;
      plan->total_payload_bytes += entry->byte_count;
    }
    payload_cursor += store->payload_size;
  }
  plan->header_size = plan->payload_base;
  if (!plan->header_size || plan->header_size > size) {
    return EFI_END_OF_FILE;
  }
  return EFI_SUCCESS;
}

EFI_STATUS ffu_print_summary(FFU_PLAN *plan) {
  if (!plan) {
    return EFI_INVALID_PARAMETER;
  }
  con_puta("FFU file size: "); con_put_dec(plan->file_size); con_crlf();
  con_puta("FFU header size sent by UFP: "); con_put_dec(plan->header_size); con_crlf();
  con_puta("Chunk size: "); con_put_dec(plan->chunk_size); con_crlf();
  con_puta("Manifest length: "); con_put_dec(plan->manifest_length); con_crlf();
  con_puta("Validation descriptors: "); con_put_dec(plan->validation_descriptor_count); con_crlf();
  con_puta("Stores: "); con_put_dec(plan->store_count); con_crlf();
  con_puta("Write descriptors: "); con_put_dec(plan->entry_count); con_crlf();
  con_puta("Payload bytes described: "); con_put_dec(plan->total_payload_bytes); con_crlf();
  for (uint32_t i = 0; i < plan->store_count; ++i) {
    FFU_STORE *s = &plan->stores[i];
    con_puta("Store "); con_put_dec(i);
    con_puta(": block="); con_put_dec(s->block_size);
    con_puta(" descriptors="); con_put_dec(s->descriptor_count);
    con_puta(" payload_base="); con_put_hex(s->payload_base, 12);
    con_puta(" payload_size="); con_put_dec(s->payload_size);
    con_crlf();
  }
  return EFI_SUCCESS;
}

EFI_STATUS ffu_packet_dry_run(FFU_PLAN *plan) {
  uint64_t header_packets;
  uint64_t payload_packets = 0;
  uint32_t in_packet = 0;
  if (!plan) {
    return EFI_INVALID_PARAMETER;
  }
  header_packets = (plan->header_size + UFP_MAX_HEADER_DATA - 1U) / UFP_MAX_HEADER_DATA;
  for (uint32_t i = 0; i < plan->entry_count; ++i) {
    uint32_t left = plan->entries[i].byte_count;
    while (left) {
      uint32_t room = UFP_MAX_PAYLOAD_DATA - in_packet;
      uint32_t take = left < room ? left : room;
      in_packet += take;
      left -= take;
      if (in_packet == UFP_MAX_PAYLOAD_DATA) {
        ++payload_packets;
        in_packet = 0;
      }
    }
  }
  if (in_packet) {
    ++payload_packets;
  }
  ++payload_packets;
  con_puta("Recovered UFP packet plan"); con_crlf();
  con_puta("  Secure header packets: "); con_put_dec(header_packets); con_puta(" max data "); con_put_hex(UFP_MAX_HEADER_DATA, 8); con_crlf();
  con_puta("  Secure payload packets: "); con_put_dec(payload_packets); con_puta(" plus final progress=100 packet"); con_crlf();
  con_puta("  Switch reboot request: 7 bytes NOKXCBR"); con_crlf();
  return EFI_SUCCESS;
}

static EFI_STATUS read_location(UEFI_FILE_VIEW *view, uint64_t descriptor_offset, uint32_t index, uint32_t *method, uint32_t *block) {
  uint8_t loc[8];
  EFI_STATUS st = file_read_exact(view, descriptor_offset + 8ULL + (uint64_t)index * 8ULL, loc, sizeof(loc));
  if (EFI_ERROR(st)) {
    return st;
  }
  *method = rd_le32(loc);
  *block = rd_le32(loc + 4);
  return EFI_SUCCESS;
}

EFI_STATUS ffu_flash_to_block_io(UEFI_FILE_VIEW *view, FFU_PLAN *plan, EFI_BLOCK_IO_PROTOCOL *bio, int commit) {
  EFI_STATUS st = EFI_SUCCESS;
  uint8_t *buffer;
  UINTN buffer_size = 1024U * 1024U;
  uint32_t media_block;
  uint32_t media_id;

  if (!view || !plan || !bio || !bio->Media) {
    return EFI_INVALID_PARAMETER;
  }
  if (!bio->Media->MediaPresent || bio->Media->ReadOnly || !bio->Media->BlockSize) {
    return EFI_UNSUPPORTED;
  }
  media_block = bio->Media->BlockSize;
  media_id = bio->Media->MediaId;
  if (buffer_size < media_block) {
    buffer_size = media_block;
  }
  buffer_size -= buffer_size % media_block;
  buffer = (uint8_t *)uefi_alloc(buffer_size);
  if (!buffer) {
    return EFI_OUT_OF_RESOURCES;
  }

  con_puta(commit ? "Flash mode: COMMIT" : "Flash mode: dry-run"); con_crlf();
  for (uint32_t i = 0; i < plan->entry_count; ++i) {
    FFU_WRITE_ENTRY *entry = &plan->entries[i];
    FFU_STORE *store = &plan->stores[entry->store_index];
    if ((entry->byte_count % media_block) != 0) {
      st = EFI_UNSUPPORTED;
      break;
    }
    for (uint32_t l = 0; l < entry->location_count; ++l) {
      uint32_t method;
      uint32_t block;
      uint64_t target_byte;
      uint64_t lba;
      uint32_t remaining = entry->byte_count;
      uint32_t done = 0;
      st = read_location(view, entry->descriptor_offset, l, &method, &block);
      if (EFI_ERROR(st)) {
        goto out;
      }
      if (method != 0) {
        con_puta("Unsupported non-absolute disk access method in FFU descriptor: ");
        con_put_dec(method);
        con_crlf();
        st = EFI_UNSUPPORTED;
        goto out;
      }
      target_byte = (uint64_t)block * store->block_size;
      if ((target_byte % media_block) != 0) {
        st = EFI_UNSUPPORTED;
        goto out;
      }
      lba = target_byte / media_block;
      if (lba > bio->Media->LastBlock || (entry->byte_count / media_block) > (bio->Media->LastBlock - lba + 1ULL)) {
        st = EFI_BAD_BUFFER_SIZE;
        goto out;
      }
      while (remaining) {
        UINTN take = remaining < buffer_size ? remaining : buffer_size;
        take -= take % media_block;
        if (!take) {
          st = EFI_BAD_BUFFER_SIZE;
          goto out;
        }
        if (commit) {
          st = file_read_exact(view, entry->payload_offset + done, buffer, take);
          if (EFI_ERROR(st)) {
            goto out;
          }
          st = bio->WriteBlocks(bio, media_id, lba + (done / media_block), take, buffer);
          if (EFI_ERROR(st)) {
            goto out;
          }
        }
        remaining -= (uint32_t)take;
        done += (uint32_t)take;
      }
    }
    if ((i % 128U) == 0U || i + 1U == plan->entry_count) {
      con_puta("  descriptor ");
      con_put_dec(i + 1U);
      con_puta("/");
      con_put_dec(plan->entry_count);
      con_crlf();
    }
  }
  if (commit && bio->FlushBlocks) {
    st = bio->FlushBlocks(bio);
  }
out:
  uefi_free(buffer);
  return st;
}

static EFI_STATUS read_location_mem(const uint8_t *header, uint64_t header_size, uint64_t descriptor_offset, uint32_t index, uint32_t *method, uint32_t *block) {
  uint8_t loc[8];
  EFI_STATUS st = mem_read_exact(header, header_size, descriptor_offset + 8ULL + (uint64_t)index * 8ULL, loc, sizeof(loc));
  if (EFI_ERROR(st)) {
    return st;
  }
  *method = rd_le32(loc);
  *block = rd_le32(loc + 4);
  return EFI_SUCCESS;
}

static EFI_STATUS stream_write_completed_entry(FFU_STREAM_FLASH *ctx, FFU_WRITE_ENTRY *entry) {
  EFI_STATUS st;
  FFU_STORE *store = &ctx->plan.stores[entry->store_index];
  for (uint32_t l = 0; l < entry->location_count; ++l) {
    uint32_t method;
    uint32_t block;
    uint64_t target_byte;
    uint64_t lba;
    st = read_location_mem(ctx->header, ctx->header_size, entry->descriptor_offset, l, &method, &block);
    if (EFI_ERROR(st)) {
      return st;
    }
    if (method != 0) {
      return EFI_UNSUPPORTED;
    }
    target_byte = (uint64_t)block * store->block_size;
    if ((target_byte % ctx->media_block) != 0 || (entry->byte_count % ctx->media_block) != 0) {
      return EFI_UNSUPPORTED;
    }
    lba = target_byte / ctx->media_block;
    if (!ctx->dry_run) {
      st = ctx->bio->WriteBlocks(ctx->bio, ctx->media_id, lba, entry->byte_count, ctx->entry_data);
      if (EFI_ERROR(st)) {
        return st;
      }
    }
    ctx->last_sector = (uint32_t)(lba + entry->byte_count / ctx->media_block - 1U);
  }
  return EFI_SUCCESS;
}

EFI_STATUS ffu_stream_flash_init(FFU_STREAM_FLASH *ctx, const uint8_t *header, uint64_t header_size, EFI_BLOCK_IO_PROTOCOL *bio, int commit) {
  EFI_STATUS st;
  uint32_t max_entry = 0;
  if (!ctx || !header || !bio || !bio->Media) {
    return EFI_INVALID_PARAMETER;
  }
  memset(ctx, 0, sizeof(*ctx));
  if (!bio->Media->MediaPresent || bio->Media->ReadOnly || !bio->Media->BlockSize) {
    return EFI_UNSUPPORTED;
  }
  st = ffu_parse_header_bytes(header, header_size, &ctx->plan);
  if (EFI_ERROR(st)) {
    ffu_plan_free(&ctx->plan);
    return st;
  }
  for (uint32_t i = 0; i < ctx->plan.entry_count; ++i) {
    if (ctx->plan.entries[i].byte_count > max_entry) {
      max_entry = ctx->plan.entries[i].byte_count;
    }
  }
  if (!max_entry || max_entry > UFP_MAX_PAYLOAD_DATA) {
    ffu_plan_free(&ctx->plan);
    return EFI_UNSUPPORTED;
  }
  ctx->entry_data = (uint8_t *)uefi_alloc(max_entry);
  if (!ctx->entry_data) {
    ffu_plan_free(&ctx->plan);
    return EFI_OUT_OF_RESOURCES;
  }
  ctx->entry_capacity = max_entry;
  ctx->bio = bio;
  ctx->header = header;
  ctx->header_size = header_size;
  ctx->media_block = bio->Media->BlockSize;
  ctx->media_id = bio->Media->MediaId;
  ctx->dry_run = commit ? 0U : 1U;
  ctx->last_sector = 0xffffffffU;
  return EFI_SUCCESS;
}

void ffu_stream_flash_free(FFU_STREAM_FLASH *ctx) {
  if (!ctx) {
    return;
  }
  uefi_free(ctx->entry_data);
  ffu_plan_free(&ctx->plan);
  memset(ctx, 0, sizeof(*ctx));
}

EFI_STATUS ffu_stream_flash_payload(FFU_STREAM_FLASH *ctx, const uint8_t *data, uint32_t len) {
  EFI_STATUS st;
  uint32_t pos = 0;
  if (!ctx || (!data && len)) {
    return EFI_INVALID_PARAMETER;
  }
  while (pos < len) {
    FFU_WRITE_ENTRY *entry;
    uint32_t need;
    uint32_t take;
    if (ctx->entry_index >= ctx->plan.entry_count) {
      return EFI_BAD_BUFFER_SIZE;
    }
    entry = &ctx->plan.entries[ctx->entry_index];
    need = entry->byte_count - ctx->entry_received;
    take = (len - pos) < need ? (len - pos) : need;
    if (ctx->entry_received + take > ctx->entry_capacity) {
      return EFI_BAD_BUFFER_SIZE;
    }
    memcpy(ctx->entry_data + ctx->entry_received, data + pos, take);
    ctx->entry_received += take;
    pos += take;
    if (ctx->entry_received == entry->byte_count) {
      st = stream_write_completed_entry(ctx, entry);
      if (EFI_ERROR(st)) {
        return st;
      }
      ctx->entry_received = 0;
      ++ctx->entry_index;
    }
  }
  if (ctx->entry_index >= ctx->plan.entry_count) {
    ctx->final_state = 1;
  }
  return EFI_SUCCESS;
}

int ffu_stream_flash_done(FFU_STREAM_FLASH *ctx) {
  return ctx && ctx->entry_index >= ctx->plan.entry_count && ctx->entry_received == 0;
}
