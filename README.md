# UFP FFU Flash Protocol Notes

## Common Framing

Extended packets start with ASCII `NOKX`. Multi-byte UFP protocol fields are big-endian. The original FFU file structures remain little-endian. Unknown `NOK*` requests return `NOKU`.

Recovered app version strings identify UFP `3.3.5.4` from build `22000.1`. The original supports `usb`, `server`, and `usbkey` transports. This rewrite does not include the OEM USB Function protocol binding; it keeps packet construction/parsing in `src/ufp_proto.c` and provides a direct UEFI Block I/O flashing path.

The app also accepts the legacy `NOKI` hello and `NOKV` info-query messages. `NOKI` echoes `NOKI`. `NOKV` returns a versioned capability list with transfer size, write-buffer size, async support, secure-FFU support, supported secure-FFU protocol bitmap, and app type.

## Switch Mode (`NOKXCB`)

Reboot request:

```text
4e 4f 4b 58 43 42 52    "NOKXCBR"
```

The response is `NOKXCB`, followed by:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 6 | 2 | Status, big-endian |
| 8 | 4 | Specifier, big-endian |

On device-side request handling, byte 6 selects the mode. Recovered values include `R`, `T`, `U`, `W`, and `Z`.

## Common Extension Requests (`NOKXC*`)

The common extension dispatcher accepts:

| Request | Response | Implemented behavior |
| --- | --- | --- |
| `NOKXCB` | 12-byte status/specifier response | Accepts modes `R`, `T`, `U`, `W`, `Z`; unknown modes return status `8` and the mode as specifier |
| `NOKXCC` | 8-byte status response | Clear-screen acknowledgement |
| `NOKXCE` | `NOKXCE` plus echo bytes | Echoes up to the request length at bytes 6..9 from payload offset 10 |
| `NOKXCD` | 12-byte status/length header plus requested payload length | Directory-read frame shape, returns status `38` without platform filesystem backing and status `48` when the requested read exceeds the recovered transfer limits |
| `NOKXCF` | 12-byte status/length header plus requested payload length | File-read frame shape, returns status `38` without platform filesystem backing and status `48` when the requested read exceeds the recovered transfer limits |
| `NOKXCM` | 8-byte status response | Display-message acknowledgement for ids `< 5`, otherwise status `11` |
| `NOKXCP` | 12-byte status/length response | Put-file frame shape, returns status `38` without platform filesystem backing, preserves the requested length field, and returns status `48` when the requested write exceeds the recovered transfer limits |

## Legacy Flash (`NOKF`)

The top-level dispatcher also supports legacy direct flash packets:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 5 | 1 | Target |
| 11 | 4 | Start sector |
| 15 | 4 | Sector count |
| 19 | 1 | Progress byte |
| 24 | 1 | Verify flag |
| 25 | 1 | Skip flag |
| 64 | n | Sector data |

The response is 18 bytes: `NOKF`, status at bytes 5..6, sector count at bytes 10..13, and specifier/write status at bytes 14..17.

## GPT Read (`NOKT`)

`NOKT` returns the beginning of the selected disk so a host can inspect GPT metadata. The response starts with `NOKT`, has status at bytes 6..7, and GPT bytes at offset 8. The original reads 34 blocks for 512-byte media and 6 blocks for 4096-byte media. Unsupported block sizes return status `8`; Block I/O read failure returns status `2` with a zeroed payload.

## Secure FFU Flash (`NOKXFS`)

The USB-key/local flashing workflow sends the FFU in two phases. Bytes 6..7 are a secure-FFU protocol bitmap. The recovered app accepts bitmap `0x001f` in the normal USB path. Byte 11 is the subblock count and must be `1`. The subblock id is a 32-bit big-endian value at bytes 12..15.

### Header Request

Header V1 uses subblock id `0x0000000b` and payload bytes at offset `32`:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 16 | 4 | Subblock length = header length + 12 |
| 20 | 4 | Absolute header offset |
| 24 | 4 | Header length |
| 28 | 1 | Options, high two bits must be clear |
| 29 | 1 | Erase options, must be zero |
| 32 | n | Header bytes |

Header V2 uses subblock id `0x00000021` and payload bytes at offset `60`:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 16 | 4 | Subblock length = chunk length + 40 |
| 20 | 4 | Absolute header offset |
| 24 | 4 | Total header transfer length |
| 28 | 1 | Options, high two bits must be clear |
| 29 | 4 | FFU file offset |
| 33 | 4 | Header chunk length |
| 37 | 1 | Erase options, must be zero |
| 60 | n | Header bytes |

