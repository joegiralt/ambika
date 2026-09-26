// Host stand-in: captures what Voice::ProcessBlock writes.
#ifndef VOICECARD_AUDIO_OUT_H_
#define VOICECARD_AUDIO_OUT_H_
#include "avrlib/base.h"
#include "voicecard/voicecard.h"

namespace ambika {

// 12-bit samples, centered on 2048, as sent to the DAC.
struct CaptureBuffer {
  uint16_t data[kAudioBlockSize];
  uint8_t size;
  void Overwrite2(uint16_t a, uint16_t b) { data[size++] = a; data[size++] = b; }
};
extern CaptureBuffer audio_buffer;

}  // namespace ambika

#endif
