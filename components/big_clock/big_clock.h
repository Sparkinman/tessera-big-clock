#pragma once
#include <vector>
#include "esphome/core/component.h"
#include "esphome/components/font/font.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::big_clock {

// The plugin: a tile ("clock") and a screensaver face ("clock") with the time in large Roboto.
class BigClock : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // The time's fonts from plugin.yaml, smallest first.
  void add_font(font::Font *f) { fonts_.push_back(f); }
  const std::vector<font::Font *> &fonts() const { return fonts_; }
  // A larger Roboto for the date than the screen's own fonts, from plugin.yaml.
  void set_date_font(font::Font *f) { date_font_ = f; }
  font::Font *date_font() const { return date_font_; }
  // The screensaver's still larger date.
  void set_saver_date_font(font::Font *f) { saver_date_font_ = f; }
  font::Font *saver_date_font() const { return saver_date_font_; }
  // AM/PM in a few sizes, smallest first, and a bold font for what plays on the music face.
  void add_ampm_font(font::Font *f) { ampm_fonts_.push_back(f); }
  const std::vector<font::Font *> &ampm_fonts() const { return ampm_fonts_; }
  void set_title_font(font::Font *f) { title_font_ = f; }
  font::Font *title_font() const { return title_font_; }

 protected:
  std::vector<font::Font *> fonts_;
  font::Font *date_font_{nullptr};
  font::Font *saver_date_font_{nullptr};
  std::vector<font::Font *> ampm_fonts_;
  font::Font *title_font_{nullptr};
};

}  // namespace esphome::big_clock
