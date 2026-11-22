#include "big_clock.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace esphome::big_clock {

using tessera::Font;
namespace ui = tessera::ui;

// Days since 1970-01-01 of a date (Howard Hinnant's algorithm): gives the weekday without depending on how the API
// numbers it.
static int32_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

static int text_w(const char *text, const lv_font_t *font) {
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}
static int ascent(const lv_font_t *font) { return font->line_height - font->base_line; }
// Where a font's digits stand in its line, from the glyph of a 0: how far their top is below the line's top, and how
// tall they are. The layout fits the digits themselves, not the empty space a line keeps above and below them.
static void glyph_box(const lv_font_t *font, uint32_t letter, int &top, int &h) {
  lv_font_glyph_dsc_t g{};
  if (lv_font_get_glyph_dsc(font, &g, letter, 0) && g.box_h > 0) {
    h = g.box_h;
    top = ascent(font) - g.box_h - g.ofs_y;
  } else {
    h = font->line_height;
    top = 0;
  }
}
static void digit_box(const lv_font_t *font, int &top, int &h) { glyph_box(font, '0', top, h); }
static int cap_h(const lv_font_t *font) {
  int top, h;
  glyph_box(font, 'M', top, h);
  return h;
}
static int cap_top(const lv_font_t *font) {
  int top, h;
  glyph_box(font, 'M', top, h);
  return top;
}
static int digit_h(const lv_font_t *font) {
  int top, h;
  digit_box(font, top, h);
  return h;
}

// The clock, for the tile and the screensaver alike: the time in large Roboto (sizes the plugin brings, see
// plugin.yaml), the largest that fits, the AM/PM beside it where the screen keeps a 12-hour clock, and the weekday and
// date in the largest font that fits under it. The time sits in a box exactly as tall as its digits, so it is centred
// on what shows rather than on its line. The screensaver gets a larger date and keeps more air around the time.
class ClockView {
 public:
  explicit ClockView(const BigClock *plugin) : plugin_(plugin) {}

