#pragma once

typedef struct {
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned long count;
}
ExtractColor;

int extract_colors(const char * path, ExtractColor colors[3]);
void color_to_hex(const ExtractColor * color, char out[8]);