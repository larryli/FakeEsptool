/*
 * slip.c - SLIP protocol encoder/decoder implementation
 *
 * Handles SLIP framing for esptool serial communication.
 */

#include "../esptool_hal.h"
#include "slip.h"

#if ENABLE_TRACE
static const char *TAG = "SLIP";
#endif

/*
 * Slip_Init - Initialize SLIP decoder
 *
 * Resets decoder state for receiving a new frame.
 */
void Slip_Init(SLIP_CTX *ctx)
{
    ctx->len = 0;
    ctx->in_frame = FALSE;
    ctx->escaped = FALSE;
}

/*
 * Slip_PutByte - Feed a byte to the decoder
 *
 * Processes one byte of SLIP-encoded data. Handles frame delimiters (0xC0)
 * and escape sequences (0xDB 0xDC, 0xDB 0xDD).
 *
 * @ctx: Pointer to SLIP context
 * @b:   Byte to process
 *
 * Returns TRUE when a complete frame has been received.
 */
BOOL Slip_PutByte(SLIP_CTX *ctx, BYTE b)
{
    if (b == SLIP_END) {
        if (ctx->in_frame && ctx->len > 0) {
            ctx->in_frame = FALSE;
            return TRUE;
        }
        ctx->len = 0;
        ctx->in_frame = TRUE;
        ctx->escaped = FALSE;
        return FALSE;
    }

    if (!ctx->in_frame) {
        return FALSE;
    }

    if (ctx->escaped) {
        if (ctx->len >= SLIP_MAX_FRAME) {
            EsptoolHal_LogD(TAG, "Frame overflow");
            ctx->in_frame = FALSE;
            ctx->len = 0;
            return FALSE;
        }
        switch (b) {
        case SLIP_ESC_END:
            ctx->buf[ctx->len++] = SLIP_END;
            break;
        case SLIP_ESC_ESC:
            ctx->buf[ctx->len++] = SLIP_ESC;
            break;
        default:
            EsptoolHal_LogD(TAG, "Invalid escape: 0x%02X", b);
            ctx->in_frame = FALSE;
            ctx->len = 0;
            return FALSE;
        }
        ctx->escaped = FALSE;
    } else if (b == SLIP_ESC) {
        ctx->escaped = TRUE;
    } else {
        if (ctx->len >= SLIP_MAX_FRAME) {
            EsptoolHal_LogD(TAG, "Frame overflow");
            ctx->in_frame = FALSE;
            ctx->len = 0;
            return FALSE;
        }
        ctx->buf[ctx->len++] = b;
    }

    return FALSE;
}

/*
 * Slip_IsComplete - Check if a complete frame has been received
 *
 * Returns TRUE if decoder has a complete frame ready for processing.
 */
BOOL Slip_IsComplete(const SLIP_CTX *ctx)
{
    return !ctx->in_frame && ctx->len > 0;
}

/*
 * Slip_GetPayload - Get pointer to decoded frame payload
 *
 * Returns pointer to internal buffer containing decoded frame data.
 */
const BYTE *Slip_GetPayload(const SLIP_CTX *ctx) { return ctx->buf; }

/*
 * Slip_GetLength - Get decoded frame length
 *
 * Returns length of decoded frame data in bytes.
 */
int Slip_GetLength(const SLIP_CTX *ctx) { return ctx->len; }

/*
 * Slip_Reset - Reset decoder state
 *
 * Prepares decoder for receiving the next frame.
 */
void Slip_Reset(SLIP_CTX *ctx)
{
    ctx->len = 0;
    ctx->in_frame = FALSE;
    ctx->escaped = FALSE;
}

/*
 * Slip_Encode - Encode data into SLIP frame
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
int Slip_Encode(const BYTE *data, int len, BYTE *out, int out_max)
{
    int pos = 0;

    if (out_max < 3) {
        return 0;
    }
    out[pos++] = SLIP_END;

    for (int i = 0; i < len && pos < out_max - 1; i++) {
        switch (data[i]) {
        case SLIP_END:
            out[pos++] = SLIP_ESC;
            out[pos++] = SLIP_ESC_END;
            break;
        case SLIP_ESC:
            out[pos++] = SLIP_ESC;
            out[pos++] = SLIP_ESC_ESC;
            break;
        default:
            out[pos++] = data[i];
            break;
        }
    }

    if (pos >= out_max) {
        return 0;
    }
    out[pos++] = SLIP_END;

    return pos;
}
