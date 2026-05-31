#ifndef ESP_SLIP_H
#define ESP_SLIP_H

#include <windows.h>

#define SLIP_END        0xC0
#define SLIP_ESC        0xDB
#define SLIP_ESC_END    0xDC
#define SLIP_ESC_ESC    0xDD

#define SLIP_MAX_FRAME  4096

typedef struct {
    BYTE buf[SLIP_MAX_FRAME];
    int  len;
    BOOL in_frame;
    BOOL escaped;
} SLIP_CTX;

void Slip_Init(SLIP_CTX *ctx);
BOOL Slip_PutByte(SLIP_CTX *ctx, BYTE b);
BOOL Slip_IsComplete(const SLIP_CTX *ctx);
const BYTE *Slip_GetPayload(const SLIP_CTX *ctx);
int  Slip_GetLength(const SLIP_CTX *ctx);
void Slip_Reset(SLIP_CTX *ctx);

int Slip_Encode(const BYTE *data, int len, BYTE *out, int out_max);

#endif
