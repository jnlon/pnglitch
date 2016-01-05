#include <stdio.h>
#include <stdlib.h>
#include "bufs.h"
#include "debug.h"

void error_fatal(int code, char* obj, char* msg) {
  fprintf(stderr,"%s: %s", obj, msg);
  fflush(stderr);
  fflush(stdout);
  exit(code);
}

void error(int code, char* obj, char* msg) {
  fprintf(stderr,"%s: %s", obj, msg);
  fflush(stderr);
  fflush(stdout);
}

void dump_buf_to_file(char* filename, BYTE *buf, long length) {

  FILE *f = fopen(filename, "wb");
  long left = length;
  long offset = 0;

  while (left != 0) {
    long writ = fwrite(buf+offset, sizeof(BYTE), left, f);
    left -= writ;
    offset += writ;
  }

  fclose(f);
  printf("Writ %s\n",filename );
}

void dbg_printbuffer(BYTE *buf, int len) {
#ifdef DEBUG
  DEBUG_PRINT(("\n/// char buffer ///\n"));
  for (int i=0;i<len;i++)
    putchar(buf[i]);
  DEBUG_PRINT(("\n/// int buffer ///\n"));
  for (int i=0;i<len;i++)
    printf("%d ", buf[i]);
  DEBUG_PRINT(("\n///\n"));
#endif
}
