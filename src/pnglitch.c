#include <stdint.h>
#include <stdlib.h>
#include <zlib.h>
#include <arpa/inet.h>
#include <png.h>
#include <stdio.h>

#include "libs.h"
#include "debug.h"
#include "pnglitch.h"


typedef unsigned char BYTE;

unsigned char PNG_SIGNATURE[] = {137, 80, 78, 71, 13, 10, 26, 10}; //len 8
unsigned char PNG_IEND_CHUNK[] = {0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130}; //len 12
unsigned char IDAT_HDR_BYTES[] = {73, 68, 65, 84};


unsigned char *uncompress_buffer(struct z_stream_s *inflate_stream, 
    unsigned char *unzip_idats_buf, 
    long *unzip_buf_len, long *unzip_buf_offset) {

  do {  

    //tell inflater where to write, how much room it has
    inflate_stream->next_out = unzip_idats_buf + *unzip_buf_offset; 
    inflate_stream->avail_out = *unzip_buf_len - *unzip_buf_offset; 

    long prevtotal = inflate_stream->total_out;

    int ret = inflate(inflate_stream, Z_NO_FLUSH);

    long last_inflate_len = inflate_stream->total_out - prevtotal;

    *unzip_buf_offset += last_inflate_len;

    if (ret == Z_DATA_ERROR ||
        ret == Z_BUF_ERROR ||
        inflate_stream->avail_out <= 0) 
    { 
      //Increase size if output buffer
      *unzip_buf_len = (*unzip_buf_len*2) + 1;
      unzip_idats_buf = realloc(unzip_idats_buf, *unzip_buf_len);
      continue;
    }
    else if (ret == Z_DATA_ERROR || ret == Z_DATA_ERROR) {
      DEBUG_PRINT(( "zlib error when decompresing!\n \
            Did libpng return a bad image?\n \
            last return was '%d'\n \
            total output was '%ld'\n \
            last inflate len was '%ld' \n",
            ret, inflate_stream->total_out, last_inflate_len));
      return NULL;
    }
  } 
  while (inflate_stream->avail_in != 0);

  return unzip_idats_buf;

}

unsigned char *zip_idats(unsigned char *raw_data, ulong data_len, long long *compressed_length) {

  DEBUG_PRINT(("Zipping glitched buffer length %ld\n", data_len));

  //Init a new compressing zlib
  int ret;
  struct z_stream_s deflate_stream;
  my_init_zlib(&deflate_stream);
  ret = deflateInit(&deflate_stream, Z_DEFAULT_COMPRESSION);
  if (ret != Z_OK) 
    error_fatal(-1, "zlib deflate init", "ret != Z_OK");

  long zipped_offset = 0;
  long zipped_len = 1000;
  //This grows
  unsigned char *zipped_idats = calloc(zipped_len, 1);

  deflate_stream.next_in = raw_data; 
  deflate_stream.avail_in = data_len; 
  do {  // This compresses

    deflate_stream.next_out = (zipped_idats + zipped_offset); 
    deflate_stream.avail_out = zipped_len - zipped_offset; 

    long prevtotal = deflate_stream.total_out;

    ret = deflate(&deflate_stream, Z_FINISH);

    long last_deflate_len = deflate_stream.total_out - prevtotal;
    zipped_offset += last_deflate_len;

    //needs bigger buffer
    if (ret == Z_STREAM_END) 
      break;
    else if ( ret == Z_DATA_ERROR ||
              ret == Z_BUF_ERROR  ||
             (ret == Z_OK && deflate_stream.avail_out == 0)) 
    {
      zipped_len = (zipped_len*2) + 1;
      DEBUG_PRINT(("Setting bigger buffer (%ld)\n", zipped_len));
      zipped_idats = realloc(zipped_idats, zipped_len);
      continue;
    }
    else if (ret == Z_DATA_ERROR || ret == Z_DATA_ERROR) {
      DEBUG_PRINT(("Error in zip_idats: ret was %d, \
            total out was %ld, last deflate len was %ld\n",
            ret, deflate_stream.total_out, last_deflate_len));
      free(zipped_idats);
      return NULL;
    }
  } while (1);

  zipped_idats = realloc(zipped_idats, deflate_stream.total_out);
  DEBUG_PRINT(("Compressed %ld size buffer to %ld\n", data_len, deflate_stream.total_out));

  *compressed_length = deflate_stream.total_out;

  deflateEnd(&deflate_stream);

  return(zipped_idats);
}


//Takes uncompressed concated IDAT buffer
void glitch_random_filter(unsigned char *data, unsigned long data_len, unsigned int scanline_len) {
  DEBUG_PRINT(("\nGlitching offsets with random\n"));
  for (unsigned long i=0; i<data_len; i += scanline_len) {
    //DEBUG_PRINT(("%lu (%d)", i, data[i]));
    data[i] = rand()%5;
  }
}

void glitch_filter(unsigned char *data, unsigned long data_len, unsigned int scanline_len, int filter) {
  DEBUG_PRINT(("\nGlitching offsets with %d\n", filter));
  for (unsigned long i=0; i<data_len; i += scanline_len) {
    //DEBUG_PRINT(("%lu (%d) ", i, data[i]));
    data[i] = filter;
  }
}


void glitch_random(unsigned char *data, unsigned long data_len, unsigned int scanline_len, float freq) {

  srand(84677210);
  long glitches = (long) (((double) data_len) * freq);

  // The plus one includes the filter byte
  for (uint32_t i = 0; i < glitches; i++) {

    uint64_t spot = ((rand() << 31) | rand())%data_len;

    // Protects filter byte from being overwritten
    if ((spot % scanline_len) == 0)
      continue;

    data[spot] = rand()%256;
  }
}

void write_glitched_image(unsigned char *glitched_idats, 
    long glitched_idats_len, unsigned char *ihdr_bytes_buf,
    unsigned char *ancil_buf, long long ancil_len, FILE* fp) {


  //TODO: is fwrite reliable, should we be checking?

  //Data written (in this order):
  // -PNG signature (constant)
  // -header chunk (IHDR)
  // -ancillary chunks (!IDATs)
  // -IDATS
  //  -spread out into fixed-size chunks
  //  -need to re-calculate crc for each
  // -IEND (constant)

  uint32_t idat_ihdr_crc = crc32(0L, IDAT_HDR_BYTES, 4); 

  fwrite(PNG_SIGNATURE, 1, 8, fp);
  fwrite(ihdr_bytes_buf, 1, 4+4+13+4, fp);
  fwrite(ancil_buf, 1, ancil_len, fp);

  long bytes_left = glitched_idats_len;
  unsigned char *idats_stream = glitched_idats;
  uint32_t idat_len = 8192;

  while (bytes_left > 0) {

    idat_len = (bytes_left < idat_len) ? bytes_left : idat_len;
    uint32_t idat_len_buf = htonl(idat_len);

    DEBUG_PRINT(("idat_len: %d\n", idat_len));

    fwrite(&idat_len_buf, sizeof(idat_len_buf), 1, fp); 
    fwrite(IDAT_HDR_BYTES, 1, 4, fp); //constant

    //calculate crc
    uint32_t idat_data_crc = crc32(0L, idats_stream, idat_len); 
    uint32_t idat_crc = htonl(crc32_combine(idat_ihdr_crc, idat_data_crc, idat_len));

    fwrite(idats_stream, 1, idat_len, fp);
    fwrite(&idat_crc, sizeof(idat_crc), 1, fp);

    idats_stream += idat_len;
    bytes_left -= idat_len;
  }

  fwrite(PNG_IEND_CHUNK, 1, 12, fp);
}
