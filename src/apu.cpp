#include "apu.h"

namespace gb {

namespace {

constexpr std::uint8_t kDutyTable[4] = {0x01, 0x81, 0x87, 0x7E};  // bit i = duty step i

// Readback OR-masks for FF10-FF26 (unused bits read as 1).
constexpr std::uint8_t kReadMask[0x17] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF,  // NR10-NR14
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,  // FF15, NR21-NR24
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,  // NR30-NR34
    0xFF, 0xFF, 0x00, 0x00, 0xBF,  // FF1F, NR41-NR44
    0x00, 0x00, 0x70,              // NR50-NR52
};

int noise_period(std::uint8_t nr43) {
  const int r = nr43 & 7;
  const int divisor = r == 0 ? 8 : r * 16;
  return divisor << (nr43 >> 4);
}

}  // namespace

Apu::Apu() {
  // Post-boot register values; channels start idle (games retrigger anyway).
  regs_[0x00] = 0x80;
  regs_[0x01] = 0xBF;
  regs_[0x02] = 0xF3;
  regs_[0x06] = 0x3F;
  regs_[0x0A] = 0x7F;
  regs_[0x0B] = 0xFF;
  regs_[0x0C] = 0x9F;
  regs_[0x10] = 0xFF;
  regs_[0x14] = 0x77;
  regs_[0x15] = 0xF3;
  p1_.nr1 = 0xBF;
  p1_.nr2 = 0xF3;
  nr50_ = 0x77;
  nr51_ = 0xF3;
}

void Apu::power_off() {
  p1_ = Pulse{};
  p2_ = Pulse{};
  nr10_ = 0;
  sweep_enabled_ = false;
  wave_dac_ = false;
  nr32_ = 0;
  wave_freq_ = 0;
  wave_length_enable_ = false;
  wave_enabled_ = false;
  nr42_ = 0;
  nr43_ = 0;
  noise_length_enable_ = false;
  noise_enabled_ = false;
  nr50_ = 0;
  nr51_ = 0;
  fs_step_ = 0;
  fs_counter_ = 0;
  regs_.fill(0);
}

void Apu::clock_length() {
  auto clock_one = [](bool enable, int& length, bool& enabled) {
    if (enable && length > 0 && --length == 0) enabled = false;
  };
  clock_one(p1_.length_enable, p1_.length, p1_.enabled);
  clock_one(p2_.length_enable, p2_.length, p2_.enabled);
  clock_one(wave_length_enable_, wave_length_, wave_enabled_);
  clock_one(noise_length_enable_, noise_length_, noise_enabled_);
}

void Apu::clock_envelope_one(Pulse& p) {
  const int period = p.nr2 & 7;
  if (period == 0) return;
  if (--p.env_timer <= 0) {
    p.env_timer = period;
    if ((p.nr2 & 8) && p.volume < 15) ++p.volume;
    if (!(p.nr2 & 8) && p.volume > 0) --p.volume;
  }
}

void Apu::clock_envelopes() {
  clock_envelope_one(p1_);
  clock_envelope_one(p2_);
  const int period = nr42_ & 7;
  if (period != 0 && --noise_env_timer_ <= 0) {
    noise_env_timer_ = period;
    if ((nr42_ & 8) && noise_volume_ < 15) ++noise_volume_;
    if (!(nr42_ & 8) && noise_volume_ > 0) --noise_volume_;
  }
}

int Apu::sweep_calc() {
  const int shift = nr10_ & 7;
  int delta = sweep_shadow_ >> shift;
  int next = (nr10_ & 8) ? sweep_shadow_ - delta : sweep_shadow_ + delta;
  if (next > 2047) p1_.enabled = false;
  return next;
}

void Apu::clock_sweep() {
  if (!sweep_enabled_) return;
  if (--sweep_timer_ > 0) return;
  const int period = (nr10_ >> 4) & 7;
  sweep_timer_ = period == 0 ? 8 : period;
  if (period == 0) return;
  const int next = sweep_calc();
  if (next <= 2047 && (nr10_ & 7) != 0) {
    sweep_shadow_ = static_cast<std::uint16_t>(next);
    p1_.freq = static_cast<std::uint16_t>(next);
    sweep_calc();  // second overflow check, result discarded
  }
}

void Apu::step_frame_sequencer() {
  switch (fs_step_) {
    case 0: clock_length(); break;
    case 2: clock_length(); clock_sweep(); break;
    case 4: clock_length(); break;
    case 6: clock_length(); clock_sweep(); break;
    case 7: clock_envelopes(); break;
    default: break;
  }
  fs_step_ = (fs_step_ + 1) & 7;
}

