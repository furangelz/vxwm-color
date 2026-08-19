#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

#include "extract.h"

static double
luminance(const ExtractColor * color) {
  double r;
  double g;
  double b;

  r = (double) color -> r / 255.0;
  g = (double) color -> g / 255.0;
  b = (double) color -> b / 255.0;

  r = r <= 0.03928 ? r / 12.92 : pow((r + 0.055) / 1.055, 2.4);
  g = g <= 0.03928 ? g / 12.92 : pow((g + 0.055) / 1.055, 2.4);
  b = b <= 0.03928 ? b / 12.92 : pow((b + 0.055) / 1.055, 2.4);

  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

static void
scale_color(const ExtractColor * color, double factor, char out[8]) {
  unsigned int r;
  unsigned int g;
  unsigned int b;

  r = (unsigned int)((double) color -> r * factor);
  g = (unsigned int)((double) color -> g * factor);
  b = (unsigned int)((double) color -> b * factor);

  if (r > 255)
    r = 255;

  if (g > 255)
    g = 255;

  if (b > 255)
    b = 255;

  snprintf(out, 8, "#%02x%02x%02x", r, g, b);
}

static void
make_foreground(const ExtractColor * color, char out[8]) {
  if (luminance(color) > 0.45)
    snprintf(out, 8, "#111111");
  else
    snprintf(out, 8, "#eeeeee");
}

static int
color_path(char * out, size_t size) {
  const char * home;

  home = getenv("HOME");

  if (!home)
    return -1;

  if (snprintf(
      out,
      size,
      "%s/.config/vxwm/colors",
      home
    ) >= (int) size)
    return -1;

  return 0;
}

static int
valid_color(const char * s) {
  int i;

  if (!s || s[0] != '#' || strlen(s) != 7)
    return 0;

  for (i = 1; i < 7; i++) {
    char c;

    c = s[i];

    if (!((c >= '0' && c <= '9') ||
        (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F')))
      return 0;
  }

  return 1;
}

static void
load_value(char * dst, size_t size,
  const char * value) {
  if (valid_color(value))
    snprintf(dst, size, "%s", value);
}

static int
autocolor_load(void) {
  char path[4096];
  FILE * file;
  char line[256];
  char key[64];
  char value[64];

  if (color_path(path, sizeof(path)) < 0)
    return -1;

  file = fopen(path, "r");

  if (!file)
    return -1;

  while (fgets(line, sizeof(line), file)) {
    if (sscanf(line, "%63[^=]=%63s", key, value) != 2)
      continue;

    if (!strcmp(key, "normbg"))
      load_value(normbgcolor, sizeof(normbgcolor), value);
    else if (!strcmp(key, "normborder"))
      load_value(normbordercolor, sizeof(normbordercolor), value);
    else if (!strcmp(key, "normfg"))
      load_value(normfgcolor, sizeof(normfgcolor), value);
    else if (!strcmp(key, "selfg"))
      load_value(selfgcolor, sizeof(selfgcolor), value);
    else if (!strcmp(key, "selborder"))
      load_value(selbordercolor, sizeof(selbordercolor), value);
    else if (!strcmp(key, "selbg"))
      load_value(selbgcolor, sizeof(selbgcolor), value);
  }

  fclose(file);

  return 0;
}

static int
save_colors(void) {
  char path[4096];
  char dir[4096];
  char * slash;
  FILE * file;

  if (color_path(path, sizeof(path)) < 0)
    return -1;

  snprintf(dir, sizeof(dir), "%s", path);

  slash = strrchr(dir, '/');

  if (slash) {
    * slash = '\0';
    mkdir(dir, 0755);
  }

  file = fopen(path, "w");

  if (!file)
    return -1;

  fprintf(file, "normbg=%s\n", normbgcolor);
  fprintf(file, "normborder=%s\n", normbordercolor);
  fprintf(file, "normfg=%s\n", normfgcolor);
  fprintf(file, "selfg=%s\n", selfgcolor);
  fprintf(file, "selborder=%s\n", selbordercolor);
  fprintf(file, "selbg=%s\n", selbgcolor);

  fclose(file);

  return 0;
}

static void
generate_colors(void) {
  ExtractColor extracted[3];
  double l;

  if (extract_colors(autocolorimage, extracted) < 0)
    return;

  l = luminance( & extracted[0]);

  if (l > 0.45) {
    scale_color( & extracted[0], 0.88, normbgcolor);
    scale_color( & extracted[0], 0.72, normbordercolor);
    make_foreground( & extracted[0], normfgcolor);

    scale_color( & extracted[1], 0.82, selbgcolor);
    scale_color( & extracted[1], 0.60, selbordercolor);
    make_foreground( & extracted[1], selfgcolor);
  } else {
    scale_color( & extracted[0], 0.45, normbgcolor);
    scale_color( & extracted[0], 0.75, normbordercolor);
    make_foreground( & extracted[0], normfgcolor);

    scale_color( & extracted[1], 0.55, selbgcolor);
    scale_color( & extracted[1], 0.85, selbordercolor);
    make_foreground( & extracted[1], selfgcolor);
  }
}

static void
rebuild_schemes(void) {
  int i;

  for (i = 0; i < LENGTH(colors); i++)
    scheme[i] = drw_scm_create(drw, colors[i], 3);

  drw_setscheme(drw, scheme[SchemeNorm]);
}

static void
autocolor(const Arg * arg) {
  (void) arg;

  generate_colors();
  save_colors();
  rebuild_schemes();

  updatebars();

  if (selmon)
    drawbar(selmon);

  XSync(dpy, False);
}