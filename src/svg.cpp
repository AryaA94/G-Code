#include "gcodesim/svg.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace gcodesim {

namespace {

std::string fmt(double v, int decimals = 2) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  return buf;
}

std::string escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

// A "nice" length for the scale bar: 1, 2 or 5 times a power of ten.
double nice_length(double max_len) {
  double p = std::pow(10.0, std::floor(std::log10(max_len)));
  for (double m : {5.0, 2.0, 1.0})
    if (m * p <= max_len) return m * p;
  return p;
}

}  // namespace

std::string color_ramp(double t) {
  // The first 85% of matplotlib's "plasma": perceptually even, fine for
  // colour-blind readers, and (unlike the full scale) no pale yellow that
  // disappears on a white background.
  static constexpr std::array<std::array<double, 3>, 5> stops = {
      {{13, 8, 135}, {106, 0, 168}, {177, 42, 144}, {225, 100, 98}, {245, 150, 60}}};
  t = std::clamp(std::isfinite(t) ? t : 0.0, 0.0, 1.0) * (stops.size() - 1);
  std::size_t i = std::min(static_cast<std::size_t>(t), stops.size() - 2);
  double f = t - static_cast<double>(i);
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%02x%02x%02x",
                static_cast<int>(std::lround(stops[i][0] + (stops[i + 1][0] - stops[i][0]) * f)),
                static_cast<int>(std::lround(stops[i][1] + (stops[i + 1][1] - stops[i][1]) * f)),
                static_cast<int>(std::lround(stops[i][2] + (stops[i + 1][2] - stops[i][2]) * f)));
  return buf;
}

