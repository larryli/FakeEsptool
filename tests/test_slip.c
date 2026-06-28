/*
 * test_slip.c - Unit tests for SLIP protocol encoder/decoder
 *
 * Tests SLIP framing with various data patterns, escape sequences,
 * and edge cases.
 */

#include "../src/fesptool/slip.h"
#include <stdio.h>
#include <string.h>

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

/* Test helper macro */
#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (condition) {                                                       \
            printf("  PASS: %s\n", message);                                   \
            tests_passed++;                                                    \
        } else {                                                               \
            printf("  FAIL: %s\n", message);                                   \
            tests_failed++;                                                    \
        }                                                                      \
    } while (0)

/* Helper: feed raw bytes to decoder, return true if frame complete */
static bool feed_bytes(fesp_slip_ctx_t *ctx, const uint8_t *data, int len)
{
    bool complete = false;
    for (int i = 0; i < len; i++) {
        if (fesp_slip_put_byte(ctx, data[i])) {
            complete = true;
        }
    }
    return complete;
}

/* ========================================================================
 * Encoder tests
 * ======================================================================== */

/* Test E1: Encode simple data (no special bytes) */
static void test_encode_simple(void)
{
    printf("\nTest E1: Encode simple data\n");

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t out[32] = {0};

    int len = fesp_slip_encode(data, 5, out, sizeof(out));

    TEST_ASSERT(len == 7, "Encoded length is 7 (END + 5 bytes + END)");
    TEST_ASSERT(out[0] == 0xC0, "Starts with END marker");
    TEST_ASSERT(out[len - 1] == 0xC0, "Ends with END marker");
    TEST_ASSERT(memcmp(out + 1, data, 5) == 0, "Payload matches original");
}

/* Test E2: Encode data with 0xC0 (SLIP_END) */
static void test_encode_escape_end(void)
{
    printf("\nTest E2: Encode data with 0xC0\n");

    uint8_t data[] = {0x01, 0xC0, 0x03};
    uint8_t out[32] = {0};

    int len = fesp_slip_encode(data, 3, out, sizeof(out));

    TEST_ASSERT(len == 6, "Encoded length is 6 (END + 1 + ESC+ESC_END + 1 + END)");
    TEST_ASSERT(out[0] == 0xC0, "Starts with END marker");
    TEST_ASSERT(out[1] == 0x01, "First data byte");
    TEST_ASSERT(out[2] == 0xDB, "Escape byte for 0xC0");
    TEST_ASSERT(out[3] == 0xDC, "Escaped END becomes 0xDC");
    TEST_ASSERT(out[4] == 0x03, "Third data byte");
    TEST_ASSERT(out[5] == 0xC0, "Ends with END marker");
}

/* Test E3: Encode data with 0xDB (SLIP_ESC) */
static void test_encode_escape_esc(void)
{
    printf("\nTest E3: Encode data with 0xDB\n");

    uint8_t data[] = {0x01, 0xDB, 0x03};
    uint8_t out[32] = {0};

    int len = fesp_slip_encode(data, 3, out, sizeof(out));

    TEST_ASSERT(len == 6, "Encoded length is 6");
    TEST_ASSERT(out[2] == 0xDB, "Escape byte for 0xDB");
    TEST_ASSERT(out[3] == 0xDD, "Escaped 0xDB becomes 0xDD");
}

/* Test E4: Encode data with both 0xC0 and 0xDB */
static void test_encode_both_escapes(void)
{
    printf("\nTest E4: Encode data with both 0xC0 and 0xDB\n");

    uint8_t data[] = {0xC0, 0xDB};
    uint8_t out[32] = {0};

    int len = fesp_slip_encode(data, 2, out, sizeof(out));

    TEST_ASSERT(len == 6, "Encoded length is 6");
    TEST_ASSERT(out[1] == 0xDB, "First escape byte");
    TEST_ASSERT(out[2] == 0xDC, "Escaped 0xC0");
    TEST_ASSERT(out[3] == 0xDB, "Second escape byte");
    TEST_ASSERT(out[4] == 0xDD, "Escaped 0xDB");
}

/* Test E5: Encode empty data */
static void test_encode_empty(void)
{
    printf("\nTest E5: Encode empty data\n");

    uint8_t out[32] = {0};

    int len = fesp_slip_encode(NULL, 0, out, sizeof(out));

    TEST_ASSERT(len == 2, "Encoded length is 2 (just two END markers)");
    TEST_ASSERT(out[0] == 0xC0, "Starts with END marker");
    TEST_ASSERT(out[1] == 0xC0, "Ends with END marker");
}

