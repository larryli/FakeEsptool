/*
 * slip.c - SLIP protocol encoder/decoder implementation
 *
 * Handles SLIP framing for esptool serial communication.
 */

#include "slip.h"
#include "../fesptool_hal.h"

#if ENABLE_TRACE
static const char *TAG = "SLIP";
#endif

/*
 * fesp_slip_init - Initialize SLIP decoder
 *
 * Resets decoder state for receiving a new frame.
 */
void fesp_slip_init(fesp_slip_ctx_t *ctx)
{
    ctx->len = 0;
    ctx->in_frame = false;
    ctx->escaped = false;
}

/*
 * fesp_slip_put_byte - Feed a uint8_t to the decoder
 *
 * Processes one uint8_t of SLIP-encoded data. Handles frame delimiters (0xC0)
 * and escape sequences (0xDB 0xDC, 0xDB 0xDD).
 *
 * @ctx: Pointer to SLIP context
 * @b:   uint8_t to process
 *
 * Returns true when a complete frame has been received.
 */
bool fesp_slip_put_byte(fesp_slip_ctx_t *ctx, uint8_t b)
{
    if (b == FESP_SLIP_END) {
        if (ctx->in_frame && ctx->len > 0) {
            ctx->in_frame = false;
            return true;
        }
        ctx->len = 0;
        ctx->in_frame = true;
        ctx->escaped = false;
        return false;
    }

    if (!ctx->in_frame) {
        return false;
    }

    if (ctx->escaped) {
        if (ctx->len >= FESP_SLIP_MAX_FRAME) {
            FESP_HAL_LOGD(TAG, "Frame overflow");
            ctx->in_frame = false;
            ctx->len = 0;
            return false;
        }
        switch (b) {
        case FESP_SLIP_ESC_END:
            ctx->buf[ctx->len++] = FESP_SLIP_END;
            break;
        case FESP_SLIP_ESC_ESC:
            ctx->buf[ctx->len++] = FESP_SLIP_ESC;
            break;
        default:
            FESP_HAL_LOGD(TAG, "Invalid escape: 0x%02X", b);
            ctx->in_frame = false;
            ctx->len = 0;
            return false;
        }
        ctx->escaped = false;
    } else if (b == FESP_SLIP_ESC) {
        ctx->escaped = true;
    } else {
        if (ctx->len >= FESP_SLIP_MAX_FRAME) {
            FESP_HAL_LOGD(TAG, "Frame overflow");
            ctx->in_frame = false;
            ctx->len = 0;
            return false;
        }
        ctx->buf[ctx->len++] = b;
    }

    return false;
}

/*
 * fesp_slip_is_complete - Check if a complete frame has been received
 *
 * Returns true if decoder has a complete frame ready for processing.
 */
bool fesp_slip_is_complete(const fesp_slip_ctx_t *ctx)
{
    return !ctx->in_frame && ctx->len > 0;
}

/*
 * fesp_slip_get_payload - Get pointer to decoded frame payload
 *
 * Returns pointer to internal buffer containing decoded frame data.
 */
const uint8_t *fesp_slip_get_payload(const fesp_slip_ctx_t *ctx)
{
    return ctx->buf;
}

/*
 * fesp_slip_get_length - Get decoded frame length
 *
 * Returns length of decoded frame data in bytes.
 */
int fesp_slip_get_length(const fesp_slip_ctx_t *ctx) { return ctx->len; }

/*
 * fesp_slip_reset - Reset decoder state
 *
 * Prepares decoder for receiving the next frame.
 */
void fesp_slip_reset(fesp_slip_ctx_t *ctx)
{
    ctx->len = 0;
    ctx->in_frame = false;
    ctx->escaped = false;
}

/*
 * fesp_slip_encode - Encode data into SLIP frame
 *
 * Wraps data with SLIP frame delimiters and escapes special bytes.
 *
 * @data:    Pointer to data to encode
 * @len:     Length of data in bytes
 * @out:     Output buffer for encoded frame
 * @out_max: Size of output buffer
 *
 * Returns encoded frame length, or 0 on error (buffer too small).
 */
int fesp_slip_encode(const uint8_t *data, int len, uint8_t *out, int out_max)
{
    int pos = 0;

    if (out_max < 3) {
        return 0;
    }
    out[pos++] = FESP_SLIP_END;

    for (int i = 0; i < len && pos < out_max - 1; i++) {
        switch (data[i]) {
        case FESP_SLIP_END:
            out[pos++] = FESP_SLIP_ESC;
            out[pos++] = FESP_SLIP_ESC_END;
            break;
        case FESP_SLIP_ESC:
            out[pos++] = FESP_SLIP_ESC;
            out[pos++] = FESP_SLIP_ESC_ESC;
            break;
        default:
            out[pos++] = data[i];
            break;
        }
    }

    if (pos >= out_max) {
        return 0;
    }
    out[pos++] = FESP_SLIP_END;

    return pos;
}
