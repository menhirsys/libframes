#define LIBFRAMES_MAX_FRAME_SZ 128
#define LIBFRAMES_RX_RING_FRAMES 10
#define LIBFRAMES_DLE 0x7d
#define LIBFRAMES_XOR 0x20
#define LIBFRAMES_LIM 0x7e
#include "libframes.c"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "error.h"

void libframes_write_platform(void *p, uint32_t sz) {
    // This is a loopback.
    libframes_inject_rx_ring(p, sz);
}

void stress_test(uint32_t);

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
        EXPECT(libframes_read_end(), 0);
        EXPECT(hello_copy_sz, sizeof(hello));
        EXPECT(strcmp(hello, hello_copy), 0);
    }

    puts("handwritten tests all done!");

    // 5s of stress test, where we repeatedly overfill the rx buffer.
    puts("stress test, overfilling rx buffer");
    stress_test(LIBFRAMES_RX_RING_SZ);
    libframes_stats_t libframes_stats_copy = libframes_stats;

    // Hack: reset.
    rx_ring_tail = 0;
    rx_ring_unread = 0;

    // 5s of testing that there are *no* errors when we don't overfill the rx
    // buffer.
    puts("stress test, not overfilling rx buffer");
    stress_test(LIBFRAMES_RX_RING_SZ - (2 * LIBFRAMES_MAX_FRAME_SZ));

    // Between this stress test and the last one, we should have handled a lot
    // more frames and bytes. But the error stats should not have increased!
    EXPECT(libframes_stats.rx_false_starts, libframes_stats_copy.rx_false_starts);
    EXPECT(libframes_stats.rx_frame_rejected_encoding_error, libframes_stats_copy.rx_frame_rejected_encoding_error);
    EXPECT(libframes_stats.rx_frame_rejected_too_big, libframes_stats_copy.rx_frame_rejected_too_big);
    EXPECT(libframes_stats.rx_frame_rejected_too_small, libframes_stats_copy.rx_frame_rejected_too_small);
    EXPECT(libframes_stats.rx_frame_rejected_bad_crc8, libframes_stats_copy.rx_frame_rejected_bad_crc8);
    return 0;
}

void stress_test(uint32_t fill_amount) {
    time_t start_time = time(NULL);
    srand(start_time);
    while (time(NULL) - start_time < 5) {
        // Fill up a certain amount of the rx buffer.
        while (rx_ring_unread < fill_amount) {
            // Send a frame of size between 0 and LIBFRAMES_MAX_FRAME_SZ - 1.
            // The frame will actually be much larger because of encoding.
            int frame_sz = rand() % LIBFRAMES_MAX_FRAME_SZ;
            char frame[LIBFRAMES_MAX_FRAME_SZ];
            for (int i = 0; i < frame_sz; i++) {
                // Fill up the frames with a, b, DLE, and LIM.
                char choices[] = {'a', 'b', LIBFRAMES_DLE, LIBFRAMES_LIM};
                frame[i] = choices[rand() % sizeof(choices)];
            }
            EXPECT(libframes_write_begin(), 0);
            EXPECT(libframes_write(frame, frame_sz), 0);
            EXPECT(libframes_write_end(), 0);
        }

        // Read as many frames out as we can.
        int frame_count;
        for (frame_count = 0; ; frame_count++) {
            uint32_t frame_sz;
            int ret = libframes_read_begin(&frame_sz);
            if (ret == LIBFRAMES_READ_ERROR_NO_FRAME) {
                // Nothing left to do.
                break;
            }
            // The first complete frame is probably junk from the time that we
            // overfilled the receive buffer. It's okay if it's garbage.
            if (fill_amount == LIBFRAMES_RX_RING_SZ && frame_count > 0) {
                // Otherwise, since this is a lossless channel, bad crc8 is
                // never okay.
                EXPECT_NOT(ret, LIBFRAMES_READ_ERROR_BAD_CRC8);
                // A badly encoded frame should not be possible, either.
                EXPECT_NOT(ret, LIBFRAMES_READ_ERROR_BAD_ENCODING);
            }
            if (ret == 0) {
                // Everything went well and we got a frame.
                EXPECT(libframes_read_end(), 0);
            }
        }
    }
    
    // Print stats.
    printf("    rx_false_starts = %" PRIu32 "\n", libframes_stats.rx_false_starts);
    printf("    rx_frame_rejected_encoding_error = %" PRIu32 "\n", libframes_stats.rx_frame_rejected_encoding_error);
    // Will never increase because the frames are all below LIBFRAMES_MAX_FRAME_SZ:
    printf("    rx_frame_rejected_too_big = %" PRIu32 "\n", libframes_stats.rx_frame_rejected_too_big);
    printf("    rx_frame_rejected_too_small = %" PRIu32 "\n", libframes_stats.rx_frame_rejected_too_small);
    printf("    rx_frame_rejected_bad_crc8 = %" PRIu32 "\n", libframes_stats.rx_frame_rejected_bad_crc8);
    printf("    rx_frame_count = %" PRIu32 "\n", libframes_stats.rx_frame_count);
    printf("    min_rx_frame_sz = %" PRIu32 "\n", libframes_stats.min_rx_frame_sz);
    printf("    max_rx_frame_sz = %" PRIu32 "\n", libframes_stats.max_rx_frame_sz);
    // Will never increase.
    printf("    read_overreach = %" PRIu32 "\n", libframes_stats.read_overreach);
    printf("    read_discard_frame_count = %" PRIu32 "\n", libframes_stats.read_discard_frame_count);
    printf("    read_discard_byte_count = %" PRIu32 "\n", libframes_stats.read_discard_byte_count);
    printf("    read_byte_count = %" PRIu32 "\n", libframes_stats.read_byte_count);
    printf("    write_frame_min_sz = %" PRIu32 "\n", libframes_stats.write_frame_min_sz);
    printf("    write_frame_max_sz = %" PRIu32 "\n", libframes_stats.write_frame_max_sz);
    printf("    write_frame_count = %" PRIu32 "\n", libframes_stats.write_frame_count);
    printf("    write_byte_count = %" PRIu32 "\n", libframes_stats.write_byte_count);
}