  void build(lv_obj_t *parent, int width, int height, JsonObjectConst options, bool saver) {
    width_ = width;
    height_ = height;
    const std::string date = options["date"] | "long";
    date_mode_ = date == "none" ? 0 : date == "short" ? 1 : 2;
    seconds_ = options["seconds"] | false;

    // Is the screen on a 12-hour clock? Its own clock text of a fixed afternoon moment says so.
    const std::string sample = tessera::clock_text(13 * 3600 + 7 * 60);
    twelve_ = sample.find('M') != std::string::npos || sample.find('m') != std::string::npos;

    gap_ = ui::px(ui::large() ? 10 : 6);

    // The date line: the largest font that keeps it to about a fifth of the tile and fits the width with a long date.
    int date_h = 0;
    if (date_mode_) {
      const std::string longest = date_mode_ == 2 ? "Wednesday, September 30" : "Wed Sep 30";
      // The plugin's own larger Roboto first, then the screen's fonts.
      std::vector<const lv_font_t *> fonts;
      if (saver && plugin_->saver_date_font()) fonts.push_back(plugin_->saver_date_font()->get_lv_font());
      if (plugin_->date_font()) fonts.push_back(plugin_->date_font()->get_lv_font());
      for (Font f : {Font::HEADLINE, Font::TITLE, Font::BODY_LARGE, Font::BODY}) fonts.push_back(ui::font(f));
      for (const lv_font_t *f : fonts) {
        date_font_ = f;
        if (f->line_height <= height_ / (saver ? 6 : 4) && text_w(longest.c_str(), f) <= width_ * 9 / 10) break;
      }
      date_h = date_font_->line_height;
      if (height_ - date_h - gap_ < height_ / 2) date_h = 0;  // too short a tile for both: the time wins
    }
    date_gap_ = (saver ? 4 : 2) * gap_;
    // The time keeps a little air: it fills at most 85% of the height the date leaves (60% on the screensaver).
    const int room_h = (height_ - (date_h ? date_h + date_gap_ : 0)) * (saver ? 60 : 85) / 100;

    // AM/PM: the largest of the plugin's own sizes whose capitals stand about a third as tall as the digits, else the
    // screen's own fonts.
    std::vector<const lv_font_t *> ampm;
    for (auto it = plugin_->ampm_fonts().rbegin(); it != plugin_->ampm_fonts().rend(); ++it) ampm.push_back((*it)->get_lv_font());
    ampm.push_back(ui::font(Font::TITLE));
    ampm.push_back(ui::font(Font::BODY));
    auto ampm_for = [&](int digits) {
      for (const lv_font_t *f : ampm)
        if (cap_h(f) * 100 <= digits * 38) return f;
      return ampm.back();
    };
    auto ampm_w = [&](const lv_font_t *f) { return std::max(text_w("AM", f), text_w("PM", f)); };

    // The time's font: the largest of the plugin's own that fits, else the screen's VALUE font.
    const char *widest = twelve_ ? "12:59" : "23:59";
    const lv_font_t *fallback = ui::font(Font::VALUE);
    time_font_ = fallback;
    std::vector<const lv_font_t *> sizes;
    for (font::Font *f : plugin_->fonts()) sizes.push_back(f->get_lv_font());
    for (auto it = sizes.rbegin(); it != sizes.rend(); ++it) {
      const lv_font_t *f = *it;
      int w = text_w(widest, f) + (twelve_ ? gap_ + ampm_w(ampm_for(digit_h(f))) : 0);
      if (seconds_) w += gap_ + text_w("00", seconds_font_for(f, sizes, fallback));
      if (digit_h(f) <= room_h && w <= width_ * 92 / 100) {
        time_font_ = f;
        break;
      }
    }
    if (seconds_) secs_font_ = seconds_font_for(time_font_, sizes, fallback);
    int digit_top = 0;
    digit_box(time_font_, digit_top, digits_h_);
    if (twelve_) {
      suffix_font_ = ampm_for(digits_h_);
      suffix_w_ = ampm_w(suffix_font_);
    }

    const int block_h = digits_h_ + (date_h ? date_gap_ + date_h : 0);
    top_ = std::max(0, (height_ - block_h) / 2);

    // The box: as wide as the tile and as tall as the digits. It never clips them; it only gives the layout their
    // real top and bottom instead of their line's.
    box_ = lv_obj_create(parent);
    lv_obj_remove_style_all(box_);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_size(box_, width_, digits_h_);
    lv_obj_set_pos(box_, 0, top_);

    time_ = ui::label(box_, Font::VALUE, theme::INK);
    lv_obj_set_style_text_font(time_, time_font_, 0);
    lv_obj_set_size(time_, LV_SIZE_CONTENT, time_font_->line_height);
    time_y_ = -digit_top;
    baseline_ = time_y_ + ascent(time_font_);
    if (seconds_) {
      secs_ = ui::label(box_, Font::VALUE, theme::MUTED);
      lv_obj_set_style_text_font(secs_, secs_font_, 0);
      lv_obj_set_size(secs_, LV_SIZE_CONTENT, secs_font_->line_height);
      secs_w_ = text_w("00", secs_font_);
    }

    if (twelve_) {
      suffix_ = ui::label(parent, Font::TITLE, theme::INK_SOFT);
      lv_obj_set_style_text_font(suffix_, suffix_font_, 0);
      lv_obj_set_size(suffix_, suffix_w_ + 2, suffix_font_->line_height);
    }
    if (date_h) {
      date_ = ui::label(parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_font(date_, date_font_, 0);
      lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_size(date_, width_, date_h);
      lv_obj_set_pos(date_, 0, top_ + digits_h_ + date_gap_);
    }
    note_ = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(note_, width_, ui::line_height(Font::BODY));
    lv_obj_set_pos(note_, 0, (height_ - ui::line_height(Font::BODY)) / 2);
    lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
    show_all(false);
    recolor();
  }

  void tick(uint32_t epoch) {
    if (!epoch) {
      // No time from Home Assistant yet: a word instead of a wrong time.
      show_all(false);
      ui::set_text(note_, plugin_->text("waiting"));
      lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    const tessera::LocalTime t = tessera::local_time(epoch);
    int hour = t.hour;
    const bool pm = hour >= 12;
    if (twelve_) {
      hour %= 12;
      if (hour == 0) hour = 12;
    }
    char buf[8];
    // A 12-hour clock writes 9:05, not 09:05.
    snprintf(buf, sizeof buf, twelve_ ? "%d:%02d" : "%02d:%02d", hour, static_cast<int>(t.minute));
    if (time_text_ != buf) {
      time_text_ = buf;
      ui::set_text(time_, time_text_);
      layout();
    }
    if (secs_) {
      snprintf(buf, sizeof buf, "%02d", static_cast<int>(t.second));
      ui::set_text(secs_, std::string(buf));
    }
    if (suffix_) ui::set_text(suffix_, plugin_->text(pm ? "pm" : "am"));
    if (date_) ui::set_text(date_, date_text(t));
    if (!visible_) {
      lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
      show_all(true);
    }
  }

  void recolor() {
    if (time_) ui::set_color(time_, theme::INK);
    if (suffix_) ui::set_color(suffix_, theme::INK_SOFT);
    for (lv_obj_t *o : {secs_, date_, note_})
      if (o) ui::set_color(o, theme::MUTED);
  }

 private:
  // The seconds' font: the largest of the sizes at most half the time's, else the screen's VALUE font.
  static const lv_font_t *seconds_font_for(const lv_font_t *time, const std::vector<const lv_font_t *> &sizes,
                                           const lv_font_t *fallback) {
    const lv_font_t *best = fallback;
    for (const lv_font_t *f : sizes)
      if (f->line_height * 2 <= time->line_height + 2 && f->line_height > best->line_height) best = f;
    return best;
  }

  // The time, seconds and AM/PM centred across the tile for the text now shown.
  void layout() {
    const int tw = text_w(time_text_.c_str(), time_font_);
    const int total = tw + (secs_ ? gap_ + secs_w_ : 0) + (suffix_ ? gap_ + suffix_w_ : 0);
    const int left = std::max(0, (width_ - total) / 2);
    lv_obj_set_pos(time_, left, time_y_);
    int x = left + tw;
    if (secs_) {
      // On the time's baseline, the bottom of the digits.
      lv_obj_set_pos(secs_, x + gap_, baseline_ - ascent(secs_font_));
      x += gap_ + secs_w_;
    }
    // AM/PM with the top of its capitals on the top of the digits.
    if (suffix_) lv_obj_set_pos(suffix_, x + gap_, top_ - cap_top(suffix_font_));
  }

  void show_all(bool shown) {
    visible_ = shown;
    for (lv_obj_t *o : {box_, suffix_, date_})
      if (o) {
        if (shown) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      }
  }

  // "Saturday, November 21" or "Sat Nov 21", from the plugin's own words, falling back to the short form where the
  // long one is wider than the tile.
  std::string date_text(const tessera::LocalTime &t) const {
    static const char *const DAY[7] = {"d0", "d1", "d2", "d3", "d4", "d5", "d6"};  // Sunday first
    static const char *const DAY_SHORT[7] = {"s0", "s1", "s2", "s3", "s4", "s5", "s6"};
    static const char *const MONTH[12] = {"m1", "m2", "m3", "m4", "m5", "m6", "m7", "m8", "m9", "m10", "m11", "m12"};
    static const char *const MONTH_SHORT[12] = {"n1", "n2", "n3", "n4", "n5", "n6", "n7", "n8", "n9", "n10", "n11", "n12"};
    const int32_t days = days_from_civil(t.year, t.month, t.day);
    const int weekday = static_cast<int>(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday; 0 is Sunday
    const int m = std::min(12, std::max(1, static_cast<int>(t.month))) - 1;
    char day[4];
    snprintf(day, sizeof day, "%d", static_cast<int>(t.day));
    if (date_mode_ == 2) {
      std::string text = std::string(plugin_->text("date_long_format"));
      replace(text, "{weekday}", plugin_->text(DAY[weekday]));
      replace(text, "{month}", plugin_->text(MONTH[m]));
      replace(text, "{day}", day);
      if (text_w(text.c_str(), date_font_) <= width_) return text;
    }
    std::string text = std::string(plugin_->text("date_short_format"));
    replace(text, "{weekday}", plugin_->text(DAY_SHORT[weekday]));
    replace(text, "{month}", plugin_->text(MONTH_SHORT[m]));
    replace(text, "{day}", day);
    return text;
  }

  static void replace(std::string &text, const std::string &key, const std::string &value) {
    const size_t at = text.find(key);
    if (at != std::string::npos) text.replace(at, key.size(), value);
  }

  const BigClock *plugin_;
  int width_ = 0, height_ = 0, gap_ = 6, top_ = 0;
  int digits_h_ = 0, time_y_ = 0, baseline_ = 0, date_gap_ = 12, secs_w_ = 0, suffix_w_ = 0;
  int date_mode_ = 2;  // 0 none, 1 short, 2 long
  bool seconds_ = false, twelve_ = false, visible_ = true;
  const lv_font_t *time_font_{}, *secs_font_{}, *date_font_{}, *suffix_font_{};
  std::string time_text_;
  lv_obj_t *box_{}, *time_{}, *secs_{};
  lv_obj_t *suffix_{}, *date_{}, *note_{};
};

// The tile and the screensaver face: the same clock in a card, or on the whole black glass.
class ClockTile : public tessera::Tile {
 public:
  explicit ClockTile(const BigClock *plugin) : view_(plugin) {}
  void create(const tessera::TileContext &c) override { view_.build(c.parent, c.width, c.height, c.options, false); }
  void on_tick(uint32_t epoch) override { view_.tick(epoch); }
  void on_theme() override { view_.recolor(); }

 private:
  ClockView view_;
};

class ClockSaver : public tessera::Saver {
 public:
  explicit ClockSaver(const BigClock *plugin) : view_(plugin) {}
  void show(const tessera::SaverContext &c) override { view_.build(c.parent, c.width, c.height, c.options, true); }
  void on_tick(uint32_t epoch) override { view_.tick(epoch); }

 private:
  ClockView view_;
};

// A face of the screensaver's music: while something plays, the Big clock and what plays take turns every 30 seconds
// (or as set): the cover large at the left, the title in large bold letters and the artist beside it, the time small in
// the corner. When nothing plays the screensaver goes to its Clock step, where the Big clock face shows the time and date.
class NowPlayingSaver : public tessera::Saver {
 public:
  explicit NowPlayingSaver(const BigClock *plugin) : plugin_(plugin), clock_(plugin) {}

  void show(const tessera::SaverContext &c) override {
    w_ = c.width;
    h_ = c.height;
    const std::string every = c.options["every"] | "30";
    every_ = std::max(5, atoi(every.c_str()));
    clock_box_ = layer(c.parent);
    media_box_ = layer(c.parent);
    clock_.build(clock_box_, w_, h_, c.options, true);

    const int m = std::max(ui::px(16), h_ / 14);
    const int side = std::min(h_ - 2 * m, w_ * 42 / 100);
    if (ui::pictures()) {
      cover_ = ui::picture(media_box_);
      lv_obj_set_size(cover_, side, side);
      lv_obj_set_pos(cover_, m, (h_ - side) / 2);
      lv_obj_set_style_radius(cover_, ui::px(16), 0);
    }
    text_x_ = cover_ ? m + side + m : m;
    text_w_ = std::max(1, w_ - text_x_ - m);
    // The title: the plugin's bold Roboto when it brings one (with the screen's font for letters it lacks).
    title_font_ = ui::font(Font::HEADLINE);
    if (plugin_->title_font()) {
      lv_font_t *f = const_cast<lv_font_t *>(plugin_->title_font()->get_lv_font());
      f->fallback = ui::font(Font::HEADLINE);
      title_font_ = f;
    }
    title_ = ui::text(media_box_, Font::HEADLINE, theme::INK);
    lv_obj_set_style_text_font(title_, title_font_, 0);
    artist_ = ui::text(media_box_, Font::HEADLINE, theme::INK_SOFT);
    small_time_ = ui::label(media_box_, Font::HEADLINE, theme::MUTED);
    lv_obj_set_style_text_align(small_time_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_size(small_time_, w_ / 3, ui::line_height(Font::HEADLINE));
    lv_obj_set_pos(small_time_, w_ - w_ / 3 - m, m / 2);
    show_clock(true);
  }

  // The music step's player: what plays, the cover ("step:") and whether it plays.
  void on_state(JsonObjectConst data) override {
    const std::string state = data["state"] | "";
    playing_ = state == "playing";
    const std::string title = data["title"] | "";
    const std::string artist = data["artist"] | "";
    if (title != title_text_ || artist != artist_text_) {
      title_text_ = title;
      artist_text_ = artist;
      lay_out_words();
    }
    if (cover_) ui::set_picture(cover_, data["picture"] | "", tessera::Fit::COVER);
    if (!playing_ || title_text_.empty()) show_clock(true);
  }

  void on_tick(uint32_t epoch) override {
    clock_.tick(epoch);
    if (!epoch) return;
    const tessera::LocalTime t = tessera::local_time(epoch);
    char buf[12];
    const std::string sample = tessera::clock_text(13 * 3600 + 7 * 60);
    const bool twelve = sample.find('M') != std::string::npos || sample.find('m') != std::string::npos;
    int hour = t.hour;
    if (twelve) {
      hour %= 12;
      if (hour == 0) hour = 12;
    }
    snprintf(buf, sizeof buf, twelve ? "%d:%02d" : "%02d:%02d", hour, static_cast<int>(t.minute));
    ui::set_text(small_time_, std::string(buf));
    if (!switched_at_) switched_at_ = epoch;
    if (playing_ && !title_text_.empty() && epoch >= switched_at_ + every_) {
      show_clock(!clock_shown_);
      switched_at_ = epoch;
    }
  }

 private:
  static lv_obj_t *layer(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(o, 0, 0);
    return o;
  }

  void show_clock(bool clock) {
    clock_shown_ = clock;
    if (clock) {
      lv_obj_remove_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(media_box_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(media_box_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  static int words_h(const std::string &text, const lv_font_t *font, int width) {
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), font, 0, 0, width, LV_TEXT_FLAG_NONE);
    return size.y;
  }

  // The title (at most three lines of the bold font, else the screen's) and the artist under it, in the middle of the
  // column beside the cover.
  void lay_out_words() {
    const lv_font_t *tf = title_font_;
    if (words_h(title_text_, tf, text_w_) > tf->line_height * 3) tf = ui::font(Font::HEADLINE);
    lv_obj_set_style_text_font(title_, tf, 0);
    ui::set_text(title_, title_text_);
    ui::set_text(artist_, artist_text_);
    const int th = std::min<int>(words_h(title_text_, tf, text_w_), tf->line_height * 3);
    const lv_font_t *af = ui::font(Font::HEADLINE);
    const int ah = artist_text_.empty() ? 0 : std::min<int>(words_h(artist_text_, af, text_w_), af->line_height * 2);
    const int gap = ui::px(12);
    const int block = th + (ah ? gap + ah : 0);
    const int y = std::max(0, (h_ - block) / 2);
    lv_obj_set_size(title_, text_w_, th);
    lv_obj_set_pos(title_, text_x_, y);
    lv_obj_set_size(artist_, text_w_, ah);
    lv_obj_set_pos(artist_, text_x_, y + th + gap);
  }

  const BigClock *plugin_;
  ClockView clock_;
  int w_ = 0, h_ = 0, text_x_ = 0, text_w_ = 0;
  uint32_t every_ = 30, switched_at_ = 0;
  bool playing_ = false, clock_shown_ = true;
  std::string title_text_, artist_text_;
  const lv_font_t *title_font_{};
  lv_obj_t *clock_box_{}, *media_box_{}, *cover_{}, *title_{}, *artist_{}, *small_time_{};
};

void BigClock::setup() {
  add_tile("clock", [this]() { return new ClockTile(this); });
  // The id and step are the manifest's: a face of the screensaver's clock.
  add_saver("clock", tessera::SaverStep::CLOCK, [this]() { return new ClockSaver(this); });
  // A face of the music step: the clock and what plays, taking turns.
  add_saver("nowplaying", tessera::SaverStep::MEDIA, [this]() { return new NowPlayingSaver(this); });
}

}  // namespace esphome::big_clock
