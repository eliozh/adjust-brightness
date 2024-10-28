#pragma OPENCL EXTENSION cl_khr_fp64 : enable
double hue_to_rgb(double p, double q, double t) {
  if (t < 0) {
    t += 1.0;
  }
  if (t > 1) {
    t = fmod(t, 1.0);
  }
  if (t < 1.0 / 6) {
    return p + (q - p) * 6 * t;
  }
  if (t < 1.0 / 2) {
    return q;
  }
  if (t < 2.0 / 3) {
    return p + (q - p) * (2.0 / 3 - t) * t;
  }
  return p;
}
__kernel void adjust_brightness(__global unsigned char *in, __global unsigned char *out,
                                const double factor) {
  int id = get_global_id(0);
  if (id % 3 != 0) {
    return;
  }

  double h, s, l;
  double r, g, b;
  double r_, g_, b_;
  double m, M, c;

  r = in[id] / 255.0;
  g = in[id + 1] / 255.0;
  b = in[id + 2] / 255.0;

  m = fmin(r, fmin(g, b));
  M = fmax(r, fmax(g, b));
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

  if (s == 0) {
    r_ = g_ = b_ = l;
  } else {
    double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    double p = 2 * l - q;
    r_ = hue_to_rgb(p, q, h + 1.0 / 3);
    g_ = hue_to_rgb(p, q, h);
    b_ = hue_to_rgb(p, q, h - 1.0 / 3);
  }

  out[id] = (unsigned char)(r_ * 255);
  out[id + 1] = (unsigned char)(g_ * 255);
  out[id + 2] = (unsigned char)(b_ * 255);
}
