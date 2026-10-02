#pragma once

#include <string>
#include <vector>

#include "gcodesim/segment.hpp"

namespace gcodesim {

enum class ColorBy { Feed, Depth };

struct SvgOptions {
  int width = 800;
  int height = 600;
  ColorBy color_by = ColorBy::Feed;
  bool show_rapids = true;
  std::string title;
};

// Top view (XY) of the toolpath. Cutting moves are coloured by feed or by
// depth, rapids are dashed grey. Returns a complete SVG document.
std::string render_svg(const std::vector<Segment>& segments, const SvgOptions& options = {});

// Colour for t in [0, 1] on the scale used by the renderer, as "#rrggbb".
std::string color_ramp(double t);

}  // namespace gcodesim