The recovered Header V2 receiver appends chunks at an internal receive index. A zero file offset starts a new header transfer, nonzero offsets require an existing partial header buffer, and an overrun of the total header length returns status `16` with the chunk length as specifier.

Maximum recovered header data per packet: `0x1ffbdc`.

### Payload Request

Payload request variants:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 8 | 1 | Progress percent |
| 16 | 4 | Subblock length |
| 20 | 4 | Data length |
| 24 | 1 | Options, only bit 0 is accepted |

| Subblock | Protocol mask | Overhead | Data offset |
| ---: | ---: | ---: | ---: |
| `0x0000000c` payload V1 | `0x0003` | `8` | `28` |
| `0x0000001b` payload V2 | `0x000c` | `12` | `32` |
| `0x0000001d` payload V3 | `0x0010` | `44` | `64` |

Maximum recovered payload data per packet: `0x1ffc18`.

When all block entries have been sent, the original sends a final 32-byte payload packet with progress `100` and zero data length.

### Payload Response

Short/header responses are 16 bytes: `NOKXFS`, status at bytes 6..7, specifier at bytes 8..11, and four zero bytes at 12..15.

Payload status responses are 36 bytes. Status is at bytes 6..7 and specifier at bytes 8..11. A successful payload status contains one subblock, a subblock length of `14` at bytes 16..19, diagnostic/first sector at bytes 24..27, last sector at bytes 28..31, and final state at byte 32. Final state `1` means flashing completed.

Recovered protocol error constants:

| Value | Meaning |
| ---: | --- |
| `33540` | Invalid message id |
| `34054` | Invalid response size |
| `34055` | Missing subblocks |
| `34056` | Invalid payload status length |

## Async Flash (`NOKXFF`)

Device-side async flash requests are handled by action byte 6.

`S` starts async mode. Request bytes 8 and 9 are protocol version and type. The recovered app accepts version `1`, type `0`, and replies:

```text
NOKXFFS <status16>
```

`W` writes data. Important request fields:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 10 | 1 | Target |
| 12 | 4 | Start sector, big-endian |
| 16 | 4 | Sector count, big-endian |
| 20 | 1 | Sequence/status byte used by the app |
| 25 | 1 | Skip flag |
| 64 | n | Data |

The status message is `NOKXFFW`, 27 bytes, carrying count, start sector, write status, last sector, and UFP status. The original sends this status before performing the write; it reports the previous write result and updates the stored status for the next `W` or final `E`.

`E` ends async mode. The response is `NOKXFFE`, 21 bytes, with write status, last sector, and UFP status.

## Read/Write Parameters (`NOKXFR`/`NOKXFW`)

Host tools can probe capabilities and set small preflight options before secure FFU streaming. Request parameter ids are stored big-endian at bytes 7..10.

Read-parameter responses use this 17-byte header followed by optional data:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 6 | `NOKXFR` |
| 6 | 2 | Status, big-endian |
| 8 | 1 | Zero |
| 9 | 4 | Requested parameter id, big-endian |
| 13 | 4 | Data length, big-endian |
| 17 | n | Data |

Write-parameter responses use the same header shape with `NOKXFW`, the action id at bytes 9..12, zero data length at bytes 13..16, and total length 17.

Recovered parameter ids used by the USB FFU path:

