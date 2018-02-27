#ifndef Y4M_HH
#define Y4M_HH

#include <string>
#include <tuple>

/* parse Y4M header */
class Y4MParser
{
public:
  Y4MParser(const std::string & y4m_path);

  /* accessors */
  int get_frame_width() { return width_; }
  int get_frame_height() { return height_; }

  float get_frame_rate_float()
  {
    return 1.0f * frame_rate_numerator_ / frame_rate_denominator_;
  }

  std::tuple<int, int> get_frame_rate()
  {
    return { frame_rate_numerator_, frame_rate_denominator_ };
  }

  bool is_interlaced() { return interlaced_; }

private:
  int width_, height_;
  int frame_rate_numerator_, frame_rate_denominator_;
  bool interlaced_;
};

#endif /* Y4M_HH */
