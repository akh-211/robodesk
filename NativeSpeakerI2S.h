#pragma once

#include <Arduino.h>
#include "driver/i2s_std.h"

// RoboDesk realtime speaker transport.
// Uses the native ESP-IDF channel API so DMA depth is explicit instead of
// inheriting ESP_I2S defaults (6 descriptors x 240 frames).
class RoboDeskNativeSpeakerI2S {
 public:
  RoboDeskNativeSpeakerI2S() = default;
  ~RoboDeskNativeSpeakerI2S() { end(); }

  bool begin(i2s_port_t port, uint32_t sampleRate, int bclk, int ws, int dout) {
    end();
    port_ = port;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    chan.dma_desc_num = 6;    // 6 * 20 ms = ~120 ms DMA cushion; dedicated task removes main-loop jitter.
    chan.dma_frame_num = 480; // 20 ms @ 24 kHz; 1920 B stereo16 <= 4092 B DMA limit.
    chan.auto_clear = true;
    if (i2s_new_channel(&chan, &tx_, nullptr) != ESP_OK || !tx_) {
      tx_ = nullptr;
      return false;
    }

    i2s_std_config_t cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = gpio_num_t(bclk),
        .ws = gpio_num_t(ws),
        .dout = gpio_num_t(dout),
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
          .mclk_inv = false,
          .bclk_inv = false,
          .ws_inv = false,
        },
      },
    };
    if (i2s_channel_init_std_mode(tx_, &cfg) != ESP_OK) { end(); return false; }
    if (i2s_channel_enable(tx_) != ESP_OK) { end(); return false; }
    return true;
  }

  size_t write(const void* data, size_t bytes, uint32_t timeoutMs = 100) {
    if (!tx_ || !data || !bytes) return 0;
    size_t written = 0;
    // A timeout can still accept a prefix. Report it so the feeder retries
    // only the remaining PCM instead of losing or duplicating samples.
    i2s_channel_write(tx_, data, bytes, &written, timeoutMs);
    return written;
  }

  void end() {
    if (!tx_) return;
    i2s_channel_disable(tx_);
    i2s_del_channel(tx_);
    tx_ = nullptr;
  }

  int port() const { return int(port_); }
  bool valid() const { return tx_ != nullptr; }
  static constexpr uint16_t dmaDescriptors() { return 6; }
  static constexpr uint16_t dmaFrames() { return 480; }
  static constexpr uint32_t dmaDurationMs(uint32_t rate) {
    return (uint32_t(dmaDescriptors())*dmaFrames()*1000u+rate-1u)/rate;
  }

 private:
  i2s_chan_handle_t tx_ = nullptr;
  i2s_port_t port_ = I2S_NUM_1;
};
