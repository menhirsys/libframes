#include "libframes.h"

void libframes_write_platform(void *, uint32_t);

libframes_stats_t libframes_stats = {
    .min_rx_frame_sz = 666,
    .write_frame_min_sz = 666
};

#define LIBFRAMES_RX_RING_SZ (LIBFRAMES_MAX_FRAME_SZ * LIBFRAMES_RX_RING_FRAMES)
static char rx_ring[LIBFRAMES_RX_RING_SZ];
static uint32_t rx_ring_tail = 0;
static uint32_t rx_ring_unread = 0;

static enum {
    NOT_READING,
    READING
} read_state = NOT_READING;

static uint8_t crc8_table[256];

static char frame_buffer[LIBFRAMES_MAX_FRAME_SZ];
static uint32_t frame_buffer_sz;
static uint32_t frame_buffer_off;

void libframes_inject_rx_ring(void *p, uint32_t sz) {
    uint32_t off = 0;
    while (rx_ring_unread < LIBFRAMES_RX_RING_SZ && off < sz) {
        uint32_t rx_ring_head = rx_ring_tail + rx_ring_unread;
        if (rx_ring_head >= LIBFRAMES_RX_RING_SZ) {
            rx_ring_head -= LIBFRAMES_RX_RING_SZ;
        }
        rx_ring[rx_ring_head] = ((char *)p)[off];
        rx_ring_unread++;
        off++;
    }
}

int libframes_read_begin(uint32_t *p_sz) {
    // Check that we are not already reading a frame.
    if (read_state == READING) {
        return LIBFRAMES_ERROR_NOT_READY;
    }

    uint32_t original_rx_ring_tail = rx_ring_tail;
    uint32_t original_rx_ring_unread = rx_ring_unread;

    int limit_bytes_found = 0;
    frame_buffer_sz = 0;
    frame_buffer_off = 0;
    uint8_t running_crc8 = 0;
    int prev_was_dle = 0;
    while (rx_ring_unread > 0) {
        if (rx_ring[rx_ring_tail] == LIBFRAMES_LIM) {
            limit_bytes_found++;
            // If we got a frame begin and a frame end limit byte, we found a
            // complete frame.
            if (limit_bytes_found == 2) {
                // Was the last byte a DLE? Then the frame was encoded badly.
                if (prev_was_dle) {
                    libframes_stats.rx_frame_rejected_encoding_error++;
                    return LIBFRAMES_READ_ERROR_BAD_ENCODING;
                }
                // Is it at least the minimum frame? Must at least have crc8.
                if (frame_buffer_sz < 1) {
                    libframes_stats.rx_frame_rejected_too_small++;
                    return LIBFRAMES_READ_ERROR_TOO_SMALL;
                }
                // running_crc8 should have been xor'ed with the frame crc8,
                // thus it should be 0.
                if (running_crc8 != 0) {
                    libframes_stats.rx_frame_rejected_bad_crc8++;
                    return LIBFRAMES_READ_ERROR_BAD_CRC8;
                }
                frame_buffer_sz--;
                // Update some more frame statistics.
                libframes_stats.rx_frame_count++;
                if (frame_buffer_sz < libframes_stats.min_rx_frame_sz) {
                    libframes_stats.min_rx_frame_sz = frame_buffer_sz;
                }
                if (frame_buffer_sz > libframes_stats.max_rx_frame_sz) {
                    libframes_stats.max_rx_frame_sz = frame_buffer_sz;
                }
                // Read past the terminating LIM.
                rx_ring_tail = rx_ring_tail + 1 == LIBFRAMES_RX_RING_SZ ? 0 : rx_ring_tail + 1;
                rx_ring_unread--;
                // Ready to read frame!
                read_state = READING;
                *p_sz = frame_buffer_sz;
                return 0;
            }
        } else if (limit_bytes_found == 1) {
            // Is the frame too big yet?
            if (frame_buffer_sz == LIBFRAMES_MAX_FRAME_SZ) {
                libframes_stats.rx_frame_rejected_too_big++;
                return LIBFRAMES_READ_ERROR_TOO_BIG;
            }
            // These are frame contents; store in frame_buffer after decoding.
            if (rx_ring[rx_ring_tail] == LIBFRAMES_DLE) {
                prev_was_dle = 1;
            } else {
                uint8_t b;
                if (prev_was_dle) {
                    prev_was_dle = 0;
                    b = rx_ring[rx_ring_tail] ^ LIBFRAMES_XOR;
                } else {
                    b = rx_ring[rx_ring_tail];
                }
                frame_buffer[frame_buffer_sz++] = rx_ring[rx_ring_tail];
                running_crc8 = crc8_table[running_crc8 ^ b];
            }
        } else if (limit_bytes_found == 0) {
            // We've received a byte, but it's not in a frame and it's not a
            // frame delimiter.
            libframes_stats.rx_false_starts++;
        }

        rx_ring_tail = rx_ring_tail + 1 == LIBFRAMES_RX_RING_SZ ? 0 : rx_ring_tail + 1;
        rx_ring_unread--;
    }

    // Tail reached head without finding a complete frame!
    rx_ring_tail = original_rx_ring_tail;
    rx_ring_unread = original_rx_ring_unread;
    return LIBFRAMES_READ_ERROR_NO_FRAME;
}