/* Test E6: Encode output buffer too small */
static void test_encode_buffer_too_small(void)
{
    printf("\nTest E6: Encode output buffer too small\n");

    uint8_t data[] = {0x01, 0x02, 0x03};
    uint8_t out[2] = {0};

    int len = fesp_slip_encode(data, 3, out, sizeof(out));

    TEST_ASSERT(len == 0, "Returns 0 when buffer too small");
}

/* Test E7: Encode output buffer exactly minimal size (3) */
static void test_encode_buffer_minimal(void)
{
    printf("\nTest E7: Encode output buffer minimal (3 bytes)\n");

    uint8_t data[] = {0x01};
    uint8_t out[3] = {0};

    int len = fesp_slip_encode(data, 1, out, sizeof(out));

    TEST_ASSERT(len == 3, "Returns 3 for single byte");
    TEST_ASSERT(out[0] == 0xC0, "Starts with END");
    TEST_ASSERT(out[1] == 0x01, "Data byte");
    TEST_ASSERT(out[2] == 0xC0, "Ends with END");
}

/* Test E8: Encode truncation (data larger than buffer) */
static void test_encode_truncation(void)
{
    printf("\nTest E8: Encode truncation\n");

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t out[5] = {0};

    /* Buffer: END(1) + 3 data bytes + END(1) = 5 total, 2 bytes silently dropped */
    int len = fesp_slip_encode(data, 5, out, sizeof(out));

    TEST_ASSERT(len == 5, "Returns 5 (END + 3 data + END)");
    TEST_ASSERT(out[0] == 0xC0, "Starts with END");
    TEST_ASSERT(out[1] == 0x01, "First byte kept");
    TEST_ASSERT(out[2] == 0x02, "Second byte kept");
    TEST_ASSERT(out[3] == 0x03, "Third byte kept");
    TEST_ASSERT(out[4] == 0xC0, "Ends with END");
}

/* ========================================================================
 * Decoder tests
 * ======================================================================== */

/* Test D1: Decode simple frame */
static void test_decode_simple(void)
{
    printf("\nTest D1: Decode simple frame\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    uint8_t frame[] = {0xC0, 0x01, 0x02, 0x03, 0xC0};
    bool complete = feed_bytes(&ctx, frame, sizeof(frame));

    TEST_ASSERT(complete, "Frame is complete");
    TEST_ASSERT(fesp_slip_is_complete(&ctx), "is_complete returns true");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 3, "Payload length is 3");
    TEST_ASSERT(
        memcmp(fesp_slip_get_payload(&ctx), "\x01\x02\x03", 3) == 0,
        "Payload matches");
}

/* Test D2: Decode frame with escaped 0xC0 */
static void test_decode_escape_end(void)
{
    printf("\nTest D2: Decode frame with escaped 0xC0\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    uint8_t frame[] = {0xC0, 0x01, 0xDB, 0xDC, 0x03, 0xC0};
    bool complete = feed_bytes(&ctx, frame, sizeof(frame));

    TEST_ASSERT(complete, "Frame is complete");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 3, "Payload length is 3");

    const uint8_t *payload = fesp_slip_get_payload(&ctx);
    TEST_ASSERT(payload[0] == 0x01, "First byte");
    TEST_ASSERT(payload[1] == 0xC0, "Escaped 0xC0 decoded");
    TEST_ASSERT(payload[2] == 0x03, "Third byte");
}

/* Test D3: Decode frame with escaped 0xDB */
static void test_decode_escape_esc(void)
{
    printf("\nTest D3: Decode frame with escaped 0xDB\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    uint8_t frame[] = {0xC0, 0x01, 0xDB, 0xDD, 0x03, 0xC0};
    bool complete = feed_bytes(&ctx, frame, sizeof(frame));

    TEST_ASSERT(complete, "Frame is complete");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 3, "Payload length is 3");

    const uint8_t *payload = fesp_slip_get_payload(&ctx);
    TEST_ASSERT(payload[1] == 0xDB, "Escaped 0xDB decoded");
}

