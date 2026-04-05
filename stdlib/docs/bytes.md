# @stdlib/bytes - Byte Order Utilities

Byte swapping, endianness conversion, and endian-aware buffer I/O for networking protocols, binary file formats, and cross-platform data exchange.

## Import

```hemlock
import { htons, htonl, ntohl, ntohs } from "@stdlib/bytes";
import { bswap16, bswap32, bswap64 } from "@stdlib/bytes";
import { read_u32_be, write_u32_be, is_little_endian } from "@stdlib/bytes";
```

## Byte Swap Functions

### bswap16(val: u16): u16

Unconditionally reverse the bytes of a 16-bit value.

```hemlock
import { bswap16 } from "@stdlib/bytes";
print(bswap16(0x1234));  // 0x3412 = 13330
```

### bswap32(val: u32): u32

Unconditionally reverse the bytes of a 32-bit value.

```hemlock
import { bswap32 } from "@stdlib/bytes";
print(bswap32(0x12345678));  // 0x78563412
```

### bswap64(val: u64): u64

Unconditionally reverse the bytes of a 64-bit value.

```hemlock
import { bswap64 } from "@stdlib/bytes";
print(bswap64(0x0102030405060708));  // 0x0807060504030201
```

## Host-to-Network Byte Order

Convert values from the host's native byte order to network byte order (big-endian). On big-endian systems these are no-ops; on little-endian systems they swap bytes.

### htons(val: u16): u16

Host to network byte order, 16-bit.

```hemlock
import { htons } from "@stdlib/bytes";
let port = htons(8080);  // Ready for network transmission
```

### htonl(val: u32): u32

Host to network byte order, 32-bit.

```hemlock
import { htonl } from "@stdlib/bytes";
let addr = htonl(0xC0A80001);  // 192.168.0.1 in network order
```

### htonll(val: u64): u64

Host to network byte order, 64-bit.

```hemlock
import { htonll } from "@stdlib/bytes";
let timestamp = htonll(1234567890123456789);
```

## Network-to-Host Byte Order

Convert values from network byte order (big-endian) to the host's native byte order. Inverse of the hton* functions.

### ntohs(val: u16): u16

Network to host byte order, 16-bit.

### ntohl(val: u32): u32

Network to host byte order, 32-bit.

### ntohll(val: u64): u64

Network to host byte order, 64-bit.

```hemlock
import { ntohs, ntohl } from "@stdlib/bytes";

// Roundtrip is always identity
let port = 443;
print(ntohs(htons(port)) == port);  // true
```

## Endianness Query

### is_little_endian(): bool

Returns `true` if the system uses little-endian byte order, `false` for big-endian.

```hemlock
import { is_little_endian } from "@stdlib/bytes";
if (is_little_endian()) {
    print("Little-endian system (x86, ARM, etc.)");
} else {
    print("Big-endian system");
}
```

## Endian-Aware Buffer Read

Read multi-byte integers from a pointer or buffer at a given byte offset, interpreting the bytes in the specified endianness. Works with both `ptr` and `buffer` types.

### read_u16_be(p: ptr, offset: i32): u16
### read_u16_le(p: ptr, offset: i32): u16
### read_u32_be(p: ptr, offset: i32): u32
### read_u32_le(p: ptr, offset: i32): u32
### read_u64_be(p: ptr, offset: i32): u64
### read_u64_le(p: ptr, offset: i32): u64

```hemlock
import { read_u32_be, read_u16_le } from "@stdlib/bytes";

let buf = alloc(8);
// Assume buf contains a binary protocol header...
let msg_type = read_u16_le(buf, 0);   // Little-endian field
let length = read_u32_be(buf, 2);     // Big-endian (network order) field
free(buf);
```

## Endian-Aware Buffer Write

Write multi-byte integers to a pointer or buffer at a given byte offset, encoding the bytes in the specified endianness.

### write_u16_be(p: ptr, offset: i32, val: u16): void
### write_u16_le(p: ptr, offset: i32, val: u16): void
### write_u32_be(p: ptr, offset: i32, val: u32): void
### write_u32_le(p: ptr, offset: i32, val: u32): void
### write_u64_be(p: ptr, offset: i32, val: u64): void
### write_u64_le(p: ptr, offset: i32, val: u64): void

```hemlock
import { write_u16_be, write_u32_be } from "@stdlib/bytes";

let packet = alloc(64);
memset(packet, 0, 64);

// Build a network packet header (big-endian)
write_u16_be(packet, 0, 0x0800);  // EtherType: IPv4
write_u32_be(packet, 2, 1024);    // Payload length

free(packet);
```

## Example: DNS Query Header

```hemlock
import { write_u16_be, read_u16_be, htons } from "@stdlib/bytes";

// Build a DNS query header (all fields big-endian)
let header = alloc(12);
memset(header, 0, 12);

write_u16_be(header, 0, 0x1234);  // Transaction ID
write_u16_be(header, 2, 0x0100);  // Flags: standard query
write_u16_be(header, 4, 1);       // Questions: 1
write_u16_be(header, 6, 0);       // Answer RRs: 0
write_u16_be(header, 8, 0);       // Authority RRs: 0
write_u16_be(header, 10, 0);      // Additional RRs: 0

// Read back
let txn_id = read_u16_be(header, 0);
print("Transaction ID: " + txn_id);  // 4660 (0x1234)

free(header);
```
