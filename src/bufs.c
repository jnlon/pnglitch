#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "debug.h"
#include "bufs.h"

void int_to_four_bytes(uint i, BYTE *buf) {
  buf[0] = ((i >> 24) & 0xFF);
  buf[1] = ((i >> 16) & 0xFF);
  buf[2] = ((i >> 8) & 0xFF);
  buf[3] = (i & 0xFF);
}

unsigned int four_bytes_to_int(BYTE bb[]) {
  uint b1 = bb[0] & 0x0000FF;
  uint b2 = bb[1] & 0x0000FF;
  uint b3 = bb[2] & 0x0000FF;
  uint b4 = bb[3] & 0x0000FF;

  return (uint)((b1 << 24) | (b2 << 16) | (b3 << 8) | b4);
}

void buf_read(unsigned char *dst, unsigned char **src, int to_read) {

  //printf("reading %d\n", to_read);

  for (int i=0;i<to_read;i++) {
    dst[i] = src[0][i];
    //DEBUG_PRINT(("%d ", dst[i]));
  }

  src[0] += to_read;
}

void buf_slice(long start, long len, BYTE* result, BYTE* data)  {
  memcpy(result, &data[start], len);
}

//TODO: I should really be using fmemopen instead. This accomplishes the same task.
//Note: base_offset is incremented automatically, so PNG_LENGTH is incremented here
void append_bytes(BYTE *basebuf, BYTE *inbuf, long long *base_offset, int inbuf_length) {
  BYTE* end_of_buf = basebuf + *base_offset;
  for (int i=0;i<inbuf_length;i++) {
    end_of_buf[i] = inbuf[i];
  }
  (*base_offset) += inbuf_length;
}

