#include "ufp_proto.h"
#include "ufp_base.h"

static int has_magic(const uint8_t *msg, size_t len, uint8_t app, uint8_t ext) {
  return len >= 6 &&
         msg[0] == UFP_MAGIC_N &&
         msg[1] == UFP_MAGIC_O &&
         msg[2] == UFP_MAGIC_K &&
         msg[3] == UFP_MAGIC_X &&
         msg[4] == app &&
         msg[5] == ext;
}

size_t ufp_build_switch_reboot(uint8_t *out, size_t cap) {
  if (!out || cap < 7) {
    return 0;
  }
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'K';
  out[3] = 'X';
  out[4] = 'C';
  out[5] = 'B';
  out[6] = 'R';
  return 7;
}

uint16_t ufp_parse_switch_response(const uint8_t *msg, size_t len, UFP_RESPONSE *resp) {
  if (resp) {
    memset(resp, 0, sizeof(*resp));
  }
  if (!has_magic(msg, len, 'C', 'B') || len < 12) {
    return UFP_STATUS_INVALID_MSG;
  }
  if (resp) {
    resp->status = rd_be16(msg + 6);
    resp->specifier = rd_be32(msg + 8);
  }
  return rd_be16(msg + 6);
}

size_t ufp_build_secure_header_request(uint8_t *out, size_t cap, uint32_t absolute_header_offset, uint32_t file_offset, uint32_t chunk_len) {
  if (!out || cap < (size_t)chunk_len + 60 || chunk_len > UFP_MAX_HEADER_DATA) {
    return 0;
  }
  memset(out, 0, 60);
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'K';
  out[3] = 'X';
  out[4] = 'F';
  out[5] = 'S';
  out[6] = 0;
  out[7] = UFP_SEC_MSG_HEADER;
  out[11] = 1;
  out[15] = UFP_SEC_SUBBLOCK_HEADER;
  wr_be32(out + 16, chunk_len + 40);
  wr_be32(out + 20, absolute_header_offset);
  wr_be32(out + 24, file_offset + chunk_len);
  out[28] = 0;
  wr_be32(out + 29, file_offset);
  wr_be32(out + 33, chunk_len);
  out[37] = 0;
  return (size_t)chunk_len + 60;
}

uint16_t ufp_parse_secure_header_response(const uint8_t *msg, size_t len, UFP_RESPONSE *resp) {
  if (resp) {
    memset(resp, 0, sizeof(*resp));
  }
  if (!has_magic(msg, len, 'F', 'S') || len < 16) {
    return UFP_STATUS_INVALID_MSG;
  }
  if (resp) {
    resp->status = rd_be16(msg + 6);
    resp->specifier = rd_be32(msg + 8);
  }
  return rd_be16(msg + 6);
}

size_t ufp_build_secure_payload_request(uint8_t *out, size_t cap, uint8_t progress, uint32_t data_len, int final_packet) {
  if (!out || cap < (size_t)data_len + 32 || data_len > UFP_MAX_PAYLOAD_DATA) {
    return 0;
  }
  memset(out, 0, 32);
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'K';
  out[3] = 'X';
  out[4] = 'F';
  out[5] = 'S';
  out[6] = 0;
  out[7] = UFP_SEC_MSG_PAYLOAD;
  out[8] = final_packet ? 100 : progress;
  out[11] = 1;
  out[15] = UFP_SEC_SUBBLOCK_PAYLOAD_WRITE;
  wr_be32(out + 16, data_len + 12);
  wr_be32(out + 20, data_len);
  out[24] = 0;
  return (size_t)data_len + 32;
}

uint16_t ufp_parse_secure_payload_response(const uint8_t *msg, size_t len, int expected_final, UFP_RESPONSE *resp) {
  uint16_t status;
  uint32_t sb_len;
  if (resp) {
    memset(resp, 0, sizeof(*resp));
  }
  if (len < 16) {
    return UFP_STATUS_RESP_INVALID_SIZE;
  }
  if (!has_magic(msg, len, 'F', 'S')) {
    return UFP_STATUS_INVALID_MSG;
  }
  status = rd_be16(msg + 6);
  if (resp) {
    resp->status = status;
    resp->specifier = len >= 12 ? rd_be32(msg + 8) : 0;
  }
  if (status != 0) {
    return status;
  }
  if (msg[15] == 0) {
    return UFP_STATUS_RESP_NO_SUBBLOCKS;
  }
  if (len < 0x24) {
    return UFP_STATUS_RESP_BAD_STATUS_LEN;
  }
  sb_len = rd_be32(msg + 16);
  if (sb_len != 14) {
    return UFP_STATUS_RESP_BAD_STATUS_LEN;
  }
  if (resp) {
    resp->last_sector = rd_be32(msg + 28);
    resp->final_state = len > 32 ? msg[32] : 0;
  }
  if (expected_final && len > 32 && msg[32] != 1) {
    return msg[32];
  }
  return UFP_STATUS_OK;
}

size_t ufp_build_async_start_response(uint8_t *out, size_t cap, uint16_t status) {
  if (!out || cap < 9) {
    return 0;
  }
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'K';
  out[3] = 'X';
  out[4] = 'F';
  out[5] = 'F';
  out[6] = 'S';
  wr_be16(out + 7, status);
  return 9;
}

size_t ufp_build_async_write_status(uint8_t *out, size_t cap, uint32_t count, uint32_t start_sector, uint32_t write_status, uint32_t last_sector, uint16_t ufp_status) {
  if (!out || cap < 27) {
    return 0;
  }
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'K';
  out[3] = 'X';
  out[4] = 'F';
  out[5] = 'F';
  out[6] = 'W';
  wr_be16(out + 7, 0);
  wr_be32(out + 9, count);
  wr_be32(out + 13, start_sector);
  wr_be32(out + 17, write_status);
  wr_be32(out + 21, last_sector);
  wr_be16(out + 25, ufp_status);
  return 27;
}

size_t ufp_build_async_end_response(uint8_t *out, size_t cap, uint32_t write_status, uint32_t last_sector, uint32_t ufp_status) {
  if (!out || cap < 21) {
    return 0;
  }
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'K';
  out[3] = 'X';
  out[4] = 'F';
  out[5] = 'F';
  out[6] = 'E';
  wr_be16(out + 7, 0);
  wr_be32(out + 9, write_status);
  wr_be32(out + 13, last_sector);
  wr_be32(out + 17, ufp_status);
  return 21;
}

size_t ufp_build_secure_status_response(uint8_t *out, size_t cap, uint16_t status, uint32_t specifier, uint32_t first_bad_sector, uint32_t last_sector, uint8_t final_state) {
  if (!out || cap < 36) {
    return 0;
  }
  memset(out, 0, 36);
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'K';
  out[3] = 'X';
  out[4] = 'F';
  out[5] = 'S';
  wr_be16(out + 6, status);
  wr_be32(out + 8, specifier);
  out[15] = 1;
  wr_be32(out + 16, 14);
  out[23] = 20;
  wr_be32(out + 24, first_bad_sector);
  wr_be32(out + 28, last_sector);
  out[32] = final_state;
  return 36;
}
