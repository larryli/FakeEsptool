/*
 * slip.c - SLIP protocol encoder/decoder implementation
 *
 * Handles SLIP framing for esptool serial communication.
 */

#include "slip.h"
#include "../utils/trace.h"

#if ENABLE_TRACE
static const char *TAG = "SLIP";
#endif

void Slip_Init(SLIP_CTX *ctx)
{
    ctx->len = 0;
    ctx->in_frame = FALSE;
    ctx->escaped = FALSE;
}

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

    if (!ctx->in_frame)
        return FALSE;

    if (ctx->escaped) {
        if (ctx->len >= SLIP_MAX_FRAME) {
            TRACE_FW(TAG, "Frame overflow");
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
            TRACE_FW(TAG, "Invalid escape: 0x%02X", b);
            ctx->in_frame = FALSE;
            ctx->len = 0;
            return FALSE;
        }
        ctx->escaped = FALSE;
    } else if (b == SLIP_ESC) {
        ctx->escaped = TRUE;
    } else {
        if (ctx->len >= SLIP_MAX_FRAME) {
            TRACE_FW(TAG, "Frame overflow");
            ctx->in_frame = FALSE;
            ctx->len = 0;
            return FALSE;
        }
        ctx->buf[ctx->len++] = b;
    }

    return FALSE;
}

BOOL Slip_IsComplete(const SLIP_CTX *ctx)
{
    return !ctx->in_frame && ctx->len > 0;
}

const BYTE *Slip_GetPayload(const SLIP_CTX *ctx)
{
    return ctx->buf;
}

int Slip_GetLength(const SLIP_CTX *ctx)
{
    return ctx->len;
}

void Slip_Reset(SLIP_CTX *ctx)
{
    ctx->len = 0;
    ctx->in_frame = FALSE;
    ctx->escaped = FALSE;
}

int Slip_Encode(const BYTE *data, int len, BYTE *out, int out_max)
{
    int pos = 0;

    if (out_max < 1)
        return 0;
    out[pos++] = SLIP_END;

    for (int i = 0; i < len && pos < out_max - 2; i++) {
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

    if (pos >= out_max)
        return 0;
    out[pos++] = SLIP_END;

    return pos;
}
