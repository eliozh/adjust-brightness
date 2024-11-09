#pragma OPENCL EXTENSION cl_khr_fp64 : enable
__kernel void adjust_brightness(__global unsigned char *in,
                                __global unsigned char *out,
                                const double factor) {
  int id = get_global_id(0);
  if (id % 3 != 0) {
    return;
  }

  double r, g, b;

  r = in[id] * factor;
  g = in[id + 1] * factor;
  b = in[id + 2] * factor;

  if (r > 255.0) {
    out[id] = 255;
  } else {
    out[id] = (unsigned char)r;
  }

  if (g > 255.0) {
    out[id + 1] = 255;
  } else {
    out[id + 1] = (unsigned char)g;
  }

  if (b > 255.0) {
    out[id + 2] = 255;
  } else {
    out[id + 2] = (unsigned char)b;
  }
}