void Apu::trigger_pulse(Pulse& p, bool with_sweep) {
  p.enabled = p.dac_on();
  if (p.length == 0) p.length = 64;
  p.timer = (2048 - p.freq) * 4;
  p.volume = p.nr2 >> 4;
  p.env_timer = p.nr2 & 7;
  if (with_sweep) {
    sweep_shadow_ = p.freq;
    const int period = (nr10_ >> 4) & 7;
    sweep_timer_ = period == 0 ? 8 : period;
    sweep_enabled_ = period != 0 || (nr10_ & 7) != 0;
    if ((nr10_ & 7) != 0) sweep_calc();
  }
}

void Apu::trigger_wave() {
  wave_enabled_ = wave_dac_;
  if (wave_length_ == 0) wave_length_ = 256;
  wave_timer_ = (2048 - wave_freq_) * 2;
  wave_pos_ = 0;
}

void Apu::trigger_noise() {
  noise_enabled_ = (nr42_ & 0xF8) != 0;
  if (noise_length_ == 0) noise_length_ = 64;
  noise_timer_ = noise_period(nr43_);
  noise_volume_ = nr42_ >> 4;
  noise_env_timer_ = nr42_ & 7;
  lfsr_ = 0x7FFF;
}

void Apu::emit_sample() {
  int out[4] = {0, 0, 0, 0};
  if (p1_.enabled && p1_.dac_on()) {
    const int bit = (kDutyTable[p1_.nr1 >> 6] >> p1_.duty_pos) & 1;
    out[0] = (bit ? p1_.volume : 0) * 2 - 15;
  }
  if (p2_.enabled && p2_.dac_on()) {
    const int bit = (kDutyTable[p2_.nr1 >> 6] >> p2_.duty_pos) & 1;
    out[1] = (bit ? p2_.volume : 0) * 2 - 15;
  }
  if (wave_enabled_ && wave_dac_) {
    static constexpr int kShift[4] = {4, 0, 1, 2};
    out[2] = (wave_sample_ >> kShift[(nr32_ >> 5) & 3]) * 2 - 15;
  }
  if (noise_enabled_ && (nr42_ & 0xF8) != 0) {
    out[3] = ((~lfsr_ & 1) ? noise_volume_ : 0) * 2 - 15;
  }

  int left = 0, right = 0;
  for (int ch = 0; ch < 4; ++ch) {
    if (nr51_ & (0x10 << ch)) left += out[ch];
    if (nr51_ & (0x01 << ch)) right += out[ch];
  }
  left *= ((nr50_ >> 4) & 7) + 1;
  right *= (nr50_ & 7) + 1;

  // ~30Hz high-pass at 48kHz (charge factor exp(-2*pi*30/48000)).
  constexpr double kCharge = 0.9961;
  const double l_in = left * 24.0;
  const double r_in = right * 24.0;
  const double l_out = l_in - cap_left_;
  const double r_out = r_in - cap_right_;
  cap_left_ = l_in - l_out * kCharge;
  cap_right_ = r_in - r_out * kCharge;
  const auto clamp = [](double v) {
    if (v > 32767.0) return std::int16_t{32767};
    if (v < -32768.0) return std::int16_t{-32768};
    return static_cast<std::int16_t>(v);
  };
  samples_.push_back(clamp(l_out));
  samples_.push_back(clamp(r_out));
  if (samples_.size() > kSampleRate * 4)  // ~1s undrained: keep the tail
    samples_.erase(samples_.begin(), samples_.begin() + kSampleRate * 2);
}

void Apu::tick(int tcycles) {
  for (int i = 0; i < tcycles; ++i) {
    if (power_) {
      if (++fs_counter_ >= 8192) {
        fs_counter_ = 0;
        step_frame_sequencer();
      }
      if (--p1_.timer <= 0) {
        p1_.timer += (2048 - p1_.freq) * 4;
        p1_.duty_pos = (p1_.duty_pos + 1) & 7;
      }
      if (--p2_.timer <= 0) {
        p2_.timer += (2048 - p2_.freq) * 4;
        p2_.duty_pos = (p2_.duty_pos + 1) & 7;
      }
      if (--wave_timer_ <= 0) {
        wave_timer_ += (2048 - wave_freq_) * 2;
        wave_pos_ = (wave_pos_ + 1) & 31;
        const std::uint8_t byte = wave_ram_[wave_pos_ >> 1];
        wave_sample_ = (wave_pos_ & 1) ? (byte & 0x0F) : (byte >> 4);
      }
      if (--noise_timer_ <= 0) {
        noise_timer_ += noise_period(nr43_);
        const std::uint16_t bit = (lfsr_ ^ (lfsr_ >> 1)) & 1;
        lfsr_ = static_cast<std::uint16_t>((lfsr_ >> 1) | (bit << 14));
        if (nr43_ & 8)
          lfsr_ = static_cast<std::uint16_t>((lfsr_ & ~0x40) | (bit << 6));
      }
    }
    sample_acc_ += kSampleRate;
    if (sample_acc_ >= kClockRate) {
      sample_acc_ -= kClockRate;
      emit_sample();
    }
  }
}

