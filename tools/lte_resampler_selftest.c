// IQ resampling must produce the same stream regardless of SDR callback sizes.
#include "srsran/phy/resampling/resample_arb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int check_stream(float rate, bool interpolate)
{
  const int n = 4096;
  const int capacity = (int)ceilf((n + 1) * rate) + 64;
  cf_t* input = calloc(n, sizeof(cf_t));
  cf_t* whole = calloc(capacity, sizeof(cf_t));
  cf_t* chunks = calloc(capacity, sizeof(cf_t));
  if (!input || !whole || !chunks) exit(1);
  for (int i = 0; i < n; ++i) {
    input[i] = cosf(0.07f * i) + I * sinf(0.07f * i);
  }

  srsran_resample_arb_t a, b;
  srsran_resample_arb_init(&a, rate, interpolate);
  srsran_resample_arb_init(&b, rate, interpolate);
  const int expected = srsran_resample_arb_compute(&a, input, whole, n);
  const int sizes[] = {1, 2, 7, 31, 3, 257, 8, 101};
  int actual = 0;
  for (int pos = 0, block = 0; pos < n; ++block) {
    int count = sizes[block % (sizeof(sizes) / sizeof(sizes[0]))];
    if (count > n - pos) count = n - pos;
    actual += srsran_resample_arb_compute(&b, input + pos, chunks + actual, count);
    pos += count;
    // Empty callbacks must not reset the filter or phase.
    if (srsran_resample_arb_compute(&b, NULL, NULL, 0) != 0) exit(1);
  }

  int ok = expected == actual && fabsf(expected - n * rate) <= 1.0f;
  float error = 0;
  float power = 0;
  const int warmup = (int)ceilf(16 * rate);
  for (int i = 0; ok && i < actual; ++i) {
    error = fmaxf(error, cabsf(whole[i] - chunks[i]));
    if (i >= warmup) power += crealf(whole[i] * conjf(whole[i]));
  }
  power /= actual - warmup;
  ok = ok && error < 1e-5f && power > 0.95f && power < 1.05f;
  if (!ok) {
    fprintf(stderr, "rate %.6f, interpolation %d: whole=%d chunked=%d max error=%g\n",
            rate, interpolate, expected, actual, error);
  }
  free(input);
  free(whole);
  free(chunks);
  return ok;
}

int main(void)
{
  const float rates[] = {0.1f, 0.8f, 0.96f, 1.0f, 1.536f, 3.2f, 16.0f};
  int ok = 1;
  for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
    ok &= check_stream(rates[i], false);
    ok &= check_stream(rates[i], true);
  }
  if (ok) puts("LTE IQ resampling retains history, phase and sample count across callbacks");
  return ok ? 0 : 1;
}