int libframes_read(void *p, uint32_t sz, uint32_t *sz_read) {
    // Check that we are reading a frame.
    if (read_state != READING) {
        return LIBFRAMES_ERROR_NOT_READY;
    }

    // Read up to sz bytes into p, updating sz_read.
    *sz_read = 0;
    while (frame_buffer_off < frame_buffer_sz && sz > 0) {
        ((char *)p)[*sz_read] = frame_buffer[frame_buffer_off];
        (*sz_read)++;
        frame_buffer_off++;
        sz--;
        libframes_stats.read_byte_count++;
    }

    // Did we want to read more but there wasn't enough frame data?
    if (sz > 0) {
        libframes_stats.read_overreach++;
    }

    return 0;
}

int libframes_read_exact(void *p, uint32_t sz) {
    uint32_t sz_read;
    int err;
    // If libframes_read returns an error, pass it on.
    if ((err = libframes_read(p, sz, &sz_read))) {
        return err;
    }
    // If we didn't read as much as we wanted, that's an error too.
    if (sz_read != sz) {
        return LIBFRAMES_READ_ERROR_NOT_ENOUGH;
    }
    return 0;
}

uint32_t libframes_read_end(void) {
    // Check that we are reading a frame.
    if (read_state == NOT_READING) {
        return LIBFRAMES_ERROR_NOT_READY;
    }

    // Update some stats.
    if (frame_buffer_sz > 0) {
        libframes_stats.read_discard_frame_count++;
    }
    uint32_t frame_bytes_remaining = frame_buffer_sz - frame_buffer_off;
    libframes_stats.read_discard_byte_count += frame_bytes_remaining;
    libframes_stats.read_byte_count += frame_bytes_remaining;

    // Not reading anymore.
    read_state = NOT_READING;

    return 0;
}

static enum {
    NOT_WRITING,
    WRITING
} write_state = NOT_WRITING;
static uint8_t writing_running_crc8;
static uint32_t writing_frame_sz;

int libframes_write_begin(void) {
    if (write_state == WRITING) {
        return LIBFRAMES_ERROR_NOT_READY;
    }

    write_state = WRITING;
    writing_running_crc8 = 0;

    // Write out the first LIM.
    uint8_t b = LIBFRAMES_LIM;
    libframes_write_platform(&b, 1);
    writing_frame_sz = 1;

    return 0;
}

int libframes_write(void *p, uint32_t sz) {
    if (write_state == NOT_WRITING) {
        return LIBFRAMES_ERROR_NOT_READY;
    }

    uint32_t off;
    for (off = 0; off < sz; off++) {
        char b = ((char *)p)[off];
        writing_running_crc8 = crc8_table[writing_running_crc8 ^ b];
        if (b == LIBFRAMES_DLE || b == LIBFRAMES_LIM) {
            uint8_t escaped[] = {LIBFRAMES_DLE, b ^ LIBFRAMES_XOR};
            libframes_write_platform(&escaped, sizeof(escaped));
            writing_frame_sz += 2;
        } else {
            libframes_write_platform(&b, 1);
            writing_frame_sz += 1;
        }
    }

    libframes_stats.write_byte_count += sz;

    return 0;
}

int libframes_write_end(void) {
    if (write_state == NOT_WRITING) {
        return LIBFRAMES_ERROR_NOT_READY;
    }

    // Write out the crc8 and the closing LIM.
    libframes_write(&writing_running_crc8, 1);
    uint8_t b = LIBFRAMES_LIM;
    libframes_write_platform(&b, 1);
    writing_frame_sz++;
    libframes_stats.write_byte_count++;

    // Update some stats.
    libframes_stats.write_frame_count++;
    if (writing_frame_sz < libframes_stats.write_frame_min_sz) {
        libframes_stats.write_frame_min_sz = writing_frame_sz;
    }
    if (writing_frame_sz > libframes_stats.write_frame_max_sz) {
        libframes_stats.write_frame_max_sz = writing_frame_sz;
    }

    write_state = NOT_WRITING;
    return 0;
}

static uint8_t crc8_table[256] = {
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
    0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
    0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65,
    0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
    0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5,
    0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
    0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85,
    0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
    0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2,
    0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
    0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2,
    0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
    0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32,
    0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
    0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42,
    0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
    0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c,
    0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
    0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec,
    0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
    0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
    0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
    0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c,
    0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
    0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b,
    0x76, 0x71, 0x78, 0x7f, 0x6a, 0x6d, 0x64, 0x63,
    0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b,
    0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
    0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb,
    0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
    0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb,
    0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3
};
