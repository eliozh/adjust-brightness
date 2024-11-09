#include "CL/cl.h"
#include "png.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SOURCE_SIZE (1 << 20)

void adjust_brightness(const char *in, const char *out, double factor);
void _adjust_brightness_cl(png_bytep buffer_in, png_bytep buffer_out,
                           double factor, size_t image_size);
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

  buffer_out = (png_bytep)malloc(image_size);
  struct timespec start, end;
  printf("[INFO] Start running brightness adjustment with no acc ...\n");
  timespec_get(&start, TIME_UTC);
  for (int i = 0; i < image_size; i += 3) {
    _adjust_brightness(buffer_in + i, buffer_out + i, factor);
  }
  timespec_get(&end, TIME_UTC);
  printf(
      "Time taken for brightness adjustment with no acc: %llu nanosecond(s)\n",
      (end.tv_sec - start.tv_sec) * (int)1e9 + (end.tv_nsec - start.tv_nsec));
  png_image_write_to_file(&image_in, out, 0, buffer_out, 0, NULL);

  memset(buffer_out, 0, image_size);
  _adjust_brightness_cl(buffer_in, buffer_out, factor, image_size);

  png_image_write_to_file(&image_in, "output_cl.png", 0, buffer_out, 0, NULL);
}

void _adjust_brightness_cl(png_bytep buffer_in, png_bytep buffer_out,
                           double factor, size_t image_size) {
  struct timespec start, end;
  cl_platform_id platform;
  cl_device_id device;
  cl_context context;
  cl_program program;
  cl_int err;
  size_t source_size;
  char *source_str;

  err = clGetPlatformIDs(1, &platform, NULL);
  err |= clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);

  if (err != CL_SUCCESS) {
    fprintf(stderr, "[ERROR] Failed to get device.\n");
    exit(1);
  }

  // initialize
  FILE *fp = fopen("./src/adjust_brightness.cl", "r");
  if (fp == NULL) {
    fprintf(stderr, "[ERROR] Failed to open cl file.\n");
    exit(1);
  }

  source_str = (char *)malloc(sizeof(char) * MAX_SOURCE_SIZE);
  source_size = fread(source_str, 1, MAX_SOURCE_SIZE, fp);
  fclose(fp);

  char *log;
  size_t build_log_len;
  context = clCreateContext(0, 1, &device, NULL, NULL, &err);
  program = clCreateProgramWithSource(context, 1, (const char **)&source_str,
                                      (const size_t *)&source_size, &err);
  err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);

  if (err != CL_SUCCESS) {
    cl_int error;
    error = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0,
                                  NULL, &build_log_len);

    if (error) {
      fprintf(stderr, "[ERROR] clGetProgramBuildInfo failded.\n");
      exit(1);
    }

    log = (char *)malloc(build_log_len * sizeof(char));
    if (!log) {
      fprintf(stderr, "[ERROR] malloc failed.\n");
      exit(1);
    }
    error = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG,
                                  build_log_len, log, NULL);
    if (error) {
      fprintf(stderr, "[ERROR] clGetProgramBuildInfo failed.\n");
      exit(1);
    }

    fprintf(stderr, "[ERROR] Build log: \n%s\n", log);
    free(log);
    fprintf(stderr, "[ERROR] clBuildProgram failed.\n");
    exit(EXIT_FAILURE);
  }

  cl_kernel kernel;
  cl_command_queue queue;
  size_t global_size, local_size;
  size_t bytes_in, bytes_out;

  cl_mem d_in;
  cl_mem d_out;

  bytes_in = image_size * sizeof(png_byte);
  bytes_out = image_size * sizeof(png_byte);
  local_size = 1024;
  global_size = ceil(image_size / (float)local_size) * local_size;

  kernel = clCreateKernel(program, "adjust_brightness", &err);
  queue = clCreateCommandQueueWithProperties(context, device, NULL, &err);

  printf("[INFO] Start running brightness adjustment with acc ...\n");
  timespec_get(&start, TIME_UTC);
  d_in = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes_in, NULL, NULL);
  d_out = clCreateBuffer(context, CL_MEM_WRITE_ONLY, bytes_out, NULL, NULL);
  err = clEnqueueWriteBuffer(queue, d_in, CL_TRUE, 0, bytes_in, buffer_in, 0,
                             NULL, NULL);
  err |= clSetKernelArg(kernel, 0, sizeof(cl_mem), &d_in);
  err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &d_out);
  err |= clSetKernelArg(kernel, 2, sizeof(double), &factor);
  err |= clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_size,
                                &local_size, 0, NULL, NULL);
  clFinish(queue);
  clEnqueueReadBuffer(queue, d_out, CL_TRUE, 0, bytes_out, (void *)buffer_out,
                      0, NULL, NULL);
  timespec_get(&end, TIME_UTC);
  printf("[INFO] Time taken for brightness adjustment with acc:%llu "
         "nanosecond(s)\n",
         (end.tv_sec - start.tv_sec) * (int)1e9 +
             (end.tv_nsec - start.tv_nsec));
}

void _adjust_brightness(png_bytep buffer_in, png_bytep buffer_out,
                        double factor) {
  double r, g, b;
  r = buffer_in[0] * factor;
  g = buffer_in[1] * factor;
  b = buffer_in[2] * factor;

  if (r > 255) {
    buffer_out[0] = 255;
  } else {
    buffer_out[0] = (int)r;
  }

  if (g > 255) {
    buffer_out[1] = 255;
  } else {
    buffer_out[1] = (int)g;
  }

  if (b > 255) {
    buffer_out[2] = 255;
  } else {
    buffer_out[2] = (int)b;
  }

  /* double r, g, b; */
  /* double h, s, l; */
  /* double r_, g_, b_; // rgb value after adjusting brightness */
  /* double m, M, c; */

  /* Convert RGB to HSL */
  /* r = buffer_in[0] / 255.0; */
  /* g = buffer_in[1] / 255.0; */
  /* b = buffer_in[2] / 255.0; */
  /**/
  /* m = min_rgb(r, g, b); */
  /* M = max_rgb(r, g, b); */
  /* c = M - m; */
  /**/
  /* l = (m + M) / 2.0; */
  /**/
  /* if (c == 0) { */
  /*   h = 0; */
  /*   s = 0; */
  /* } else { */
  /*   s = l > 0.5 ? c / (2 - M - m) : c / (M + m); */
  /**/
  /*   if (M == r) { */
  /*     h = (g - b) / c + (g < b ? 6 : 0); */
  /*     h = fmod(h, 6.0); */
  /*   } else if (M == g) { */
  /*     h = (b - r) / c + 2; */
  /*   } else if (M == b) { */
  /*     h = (r - g) / c + 4; */
  /*   } */
  /*   h /= 6.0; */
  /* } */
  /**/
  /* l = l * factor; */
  /* if (l > 1.0) { */
  /*   l = 1.0; */
  /* } */

  /* Convert HSL to RGB */
  /* if (s == 0) { */
  /*   r_ = g_ = b_ = l; */
  /* } else { */
  /*   double q = l < 0.5 ? l * (1 + s) : l + s - l * s; */
  /*   double p = 2 * l - q; */
  /*   r_ = hue_to_rgb(p, q, h + 1.0 / 3); */
  /*   g_ = hue_to_rgb(p, q, h); */
  /*   b_ = hue_to_rgb(p, q, h - 1.0 / 3); */
  /* } */
  /**/
  /* buffer_out[0] = (int)(r_ * 255); */
  /* buffer_out[1] = (int)(g_ * 255); */
  /* buffer_out[2] = (int)(b_ * 255); */
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
