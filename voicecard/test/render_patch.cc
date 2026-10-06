// Render real factory patches through the voicecard code.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include "avrlib/base.h"
#include "voicecard/voice.h"
#include "voicecard/audio_out.h"
#include "voicecard/resources.h"
namespace ambika { CaptureBuffer audio_buffer; }
using namespace ambika;

// 144-byte patch payload out of a .PAT: RIFF 8 + MBKS 4 + name 24 + obj 12
static bool load_pat(const char* path, uint8_t* out144, char* name16) {
  FILE* f = fopen(path, "rb"); if (!f) return false;
  uint8_t d[256]; size_t n = fread(d, 1, sizeof d, f); fclose(f);
  if (n < 192 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "MBKS", 4)) return false;
  memcpy(name16, d + 20, 16); name16[16] = 0;
  memcpy(out144, d + 48, 144);
  return true;
}
static void wav(const char* path, const std::vector<int16_t>& s, int fs){
  FILE* f=fopen(path,"wb");
  int n=s.size()*2, hdr=36+n;
  fwrite("RIFF",1,4,f); fwrite(&hdr,4,1,f); fwrite("WAVEfmt ",1,8,f);
  int sz=16; short fmt=1, ch=1, bits=16; int br=fs*2; short al=2;
  fwrite(&sz,4,1,f); fwrite(&fmt,2,1,f); fwrite(&ch,2,1,f); fwrite(&fs,4,1,f);
  fwrite(&br,4,1,f); fwrite(&al,2,1,f); fwrite(&bits,2,1,f);
  fwrite("data",1,4,f); fwrite(&n,4,1,f); fwrite(s.data(),2,s.size(),f);
  fclose(f); printf("  %s  (%.1f s)\n", path, s.size()/(double)fs);
}
int main(int argc, char** argv){
  const char* out = argv[1];
  std::vector<int16_t> all;
  for (int a = 2; a < argc; a += 2) {
    const char* pat = argv[a];
    uint8_t note = atoi(argv[a+1]);
    uint8_t data[144]; char nm[20];
    if (!load_pat(pat, data, nm)) { printf("  !! cannot read %s\n", pat); continue; }
    voice.Init();
    memcpy(voice.mutable_patch_data(), data, 144);
    voice.ResetEngines();
    voice.Trigger((note + 12) << 7, 110, 0);
    printf("    %-16s note %d  engine=%d\n", nm, note, data[106]);
    int blocks = 0;
    for (; blocks < 1470; ++blocks) {          // 1.5 s held
      audio_buffer.size = 0; voice.ProcessBlock();
      for (int i=0;i<audio_buffer.size;++i)
        all.push_back((int16_t)((audio_buffer.data[i] - 2048) * 14));
    }
    voice.Release();                            // let it decay
    for (int b = 0; b < 980; ++b) {
      audio_buffer.size = 0; voice.ProcessBlock();
      for (int i=0;i<audio_buffer.size;++i)
        all.push_back((int16_t)((audio_buffer.data[i] - 2048) * 14));
    }
    for (int i=0;i<3000;++i) all.push_back(0);
  }
  wav(out, all, 39200);
  return 0;
}
