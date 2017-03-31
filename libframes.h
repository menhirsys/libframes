#if LIBFRAMES_XOR == 0
    #error LIBFRAMES_XOR cannot be zero.
#endif

#if (LIBFRAMES_DLE ^ LIBFRAMES_XOR) == LIBFRAMES_LIM
    #error DLE xored cannot equal LIM.
#endif

#ifndef __LIBFRAMES_H__
#define __LIBFRAMES_H__

#include <stdint.h>

typedef struct {
    uint32_t
        // Frame didn't begin immediately after the previous frame ended.
        rx_false_starts,
        // Frame was rejected: it had encoding errors.
        rx_frame_rejected_encoding_error,
        // Frame was rejected: it was larger than LIBFRAMES_MAX_FRAME_SZ.
        rx_frame_rejected_too_big,
        // Frame was rejected: it was too small.
        rx_frame_rejected_too_small,
        // Frame was rejected: bad crc8.
        rx_frame_rejected_bad_crc8,
        // Number of valid frames received.
        rx_frame_count,
        // The min and max valid received frame sizes.
        min_rx_frame_sz,
        max_rx_frame_sz,
        // Attempted to read past the end of a frame.
        read_overreach,
        // The number of times discarding a frame was not equivalent to handling it.
        read_discard_frame_count,
        // The number of bytes that have been dropped.
        read_discard_byte_count,
        // The number of bytes handled (dropped or read).
        read_byte_count,
        // The min and max sent frame sizes.
        write_frame_min_sz,
        write_frame_max_sz,
        // The number of frames sent.
        write_frame_count,
        // The number of bytes sent.
        write_byte_count;
} libframes_stats_t;

#define LIBFRAMES_ERROR_NOT_READY 1
#define LIBFRAMES_READ_ERROR_NO_FRAME 2
#define LIBFRAMES_READ_ERROR_NOT_ENOUGH 3

// Negative return codes can be ignored, and are equivalent (for the caller) to
// LIBFRAMES_READ_NO_FRAME.
#define LIBFRAMES_READ_ERROR_BAD_ENCODING -1
#define LIBFRAMES_READ_ERROR_TOO_SMALL -2
#define LIBFRAMES_READ_ERROR_BAD_CRC8 -3
#define LIBFRAMES_READ_ERROR_TOO_BIG -4

// Check whether there is a complete and valid frame available, and if there
// is, make that the "current frame".
int libframes_read_begin(uint32_t *);
// Read up to a certain number of bytes out of the current frame.
int libframes_read(void *, uint32_t, uint32_t *);
// Read exactly a certain number of bytes out of the current frame.
int libframes_read_exact(void *p, uint32_t sz);
// We're done reading, and discard the rest of the current frame.
uint32_t libframes_read_end(void);

// Emit the frame header.
int libframes_write_begin(void);
// Encodes and emits data.
int libframes_write(void *, uint32_t);
// Emits encoded crc8 and footer.
int libframes_write_end(void);

#endif