/* Test D4: Decode empty frame (consecutive END markers) */
static void test_decode_empty_frame(void)
{
    printf("\nTest D4: Decode empty frame\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    uint8_t frame[] = {0xC0, 0xC0};
    bool complete = feed_bytes(&ctx, frame, sizeof(frame));

    /* Two consecutive ENDs: decoder requires len > 0 for completion */
    TEST_ASSERT(!complete, "Empty frame not reported as complete");
    TEST_ASSERT(!fesp_slip_is_complete(&ctx), "is_complete returns false");
}

/* Test D5: Decode multiple consecutive frames */
static void test_decode_multiple_frames(void)
{
    printf("\nTest D5: Decode multiple consecutive frames\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    /* Frame 1: 0x01 0x02 */
    uint8_t frame1[] = {0xC0, 0x01, 0x02, 0xC0};
    bool complete = feed_bytes(&ctx, frame1, sizeof(frame1));

    TEST_ASSERT(complete, "Frame 1 complete");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 2, "Frame 1 length is 2");
    TEST_ASSERT(
        memcmp(fesp_slip_get_payload(&ctx), "\x01\x02", 2) == 0,
        "Frame 1 payload matches");

    /* Reset and decode frame 2 */
    fesp_slip_reset(&ctx);

    uint8_t frame2[] = {0xC0, 0x03, 0x04, 0x05, 0xC0};
    complete = feed_bytes(&ctx, frame2, sizeof(frame2));

    TEST_ASSERT(complete, "Frame 2 complete");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 3, "Frame 2 length is 3");
    TEST_ASSERT(
        memcmp(fesp_slip_get_payload(&ctx), "\x03\x04\x05", 3) == 0,
        "Frame 2 payload matches");
}

/* Test D6: Decode frame with multiple escapes */
static void test_decode_multiple_escapes(void)
{
    printf("\nTest D6: Decode frame with multiple escape sequences\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    /* 0xC0, 0xDB, 0xFF - both special bytes */
    uint8_t frame[] = {0xC0, 0xDB, 0xDC, 0xDB, 0xDD, 0xFF, 0xC0};
    bool complete = feed_bytes(&ctx, frame, sizeof(frame));

    TEST_ASSERT(complete, "Frame is complete");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 3, "Payload length is 3");

    const uint8_t *payload = fesp_slip_get_payload(&ctx);
    TEST_ASSERT(payload[0] == 0xC0, "Escaped 0xC0 decoded");
    TEST_ASSERT(payload[1] == 0xDB, "Escaped 0xDB decoded");
    TEST_ASSERT(payload[2] == 0xFF, "Normal byte");
}

/* Test D7: Decode invalid escape sequence */
static void test_decode_invalid_escape(void)
{
    printf("\nTest D7: Decode invalid escape sequence\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    /* 0xDB followed by invalid escape byte 0x00 */
    uint8_t frame[] = {0xC0, 0x01, 0xDB, 0x00, 0x03, 0xC0};
    bool complete = feed_bytes(&ctx, frame, sizeof(frame));

    /* Invalid escape resets the decoder */
    TEST_ASSERT(!complete, "Frame not complete (decoder reset)");
    TEST_ASSERT(!fesp_slip_is_complete(&ctx), "is_complete returns false");
}

/* Test D8: Decode frame preceded by noise (bytes before first END) */
static void test_decode_noise_before_frame(void)
{
    printf("\nTest D8: Decode frame preceded by noise\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    /* Noise bytes before the actual frame */
    uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xC0, 0x01, 0x02, 0xC0};
    bool complete = feed_bytes(&ctx, data, sizeof(data));

    TEST_ASSERT(complete, "Frame complete after noise");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 2, "Payload length is 2");
    TEST_ASSERT(
        memcmp(fesp_slip_get_payload(&ctx), "\x01\x02", 2) == 0,
        "Payload matches");
}

/* Test D9: Decode incomplete frame (no closing END) */
static void test_decode_incomplete(void)
{
    printf("\nTest D9: Decode incomplete frame\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    uint8_t data[] = {0xC0, 0x01, 0x02, 0x03};
    bool complete = feed_bytes(&ctx, data, sizeof(data));

    TEST_ASSERT(!complete, "Frame not complete");
    TEST_ASSERT(fesp_slip_is_complete(&ctx) == false,
                "is_complete returns false");
}

/* Test D10: Decode byte-by-byte (minimal input) */
static void test_decode_byte_by_byte(void)
{
    printf("\nTest D10: Decode byte-by-byte\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    uint8_t frame[] = {0xC0, 0x41, 0xC0}; /* 'A' */
    bool complete = false;

    for (int i = 0; i < 3; i++) {
        complete = fesp_slip_put_byte(&ctx, frame[i]);
    }

    TEST_ASSERT(complete, "Frame complete byte-by-byte");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 1, "Payload length is 1");
    TEST_ASSERT(fesp_slip_get_payload(&ctx)[0] == 0x41, "Payload is 'A'");
}

/* Test D11: Reset clears state */
static void test_reset_clears_state(void)
{
    printf("\nTest D11: Reset clears state\n");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);

    /* Feed partial frame */
    fesp_slip_put_byte(&ctx, 0xC0);
    fesp_slip_put_byte(&ctx, 0x01);

    /* Reset */
    fesp_slip_reset(&ctx);

    TEST_ASSERT(!fesp_slip_is_complete(&ctx), "Not complete after reset");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 0, "Length is 0 after reset");
}

