#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace gb {

// DMG APU: two pulse channels (CH1 with frequency sweep), the 4-bit wave
// channel, and LFSR noise, clocked per T-cycle with the 512Hz frame
// sequencer, mixed per NR50/NR51 into interleaved stereo int16 at
// kSampleRate. Simplifications (documented in the README): the frame
// sequencer runs from its own 8192-cycle counter rather than DIV bit 4, and
// the obscure length/sweep power-cycle edge cases are not modeled — games
// sound right; blargg's dmg_sound suite is not a claimed target.
class Apu {
 public:
  static constexpr int kSampleRate = 48000;
  static constexpr int kClockRate = 4194304;

  Apu();

  void tick(int tcycles);

  std::uint8_t read(std::uint16_t addr) const;
  void write(std::uint16_t addr, std::uint8_t value);

  // Drains generated samples (interleaved L/R). When nobody drains — the
  // headless runner without --wav — the buffer self-bounds.
  std::vector<std::int16_t> take_samples();

 private:
  struct Pulse {
    std::uint8_t nr1 = 0;  // duty/length load
    std::uint8_t nr2 = 0;  // envelope
    std::uint16_t freq = 0;
    bool length_enable = false;
    bool enabled = false;
    int timer = 0;
    int duty_pos = 0;
    int volume = 0;
    int env_timer = 0;
    int length = 0;
    bool dac_on() const { return (nr2 & 0xF8) != 0; }
  };

  void step_frame_sequencer();
  void clock_length();
  void clock_envelopes();
  void clock_sweep();
  int sweep_calc();  // next CH1 frequency; disables CH1 on overflow
  void trigger_pulse(Pulse& p, bool with_sweep);
  void trigger_wave();
  void trigger_noise();
  void clock_envelope_one(Pulse& p);
  void power_off();
  void emit_sample();

  bool power_ = true;
  std::uint8_t nr50_ = 0x77;
  std::uint8_t nr51_ = 0xF3;

  Pulse p1_, p2_;

  // CH1 sweep
  std::uint8_t nr10_ = 0;
  std::uint16_t sweep_shadow_ = 0;
  int sweep_timer_ = 0;
  bool sweep_enabled_ = false;

  // CH3 wave
  bool wave_dac_ = false;
  std::uint8_t nr32_ = 0;
  std::uint16_t wave_freq_ = 0;
  bool wave_length_enable_ = false;
  bool wave_enabled_ = false;
  int wave_timer_ = 0;
  int wave_pos_ = 0;
  int wave_length_ = 0;
  std::uint8_t wave_sample_ = 0;
  std::array<std::uint8_t, 16> wave_ram_{};

  // CH4 noise
  std::uint8_t nr42_ = 0;
  std::uint8_t nr43_ = 0;
  bool noise_length_enable_ = false;
  bool noise_enabled_ = false;
  int noise_timer_ = 0;
  int noise_volume_ = 0;
  int noise_env_timer_ = 0;
  int noise_length_ = 0;
  std::uint16_t lfsr_ = 0x7FFF;

  int fs_counter_ = 0;
  int fs_step_ = 0;
  int sample_acc_ = 0;

  // High-pass "capacitor" per side, as on hardware: strips the DC offset
  // that enabled-but-quiet DACs otherwise leave in the output.
  double cap_left_ = 0.0;
  double cap_right_ = 0.0;

  // Raw register bytes for readback (OR-masked); NR52 is composed live.
  std::array<std::uint8_t, 0x17> regs_{};

  std::vector<std::int16_t> samples_;
};

}  // namespace gb
