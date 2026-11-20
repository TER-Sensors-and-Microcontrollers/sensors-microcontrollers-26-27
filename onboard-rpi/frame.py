"""
Packet format (little endian):
    MAGIC(2) | VER(1) | SEQ(1) | TIMESTAMP (4) | DIRTY_BITMAP(ceil(N/8)) | SNAPSHOT(FRAME_LEN) | CRC16(2)

- MAGIC:        start bytes

- VER:          hash of LAYOUT so the receiver can detect a format mismatch

- SEQ:          sequence number, starts at 0 and counts up, so reciever can 
                detect dropped packets

- TIMESTAMP:    float, sender time, tbd epoch time or monotonic

- DIRTY_BITMAP: bit i = field i was updated since the last packet that was sent,
                can help detect CAN bus failure

- SNAPSHOT:     the data frame, holds the latest value of every field

- CRC16:        checksum for SNAPSHOT, so receiver can detect corrupted packets
"""
import zlib

CAN_SFF_MASK = 0x000007FF # standard 11-bit id
CAN_EFF_MASK = 0x1FFFFFFF # extended 29-bit id
CAN_EFF_FLAG = 0x80000000 # extended frame flag

MAGIC = b"\xAA\x55"


def std(i):
    """11-bit ID key."""
    return i & CAN_SFF_MASK


"""
We only use standard CAN, but this is here for flexibility.
"""
def ext(i):
    """29-bit ID key (J1939 etc)."""
    return (i & CAN_EFF_MASK) | CAN_EFF_FLAG


# can_id key -> data length (bytes) for that field. Order = field index = bit index.
# len cannot exceed 8 bytes, according to CAN format
FIELDS = [
    (std(0x100), 8),
    (std(0x101), 4),
    (std(0x102), 8),
    (std(0x103), 8)
]

# ----------------------------------------------------------------- derived layout
LAYOUT = {}          # key -> (field_index, offset, length)
_off = 0
for _idx, (_key, _len) in enumerate(FIELDS):
    LAYOUT[_key] = (_idx, _off, _len)
    _off += _len
FRAME_LEN = _off
N_FIELDS = len(FIELDS)
BITMAP_LEN = (N_FIELDS + 7) // 8
LAYOUT_VER = zlib.crc32(repr(FIELDS).encode()) & 0xFF