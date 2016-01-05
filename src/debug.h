#ifndef DEBUG_H_
#define DEBUG_H_

#ifdef DEBUG
# define DEBUG_PRINT(x) printf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

void error(int code, char* obj, char* msg); 
void error_fatal(int code, char* obj, char* msg); 
void dump_buf_to_file(char* filename, unsigned char *buf, long length);
void dbg_printbuffer(unsigned char *buf, int buf_len);

#endif