std::string render_svg(const std::vector<Segment>& segments, const SvgOptions& opt) {
  const double W = std::max(opt.width, 200), H = std::max(opt.height, 150);
  const double legend_w = 90.0, margin = 30.0, title_h = opt.title.empty() ? 0.0 : 24.0;

  // What gets drawn, and the range of the colour value.
  double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
  double vmin = 1e300, vmax = -1e300;
  auto value_of = [&](const Segment& s) {
    return opt.color_by == ColorBy::Feed ? s.feed : std::min(s.start.z, s.end.z);
  };
  std::vector<std::vector<Vec3>> paths(segments.size());
  // The view is framed on the cutting moves: a rapid from the machine's
  // home position would otherwise shrink the part to a dot. Rapids are
  // clipped to the frame. With no cutting at all, frame everything.
  bool any_cut = false;
  for (const auto& s : segments) any_cut = any_cut || s.is_cutting();
  for (std::size_t i = 0; i < segments.size(); ++i) {
    const Segment& s = segments[i];
    if (!s.is_motion() || (s.type == MoveType::Rapid && !opt.show_rapids)) continue;
    paths[i] = tessellate(s, 0.02);
    if (s.is_cutting() || !any_cut) {
      for (Vec3 p : paths[i]) {
        xmin = std::min(xmin, p.x), xmax = std::max(xmax, p.x);
        ymin = std::min(ymin, p.y), ymax = std::max(ymax, p.y);
      }
    }
    if (s.is_cutting()) {
      double v = value_of(s);
      vmin = std::min(vmin, v), vmax = std::max(vmax, v);
    }
  }
  if (xmin <= xmax) {
    double pad = 0.04 * std::max({xmax - xmin, ymax - ymin, 1.0});
    xmin -= pad, xmax += pad, ymin -= pad, ymax += pad;
  }

  std::ostringstream svg;
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << W << "\" height=\"" << H
      << "\" viewBox=\"0 0 " << W << ' ' << H
      << "\" font-family=\"Helvetica, Arial, sans-serif\" font-size=\"12\">\n";
  svg << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
  if (!opt.title.empty())
    svg << "<text x=\"" << margin << "\" y=\"20\" font-size=\"15\" fill=\"#222\">" << escape(opt.title)
        << "</text>\n";

  if (xmin > xmax) {
    svg << "<text x=\"" << W / 2 << "\" y=\"" << H / 2
        << "\" text-anchor=\"middle\" fill=\"#666\">no moves to draw</text>\n</svg>\n";
    return svg.str();
  }

  // Same scale on both axes so circles stay round. SVG's y points down.
  const double plot_w = W - 2 * margin - legend_w, plot_h = H - 2 * margin - title_h;
  const double span_x = std::max(xmax - xmin, 1e-6), span_y = std::max(ymax - ymin, 1e-6);
  const double scale = std::min(plot_w / span_x, plot_h / span_y);
  const double ox = margin + (plot_w - span_x * scale) / 2.0;
  const double oy = margin + title_h + (plot_h + span_y * scale) / 2.0;
  auto X = [&](double x) { return fmt(ox + (x - xmin) * scale); };
  auto Y = [&](double y) { return fmt(oy - (y - ymin) * scale); };

  // work origin
  if (xmin <= 0 && 0 <= xmax && ymin <= 0 && 0 <= ymax) {
    svg << "<g stroke=\"#c33\" stroke-width=\"1\"><line x1=\"" << fmt(ox - xmin * scale - 6) << "\" y1=\""
        << Y(0) << "\" x2=\"" << fmt(ox - xmin * scale + 6) << "\" y2=\"" << Y(0) << "\"/><line x1=\"" << X(0)
        << "\" y1=\"" << fmt(oy + ymin * scale - 6) << "\" x2=\"" << X(0) << "\" y2=\""
        << fmt(oy + ymin * scale + 6) << "\"/></g>\n";
  }

  auto polyline = [&](const std::vector<Vec3>& pts) {
    std::string s;
    for (Vec3 p : pts) s += X(p.x) + ',' + Y(p.y) + ' ';
    if (!s.empty()) s.pop_back();
    return s;
  };

  svg << "<clipPath id=\"frame\"><rect x=\"" << margin << "\" y=\"" << margin + title_h << "\" width=\""
      << plot_w << "\" height=\"" << plot_h << "\"/></clipPath>\n";
  svg << "<g clip-path=\"url(#frame)\" fill=\"none\" stroke=\"#9a9a9a\" stroke-width=\"0.8\" "
         "stroke-dasharray=\"4 3\">\n";
  for (std::size_t i = 0; i < segments.size(); ++i)
    if (segments[i].type == MoveType::Rapid && !paths[i].empty())
      svg << "<polyline points=\"" << polyline(paths[i]) << "\"/>\n";
  svg << "</g>\n<g fill=\"none\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (!segments[i].is_cutting() || paths[i].empty()) continue;
    double t = vmax > vmin ? (value_of(segments[i]) - vmin) / (vmax - vmin) : 1.0;
    svg << "<polyline stroke=\"" << color_ramp(t) << "\" points=\"" << polyline(paths[i]) << "\"/>\n";
  }
  svg << "</g>\n";

  // scale bar
  double bar = nice_length(span_x * 0.3);
  double bx = margin, by = H - 12;
  svg << "<g stroke=\"#333\" stroke-width=\"1.2\"><line x1=\"" << bx << "\" y1=\"" << by << "\" x2=\""
      << fmt(bx + bar * scale) << "\" y2=\"" << by << "\"/></g><text x=\"" << fmt(bx + bar * scale + 6)
      << "\" y=\"" << by + 4 << "\" fill=\"#333\">" << (bar >= 1 ? fmt(bar, 0) : fmt(bar, 3))
      << " mm</text>\n";

  // colour legend
  if (vmin <= vmax) {
    const double lx = W - legend_w + 10, ly = margin + title_h + 18, lh = std::min(200.0, plot_h - 30);
    svg << "<defs><linearGradient id=\"ramp\" x1=\"0\" y1=\"1\" x2=\"0\" y2=\"0\">";
    for (int k = 0; k <= 4; ++k)
      svg << "<stop offset=\"" << k * 25 << "%\" stop-color=\"" << color_ramp(k / 4.0) << "\"/>";
    svg << "</linearGradient></defs>\n";
    svg << "<text x=\"" << lx << "\" y=\"" << ly - 8 << "\" fill=\"#333\">"
        << (opt.color_by == ColorBy::Feed ? "feed mm/min" : "depth Z mm") << "</text>\n";
    svg << "<rect x=\"" << lx << "\" y=\"" << ly << "\" width=\"14\" height=\"" << lh
        << "\" fill=\"url(#ramp)\" stroke=\"#666\" stroke-width=\"0.5\"/>\n";
    int decimals = opt.color_by == ColorBy::Feed ? 0 : 2;
    svg << "<text x=\"" << lx + 20 << "\" y=\"" << ly + 10 << "\" fill=\"#333\">" << fmt(vmax, decimals)
        << "</text>\n";
    svg << "<text x=\"" << lx + 20 << "\" y=\"" << ly + lh << "\" fill=\"#333\">" << fmt(vmin, decimals)
        << "</text>\n";
    if (opt.show_rapids) {
      svg << "<line x1=\"" << lx << "\" y1=\"" << ly + lh + 24 << "\" x2=\"" << lx + 24 << "\" y2=\""
          << ly + lh + 24 << "\" stroke=\"#9a9a9a\" stroke-dasharray=\"4 3\"/><text x=\"" << lx + 30
          << "\" y=\"" << ly + lh + 28 << "\" fill=\"#333\">rapid</text>\n";
    }
  }
  svg << "</svg>\n";
  return svg.str();
}

}  // namespace gcodesim
