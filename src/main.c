#include "png.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void adjust_brightness(const char *in, const char *out, double factor);
void _adjust_brightness(png_bytep buffer_in, png_bytep buffer_out,
                        double factor);
double hue_to_rgb(double p, double q, double t);
double max_rgb(double r, double g, double b);
double min_rgb(double r, double g, double b);

int main(int argc, char *argv[]) {
  if (argc < 4) {
    printf("[ERROR] No enough parameters.\n");
    exit(1);
  }

  // filenames of input file and output file
  const char *in = argv[1];
  const char *out = argv[2];
  double factor = atof(argv[3]);

  adjust_brightness(in, out, factor);

  return 0;
}

void adjust_brightness(const char *in, const char *out, double factor) {
  png_image image_in;
  png_bytep buffer_in, buffer_out;
  int32_t image_size, row_stride, width, height;

  // required initialization for read png with simplified API
  memset(&image_in, 0, sizeof(png_image));
  image_in.version = PNG_IMAGE_VERSION;

  png_image_begin_read_from_file(&image_in, in);
  image_in.format = PNG_FORMAT_RGB;

  image_size = PNG_IMAGE_SIZE(image_in);
  row_stride = PNG_IMAGE_ROW_STRIDE(image_in);
  width = image_in.width;
  height = image_in.height;
  buffer_in = (png_bytep)malloc(image_size);
  png_image_finish_read(&image_in, NULL, buffer_in, row_stride, NULL);

  for (int i = 0; i < 21; i++) {
    printf("%d ", buffer_in[i]);
  }
  printf("\n");

  buffer_out = (png_bytep)malloc(image_size);
  for (int i = 0; i < image_size; i += 3) {
    _adjust_brightness(buffer_in + i, buffer_out + i, factor);
  }

  for (int i = 0; i < 21; i++) {
    printf("%d ", buffer_out[i]);
  }
  printf("\n");

  png_image_write_to_file(&image_in, out, 0, buffer_out, 0, NULL);
}

void _adjust_brightness(png_bytep buffer_in, png_bytep buffer_out,
                        double factor) {
  double r, g, b;
  double h, s, l;
  double r_, g_, b_; // rgb value after adjusting brightness
  double m, M, c;

  /* Convert RGB to HSL */
  r = buffer_in[0] / 255.0;
  g = buffer_in[1] / 255.0;
  b = buffer_in[2] / 255.0;

  m = min_rgb(r, g, b);
  M = max_rgb(r, g, b);
  c = M - m;

  l = (m + M) / 2.0;

  if (c == 0) {
    h = 0;
    s = 0;
  } else {
    s = l > 0.5 ? c / (2 - M - m) : c / (M + m);

    if (M == r) {
      h = (g - b) / c + (g < b ? 6 : 0);
      h = fmod(h, 6.0);
    } else if (M == g) {
      h = (b - r) / c + 2;
    } else if (M == b) {
      h = (r - g) / c + 4;
    }
    h /= 6.0;
  }

  l = l * factor;
  if (l > 1.0) {
    l = 1.0;
  }

  /* Convert HSL to RGB */
  if (s == 0) {
    r_ = g_ = b_ = l;
  } else {
    double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    double p = 2 * l - q;
    r_ = hue_to_rgb(p, q, h + 1.0 / 3);
    g_ = hue_to_rgb(p, q, h);
    b_ = hue_to_rgb(p, q, h - 1.0 / 3);
  }

  buffer_out[0] = (int)(r_ * 255);
  buffer_out[1] = (int)(g_ * 255);
  buffer_out[2] = (int)(b_ * 255);
}

double hue_to_rgb(double p, double q, double t) {
  if (t < 0)
    t += 1;
  if (t > 1)
    t = fmod(t, 1.0);
  if (t < 1.0 / 6)
    return p + (q - p) * 6 * t;
  if (t < 1.0 / 2)
    return q;
  if (t < 2.0 / 3)
    return p + (q - p) * (2.0 / 3 - t) * 6;
  return p;
}

double max_rgb(double r, double g, double b) {
  if (r > g) {
    return r > b ? r : b;
  } else {
    return g > b ? g : b;
  }
}

double min_rgb(double r, double g, double b) {
  if (r < g) {
    return r < b ? r : b;
  } else {
    return g < b ? g : b;
  }
}
