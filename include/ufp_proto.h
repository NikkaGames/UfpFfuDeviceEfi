#ifndef UFP_PROTO_H
#define UFP_PROTO_H

#include <stdint.h>
#include <stddef.h>

#define UFP_MAGIC_N 0x4e
#define UFP_MAGIC_O 0x4f
#define UFP_MAGIC_K 0x4b
#define UFP_MAGIC_X 0x58

#define UFP_STATUS_OK 0
#define UFP_STATUS_INVALID_MSG 33540
#define UFP_STATUS_RESP_INVALID_SIZE 34054
#define UFP_STATUS_RESP_NO_SUBBLOCKS 34055
#define UFP_STATUS_RESP_BAD_STATUS_LEN 34056

#define UFP_MAX_HEADER_DATA 0x1FFBDCU
#define UFP_MAX_PAYLOAD_DATA 0x1FFC18U

#define UFP_SEC_MSG_HEADER 2
#define UFP_SEC_MSG_PAYLOAD 4
#define UFP_SEC_SUBBLOCK_HEADER_V1 0x0b
#define UFP_SEC_SUBBLOCK_PAYLOAD_V1 0x0c
#define UFP_SEC_SUBBLOCK_PAYLOAD_V2 0x1b
#define UFP_SEC_SUBBLOCK_PAYLOAD_V3 0x1d
#define UFP_SEC_SUBBLOCK_HEADER_V2 0x21
#define UFP_SEC_SUBBLOCK_HEADER UFP_SEC_SUBBLOCK_HEADER_V2
#define UFP_SEC_SUBBLOCK_PAYLOAD_WRITE UFP_SEC_SUBBLOCK_PAYLOAD_V2

typedef struct {
  uint16_t status;
  uint32_t specifier;
  uint32_t last_sector;
  uint8_t final_state;
} UFP_RESPONSE;

size_t ufp_build_switch_reboot(uint8_t *out, size_t cap);
uint16_t ufp_parse_switch_response(const uint8_t *msg, size_t len, UFP_RESPONSE *resp);

size_t ufp_build_secure_header_request(uint8_t *out, size_t cap, uint32_t absolute_header_offset, uint32_t file_offset, uint32_t chunk_len);
uint16_t ufp_parse_secure_header_response(const uint8_t *msg, size_t len, UFP_RESPONSE *resp);

size_t ufp_build_secure_payload_request(uint8_t *out, size_t cap, uint8_t progress, uint32_t data_len, int final_packet);
uint16_t ufp_parse_secure_payload_response(const uint8_t *msg, size_t len, int expected_final, UFP_RESPONSE *resp);

size_t ufp_build_async_start_response(uint8_t *out, size_t cap, uint16_t status);
size_t ufp_build_async_write_status(uint8_t *out, size_t cap, uint32_t count, uint32_t start_sector, uint32_t write_status, uint32_t last_sector, uint16_t ufp_status);
size_t ufp_build_async_end_response(uint8_t *out, size_t cap, uint32_t write_status, uint32_t last_sector, uint32_t ufp_status);
size_t ufp_build_secure_status_response(uint8_t *out, size_t cap, uint16_t status, uint32_t specifier, uint32_t first_bad_sector, uint32_t last_sector, uint8_t final_state);

#endif