std::vector<std::int16_t> Apu::take_samples() {
  std::vector<std::int16_t> out;
  out.swap(samples_);
  return out;
}

std::uint8_t Apu::read(std::uint16_t addr) const {
  if (addr >= 0xFF30 && addr <= 0xFF3F) return wave_ram_[addr - 0xFF30];
  if (addr == 0xFF26) {
    std::uint8_t v = 0x70;
    if (power_) v |= 0x80;
    if (p1_.enabled) v |= 0x01;
    if (p2_.enabled) v |= 0x02;
    if (wave_enabled_) v |= 0x04;
    if (noise_enabled_) v |= 0x08;
    return v;
  }
  if (addr >= 0xFF10 && addr <= 0xFF25)
    return regs_[addr - 0xFF10] | kReadMask[addr - 0xFF10];
  return 0xFF;  // FF27-FF2F
}

void Apu::write(std::uint16_t addr, std::uint8_t value) {
  if (addr >= 0xFF30 && addr <= 0xFF3F) {
    wave_ram_[addr - 0xFF30] = value;
    return;
  }
  if (addr == 0xFF26) {
    const bool on = value & 0x80;
    if (power_ && !on) power_off();
    power_ = on;
    return;
  }
  if (!power_ || addr < 0xFF10 || addr > 0xFF25) return;
  regs_[addr - 0xFF10] = value;

  switch (addr) {
    case 0xFF10:
      nr10_ = value;
      break;
    case 0xFF11:
      p1_.nr1 = value;
      p1_.length = 64 - (value & 0x3F);
      break;
    case 0xFF12:
      p1_.nr2 = value;
      if (!p1_.dac_on()) p1_.enabled = false;
      break;
    case 0xFF13:
      p1_.freq = static_cast<std::uint16_t>((p1_.freq & 0x700) | value);
      break;
    case 0xFF14:
      p1_.freq = static_cast<std::uint16_t>((p1_.freq & 0xFF) | ((value & 7) << 8));
      p1_.length_enable = value & 0x40;
      if (value & 0x80) trigger_pulse(p1_, true);
      break;
    case 0xFF16:
      p2_.nr1 = value;
      p2_.length = 64 - (value & 0x3F);
      break;
    case 0xFF17:
      p2_.nr2 = value;
      if (!p2_.dac_on()) p2_.enabled = false;
      break;
    case 0xFF18:
      p2_.freq = static_cast<std::uint16_t>((p2_.freq & 0x700) | value);
      break;
    case 0xFF19:
      p2_.freq = static_cast<std::uint16_t>((p2_.freq & 0xFF) | ((value & 7) << 8));
      p2_.length_enable = value & 0x40;
      if (value & 0x80) trigger_pulse(p2_, false);
      break;
    case 0xFF1A:
      wave_dac_ = value & 0x80;
      if (!wave_dac_) wave_enabled_ = false;
      break;
    case 0xFF1B:
      wave_length_ = 256 - value;
      break;
    case 0xFF1C:
      nr32_ = value;
      break;
    case 0xFF1D:
      wave_freq_ = static_cast<std::uint16_t>((wave_freq_ & 0x700) | value);
      break;
    case 0xFF1E:
      wave_freq_ = static_cast<std::uint16_t>((wave_freq_ & 0xFF) | ((value & 7) << 8));
      wave_length_enable_ = value & 0x40;
      if (value & 0x80) trigger_wave();
      break;
    case 0xFF20:
      noise_length_ = 64 - (value & 0x3F);
      break;
    case 0xFF21:
      nr42_ = value;
      if ((nr42_ & 0xF8) == 0) noise_enabled_ = false;
      break;
    case 0xFF22:
      nr43_ = value;
      break;
    case 0xFF23:
      noise_length_enable_ = value & 0x40;
      if (value & 0x80) trigger_noise();
      break;
    case 0xFF24:
      nr50_ = value;
      break;
    case 0xFF25:
      nr51_ = value;
      break;
    default:
      break;
  }
}

}  // namespace gb
