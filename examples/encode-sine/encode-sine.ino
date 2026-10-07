// Encodes a 1 kHz sine tone to AAC-LC (ADTS) and prints the size of each frame
#include "faac.h"
#include <math.h>

const uint32_t sample_rate = 44100;
const uint32_t channels = 1;

faac_encoder *enc = nullptr;
faac_encoder_info info;
int16_t *pcm = nullptr;
uint8_t *out = nullptr;
float phase = 0.0f;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  faac_params p;
  faac_params_init(&p, sizeof(p));
  p.sample_rate = sample_rate;
  p.num_channels = channels;
  p.bit_rate = 64000;  // per channel
  p.output_format = FAAC_STREAM_ADTS;
  faac_status st = faac_encoder_open(&p, &enc);
  if (st < 0) {
    Serial.print("faac_encoder_open: ");
    Serial.println(faac_strerror(st));
    while (true);
  }

  info.struct_size = sizeof(info);
  faac_encoder_get_info(enc, &info);
  pcm = new int16_t[info.frame_samples * channels];
  out = new uint8_t[info.max_output_bytes];
  Serial.print("frame samples: ");
  Serial.println(info.frame_samples);
}

void loop() {
  uint32_t samples = info.frame_samples * channels;
  for (uint32_t i = 0; i < samples; i += channels) {
    int16_t v = 16000 * sinf(phase);
    for (uint32_t ch = 0; ch < channels; ch++) pcm[i + ch] = v;
    phase += 2.0f * M_PI * 1000.0f / sample_rate;
    if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
  }

  uint32_t written = 0;
  uint32_t start = millis();
  faac_status st = faac_encoder_encode(enc, pcm, samples, out,
                                       info.max_output_bytes, &written);
  if (st < 0) {
    Serial.println(faac_strerror(st));
    return;
  }
  Serial.print("encoded bytes: ");
  Serial.print(written);
  Serial.print(" in ms: ");
  Serial.println(millis() - start);
}
