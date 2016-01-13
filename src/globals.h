#ifndef GLOBALS_H_
#define GLOBALS_H_

#define OUTPUT_DIRECTORY "pnglitch_output"

//Need to be global for custom libpng write/read fns
extern long long MY_PNG_READ_OFFSET;
extern unsigned char *ENTIRE_PNG_BUF;
extern long long PNG_LENGTH; //Also used for writing

#endif
