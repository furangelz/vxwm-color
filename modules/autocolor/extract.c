#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <jpeglib.h>

#include "extract.h"

#define COLOR_BUCKETS 4096
#define PERCEPTUAL_DISTANCE 22.0

typedef struct {
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned long count;
}
ExtractBucket;

static double
srgb_to_linear(unsigned char v) {
  double x = (double) v / 255.0;

  if (x <= 0.04045)
    return x / 12.92;

  return pow((x + 0.055) / 1.055, 2.4);
}

static void
rgb_to_lab(unsigned char r, unsigned char g, unsigned char b,
  double * l, double * a, double * bb) {
  double rr = srgb_to_linear(r);
  double gg = srgb_to_linear(g);
  double bl = srgb_to_linear(b);

  double x = rr * 0.4124564 + gg * 0.3575761 + bl * 0.1804375;
  double y = rr * 0.2126729 + gg * 0.7151522 + bl * 0.0721750;
  double z = rr * 0.0193339 + gg * 0.1191920 + bl * 0.9503041;

  x /= 0.95047;
  y /= 1.00000;
  z /= 1.08883;

  if (x > 0.008856)
    x = cbrt(x);
  else
    x = 7.787 * x + 16.0 / 116.0;

  if (y > 0.008856)
    y = cbrt(y);
  else
    y = 7.787 * y + 16.0 / 116.0;

  if (z > 0.008856)
    z = cbrt(z);
  else
    z = 7.787 * z + 16.0 / 116.0;

  * l = 116.0 * y - 16.0;
  * a = 500.0 * (x - y);
  * bb = 200.0 * (y - z);
}

static double
lab_distance(const ExtractBucket * a,
  const ExtractColor * b) {
  double l1, a1, b1;
  double l2, a2, b2;
  double dl, da, db;

  rgb_to_lab(a -> r, a -> g, a -> b, & l1, & a1, & b1);
  rgb_to_lab(b -> r, b -> g, b -> b, & l2, & a2, & b2);

  dl = l1 - l2;
  da = a1 - a2;
  db = b1 - b2;

  return sqrt(dl * dl + da * da + db * db);
}

static int
bucket_index(unsigned char r, unsigned char g, unsigned char b) {
  return ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
}

static void
insert_color(ExtractBucket buckets[COLOR_BUCKETS],
  unsigned char r,
  unsigned char g,
  unsigned char b) {
  int index;
  ExtractBucket * bucket;

  index = bucket_index(r, g, b);
  bucket = & buckets[index];

  if (bucket -> count == 0) {
    bucket -> r = r;
    bucket -> g = g;
    bucket -> b = b;
  } else {
    bucket -> r = (unsigned char)
      (((unsigned long) bucket -> r * bucket -> count + r) /
        (bucket -> count + 1));

    bucket -> g = (unsigned char)
      (((unsigned long) bucket -> g * bucket -> count + g) /
        (bucket -> count + 1));

    bucket -> b = (unsigned char)
      (((unsigned long) bucket -> b * bucket -> count + b) /
        (bucket -> count + 1));
  }

  bucket -> count++;
}

static int
compare_colors(const void * a,
  const void * b) {
  const ExtractBucket * ca = a;
  const ExtractBucket * cb = b;

  if (ca -> count < cb -> count)
    return 1;

  if (ca -> count > cb -> count)
    return -1;

  return 0;
}

int
extract_colors(const char * path, ExtractColor colors[3]) {
  FILE * file;
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  ExtractBucket * buckets;
  JSAMPARRAY row;
  unsigned int width;
  int channels;
  int i;
  int found;

  file = fopen(path, "rb");
  if (!file)
    return -1;

  cinfo.err = jpeg_std_error( & jerr);
  jpeg_create_decompress( & cinfo);
  jpeg_stdio_src( & cinfo, file);

  if (jpeg_read_header( & cinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress( & cinfo);
    fclose(file);
    return -1;
  }

  jpeg_start_decompress( & cinfo);

  width = cinfo.output_width;
  channels = cinfo.output_components;

  if (channels < 3) {
    jpeg_finish_decompress( & cinfo);
    jpeg_destroy_decompress( & cinfo);
    fclose(file);
    return -1;
  }

  buckets = calloc(COLOR_BUCKETS, sizeof( * buckets));

  if (!buckets) {
    jpeg_finish_decompress( & cinfo);
    jpeg_destroy_decompress( & cinfo);
    fclose(file);
    return -1;
  }

  row = ( * cinfo.mem -> alloc_sarray)(
    (j_common_ptr) & cinfo,
    JPOOL_IMAGE,
    width * channels,
    1
  );

  while (cinfo.output_scanline < cinfo.output_height) {
    unsigned char * pixel;

    jpeg_read_scanlines( & cinfo, row, 1);
    pixel = row[0];

    for (i = 0; i < (int) width; i++) {
      unsigned char r;
      unsigned char g;
      unsigned char b;

      r = pixel[i * channels];
      g = pixel[i * channels + 1];
      b = pixel[i * channels + 2];

      insert_color(buckets, r, g, b);
    }
  }

  jpeg_finish_decompress( & cinfo);
  jpeg_destroy_decompress( & cinfo);
  fclose(file);

  qsort(
    buckets,
    COLOR_BUCKETS,
    sizeof( * buckets),
    compare_colors
  );

  found = 0;

  for (i = 0; i < COLOR_BUCKETS && found < 3; i++) {
    double weight;
    double distance;
    int j;

    if (buckets[i].count == 0)
      continue;

    weight = log((double) buckets[i].count + 1.0);

    if (weight < 1.0)
      continue;

    distance = 1000000.0;

    for (j = 0; j < found; j++) {
      double d;

      d = lab_distance( & buckets[i], & colors[j]);

      if (d < distance)
        distance = d;
    }

    if (found > 0 && distance < PERCEPTUAL_DISTANCE)
      continue;

    colors[found].r = buckets[i].r;
    colors[found].g = buckets[i].g;
    colors[found].b = buckets[i].b;
    colors[found].count =
      (unsigned long)((double) buckets[i].count * weight);

    found++;
  }

  free(buckets);

  while (found < 3) {
    if (found == 0) {
      colors[found].r = 0;
      colors[found].g = 0;
      colors[found].b = 0;
      colors[found].count = 0;
    } else {
      colors[found] = colors[found - 1];
    }

    found++;
  }

  return 0;
}

void
color_to_hex(const ExtractColor * color, char out[8]) {
  snprintf(
    out,
    8,
    "#%02x%02x%02x",
    (unsigned int) color -> r,
    (unsigned int) color -> g,
    (unsigned int) color -> b
  );
}