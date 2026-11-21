/***************************************************************************
 * cm93depth_pi - route check (see route_check.h)
 *
 * Copyright (C) 2026 Mike
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "route_check.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

namespace cm93 {

static const double kPi = 3.1415926535897931160;
static const double kDeg = kPi / 180.0;
static const double kNmPerRad = 60.0 / kDeg;  // 1' of latitude = 1 nm
static const double kNaN = std::numeric_limits<double>::quiet_NaN();

void rhumb_course_distance(const LatLon &a, const LatLon &b, double &courseDeg,
                           double &distNm) {
  const double p1 = a.lat * kDeg, p2 = b.lat * kDeg;
  double dl = std::remainder(b.lon - a.lon, 360.0) * kDeg;
  const double dp = p2 - p1;
  const double dpsi = std::log(std::tan(kPi / 4 + p2 / 2) / std::tan(kPi / 4 + p1 / 2));
  const double q = std::fabs(dpsi) > 1e-12 ? dp / dpsi : std::cos(p1);
  distNm = std::hypot(dp, q * dl) * kNmPerRad;
  double c = std::atan2(dl, dpsi) / kDeg;
  courseDeg = c < 0 ? c + 360.0 : c;
}

LatLon rhumb_destination(const LatLon &a, double courseDeg, double distNm) {
  const double d = distNm / kNmPerRad, th = courseDeg * kDeg;
  const double p1 = a.lat * kDeg;
  double p2 = p1 + d * std::cos(th);
  p2 = std::clamp(p2, -kPi / 2 + 1e-9, kPi / 2 - 1e-9);
  const double dpsi = std::log(std::tan(kPi / 4 + p2 / 2) / std::tan(kPi / 4 + p1 / 2));
  const double q = std::fabs(dpsi) > 1e-12 ? (p2 - p1) / dpsi : std::cos(p1);
  const double dl = q > 1e-12 ? d * std::sin(th) / q : 0.0;
  double lon = a.lon + dl / kDeg;
  lon = std::remainder(lon, 360.0);
  return {p2 / kDeg, lon};
}

const char *check_status_name(CheckStatus s) {
  switch (s) {
    case CheckStatus::Pass: return "PASS";
    case CheckStatus::Warn: return "WARNING";
    default: return "FAIL";
  }
}

void RouteChecker::start(const std::vector<RouteWaypoint> &route, const std::string &name,
                         const RouteCheckConfig &rc, const EngineConfig &ec) {
  m_rc = rc;
  m_rc.halfWidthNm = std::clamp(rc.halfWidthNm, 0.01, 5.0);
  m_rc.waypointRadiusNm = std::clamp(rc.waypointRadiusNm, 0.0, 5.0);
  m_rc.chunkNm = std::clamp(rc.chunkNm, 0.2, 20.0);
  m_ec = ec;
  m_res = RouteCheckResult();
  m_res.routeName = name;
  m_res.config = m_rc;
  m_tasks.clear();
  m_next = 0;
  if (route.size() < 2) {
    m_res.status = "A route needs at least two waypoints";
    m_running = false;
    return;
  }
  for (size_t i = 0; i < route.size(); i++) {
    WaypointCheck w;
    w.index = (int)i;
    w.name = route[i].name.empty() ? std::to_string(i + 1) : route[i].name;
    w.pos = {route[i].lat, route[i].lon};
    m_res.waypoints.push_back(w);
    Task t;
    t.waypoint = true;
    t.index = (int)i;
    m_tasks.push_back(t);
  }
  for (size_t i = 0; i + 1 < route.size(); i++) {
    LegCheck l;
    l.index = (int)i;
    l.from = m_res.waypoints[i].name;
    l.to = m_res.waypoints[i + 1].name;
    rhumb_course_distance(m_res.waypoints[i].pos, m_res.waypoints[i + 1].pos, l.courseDeg,
                          l.lengthNm);
    m_res.totalNm += l.lengthNm;
    m_res.legs.push_back(l);
    // the leg in pieces of at most chunkNm (the look-ahead's corridor is a
    // straight strip from its start: short pieces follow the rhumb line)
    const int n = std::max(1, (int)std::ceil(l.lengthNm / m_rc.chunkNm));
    for (int k = 0; k < n; k++) {
      Task t;
      t.index = (int)i;
      t.offsetNm = l.lengthNm * k / n;
      t.lengthNm = l.lengthNm / n;
      t.courseDeg = l.courseDeg;
      t.a = rhumb_destination(m_res.waypoints[i].pos, l.courseDeg, t.offsetNm);
      if (t.lengthNm > 1e-6) m_tasks.push_back(t);
    }
  }
  m_running = true;
}

namespace {

// Bands named in a look-ahead scale trace ("0.00-1.20 GE, 1.20-5.00 C");
// pieces with no chart are returned in noChart as (from, to)
void parse_trace(const std::string &trace, std::string &bands,
                 std::vector<std::pair<double, double>> &noChart) {
  size_t i = 0;
  while (i < trace.size()) {
    size_t e = trace.find(", ", i);
    std::string piece = trace.substr(i, e == std::string::npos ? std::string::npos : e - i);
    i = e == std::string::npos ? trace.size() : e + 2;
    size_t sp = piece.rfind(' ');
    if (sp == std::string::npos) continue;
    std::string b = piece.substr(sp + 1);
    if (b == "none") {
      double a = 0, z = 0;
      if (std::sscanf(piece.c_str(), "%lf-%lf", &a, &z) == 2) noChart.push_back({a, z});
      continue;
    }
    for (char c : b)
      if (bands.find(c) == std::string::npos) bands += c;
  }
}

std::string fmt_pos(const LatLon &p) {
  char b[64];
  double la = std::fabs(p.lat), lo = std::fabs(p.lon);
  std::snprintf(b, sizeof b, "%02d %06.3f'%c %03d %06.3f'%c", (int)la, (la - (int)la) * 60.0,
                p.lat < 0 ? 'S' : 'N', (int)lo, (lo - (int)lo) * 60.0, p.lon < 0 ? 'W' : 'E');
  return b;
}

const char *class_word(const std::string &c) {
  if (c == "SOUNDG") return "sounding";
  if (c == "DEPARE") return "depth area";
  if (c == "DRGARE") return "dredged area";
  if (c == "DEPCNT") return "depth contour";
  if (c == "SBDARE") return "seabed area";
  if (c == "OBSTRN") return "obstruction";
  if (c == "WRECKS") return "wreck";
  if (c == "UWTROC") return "rock";
  if (c == "LNDARE") return "land";
  if (c == "HOLE") return "gap in the depth areas";
  return c.c_str();
}

// Look-ahead hits -> issues, merging repeats of the same kind close together
void add_hits(const LookAheadResult &la, double offsetNm, int leg, int wp,
              const RouteCheckConfig &rc, std::vector<RouteIssue> &out, bool &haveLeast,
              double &least, LatLon &leastPos, std::string &leastCls) {
  std::vector<LookHit> hs = la.hits;
  std::sort(hs.begin(), hs.end(),
            [](const LookHit &a, const LookHit &b) { return a.aheadNm < b.aheadNm; });
  for (const LookHit &h : hs) {
    if (!h.land && !h.undepthed && std::isfinite(h.depth)) {
      if (!haveLeast || h.depth < least) {
        haveLeast = true;
        least = h.depth;
        leastPos = h.pos;
        leastCls = h.cls;
      }
    }
    RouteIssue is;
    if (h.land) is.kind = IssueKind::Land;
    else if (h.undepthed) is.kind = IssueKind::Undepthed;
    else if (std::isfinite(h.depth) && h.depth + rc.tideM < rc.safetyDepthM) is.kind = IssueKind::Shoal;
    else continue;
    is.leg = leg;
    is.waypoint = wp;
    is.alongNm = offsetNm + h.aheadNm;
    is.pos = h.pos;
    is.depth = h.depth;
    is.cls = h.cls;
    is.band = h.scaleChar;
    // same kind within 0.1 nm of the previous one: one issue (the shallower)
    RouteIssue *prev = nullptr;
    for (auto it = out.rbegin(); it != out.rend(); ++it)
      if (it->kind == is.kind && it->leg == leg && it->waypoint == wp) {
        prev = &*it;
        break;
      }
    if (prev && std::fabs(prev->alongNm - is.alongNm) < 0.1) {
      if (is.kind == IssueKind::Shoal && is.depth < prev->depth) {
        prev->depth = is.depth;
        prev->pos = is.pos;
        prev->cls = is.cls;
        prev->band = is.band;
      }
      continue;
    }
    if (out.size() < 200) out.push_back(is);
  }
}

CheckStatus status_of(const std::vector<RouteIssue> &issues, const RouteCheckConfig &rc) {
  CheckStatus s = CheckStatus::Pass;
  for (const RouteIssue &i : issues) {
    CheckStatus k = CheckStatus::Warn;
    if (i.kind == IssueKind::Shoal || i.kind == IssueKind::Land || i.kind == IssueKind::NoChart ||
        (i.kind == IssueKind::Undepthed && rc.undepthedFails))
      k = CheckStatus::Fail;
    s = std::max(s, k);
  }
  return s;
}

void describe(RouteIssue &i, const RouteCheckConfig &rc) {
  char b[256];
  std::string where = fmt_pos(i.pos);
  std::string at;
  if (i.waypoint >= 0) {
    std::snprintf(b, sizeof b, "%.2f nm from the waypoint", i.alongNm);
    at = b;
  } else {
    std::snprintf(b, sizeof b, "%.2f nm along the leg", i.alongNm);
    at = b;
  }
  switch (i.kind) {
    case IssueKind::Shoal:
      std::snprintf(b, sizeof b, "SHOAL %.1f m (%s, band %c)%s at %s, %s", i.depth,
                    class_word(i.cls), i.band,
                    rc.tideM != 0 ? (" + tide = " + std::to_string(i.depth + rc.tideM).substr(0, 4) + " m").c_str() : "",
                    at.c_str(), where.c_str());
      break;
    case IssueKind::Land:
      std::snprintf(b, sizeof b, "LAND (band %c) at %s, %s", i.band, at.c_str(), where.c_str());
      break;
    case IssueKind::Undepthed:
      std::snprintf(b, sizeof b, "%s with no charted depth (band %c) at %s, %s",
                    i.cls == "HOLE" ? "Gap in the depth areas" : class_word(i.cls), i.band,
                    at.c_str(), where.c_str());
      break;
    case IssueKind::PoorChart:
    case IssueKind::NoChart:
      return;  // set where they are made
  }
  i.text = b;
}

}  // namespace

void RouteChecker::runTask(DepthEngine &engine, const Task &t) {
  const RouteCheckConfig &rc = m_rc;
  LookAheadConfig lc;
  lc.basis = LookBasis::Distance;
  lc.minSogKn = 0.5;
  lc.safetyDepthM = rc.safetyDepthM - rc.tideM;
  lc.undepthedAsShoal = rc.undepthedFails;
  lc.collectHits = true;

  if (t.waypoint) {
    WaypointCheck &w = m_res.waypoints[t.index];
    EngineConfig h = m_ec;
    h.mode = SearchMode::DepthAreaHere;
    DepthResult d = engine.compute(w.pos.lat, w.pos.lon, h);
    if (d.valid && d.onLand) {
      w.onLand = true;
      RouteIssue is;
      is.kind = IssueKind::Land;
      is.waypoint = t.index;
      is.pos = w.pos;
      is.cls = "LNDARE";
      is.band = d.scaleChar;
      w.issues.push_back(is);
    } else if (d.valid && d.haveLeast) {
      w.haveDepth = true;
      w.depth = d.leastDepth;
      w.depthCls = d.leastClass;
    }
    if (rc.waypointRadiusNm > 0.005) {
      // the circle as two half-discs (sectors of +-90 deg) north and south
      lc.zone = LookZone::Sector;
      lc.halfAngleDeg = 90.0;
      lc.distanceNm = lc.maxLengthNm = rc.waypointRadiusNm;
      bool hl = false;
      double least = 0;
      LatLon lp{0, 0};
      std::string lcls;
      for (double c : {0.0, 180.0}) {
        LookAheadResult la = engine.lookAhead(w.pos.lat, w.pos.lon, c, 10.0, kNaN, lc, m_ec);
        add_hits(la, 0.0, -1, t.index, rc, w.issues, hl, least, lp, lcls);
      }
    }
    for (auto &i : w.issues) describe(i, rc);
    if (w.onLand && !w.issues.empty()) {
      char b[160];
      std::snprintf(b, sizeof b, "Waypoint on charted land (band %c), %s", w.issues[0].band,
                    fmt_pos(w.pos).c_str());
      w.issues[0].text = b;
    }
    w.status = status_of(w.issues, rc);
    return;
  }

  LegCheck &l = m_res.legs[t.index];
  lc.zone = LookZone::Corridor;
  lc.halfWidthNm = rc.halfWidthNm;
  lc.distanceNm = lc.maxLengthNm = std::max(0.05, t.lengthNm);
  LookAheadResult la = engine.lookAhead(t.a.lat, t.a.lon, t.courseDeg, 10.0, kNaN, lc, m_ec);
  const size_t first = l.issues.size();
  if (!la.valid) {
    RouteIssue is;
    is.kind = IssueKind::NoChart;
    is.leg = t.index;
    is.alongNm = t.offsetNm;
    is.pos = t.a;
    char b[200];
    std::snprintf(b, sizeof b, "NO CHART from %.2f to %.2f nm along the leg (%s)", t.offsetNm,
                  t.offsetNm + t.lengthNm, la.status.c_str());
    is.text = b;
    l.issues.push_back(is);
    return;
  }
  add_hits(la, t.offsetNm, t.index, -1, rc, l.issues, l.haveLeast, l.leastDepth, l.leastPos,
           l.leastCls);
  for (size_t i = first; i < l.issues.size(); i++) describe(l.issues[i], rc);
  std::vector<std::pair<double, double>> none;
  parse_trace(la.scaleTrace, l.bands, none);
  for (auto &n : none) {
    RouteIssue is;
    is.kind = IssueKind::NoChart;
    is.leg = t.index;
    is.alongNm = t.offsetNm + n.first;
    is.pos = rhumb_destination(m_res.waypoints[t.index].pos, l.courseDeg, is.alongNm);
    char b[160];
    std::snprintf(b, sizeof b, "NO CHART from %.2f to %.2f nm along the leg", is.alongNm,
                  t.offsetNm + n.second);
    is.text = b;
    l.issues.push_back(is);
  }
  const int cs = slot_from_char(la.coarsestScale);
  if (cs >= 0 && (l.coarsestBand == ' ' || cs < slot_from_char(l.coarsestBand)))
    l.coarsestBand = la.coarsestScale;
}

void RouteChecker::finish() {
  m_running = false;
  const RouteCheckConfig &rc = m_rc;
  for (LegCheck &l : m_res.legs) {
    // only small-scale charts somewhere along the leg
    const int cs = slot_from_char(l.coarsestBand);
    if (cs >= 0 && cs < rc.poorBandSlot) {
      RouteIssue is;
      is.kind = IssueKind::PoorChart;
      is.leg = l.index;
      is.band = l.coarsestBand;
      is.pos = m_res.waypoints[l.index].pos;
      char b[200];
      std::snprintf(b, sizeof b,
                    "Poorly charted: relies on band %c charts in places, less detailed than "
                    "band %c (bands used: %s)",
                    l.coarsestBand, kSlots[rc.poorBandSlot].ch, l.bands.c_str());
      is.text = b;
      l.issues.push_back(is);
    }
    std::stable_sort(l.issues.begin(), l.issues.end(),
                     [](const RouteIssue &a, const RouteIssue &b) {
                       if ((a.kind == IssueKind::PoorChart) != (b.kind == IssueKind::PoorChart))
                         return b.kind == IssueKind::PoorChart;
                       return a.alongNm < b.alongNm;
                     });
    l.status = status_of(l.issues, rc);
  }
  m_res.fails = m_res.warnings = 0;
  m_res.overall = CheckStatus::Pass;
  auto count = [&](CheckStatus s) {
    if (s == CheckStatus::Fail) m_res.fails++;
    if (s == CheckStatus::Warn) m_res.warnings++;
    m_res.overall = std::max(m_res.overall, s);
  };
  for (auto &l : m_res.legs) count(l.status);
  for (auto &w : m_res.waypoints) count(w.status);
  m_res.valid = true;
  char b[160];
  std::snprintf(b, sizeof b, "%s: %d leg(s)/waypoint(s) fail, %d with warnings",
                check_status_name(m_res.overall), m_res.fails, m_res.warnings);
  m_res.status = b;
}

bool RouteChecker::step(DepthEngine &engine, double budgetMs) {
  if (!m_running) return true;
  using clk = std::chrono::steady_clock;
  const auto t0 = clk::now();
  while (m_next < m_tasks.size()) {
    runTask(engine, m_tasks[m_next++]);
    if (std::chrono::duration<double, std::milli>(clk::now() - t0).count() > budgetMs) break;
  }
  if (m_next >= m_tasks.size()) {
    finish();
    return true;
  }
  return false;
}

std::string route_check_report(const RouteCheckResult &r) {
  std::string out;
  char b[512];
  const RouteCheckConfig &c = r.config;
  std::snprintf(b, sizeof b, "cm93depth_pi route check\nroute: %s, %zu waypoints, %.1f nm\n",
                r.routeName.c_str(), r.waypoints.size(), r.totalNm);
  out += b;
  std::snprintf(b, sizeof b,
                "safety depth %.1f m, height of tide %.1f m, corridor +-%.2f nm, waypoint circle "
                "%.2f nm, adequately charted: band %c or more detailed\n",
                c.safetyDepthM, c.tideM, c.halfWidthNm, c.waypointRadiusNm,
                kSlots[std::clamp(c.poorBandSlot, 0, kNumSlots - 1)].ch);
  out += b;
  out += "RESULT: " + r.status + "\n";
  out += "Charted depths only (below chart datum, plus the tide entered). A pass means no "
         "charted problem was found, not that the route is safe.\n\n";
  for (const LegCheck &l : r.legs) {
    std::string least = "-";
    if (l.haveLeast) {
      std::snprintf(b, sizeof b, "%.1f m (%s)", l.leastDepth, class_word(l.leastCls));
      least = b;
    }
    std::snprintf(b, sizeof b, "Leg %d  %s -> %s  %03.0f T  %.2f nm  least %s  bands %s  %s\n",
                  l.index + 1, l.from.c_str(), l.to.c_str(), l.courseDeg, l.lengthNm,
                  least.c_str(), l.bands.empty() ? "-" : l.bands.c_str(),
                  check_status_name(l.status));
    out += b;
    for (const RouteIssue &i : l.issues) out += "    " + i.text + "\n";
  }
  out += "\n";
  for (const WaypointCheck &w : r.waypoints) {
    std::string d = w.onLand ? std::string("on land")
                    : w.haveDepth ? (std::snprintf(b, sizeof b, "%.1f m", w.depth), std::string(b))
                                  : std::string("-");
    std::snprintf(b, sizeof b, "Waypoint %s  %s  depth here %s  %s\n", w.name.c_str(),
                  fmt_pos(w.pos).c_str(), d.c_str(), check_status_name(w.status));
    out += b;
    for (const RouteIssue &i : w.issues)
      if (!i.text.empty()) out += "    " + i.text + "\n";
  }
  return out;
}

}  // namespace cm93
