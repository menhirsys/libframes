#define LIBFRAMES_MAX_FRAME_SZ 128
#define LIBFRAMES_RX_RING_FRAMES 10
#define LIBFRAMES_DLE 0x7d
#define LIBFRAMES_XOR 0x20
#define LIBFRAMES_LIM 0x7e
#include "libframes.c"

#include <string.h>

#include "error.h"

void libframes_write_platform(void *p, uint32_t sz) {
    // This is a loopback.
    libframes_inject_rx_ring(p, sz);
}

int main(void) {
    uint32_t frame_sz;

    // Test "frame too small" error.
    // + L L
    //   ---
    {
        char frame[] = {LIBFRAMES_LIM, LIBFRAMES_LIM};
        libframes_inject_rx_ring(frame, sizeof(frame));
        EXPECT(libframes_read_begin(&frame_sz), LIBFRAMES_READ_ERROR_TOO_SMALL);
        EXPECT(libframes_stats.rx_frame_rejected_too_small, 1);
    }

    // Test "bad frame encoding" error.
    // L + L E L
    // -----
    //     -----
    {
        char frame[] = {LIBFRAMES_LIM, LIBFRAMES_DLE, LIBFRAMES_LIM};
        libframes_inject_rx_ring(frame, sizeof(frame));
        // Walk through remnants of previous bad frame.
        EXPECT(libframes_read_begin(&frame_sz), LIBFRAMES_READ_ERROR_TOO_SMALL);
        EXPECT(libframes_stats.rx_frame_rejected_too_small, 2);
        // Bad frame encoding.
        EXPECT(libframes_read_begin(&frame_sz), LIBFRAMES_READ_ERROR_BAD_ENCODING);
        EXPECT(libframes_stats.rx_frame_rejected_encoding_error, 1);
    }

    // Test "no frame yet" response.
    // L + h 0
    // -------
    {
        char frame[] = {'h', 0};
        libframes_inject_rx_ring(frame, sizeof(frame));
        EXPECT(libframes_read_begin(&frame_sz), LIBFRAMES_READ_ERROR_NO_FRAME);
    }

    // Test "bad frame crc8" error.
    // L h 0 + L
    // ---------
    {
        char frame[] = {LIBFRAMES_LIM};
        libframes_inject_rx_ring(frame, sizeof(frame));
        EXPECT(libframes_read_begin(&frame_sz), LIBFRAMES_READ_ERROR_BAD_CRC8);
        EXPECT(libframes_stats.rx_frame_rejected_bad_crc8, 1);
    }

    // Test "frame too big" error. Won't use libframes_inject_rx_ring because
    // then we'd have to calculate crc8 here.
    // L + L . . . L
    // -----
    //     ---------
    {
        EXPECT(libframes_write_begin(), 0);
        char frame[LIBFRAMES_MAX_FRAME_SZ + 1];
        EXPECT(libframes_write(frame, sizeof(frame)), 0);
        EXPECT(libframes_write_end(), 0);
        // Walk through remnants of previous bad frame.
        EXPECT(libframes_read_begin(&frame_sz), LIBFRAMES_READ_ERROR_TOO_SMALL);
        EXPECT(libframes_stats.rx_frame_rejected_too_small, 3);
        // Frame too big.
        EXPECT(libframes_read_begin(&frame_sz), LIBFRAMES_READ_ERROR_TOO_BIG);
        EXPECT(libframes_stats.rx_frame_rejected_too_big, 1);
    }

    // Test that after all of this nonsense, we write and receive a 'hello'
    // frame.
    {
        EXPECT(libframes_write_begin(), 0);
        char hello[] = "hell0";
        EXPECT(libframes_write(hello, sizeof(hello)), 0);
        EXPECT(libframes_write_end(), 0);
        // Walk through remnants of previous bad frame.
        EXPECT(libframes_read_begin(&frame_sz), LIBFRAMES_READ_ERROR_TOO_SMALL);
        EXPECT(libframes_stats.rx_frame_rejected_too_small, 4);
        // Now we should find "hell0\x00".
        EXPECT(libframes_read_begin(&frame_sz), 0);
        EXPECT(frame_sz, sizeof(hello));
        char hello_copy[sizeof(hello)];
        uint32_t hello_copy_sz;
        EXPECT(libframes_read(hello_copy, sizeof(hello_copy), &hello_copy_sz), 0);
        EXPECT(hello_copy_sz, sizeof(hello));
        EXPECT(strcmp(hello, hello_copy), 0);
    }

    // .. and now 

    return 0;
}