/* ========================================================================
 * Round-trip tests
 * ======================================================================== */

/* Test R1: Encode then decode simple data */
static void test_roundtrip_simple(void)
{
    printf("\nTest R1: Round-trip simple data\n");

    uint8_t original[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t encoded[32] = {0};

    int enc_len = fesp_slip_encode(original, 5, encoded, sizeof(encoded));
    TEST_ASSERT(enc_len > 0, "Encode succeeds");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);
    bool complete = feed_bytes(&ctx, encoded, enc_len);

    TEST_ASSERT(complete, "Decode complete");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 5, "Length matches");
    TEST_ASSERT(
        memcmp(fesp_slip_get_payload(&ctx), original, 5) == 0,
        "Payload matches original");
}

/* Test R2: Encode then decode data with all special bytes */
static void test_roundtrip_special_bytes(void)
{
    printf("\nTest R2: Round-trip data with special bytes\n");

    uint8_t original[] = {0x00, 0xC0, 0xDB, 0xFF, 0xC0, 0xDB};
    uint8_t encoded[64] = {0};

    int enc_len = fesp_slip_encode(original, 6, encoded, sizeof(encoded));
    TEST_ASSERT(enc_len > 0, "Encode succeeds");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);
    bool complete = feed_bytes(&ctx, encoded, enc_len);

    TEST_ASSERT(complete, "Decode complete");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 6, "Length matches");
    TEST_ASSERT(
        memcmp(fesp_slip_get_payload(&ctx), original, 6) == 0,
        "Payload matches original");
}

/* Test R3: Round-trip empty data (edge case: decoder requires payload) */
static void test_roundtrip_empty(void)
{
    printf("\nTest R3: Round-trip empty data (decoder requires payload)\n");

    uint8_t encoded[32] = {0};

    int enc_len = fesp_slip_encode(NULL, 0, encoded, sizeof(encoded));
    TEST_ASSERT(enc_len == 2, "Encode produces 2 bytes (two END markers)");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);
    bool complete = feed_bytes(&ctx, encoded, enc_len);

    /* Decoder requires len > 0 for completion, empty frame not detected */
    TEST_ASSERT(!complete, "Decoder does not complete on empty frame");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 0, "Length is 0");
}

/* Test R4: Round-trip with only special bytes */
static void test_roundtrip_only_special(void)
{
    printf("\nTest R4: Round-trip with only special bytes\n");

    uint8_t original[] = {0xC0, 0xDB, 0xC0, 0xDB, 0xC0, 0xDB};
    uint8_t encoded[64] = {0};

    int enc_len = fesp_slip_encode(original, 6, encoded, sizeof(encoded));
    TEST_ASSERT(enc_len > 0, "Encode succeeds");

    fesp_slip_ctx_t ctx;
    fesp_slip_init(&ctx);
    bool complete = feed_bytes(&ctx, encoded, enc_len);

    TEST_ASSERT(complete, "Decode complete");
    TEST_ASSERT(fesp_slip_get_length(&ctx) == 6, "Length matches");
    TEST_ASSERT(
        memcmp(fesp_slip_get_payload(&ctx), original, 6) == 0,
        "Payload matches original");
}

/* ========================================================================
 * Main test runner
 * ======================================================================== */

int main(void)
{
    printf("=== SLIP Protocol Encoder/Decoder Tests ===\n");

    /* Encoder tests */
    test_encode_simple();
    test_encode_escape_end();
    test_encode_escape_esc();
    test_encode_both_escapes();
    test_encode_empty();
    test_encode_buffer_too_small();
    test_encode_buffer_minimal();
    test_encode_truncation();

    /* Decoder tests */
    test_decode_simple();
    test_decode_escape_end();
    test_decode_escape_esc();
    test_decode_empty_frame();
    test_decode_multiple_frames();
    test_decode_multiple_escapes();
    test_decode_invalid_escape();
    test_decode_noise_before_frame();
    test_decode_incomplete();
    test_decode_byte_by_byte();
    test_reset_clears_state();

    /* Round-trip tests */
    test_roundtrip_simple();
    test_roundtrip_special_bytes();
    test_roundtrip_empty();
    test_roundtrip_only_special();

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
