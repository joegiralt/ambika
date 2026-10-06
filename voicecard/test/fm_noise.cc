// Coherent-sampling noise measurement of one sine operator through real Fm4Op.
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include "avrlib/base.h"
#include "voicecard/resources.h"
#include "voicecard/fm4op.h"
#include "voicecard/audio_out.h"
namespace ambika { CaptureBuffer audio_buffer; }
using namespace ambika;

static const int N = 8192;

// Exact DFT at integer bins; rectangular window is correct for coherent input.
static void report(const char* lbl, const std::vector<double>& x, double fs, int sigbin) {
  int n = x.size();
  double mean = 0; for (double v : x) mean += v; mean /= n;
  std::vector<double> mag(n/2+1, 0.0);
  for (int k = 1; k <= n/2; ++k) {
    double sr=0, si=0;
    for (int i = 0; i < n; ++i) { double a=2*M_PI*k*i/n, d=x[i]-mean; sr+=d*cos(a); si-=d*sin(a); }
    mag[k]=sqrt(sr*sr+si*si);
  }
  double sig = mag[sigbin], noise = 0, worst = 0; int worstk = 0;
  for (int k=1;k<=n/2;++k) {
    if (k==sigbin) continue;
    noise += mag[k]*mag[k];
    if (mag[k] > worst) { worst = mag[k]; worstk = k; }
  }
  printf("  %-30s SNR %5.1f dB | worst spur %6.1f dBc @ %6.0f Hz\n",
         lbl, 20*log10(sig/sqrt(noise)), 20*log10(worst/sig), worstk*fs/n);
}

int main() {
  const double FS = 19600.0;                  // FM render rate
  uint8_t w[4] = {0,0,0,0};
  uint16_t att[4];
  att[0] = Fm4Op::Attenuation(127, 255);
  for (int i=1;i<4;++i) att[i] = Fm4Op::Attenuation(0, 0);

  printf("  (12-bit ideal is about 74 dB SNR; 10-bit phase truncation predicts ~60 dBc spurs)\n");
  printf("  The 'half rate' rows are the real Fm4Op::Render output. The 'midpoint model'\n"
         "  rows apply the OLD midpoint upsampling here in the test, not the filter that\n"
         "  ships in Voice::ExpandHalfRate - use analyze.sh render to hear the real one.\n\n");
  for (int bin : {46, 184, 735, 2940}) {      // coherent: exact integer cycles in N
    double f = bin * FS / N;
    // calibrate: one cycle spans 2^24 accumulator units (measured)
    uint32_t inc = (uint32_t)llround(16777216.0 * f / FS);
    Fm4Op fm; fm.Init();
    fm.SetOperatorIncrement(0, inc, 4, 0);
    std::vector<uint16_t> buf(N);
    for (int i = 0; i < N; i += 64) fm.Render(7, w, att, 0, &buf[i], 64);
    std::vector<double> half(buf.begin(), buf.end());
    char lbl[80];
    snprintf(lbl,sizeof lbl,"%6.1f Hz  half rate", f);
    report(lbl, half, FS, bin);
    std::vector<double> full(2*N);
    for (int i = 0; i < N; ++i) {
      double before = i ? buf[i-1] : buf[N-1];
      full[2*i] = (before + buf[i]) / 2.0;
      full[2*i+1] = buf[i];
    }
    snprintf(lbl,sizeof lbl,"%6.1f Hz  midpoint model", f);
    report(lbl, full, FS*2, bin);
    printf("\n");
  }
  return 0;
}
