#ifndef  PNGLITCH_H_
#define PNGLITCH_H_

struct ihdr_infos_s {
  //libpng provided
  unsigned int width;
  unsigned int height;
  unsigned int bit_depth;
  unsigned int color_type;
  unsigned int compression_type;
  unsigned int filter_method;
  unsigned int interlace_type;

  //Calculated
  unsigned int bytes_per_pixel;
  unsigned int scanline_len;
};

void write_glitched_image(unsigned char *glitched_idats, 
    long glitched_idats_len, 
    unsigned char *ihdr_bytes_buf, unsigned char *ancil_buf,
    long long ancil_buf_len, FILE *fp);

void glitch_random(unsigned char *data, unsigned long data_len, unsigned int scanline_len, float freq);

void glitch_filter(unsigned char *data, unsigned long data_len, unsigned int scanline_len, int filter);

void glitch_random_filter(unsigned char *data, unsigned long data_len, unsigned int scanline_len);

unsigned char *zip_idats(unsigned char *raw_data, unsigned long data_len, long long *compressed_length);


unsigned char *uncompress_buffer(struct z_stream_s *inflate_stream, 
    unsigned char *unzip_idats_buf, long *unzip_buf_len, long *unzip_buf_offset);

#endif
