/*
 * slip.h - SLIP protocol encoder/decoder
 *
 * Handles SLIP framing for esptool serial communication.
 */

#ifndef ESP_SLIP_H
#define ESP_SLIP_H

#include <windows.h>

/* SLIP protocol constants */
#define SLIP_END        0xC0    /* Frame delimiter */
#define SLIP_ESC        0xDB    /* Escape character */
#define SLIP_ESC_END    0xDC    /* Escaped 0xC0 */
#define SLIP_ESC_ESC    0xDD    /* Escaped 0xDB */

/* Maximum SLIP frame size (must accommodate largest packet: block_size + 16 + 8 header) */
#define SLIP_MAX_FRAME  32768

/* SLIP decoder context */
typedef struct {
    BYTE buf[SLIP_MAX_FRAME]; /* Frame buffer */
    int  len;                 /* Current frame length */
    BOOL in_frame;            /* TRUE if inside a frame */
    BOOL escaped;             /* TRUE if previous byte was ESC */
} SLIP_CTX;

/*
 * Slip_Init - Initialize SLIP decoder
 */
void Slip_Init(SLIP_CTX *ctx);

/*
 * Slip_PutByte - Feed a byte to the decoder
 *
 * Returns TRUE when frame is complete.
 */
BOOL Slip_PutByte(SLIP_CTX *ctx, BYTE b);

/*
 * Slip_IsComplete - Check if a complete frame has been received
 */
BOOL Slip_IsComplete(const SLIP_CTX *ctx);

/*
 * Slip_GetPayload - Get pointer to decoded frame payload
 */
const BYTE *Slip_GetPayload(const SLIP_CTX *ctx);

/*
 * Slip_GetLength - Get decoded frame length
 */
int  Slip_GetLength(const SLIP_CTX *ctx);

/*
 * Slip_Reset - Reset decoder state for next frame
 */
void Slip_Reset(SLIP_CTX *ctx);

/*
 * Slip_Encode - Encode data into SLIP frame
 *
 * Returns encoded length.
 */
int Slip_Encode(const BYTE *data, int len, BYTE *out, int out_max);

#endif
