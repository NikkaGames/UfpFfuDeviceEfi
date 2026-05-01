# UFP FFU Flash Protocol Notes

This is the cleaned protocol recovered from the decompiled ARM64 UEFI app in `../ufpdevicefw.c`.

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
| `FAI\0` (`0x46414900`) | UFP version | 6 bytes: `02 03 03 05 00 04` |
| `DAS\0` (`0x44415300`) | Direct async support | 2 bytes: `00 01` |
| `APPT` (`0x41505054`) | App type | 1 byte: `01` |
| `SFPI` (`0x53465049`) | Supported secure-FFU protocols | 6 bytes: protocol format `1`, bitmap `0x001f`, trailing zeros |
| `SS\0\0` (`0x53530000`) | Security/SFFU/USB summary | 8 bytes with PSB/SFFU placeholders and current USB speed |
| `TS\0\0` (`0x54530000`) | Transfer size | 4-byte transport receive size |
| `USBS` (`0x55534253`) | USB speed | 2 bytes: current and maximum speed |
| `WBS\0` (`0x57425300`) | Write-buffer size | 4-byte payload limit |

The write-parameter handler matches the recovered validation for the FFU path: `FO\0\0` requires exactly 500 bytes, `LI\0\0` requires a NUL-terminated string with length <= 200 and matching the declared length, and `MODE` requires byte 15 to be zero before accepting the 32-bit mode value at bytes 16..19. Other ids return status `11`, matching the recovered "unknown action" path.

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