| Id | Meaning | Response data |
| --- | --- | --- |
| `ATRP` (`0x41545250`) | Reset-protection support/version | 9-byte support/version block |
| `FAI\0` (`0x46414900`) | UFP version | 6 bytes: `02 03 03 05 04 00` |
| `DAS\0` (`0x44415300`) | Direct async support | 2 bytes: `00 01` |
| `APPT` (`0x41505054`) | App type | 1 byte: `01` |
| `BF\0\0` (`0x42460000`) | Boot/flashing flag | 1-byte default `00` |
| `BITL` (`0x4249544c`) | BitLocker state | Status `2`, 1-byte zero state without OEM backing |
| `BNFO` (`0x424e464f`) | Build info | NUL-terminated `Date:- Time:- Info:-` |
| `CUFO` (`0x4355464f`) | Current boot option | Status `2`, 2-byte zero boot option without OEM backing |
| `DES\0` (`0x44455300`) | Directory entry size | Status `38`, 8-byte zero size without filesystem backing |
| `DPI\0`/`DPR\0`/`DTI\0` | Platform/device info | Status `2` without OEM SMBIOS/platform helpers |
| `DTSP`/`EMWS` | Verify/write speed | 4-byte zero speed counters |
| `DUI\0`/`SN\0\0` | Device/serial GUID | Status `2`, 16 zero bytes without OEM identity helpers |
| `EMMT` (`0x454d4d54`) | eMMC test result | Status `9` without the OEM memory-card protocol |
| `EMS\0` (`0x454d5300`) | eMMC sector count | 4-byte sector count from selected Block I/O target |
| `FO\0\0` (`0x464f0000`) | FFU options | 500-byte option buffer, persisted from `NOKXFW FO\0\0` |
| `FS\0\0` (`0x46530000`) | Flashing status | 4-byte default status `00000003` |
| `FZ\0\0` (`0x465a0000`) | File size | Status `38`, 8-byte zero size without filesystem backing |
| `GSBS` (`0x47534253`) | Secure boot status | 1-byte default `00` |
| `GUFV`/`GUVS` | UEFI variable read/size | RuntimeServices `GetVariable` backing using GUID at bytes 15..30, declared/value size at bytes 31..34, UTF-16LE name length at bytes 35..38, and name bytes at byte 39 |
| `LGMR`/`SOSM` | Memory information | Status `2`/`9`, 8-byte zero size without OEM memory-map helpers |
| `LZ\0\0` (`0x4c5a0000`) | Log size | 8-byte zero size for known log types, status `8` for unknown log type |
| `MAC\0` (`0x4d414300`) | MAC address | Status `8`, 1-byte zero without network protocol backing |
| `MODE` (`0x4d4f4445`) | Transport/application mode | 4-byte mode flag persisted from `NOKXFW MODE` |
| `SDS\0` (`0x53445300`) | SD/memory-card sector count | Status `10` with a zero 4-byte count when no memory-card target is present |
| `SFPI` (`0x53465049`) | Supported secure-FFU protocols | 6 bytes: protocol format `1`, bitmap `0x001f`, trailing zeros |
| `SMBD` (`0x534d4244`) | SMBIOS data | Status `2`, 4-byte zero length without SMBIOS extraction |
| `SS\0\0` (`0x53530000`) | Security/SFFU/USB summary | 8 bytes with PSB/SFFU placeholders and current USB speed |
| `TELS` (`0x54454c53`) | Telemetry log size | 4-byte zero length |
| `TS\0\0` (`0x54530000`) | Transfer size | 4-byte transport receive size |
| `UBF\0`/`UEBO` | UEFI boot option enumeration | Status `2`, zero count/size without boot-option mutation backing |
| `UKID`/`UKTF` | Unlock id/token files | Status `10` without OEM unlock variables |
| `USBS` (`0x55534253`) | USB speed | 2 bytes: current and maximum speed |
| `pm\0\0` (`0x706d0000`) | Processor manufacturer | Status `2`, 1-byte zero without SMBIOS extraction |
| `WBS\0` (`0x57425300`) | Write-buffer size | 4-byte payload limit |

The write-parameter handler matches the recovered validation for the FFU path: `FO\0\0` requires exactly 500 bytes and updates the in-memory option buffer, `LI\0\0` requires a NUL-terminated string with length <= 200 and matching the declared length, and `MODE` requires byte 15 to be zero before accepting the 32-bit mode value at bytes 16..19. `SUFV` is backed by RuntimeServices `SetVariable` using GUID at bytes 15..30, UTF-16LE name length at bytes 31..34, a fixed 512-byte name field at bytes 35..546, attributes at bytes 547..550, data length at bytes 551..554, and data at byte 555. Known boot mutation ids (`BOCL`, `BOF\0`, `BOL\0`, `OBU\0`) return status `4` without the OEM boot-variable backend. Other ids return status `11`, matching the recovered "unknown action" path.

## Unlock, Relock, Telemetry, And Logs

The UFP extension dispatcher also recognizes non-FFU operations around service unlock/relock and logs:

| Request | Response shape |
| --- | --- |
| `NOKS` | Telemetry start; original returns no response body |
| `NOKN` | Telemetry end; original returns no response body |
| `NOKXFI` | 14 bytes: `NOKXFI`, reserved bytes 6..7, UFP status at 8..9, EFI status at 10..13 |
| `NOKXFO` | 10 bytes: `NOKXFO`, EFI status at 6..9 |
| `NOKXFT` | 12-byte status/length header plus telemetry payload |
| `NOKXFX` | 12-byte status/length header plus log payload |

