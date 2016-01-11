#define _GNU_SOURCE
#include <png.h>
#include <pngconf.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

#include "libs.h"
#include "globals.h"
#include "bufs.h"
#include "pnglitch.h"
#include "debug.h"

//Globals, needed for callbacks
long long MY_PNG_READ_OFFSET;
unsigned char *ENTIRE_PNG_BUF;
long long PNG_LENGTH; 

#define E_GLITCH "Error while glitching image!\n"
#define E_INVALID "Input file is not a valid PNG!\n"
#define MAX_PATH_LENGTH 256*2
#define NUM_OUTPUT_FILES 7

#ifdef __MINGW32__
#define DIR_SEP "\\"
#define mkdir _mkdir //TODO: make this work, args differ
#endif

#ifdef __linux__
#define DIR_SEP "/"
#endif


/* Use Libpng to tansform the input into into RGB format 
 * (basically re-encode image using all filter methods)
 * Using the write-callback method, store this data in a shared buffer.
 * Then, glitch this buffer manually
 */

int begin(char* base_file_name, unsigned char *png_buf, long long png_length) {

  MY_PNG_READ_OFFSET = 0;
  PNG_LENGTH = png_length; 
  ENTIRE_PNG_BUF = png_buf;

  if (png_sig_cmp(ENTIRE_PNG_BUF, 0, 8) != 0) {
    error(-1, "png_sig_cmp", E_INVALID);
    return -1;
  }

  DEBUG_PRINT(("Initial png size is %lld bytes\n", PNG_LENGTH));

  my_png_meta *pm = calloc(1, sizeof(my_png_meta));
  my_init_libpng(pm);

  //If libpng errors, we end up here
  if (setjmp(png_jmpbuf(pm->read_ptr))) {
    DEBUG_PRINT(("libpng called setjmp!\n"));
    my_deinit_libpng(pm);
    free(ENTIRE_PNG_BUF);
    error(-1, "libpng", "libpng encountered an error\n");
    return -1;
  }

  //Normally a file, but instead make it our buffer
  void *read_io_ptr = png_get_io_ptr(pm->read_ptr);
  png_set_read_fn(pm->read_ptr, read_io_ptr, my_png_read_fn);

  //Transform all PNG image types to RGB
  int transforms = 
    //PNG_TRANSFORM_GRAY_TO_RGB |
    PNG_TRANSFORM_STRIP_ALPHA | 
    PNG_TRANSFORM_EXPAND;

  png_read_png(pm->read_ptr, pm->info_ptr, transforms, NULL);

  //Now that it was read and transformed, its size will differ
  PNG_LENGTH = 0; 

  //Lets collect our metadata
  struct ihdr_infos_s ihdr_infos;
  ihdr_infos.bit_depth        = png_get_bit_depth(pm->read_ptr, pm->info_ptr);
  ihdr_infos.color_type       = png_get_color_type(pm->read_ptr, pm->info_ptr);
  ihdr_infos.filter_method    = png_get_filter_type(pm->read_ptr, pm->info_ptr);
  ihdr_infos.compression_type = png_get_compression_type(pm->read_ptr, pm->info_ptr);
  ihdr_infos.interlace_type   = png_get_interlace_type(pm->read_ptr, pm->info_ptr);
  ihdr_infos.height           = png_get_image_height(pm->read_ptr, pm->info_ptr);
  ihdr_infos.width            = png_get_image_width(pm->read_ptr, pm->info_ptr);

  if (ihdr_infos.color_type != 2) {
    DEBUG_PRINT((E_INVALID));
    free(ENTIRE_PNG_BUF);
    my_deinit_libpng(pm);
    DEBUG_PRINT(("Looks like libpng could not correctly convert to RGB\n"));
    return -1;
  }

  //Just in case we want to enable alpha, etc
  switch(ihdr_infos.color_type) {
    case 0:  //greyscale
    case 3:  //indexed
      ihdr_infos.bytes_per_pixel = 1;
      break;
    case 4: ihdr_infos.bytes_per_pixel = 2; break; //greyscale w/ alpha 
    case 2: ihdr_infos.bytes_per_pixel = 3; break; //Truecolour (RGB)
    case 6: ihdr_infos.bytes_per_pixel = 4; break; //Truecolour w/ alpha
    default: error_fatal(1, "ihdr_infos", "Unknown image type"); //should never happen
  }

  ihdr_infos.scanline_len = (ihdr_infos.bytes_per_pixel * ihdr_infos.width) + 1;

  DEBUG_PRINT(("HEIGHT: %u\n", ihdr_infos.height));
  DEBUG_PRINT(("WIDTH: %u\n", ihdr_infos.width));
  DEBUG_PRINT(("BIT_DEPTH: %u\n", ihdr_infos.bit_depth));

  // Don't compress, since we are merely copying it to memory,
  // we will be decompressing it again anyway
  png_set_compression_level(pm->write_ptr, Z_NO_COMPRESSION);

  void *write_io_ptr = png_get_io_ptr(pm->write_ptr);
  png_set_write_fn(pm->write_ptr, write_io_ptr, my_png_write_fn, my_png_dummy_flush);

  //Make sure we use all filters
  png_set_filter(pm->write_ptr, 0,
      PNG_FILTER_NONE  | PNG_FILTER_VALUE_NONE |
      PNG_FILTER_SUB   | PNG_FILTER_VALUE_SUB  |
      PNG_FILTER_UP    | PNG_FILTER_VALUE_UP   |
      PNG_FILTER_AVG   | PNG_FILTER_VALUE_AVG  |
      PNG_FILTER_PAETH | PNG_FILTER_VALUE_PAETH);

  //Set our comment
  struct png_text_struct comment_struct;

  comment_struct.compression = -1;
  comment_struct.key = " Glitched by pnglitch.xyz ";
  comment_struct.text = NULL;
  comment_struct.text_length = 0;
  
  png_set_text(pm->write_ptr, pm->info_ptr, &comment_struct, 1);

  //Buffer is Written using callback my_png_write_fn to buffer
  //ENTIRE_PNG_BUF. PNG_LENGTH will be updated automatically by it
  png_write_png(pm->write_ptr, pm->info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

  my_deinit_libpng(pm);

  DEBUG_PRINT(("libpng output buf is %lld bytes\n", PNG_LENGTH));

  //Now that libpng has converted the image
  //and we have it in a buffer, we process it by hand with zlib
  struct z_stream_s inflate_stream;
  my_init_zlib(&inflate_stream);
  inflateInit(&inflate_stream);

  //Pointer to keep track of where we are
  unsigned char *pngp = ENTIRE_PNG_BUF;

  //Skip PNG Signature
  pngp += 8; 

  //Get Header
  unsigned char ihdr_bytes_buf[4+4+13+4]; // size + label + content + crc
  buf_read(ihdr_bytes_buf, &pngp, 4+4+13+4);

  //When we run into non-idat chunks, we will want to preserve them.
  //The spec says there's no chunk that needs to go after IDAT,
  //so we can simply concatenate all of these chunks into a buffer
  //then write them all at once after the IHDR
  
  //ancillary chunks, eg comments
  unsigned char *ancil_chunks_buf = calloc(1,1);
  long long ancil_chunks_len = 0;

  unsigned char *unzip_idats_buf = calloc(1, 1);
  long unzip_buf_len = 1;
  long unzip_buf_offset = 0;

  long long zipped_idats_len = 0; //Length of all idats as we read them

  unsigned long accum_png_len = 8 + (4+4+13+4);

  int chunk_count = 0;

  printf("Uncompressing image data...\n");
  while (1) {
    unsigned char chunk_label[4];
    unsigned char chunk_len_buf[4];

    buf_read(chunk_len_buf, &pngp, 4);

    //first 4 bytes are the length of data section
    long chunk_len = four_bytes_to_int(chunk_len_buf);

    accum_png_len += chunk_len + 4 + 4 + 4; // plus len, crc, label
    DEBUG_PRINT(("at %lu --> %lld\n", accum_png_len, PNG_LENGTH));

    //leave at end of buffer
    if (accum_png_len >= PNG_LENGTH)
      break;

    //read the chunk label (name of this header)
    buf_read(chunk_label, &pngp, 4);

    DEBUG_PRINT(("Reading chunk %d with label '%c%c%c%c', size %ld\n",
          chunk_count, chunk_label[0], chunk_label[1], chunk_label[2],
          chunk_label[3], chunk_len));

    chunk_count += 1;

    if (memcmp(chunk_label, "IDAT", 4) == 0) {

      zipped_idats_len += chunk_len;

      //read the chunk's data section
      unsigned char *raw_chunk_buf = calloc(chunk_len, 1);
      buf_read(raw_chunk_buf, &pngp, chunk_len);

      //Tell inflate to uncompress it
      inflate_stream.next_in = raw_chunk_buf; 
      inflate_stream.avail_in = chunk_len; 

      //Now uncompress it (resizes buffer automatically)
      unsigned char *check_uncompress = uncompress_buffer(&inflate_stream, 
          unzip_idats_buf, &unzip_buf_len, &unzip_buf_offset);

      //Stop if error
      if (check_uncompress == NULL) {
        DEBUG_PRINT((E_GLITCH));
        free(ancil_chunks_buf);
        free(raw_chunk_buf);
        free(unzip_idats_buf);
        free(ENTIRE_PNG_BUF);
        return -1;
      }

      //Moving on
      unzip_idats_buf = check_uncompress;
      free(raw_chunk_buf);
      pngp += 4; // skip CRC

    } else { //This is not an idat

      ancil_chunks_buf = realloc(ancil_chunks_buf, 
          ancil_chunks_len + 4 + 4 + chunk_len + 4); //make room for new data

      //append length and label bytes
      append_bytes(ancil_chunks_buf, chunk_len_buf, &ancil_chunks_len, 4);
      append_bytes(ancil_chunks_buf, chunk_label, &ancil_chunks_len, 4);

      //append chunk data
      unsigned char *raw_chunk_buf = calloc(chunk_len, 1);
      buf_read(raw_chunk_buf, &pngp, chunk_len);
      append_bytes(ancil_chunks_buf, raw_chunk_buf, &ancil_chunks_len, chunk_len );

      //append chunk crc
      unsigned char chunk_crc_buf[4];
      buf_read(chunk_crc_buf, &pngp, 4);
      append_bytes(ancil_chunks_buf, chunk_crc_buf, &ancil_chunks_len, 4);

      free(raw_chunk_buf);

      DEBUG_PRINT(("ancillary chunks length: %lld\n", ancil_chunks_len));

    }
  }

  //buf contains all idats uncompressed, concatenated
  unsigned long unzipped_idats_len = inflate_stream.total_out; 
  unzip_idats_buf = realloc(unzip_idats_buf, unzipped_idats_len);

  //we already have ancillary chunks and idats, don't need the original
  free(ENTIRE_PNG_BUF);
  inflateEnd(&inflate_stream);

  printf("Uncompressed %lld bytes to %ld bytes\n", zipped_idats_len, unzipped_idats_len);

  printf("Glitching image data...\n");

  for (int g=0;g<NUM_OUTPUT_FILES;g++) {

    //do glitches
    switch(g) {
      case 5:
        glitch_random(unzip_idats_buf, unzipped_idats_len,
            ihdr_infos.scanline_len, 0.0005);
        break;
      case 6:
        glitch_random_filter(unzip_idats_buf, unzipped_idats_len,
            ihdr_infos.scanline_len);
        break;
      default:
        glitch_filter(unzip_idats_buf, unzipped_idats_len,
            ihdr_infos.scanline_len, g);
    }

    //recompress so we can write them to file
    long long glitched_idats_len = 0;
    unsigned char *glitched_idats = zip_idats(unzip_idats_buf,
        unzipped_idats_len, &glitched_idats_len);

    if (glitched_idats == NULL) {
      DEBUG_PRINT((E_GLITCH));
      free (unzip_idats_buf);
      free (ancil_chunks_buf);
      return -1;
    }

    char path[MAX_PATH_LENGTH];
    memset(path, 0, MAX_PATH_LENGTH);

    snprintf(path, MAX_PATH_LENGTH, "%s%s%s-%d.png", OUTPUT_DIRECTORY, DIR_SEP,
        base_file_name, g);

    DEBUG_PRINT(("Output file name is %s\n", path));

    FILE *outfp = fopen(path, "wb");

    write_glitched_image(glitched_idats, glitched_idats_len, ihdr_bytes_buf,
        ancil_chunks_buf, ancil_chunks_len, outfp);

    printf("%s\n", path);
    fflush(stdout);

    fclose(outfp);
    free(glitched_idats);
  }

  free(ancil_chunks_buf);
  free(unzip_idats_buf);
  return 0;
}

void remove_filename_extension(char* filename) {

  char *dot_p = strrchr(filename, '.');

  if (dot_p == NULL)
    return;

  dot_p[0] = '\0';
}

long get_file_buf(FILE *f, unsigned char **buf) {

  const int readsize = 4096;
  long buf_size = 0;

  while (1) {

    *buf = realloc(*buf, buf_size+readsize);

    int read = fread(((*buf)+buf_size), 1, readsize, f);

    buf_size += read;

    if (read == 0)
      break;

  }

  *buf = realloc(*buf, buf_size);

  return buf_size;
}

int main(int argc, char* argv[]) {

  //int mkdir_ret = mkdir(OUTPUT_DIRECTORY, S_IRWXU);
  int mkdir_ret = mkdir(OUTPUT_DIRECTORY);

  if (mkdir_ret == -1 && errno != EEXIST)
    error_fatal(1, "problem creating directory", strerror(errno));
  else if (access(OUTPUT_DIRECTORY, W_OK | X_OK))
    error_fatal(1, "Problem accessing directory", strerror(errno));

  for (int i=1;i<argc;i++) {

    FILE *f = fopen(argv[i], "rb");

    if (f == NULL) {
      printf("Cannot open file '%s'\n", argv[i]);
      continue;
    }

    unsigned char* png_buf = calloc(1, 1);

    PNG_LENGTH = get_file_buf(f, &png_buf);

    printf("Glitching file '%s' of size %.2lfM\n", argv[i], PNG_LENGTH / 1024.0 / 1024.0);

    if (PNG_LENGTH <= 0) {
      printf("File '%s' is empty!\n", argv[i]);
      free(png_buf);
      continue;
    }

    remove_filename_extension(argv[i]);

    //png buff is passed around to callbacks for libpng, it will be free'd there
    begin(argv[i], png_buf, PNG_LENGTH);
    fclose(f);
  }

  return 0;
}
