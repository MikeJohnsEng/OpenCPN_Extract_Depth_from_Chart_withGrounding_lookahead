/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * lookahead.cpp : least depth and first shoal in a zone ahead of the vessel.
 *
 * The zone is built in a track frame centred on the vessel: u ahead along
 * the course, v to starboard, in nautical miles (flat projection, good to
 * well under 1 % over the 50 nm the zone is limited to).
 *
 *   Corridor  0 <= u <= L, |v| <= W
 *   Sector    r <= L, |bearing off the course| <= half-angle (<= 90 deg)
 *
 * "Ahead" distance, which orders the objects and gives the time to reach
 * them, is u for the corridor and the range r for the sector.
 *
 * Which chart scale: the zone is cut into slices along its length. For each
 * slice, the most detailed scale band that charts every sample point of
 * the slice (a depth area, dredged area or land area contains it) is used.
 * If no single band does, every band charting any sample point is used.
 * Neighbouring slices with the same choice are merged and searched as one.
 * This follows the most detailed chart along the way, as a chart display
 * does, rather than mixing in generalised small-scale data where a
 * detailed chart exists.
 *
 * Objects are clipped to the zone exactly: segment by segment for areas and
 * lines, so a shoal that only clips the corner of the corridor is found at
 * the point where it enters.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "depth_engine.h"

namespace cm93 {

namespace {

const double kPi = 3.1415926535897931160;
const double kDeg = kPi / 180.0;
const double kInf = std::numeric_limits<double>::infinity();
const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kHardMaxNm = 50.0;
const double kSameNm = 0.011;  // ~20 m: the same object at another scale

// Zone (or part of it) in the track frame
struct Zone {
  bool sector = false;
  double a0 = 0, a1 = 0;     // corridor: u range; sector: r range
  double w = 0;              // corridor half-width
  double sinA = 0, cosA = 1;  // sector half-angle