This rewrite matches those frame layouts. It validates unlock request version `2` and token length, acknowledges relock, and returns empty telemetry/log payloads unless the request size exceeds the recovered transport limits, in which case status `48` is returned.

## FFU Parsing

The file parser in `src/ffu.c` follows the recovered reader:

1. Read a 32-byte security header.
2. Validate signature `SignedImage `, algorithm `0x800c`, nonzero catalog size, and hash table size aligned to 32 bytes.
3. Align `32 + catalog + hash table` to the security chunk size.
4. Read the image header with signature `ImageFlash  ` and size `24` or `28`.
5. Skip manifest and validation descriptors.
6. Parse store headers, write descriptors, and location entries.
7. Locate payload data by aligning after all store headers/descriptors.

The direct Block I/O writer supports only location entries with disk access method `0`, because that maps cleanly to absolute target block writes through UEFI Block I/O.

## USB Function Transport

The original USB path binds the UEFI USB function/device-side protocol, not USB host I/O. The recovered vtable usage matches `EFI_USBFN_IO_PROTOCOL`:

| Offset | Operation |
| ---: | --- |
| `+8` | Detect port |
| `+16` | Configure endpoints |
| `+48` | Abort transfer |
| `+64` | Set endpoint stall state |
| `+72` | Event handler |
| `+80` | Transfer |
| `+88` | Get max transfer size |
| `+96` | Allocate transfer buffer |
| `+104` | Free transfer buffer |
| `+112` | Start controller |
| `+120` | Stop controller |

The event ids recovered from the dispatcher are:

| Id | Meaning |
| ---: | --- |
| `0` | None |
| `1` | Setup packet |
| `2` | Endpoint RX status |
| `3` | Endpoint TX status |
| `4` | Detach |
| `5` | Attach |
| `6` | Reset |
| `7` | Suspend |
| `8` | Resume |
| `9` | USB speed |

The transport uses endpoint 0 for control requests and a vendor-specific two-endpoint bulk interface for UFP messages:

| Endpoint | Direction | Purpose |
| --- | --- | --- |
| `0` | control | descriptors, status stages, stalls |
| `1` | OUT | host-to-device UFP request/message |
| `1` | IN | device-to-host UFP response/message |

The original allocates two large bulk buffers and caps them at `0x241000` bytes. The reimplementation keeps that cap and accepts one UFP message per completed bulk OUT transfer.

The decompiler did not preserve the raw USB descriptor byte arrays (`unk_1800473A8`, `unk_180047410`, `unk_180047490`, `unk_1800474D0`, and related data), so `src/usb_transport.c` reconstructs a WinUSB-compatible vendor interface with Microsoft OS 1.0 descriptors. The packet protocol and flash state machine are reversed from code; the descriptor bytes are reconstructed.

| Descriptor | Implemented |
| --- | --- |
| Device | VID `045e`, PID `062a`, vendor-specific class |
| Configuration | 1 interface, 2 bulk endpoints |
| String `0xee` | `MSFT100`, vendor request `0xaa` |
| Extended compat id | `WINUSB` |
| Extended properties | `DeviceInterfaceGUID` |

If a production host tool filters on the exact original VID/PID/interface GUID, update the constants in `src/usb_transport.c`.

## USB FFU Mode

`UfpFfu.efi --usb-ffu --target=N --yes` runs the device-side FFU loop:

1. Binds `EFI_USBFN_IO_PROTOCOL`.
2. Starts/configures the USB function controller.
3. Responds to EP0 setup traffic and exposes WinUSB bulk endpoints.
4. Answers `NOKXFR/NOKXFW` parameter preflight messages.
5. Receives `NOKXFS` secure-flash header chunks over bulk OUT.
6. Parses the complete FFU header in memory.
7. Receives payload chunks over bulk OUT.
8. Writes completed FFU write descriptors to the selected UEFI Block I/O target.
9. Sends `NOKXFS` status responses over bulk IN.
10. Responds to `NOKXCBR` switch/reboot by acknowledging and exiting the app.

The USB FFU path also implements the recovered `NOKXFF` async start/write/end response shape for hosts that issue direct async flash commands.