  double metric(double u, double v) const {
    return sector ? std::hypot(u, v) : u;
  }
  bool contains(double u, double v) const {
    const double e = 1e-12;
    if (sector) {
      if (v * cosA - u * sinA > e || -v * cosA - u * sinA > e) return false;
      double r = std::hypot(u, v);
      return r <= a1 + e && r >= a0 - e;
    }
    return u >= a0 - e && u <= a1 + e && std::fabs(v) <= w + e;
  }
  // Point at which an object that covers the zone's near edge is met
  void nearPoint(double &u, double &v) const { u = a0, v = 0; }
};

// Keep the part of [t0, t1] where g0 + t (g1 - g0) <= 0
bool clip_linear(double g0, double g1, double &t0, double &t1) {
  if (g0 <= 0 && g1 <= 0) return t0 <= t1;
  if (g0 > 0 && g1 > 0) return false;
  double t = g0 / (g0 - g1);
  if (g0 > 0) t0 = std::max(t0, t);
  else t1 = std::min(t1, t);
  return t0 <= t1;
}

// Roots of a t^2 + b t + c = 0 (a > 0); false if none
bool roots(double a, double b, double c, double &r0, double &r1) {
  double disc = b * b - 4 * a * c;
  if (disc < 0) return false;
  double s = std::sqrt(disc);
  r0 = (-b - s) / (2 * a);
  r1 = (-b + s) / (2 * a);
  return true;
}

// Segment A->B against the zone. On success, the smallest "ahead" metric
// of the part inside, and where (u, v).
bool clip_segment(const Zone &z, double au, double av, double bu, double bv,
                  double &best, double &pu, double &pv) {
  const double du = bu - au, dv = bv - av;
  double t0 = 0, t1 = 1;
  auto lin = [&](double nu, double nv, double c) {  // nu*u + nv*v <= c
    return clip_linear(nu * au + nv * av - c, nu * bu + nv * bv - c, t0, t1);
  };
  const double qa = du * du + dv * dv, qb = 2 * (au * du + av * dv),
               qc = au * au + av * av;
  std::vector<std::pair<double, double>> spans;
  if (!z.sector) {
    if (!lin(-1, 0, -z.a0) || !lin(1, 0, z.a1) || !lin(0, 1, z.w) ||
        !lin(0, -1, z.w))
      return false;
    spans.push_back({t0, t1});
  } else {
    if (!lin(-z.sinA, z.cosA, 0) || !lin(-z.sinA, -z.cosA, 0)) return false;
    if (qa < 1e-18) {  // degenerate segment: a point
      if (!z.contains(au, av)) return false;
      best = z.metric(au, av), pu = au, pv = av;
      return true;
    }
    double r0, r1;
    if (!roots(qa, qb, qc - z.a1 * z.a1, r0, r1)) return false;
    t0 = std::max(t0, r0);
    t1 = std::min(t1, r1);
    if (t0 > t1) return false;
    double s0, s1;
    if (z.a0 > 0 && roots(qa, qb, qc - z.a0 * z.a0, s0, s1) && s1 > t0 &&
        s0 < t1) {
      if (s0 > t0) spans.push_back({t0, s0});
      if (s1 < t1) spans.push_back({s1, t1});
      if (spans.empty()) return false;
    } else {
      spans.push_back({t0, t1});
    }
  }
  best = kInf;
  for (auto &sp : spans) {
    double cand[3] = {sp.first, sp.second, sp.first};
    int n = 2;
    if (z.sector && qa > 1e-18)
      cand[n++] = std::clamp(-qb / (2 * qa), sp.first, sp.second);
    for (int i = 0; i < n; i++) {
      double u = au + cand[i] * du, v = av + cand[i] * dv;
      double m = z.metric(u, v);
      if (m < best) best = m, pu = u, pv = v;
    }
  }
  return std::isfinite(best);
}

// Track frame <-> lat/lon
struct Frame {
  double lat0, lon0, s, c;  // sin / cos of the course
  void toUV(double lat, double lon, double &u, double &v) const {
    double x, y;
    geo::to_local(lat0, lon0, lat, lon, x, y);
    u = x * s + y * c;
    v = x * c - y * s;
  }
  LatLon toLL(double u, double v) const {
    double x = u * s + v * c, y = u * c - v * s;
    LatLon p;
    geo::from_local(lat0, lon0, x, y, p.lat, p.lon);
    return p;
  }
};

// Sample points of a zone part, in (u, v)
std::vector<std::pair<double, double>> samples(const Zone &z) {
  std::vector<std::pair<double, double>> pts;
  if (!z.sector) {
    double mid = 0.5 * (z.a0 + z.a1);
    for (double u : {z.a0, mid, z.a1})
      for (double v : {-z.w, 0.0, z.w}) pts.push_back({u, v});
  } else {
    double mid = 0.5 * (z.a0 + z.a1);
    for (double r : {z.a0, mid, z.a1}) {
      if (r <= 0) {
        pts.push_back({0, 0});
        continue;
      }
      pts.push_back({r, 0});
      pts.push_back({r * z.cosA, r * z.sinA});
      pts.push_back({r * z.cosA, -r * z.sinA});
    }
  }
  return pts;
}

// Outline of a zone part, closed, in (u, v)
std::vector<std::pair<double, double>> outline(const Zone &z) {
  std::vector<std::pair<double, double>> o;
  if (!z.sector) {
    o = {{z.a0, -z.w}, {z.a1, -z.w}, {z.a1, z.w}, {z.a0, z.w}};
    return o;
  }
  const double A = std::atan2(z.sinA, z.cosA);
  const int n = 24;
  for (int i = 0; i <= n; i++) {  // outer arc, port to starboard
    double t = -A + 2 * A * i / n;
    o.push_back({z.a1 * std::cos(t), z.a1 * std::sin(t)});
  }
  if (z.a0 > 0)
    for (int i = n; i >= 0; i--) {
      double t = -A + 2 * A * i / n;
      o.push_back({z.a0 * std::cos(t), z.a0 * std::sin(t)});
    }
  else
    o.push_back({0, 0});
  return o;
}

double wrap360(double d) {
  d = std::fmod(d, 360.0);
  return d < 0 ? d + 360.0 : d;
}

}  // namespace

bool DepthEngine::chartedAt(double lat, double lon, int slot,
                            const EngineConfig &cfg) {
  for (auto &c : cellsAt(lat, lon, slot, cfg))
    for (const Feature &f : c->features) {
      if (f.geom != Geom::Area) continue;
      double a1, a2;
      if (f.cls != "LNDARE" && !area_depth_band(f, a1, a2)) continue;
      if (lat < f.minLat || lat > f.maxLat) continue;
      if (f.maxLon - f.minLon <= 180.0 && (lon < f.minLon || lon > f.maxLon))
        continue;
      if (geo::point_in_rings(f.parts, lat, lon)) return true;
    }
  return false;
}

LookAheadResult DepthEngine::lookAhead(double lat, double lon, double cogDeg,
                                       double sogKn, double hdgDeg,
                                       const LookAheadConfig &lc,
                                       const EngineConfig &cfg) {
  LookAheadResult res;
  res.zone = lc.zone;
  res.safetyDepthM = lc.safetyDepthM;
  if (!ready()) {
    res.status = "No charts configured (CM93 folder or S-57 ENC cells)";
    return res;
  }
  if (!std::isfinite(lat) || !std::isfinite(lon) || std::fabs(lat) > 80.0) {
    res.status = "No valid position";
    return res;
  }
  checkOffsets(cfg);
  syncEnc();
  const std::vector<int> slots = slotsDown(cfg);

  // ---- course, speed and zone size
  const bool sogOk = std::isfinite(sogKn) && sogKn >= lc.minSogKn &&
                     std::isfinite(cogDeg);
  if (sogOk) {
    res.courseDeg = wrap360(cogDeg);
    res.sogKn = sogKn;
  } else if (std::isfinite(hdgDeg)) {
    res.courseDeg = wrap360(hdgDeg);
    res.courseFromHeading = true;
    res.sogKn = std::isfinite(sogKn) ? sogKn : 0.0;
  } else {
    res.status = "No course: speed below " + std::to_string(lc.minSogKn).substr(0, 3) +
                 " kn and no heading";
    return res;
  }
  const double maxL = std::clamp(lc.maxLengthNm, 0.05, kHardMaxNm);
  double L;
  if (lc.basis == LookBasis::Distance) {
    L = std::clamp(lc.distanceNm, 0.05, maxL);
  } else {
    const double minL = std::clamp(lc.minLengthNm, 0.05, maxL);
    L = sogOk ? std::clamp(sogKn * lc.minutes / 60.0, minL, maxL) : minL;
  }
  res.lengthNm = L;

  Zone base;
  base.sector = lc.zone == LookZone::Sector;
  base.a0 = 0;
  base.a1 = L;
  base.w = std::clamp(lc.halfWidthNm, 0.01, 5.0);
  const double A = std::clamp(lc.halfAngleDeg, 1.0, 90.0) * kDeg;
  base.sinA = std::sin(A);
  base.cosA = std::cos(A);
  if (base.sector) res.halfAngleDeg = A / kDeg;
  else res.halfWidthNm = base.w;

  const Frame fr{lat, lon, std::sin(res.courseDeg * kDeg),
                 std::cos(res.courseDeg * kDeg)};
  for (auto &p : outline(base)) res.outline.push_back(fr.toLL(p.first, p.second));

  // ---- scale per slice
  const int nSlices = std::clamp((int)std::ceil(L / 0.1), 1, 40);
  struct Part {
    Zone z;
    std::vector<int> scales;  // most detailed first
  };
  std::vector<Part> parts;
  for (int i = 0; i < nSlices; i++) {
    Zone z = base;
    z.a0 = L * i / nSlices;
    z.a1 = L * (i + 1) / nSlices;
    std::vector<int> sc;  // bands ("slots"), most detailed first
    if (cfg.scaleIdx >= 0) {
      sc = slots;  // the fixed CM93 scale (none if no CM93 charts)
    } else {
      auto pts = samples(z);
      std::vector<LatLon> ll;
      for (auto &p : pts) ll.push_back(fr.toLL(p.first, p.second));
      int full = -1;
      std::vector<int> any;
      for (int s : slots) {
        if (full >= 0) break;
        int n = 0;
        double around;
        for (auto &p : ll)
          n += (chartedAt(p.lat, p.lon, s, cfg) || gapAt(p.lat, p.lon, s, cfg, around)) ? 1 : 0;
        if (n == (int)ll.size()) full = s;
        else if (n > 0) any.push_back(s);
      }
      if (full >= 0) sc.push_back(full);
      else sc = any;
    }
    if (!parts.empty() && parts.back().scales == sc) {
      parts.back().z.a1 = z.a1;
    } else {
      parts.push_back({z, sc});
    }
  }

  // ---- search each part
  std::vector<LookHit> hits;
  std::vector<std::pair<double, double>> undepthedSeen;  // (u, v)
  int coarsest = kNumSlots;
  bool anyScale = false;
  char buf[64];

  for (const Part &part : parts) {
    const Zone &z = part.z;
    std::snprintf(buf, sizeof buf, "%s%.2f-%.2f ", res.scaleTrace.empty() ? "" : ", ",
                  z.a0, z.a1);
    res.scaleTrace += buf;
    if (part.scales.empty()) {
      res.scaleTrace += "none";
      continue;
    }
    for (int s : part.scales) {
      res.scaleTrace += kSlots[s].ch;
      coarsest = std::min(coarsest, s);
      anyScale = true;
    }

    // Lat/lon box of this part
    double bMinLat = 90, bMaxLat = -90, bMinLon = 1e9, bMaxLon = -1e9;
    for (auto &p : outline(z)) {
      LatLon q = fr.toLL(p.first, p.second);
      double qlon = lon + std::remainder(q.lon - lon, 360.0);  // unwrapped
      bMinLat = std::min(bMinLat, q.lat);
      bMaxLat = std::max(bMaxLat, q.lat);
      bMinLon = std::min(bMinLon, qlon);
      bMaxLon = std::max(bMaxLon, qlon);
    }
    const double pad = 0.002;
    bMinLat -= pad, bMaxLat += pad, bMinLon -= pad, bMaxLon += pad;
    auto boxOut = [&](const Feature &f) {
      if (f.maxLat < bMinLat || f.minLat > bMaxLat) return true;
      if (f.maxLon - f.minLon > 180.0) return false;
      double fmin = lon + std::remainder(f.minLon - lon, 360.0);
      double fmax = fmin + (f.maxLon - f.minLon);
      return fmax < bMinLon || fmin > bMaxLon;
    };

    // The part's own scales, then (dangers only) every smaller scale
    std::vector<std::pair<int, bool>> scan;  // (scale, dangers only)
    for (int s : part.scales) scan.push_back({s, false});
    if (cfg.coarseDangers && cfg.scaleIdx < 0 && !part.scales.empty()) {
      int lowest = *std::min_element(part.scales.begin(), part.scales.end());
      for (int s : slots)
        if (s < lowest) scan.push_back({s, true});
    }
    for (const auto &scanItem : scan) {
      const int s = scanItem.first;  // (not a structured binding: lambdas capture it)
      const bool dangersOnly = scanItem.second;
      // Charts of this band overlapping the part
      std::vector<std::shared_ptr<CellData>> cellsS =
          cellsInBox(bMinLat, bMaxLat, bMinLon, bMaxLon, s, cfg);
      // True if a depth area, dredged area or land of this scale contains p
      auto chartedHere = [&](const LatLon &p) {
        for (auto &cc : cellsS)
          for (const Feature &g : cc->features) {
            if (g.geom != Geom::Area) continue;
            double a1, a2;
            if (g.cls != "LNDARE" && !area_depth_band(g, a1, a2)) continue;
            if (p.lat < g.minLat || p.lat > g.maxLat) continue;
            if (g.maxLon - g.minLon <= 180.0 && (p.lon < g.minLon || p.lon > g.maxLon)) continue;
            if (geo::point_in_rings(g.parts, p.lat, p.lon)) return true;
          }
        return false;
      };

      for (auto &cell : cellsS) {
        for (const Feature &f : cell->features) {
          const std::string &c = f.cls;
          const bool isArea = (c == "DEPARE" || c == "DRGARE" || c == "SBDARE");
          const bool isLand = (c == "LNDARE");
          const bool isSnd = (c == "SOUNDG");
          const bool isHaz = (c == "OBSTRN" || c == "WRECKS" || c == "UWTROC");
          const bool isCnt = (c == "DEPCNT");
          if (!(isArea || isLand || isSnd || isHaz || isCnt)) continue;
          if (dangersOnly && !isHaz) continue;
          if (isArea && !cfg.useAreas) continue;
          if (isSnd && !cfg.useSoundings) continue;
          if (isHaz && !cfg.useHazards) continue;
          if (isCnt && !cfg.useContours) continue;
          if (boxOut(f)) continue;
          res.objectsSearched++;

          auto addHit = [&, cell](double depth, bool land, bool undep, double m,
                                  double u, double v) {
            LookHit h;
            h.depth = depth;
            h.land = land;
            h.undepthed = undep;
            h.cls = c;
            h.aheadNm = m;
            h.pos = fr.toLL(u, v);
            h.rangeNm = std::hypot(u, v);
            double x = u * fr.s + v * fr.c, y = u * fr.c - v * fr.s;
            h.bearingDeg = h.rangeNm < 1e-6 ? 0.0 : wrap360(std::atan2(x, y) / kDeg);
            h.timeMin = (!res.courseFromHeading && res.sogKn > 0)
                            ? m / res.sogKn * 60.0
                            : kNaN;
            h.scaleChar = cell->ch;
            h.cellName = cell->name;
            hits.push_back(h);
          };

          if (isSnd) {
            for (size_t i = 0; i < f.points.size() && i < f.z.size(); i++) {
              double u, v;
              fr.toUV(f.points[i].lat, f.points[i].lon, u, v);
              if (z.contains(u, v)) addHit(f.z[i], false, false, z.metric(u, v), u, v);
            }
            continue;
          }

          // Value of the object
          double depth = kNaN;
          bool undep = false;
          if (isArea) {
            double d2;
            if (!area_depth_band(f, depth, d2)) continue;  // e.g. a sand patch in deep water
          } else if (isHaz) {
            DangerKind k = danger_depth(f, depth);
            if (k == DangerKind::NotDanger) continue;  // foul ground, fish haven
            if (k == DangerKind::Unknown) undep = true;
          } else if (isCnt) {
            if (!f.getNum("VALDCO", depth)) continue;
          }

          // Nearest-ahead point of the object inside the zone
          double best = kInf, bu = 0, bv = 0;
          auto tryPoint = [&](double u, double v) {
            if (!z.contains(u, v)) return;
            double m = z.metric(u, v);
            if (m < best) best = m, bu = u, bv = v;
          };
          for (auto &p : f.points) {
            double u, v;
            fr.toUV(p.lat, p.lon, u, v);
            tryPoint(u, v);
          }
          const bool closed = f.geom == Geom::Area;
          for (auto &ring : f.parts) {
            const size_t n = ring.size();
            if (n == 0) continue;
            double pu, pv;
            fr.toUV(ring[closed ? n - 1 : 0].lat, ring[closed ? n - 1 : 0].lon, pu, pv);
            if (n == 1) tryPoint(pu, pv);
            for (size_t i = closed ? 0 : 1; i < n; i++) {
              double qu, qv;
              fr.toUV(ring[i].lat, ring[i].lon, qu, qv);
              double m = kInf, mu = 0, mv = 0;
              if (clip_segment(z, pu, pv, qu, qv, m, mu, mv) && m < best)
                best = m, bu = mu, bv = mv;
              pu = qu, pv = qv;
            }
          }
          if (closed) {  // the object may cover the zone's near edge
            double nu, nv;
            z.nearPoint(nu, nv);
            double m = z.metric(nu, nv);
            if (m < best) {
              LatLon q = fr.toLL(nu, nv);
              if (geo::point_in_rings(f.parts, q.lat, q.lon)) best = m, bu = nu, bv = nv;
            }
          }
          if (!std::isfinite(best)) continue;
          // a danger area that a more detailed chart's soundings overrule
          // where the zone meets it
          if (isHaz && f.geom == Geom::Area) {
            LatLon q = fr.toLL(bu, bv);
            if (dangerOverruled(f, s, undep ? 0.0 : depth, q.lat, q.lon, cfg)) continue;
          }

          if (isLand) {
            res.landInZone = true;
            addHit(kNaN, true, false, best, bu, bv);
          } else if (undep) {
            bool dup = false;
            for (auto &q : undepthedSeen)
              if (std::fabs(q.first - bu) < kSameNm && std::fabs(q.second - bv) < kSameNm)
                dup = true;
            if (dup) continue;
            undepthedSeen.push_back({bu, bv});
            addHit(kNaN, false, true, best, bu, bv);
          } else {
            addHit(depth, false, false, best, bu, bv);
          }
        }
      }

      if (dangersOnly) continue;
      // Gaps in this scale's depth areas inside the zone: charted water
      // with no depth (a reef, rock, islet or unsurveyed area left as a hole
      // between depth areas). Found exactly, not by sampling: the zone can
      // only enter a gap across the edge of a depth area or land, so every
      // such edge is clipped to the zone, the nearest-ahead point of each is
      // probed a few metres to either side, and the nearest edge with an
      // uncharted side that is enclosed (a gap) gives the gap's entry point.
      // The boat already in a gap is caught at the zone's near edge.
      {
        struct Cand { double m, u, v; LatLon probe; };
        std::vector<Cand> cands;
        const double eps = 0.002;  // probe offset, nm (~4 m)
        {
          double nu, nv;
          z.nearPoint(nu, nv);
          LatLon q = fr.toLL(nu, nv);
          if (!chartedHere(q)) cands.push_back({z.metric(nu, nv), nu, nv, q});
        }
        for (auto &cc : cellsS)
          for (const Feature &g : cc->features) {
            if (g.geom != Geom::Area) continue;
            double a1, a2;
            if (g.cls != "LNDARE" && !area_depth_band(g, a1, a2)) continue;
            if (boxOut(g)) continue;
            for (auto &ring : g.parts) {
              const size_t n = ring.size();
              if (n < 2) continue;
              double pu, pv;
              fr.toUV(ring[n - 1].lat, ring[n - 1].lon, pu, pv);
              for (size_t i = 0; i < n; i++) {
                double qu, qv, m = kInf, mu = 0, mv = 0;
                fr.toUV(ring[i].lat, ring[i].lon, qu, qv);
                if (clip_segment(z, pu, pv, qu, qv, m, mu, mv)) {
                  double du = qu - pu, dv = qv - pv, l = std::hypot(du, dv);
                  if (l > 1e-12) {
                    // both sides of the edge at its nearest-ahead point
                    for (int side : {-1, 1}) {
                      double ou = mu - side * dv / l * eps, ov = mv + side * du / l * eps;
                      LatLon q = fr.toLL(ou, ov);
                      if (!chartedHere(q)) cands.push_back({m, mu, mv, q});
                    }
                  }
                }
                pu = qu, pv = qv;
              }
            }
          }
        std::sort(cands.begin(), cands.end(),
                  [](const Cand &x, const Cand &y) { return x.m < y.m; });
        int gapTests = 0;  // the enclosure test is the costly part
        for (const Cand &cd : cands) {
          if (++gapTests > 40) break;
          double around;
          if (!gapAt(cd.probe.lat, cd.probe.lon, s, cfg, around)) continue;
          bool dup = false;
          for (auto &q : undepthedSeen)
            if (std::fabs(q.first - cd.u) < 0.03 && std::fabs(q.second - cd.v) < 0.03) dup = true;
          if (!dup) {
            undepthedSeen.push_back({cd.u, cd.v});
            LookHit h;
            h.depth = kNaN;
            h.undepthed = true;
            h.cls = "HOLE";
            h.aheadNm = cd.m;
            h.pos = fr.toLL(cd.u, cd.v);
            h.rangeNm = std::hypot(cd.u, cd.v);
            double x = cd.u * fr.s + cd.v * fr.c, y = cd.u * fr.c - cd.v * fr.s;
            h.bearingDeg = h.rangeNm < 1e-6 ? 0.0 : wrap360(std::atan2(x, y) / kDeg);
            h.timeMin = (!res.courseFromHeading && res.sogKn > 0) ? h.aheadNm / res.sogKn * 60.0
                                                                  : kNaN;
            h.scaleChar = kSlots[s].ch;
            hits.push_back(h);
          }
          break;  // the nearest gap ahead in this part is what matters
        }
      }
    }
  }

  if (!anyScale) {
    res.status = "No chart along the zone";
    return res;
  }
  res.valid = true;
  res.coarsestScale = kSlots[coarsest].ch;

  for (const LookHit &h : hits) {
    if (h.undepthed) {
      res.undepthed++;
      if (!res.haveUndepthed || h.aheadNm < res.nearestUndepthed.aheadNm) {
        res.nearestUndepthed = h;
        res.haveUndepthed = true;
      }
    }
    if (!h.land && !h.undepthed) {
      if (!res.haveLeast || h.depth < res.least.depth - 1e-9 ||
          (std::fabs(h.depth - res.least.depth) <= 1e-9 && h.aheadNm < res.least.aheadNm)) {
        res.least = h;
        res.haveLeast = true;
      }
    }
    const bool isShoal = h.land || (h.undepthed && lc.undepthedAsShoal) ||
                         (!h.undepthed && !h.land && h.depth < lc.safetyDepthM);
    if (isShoal) {
      bool better = !res.haveShoal || h.aheadNm < res.shoal.aheadNm - 1e-9;
      if (!better && std::fabs(h.aheadNm - res.shoal.aheadNm) <= 1e-9) {
        // same place: land first, then the shallower
        double hd = h.land ? -kInf : (h.undepthed ? kInf : h.depth);
        double sd = res.shoal.land ? -kInf : (res.shoal.undepthed ? kInf : res.shoal.depth);
        better = hd < sd;
      }
      if (better) {
        res.shoal = h;
        res.haveShoal = true;
      }
    }
  }
  res.status = res.haveShoal ? "Shoal ahead" : "Clear";
  return res;
}

}  // namespace cm93
