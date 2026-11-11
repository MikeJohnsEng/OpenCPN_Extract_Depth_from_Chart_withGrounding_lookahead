/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * depth_engine.cpp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "depth_engine.h"
#include "s57_reader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>

namespace cm93 {

static const double kPi = 3.1415926535897931160;
static const double kDeg = kPi / 180.0;

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
namespace geo {

static double wrap180(double d) {
  while (d > 180.0) d -= 360.0;
  while (d <= -180.0) d += 360.0;
  return d;
}

void to_local(double lat0, double lon0, double lat, double lon, double &x,
              double &y) {
  x = wrap180(lon - lon0) * std::cos(lat0 * kDeg) * 60.0;
  y = (lat - lat0) * 60.0;
}

void from_local(double lat0, double lon0, double x, double y, double &lat,
                double &lon) {
  lat = lat0 + y / 60.0;
  double c = std::cos(lat0 * kDeg);
  lon = wrap180(lon0 + (c > 1e-9 ? x / (60.0 * c) : 0.0));
}

bool point_in_rings(const std::vector<std::vector<LatLon>> &rings, double lat,
                    double lon) {
  // Work in a local frame centred on the query point; ray along +x.
  bool inside = false;
  for (const auto &ring : rings) {
    size_t n = ring.size();
    if (n < 3) continue;
    double px, py;
    to_local(lat, lon, ring[n - 1].lat, ring[n - 1].lon, px, py);
    for (size_t i = 0; i < n; i++) {
      double qx, qy;
      to_local(lat, lon, ring[i].lat, ring[i].lon, qx, qy);
      if ((qy > 0) != (py > 0)) {
        double xint = px + (0 - py) * (qx - px) / (qy - py);
        if (xint > 0) inside = !inside;
      }
      px = qx;
      py = qy;
    }
  }
  return inside;
}

double nearest_on_parts(const std::vector<std::vector<LatLon>> &parts,
                        double lat, double lon, LatLon &nearest) {
  double best = std::numeric_limits<double>::infinity();
  double bx = 0, by = 0;
  for (const auto &part : parts) {
    if (part.empty()) continue;
    double ax, ay;
    to_local(lat, lon, part[0].lat, part[0].lon, ax, ay);
    if (part.size() == 1) {
      double d = std::hypot(ax, ay);
      if (d < best) best = d, bx = ax, by = ay;
      continue;
    }
    for (size_t i = 1; i < part.size(); i++) {
      double cx, cy;
      to_local(lat, lon, part[i].lat, part[i].lon, cx, cy);
      double dx = cx - ax, dy = cy - ay;
      double len2 = dx * dx + dy * dy;
      double t = len2 > 0 ? -(ax * dx + ay * dy) / len2 : 0.0;
      t = std::clamp(t, 0.0, 1.0);
      double nx = ax + t * dx, ny = ay + t * dy;
      double d = std::hypot(nx, ny);
      if (d < best) best = d, bx = nx, by = ny;
      ax = cx;
      ay = cy;
    }
  }
  if (std::isfinite(best)) from_local(lat, lon, bx, by, nearest.lat, nearest.lon);
  return best;
}

}  // namespace geo

// ---------------------------------------------------------------------------
// Chart bands
// ---------------------------------------------------------------------------
const SlotInfo kSlots[kNumSlots] = {
    {'Z', 0, 0}, {'A', 1, 0}, {'1', -1, 1}, {'B', 2, 0}, {'2', -1, 2},
    {'C', 3, 0}, {'3', -1, 3}, {'D', 4, 0}, {'E', 5, 0}, {'4', -1, 4},
    {'F', 6, 0}, {'5', -1, 5}, {'G', 7, 0}, {'6', -1, 6},
};

int slot_from_char(char c) {
  c = (char)std::toupper((unsigned char)c);
  for (int i = 0; i < kNumSlots; i++)
    if (kSlots[i].ch == c) return i;
  return -1;
}

int slot_of_cm93(int scaleIdx) {
  for (int i = 0; i < kNumSlots; i++)
    if (kSlots[i].cm93 == scaleIdx) return i;
  return -1;
}

// ---------------------------------------------------------------------------
// Engine: chart access
// ---------------------------------------------------------------------------
bool DepthEngine::setRoot(const std::string &root, std::string *err) {
  m_loadErrors.clear();
  m_cache.clear();
  m_lru.clear();
  m_root.clear();
  m_dict = Dictionary();
  if (root.empty()) return false;
  Dictionary d;
  if (!d.find_and_load(root, 3, err)) return false;
  m_dict = d;
  m_root = root;
  return true;
}

std::string DepthEngine::loadError(int cellIndex, int scaleIdx) const {
  auto it = m_loadErrors.find((long)scaleIdx * 10000000L + cellIndex);
  return it == m_loadErrors.end() ? std::string() : it->second;
}

void DepthEngine::clearCache() {
  m_loadErrors.clear();
  m_cache.clear();
  m_lru.clear();
  m_encCache.clear();
  m_encErrors.clear();
  m_encLru.clear();
}

// Object classes the engine uses (CM93 and ENC)
static const std::set<std::string> &keep_classes() {
  static const std::set<std::string> k = {"DEPARE", "DRGARE", "SOUNDG", "OBSTRN", "WRECKS",
                                          "UWTROC", "DEPCNT", "M_COVR", "LNDARE", "SBDARE"};
  return k;
}

std::shared_ptr<CellData> DepthEngine::getCell(int cellIndex, int scaleIdx,
                                               const EngineConfig &cfg) {
  if (!cm93Ready()) return nullptr;
  long key = (long)scaleIdx * 10000000L + cellIndex;
  auto it = m_cache.find(key);
  if (it != m_cache.end()) {
    if (it->second) {  // refresh LRU position
      m_lru.remove(key);
      m_lru.push_front(key);
    }
    return it->second;
  }

  DecodeOptions opt;
  opt.keepClasses = keep_classes();
  opt.offsetE = cfg.offsetE;
  opt.offsetN = cfg.offsetN;
  opt.openCpnSoundings = cfg.openCpnSoundings;

  auto cell = std::make_shared<CellData>();
  std::string err;
  if (!load_cell(m_root, m_dict, cellIndex, scaleIdx, opt, *cell, &err)) {
    m_cache[key] = nullptr;  // remember absence
    if (err != "cell not present") m_loadErrors[key] = err;
    return nullptr;
  }
  m_cache[key] = cell;
  m_lru.push_front(key);
  while (m_lru.size() > kMaxLoaded) {
    m_cache.erase(m_lru.back());
    m_lru.pop_back();
  }
  return cell;
}

std::shared_ptr<CellData> DepthEngine::getEnc(size_t entry) {
  const auto &es = m_enc.entries();
  if (entry >= es.size()) return nullptr;
  const std::string &path = es[entry].path;
  auto it = m_encCache.find(path);
  if (it != m_encCache.end()) {
    if (it->second) {
      m_encLru.remove(path);
      m_encLru.push_front(path);
    }
    return it->second;
  }
  DecodeOptions opt;
  opt.keepClasses = keep_classes();
  auto cell = std::make_shared<CellData>();
  std::string err;
  if (!load_enc(path, opt, *cell, nullptr, &err)) {
    m_encCache[path] = nullptr;
    m_encErrors[path] = err;
    return nullptr;
  }
  cell->scaleIdx = -1;
  m_encCache[path] = cell;
  m_encLru.push_front(path);
  while (m_encLru.size() > kMaxEncLoaded) {
    m_encCache.erase(m_encLru.back());
    m_encLru.pop_back();
  }
  return cell;
}

// Pick up a changed ENC index: rebuild the per-band lists
void DepthEngine::syncEnc() {
  if (m_encGen == m_enc.generation()) return;
  m_encGen = m_enc.generation();
  const auto &es = m_enc.entries();
  for (auto &b : m_band) b.clear();
  for (size_t i = 0; i < es.size(); i++)
    if (es[i].band >= 1 && es[i].band <= 6) m_band[es[i].band].push_back(i);
  for (auto &b : m_band)  // within a band, the more detailed cell first
    std::stable_sort(b.begin(), b.end(), [&](size_t x, size_t y) {
      int cx = es[x].cscl > 0 ? es[x].cscl : 1 << 30, cy = es[y].cscl > 0 ? es[y].cscl : 1 << 30;
      return cx < cy;
    });
  // Cells no longer in the index: forget them
  std::set<std::string> live;
  for (auto &e : es) live.insert(e.path);
  for (auto it = m_encCache.begin(); it != m_encCache.end();) {
    if (live.count(it->first)) {
      ++it;
    } else {
      m_encLru.remove(it->first);
      it = m_encCache.erase(it);
    }
  }
  m_encErrors.clear();
}

bool DepthEngine::slotEnabled(int slot, const EngineConfig &cfg) const {
  if (slot < 0 || slot >= kNumSlots) return false;
  const SlotInfo &si = kSlots[slot];
  if (si.cm93 >= 0)
    return cm93Ready() && cfg.useCm93 && (cfg.scaleIdx < 0 || cfg.scaleIdx == si.cm93);
  return cfg.useEnc && cfg.scaleIdx < 0 && !m_band[si.enc].empty();
}

std::vector<int> DepthEngine::slotsDown(const EngineConfig &cfg) const {
  std::vector<int> out;
  for (int s = kNumSlots - 1; s >= 0; s--)
    if (slotEnabled(s, cfg)) out.push_back(s);
  return out;
}

// Longitude of `lon` shifted by whole turns to lie nearest `ref`
static double near_lon(double lon, double ref) { return ref + std::remainder(lon - ref, 360.0); }

std::vector<std::shared_ptr<CellData>> DepthEngine::cellsAt(double lat, double lon, int slot,
                                                            const EngineConfig &cfg) {
  std::vector<std::shared_ptr<CellData>> out;
  if (!slotEnabled(slot, cfg)) return out;
  const SlotInfo &si = kSlots[slot];
  if (si.cm93 >= 0) {
    auto c = getCell(cell_index(lat, lon, si.cm93), si.cm93, cfg);
    if (c) out.push_back(c);
    return out;
  }
  const auto &es = m_enc.entries();
  const double eps = 1e-7;
  for (size_t i : m_band[si.enc]) {
    const EncEntry &e = es[i];
    if (lat < e.minLat - eps || lat > e.maxLat + eps) continue;
    double l = near_lon(lon, 0.5 * (e.minLon + e.maxLon));
    if (l < e.minLon - eps || l > e.maxLon + eps) continue;
    if (auto c = getEnc(i)) out.push_back(c);
  }
  return out;
}

std::vector<std::shared_ptr<CellData>> DepthEngine::cellsInBox(double minLat, double maxLat,
                                                               double minLon, double maxLon,
                                                               int slot,
                                                               const EngineConfig &cfg) {
  std::vector<std::shared_ptr<CellData>> out;
  if (!slotEnabled(slot, cfg)) return out;
  const SlotInfo &si = kSlots[slot];
  if (si.cm93 >= 0) {
    const int s = si.cm93;
    const double sz = cell_size_deg(s);
    const double step = sz * 0.5;
    std::vector<int> ids;
    minLat = std::max(minLat, -85.0);
    maxLat = std::min(maxLat, 85.0);
    if (maxLon - minLon > 360.0) maxLon = minLon + 360.0;
    for (double la = minLat;; la = std::min(la + step, maxLat)) {
      for (double lo = minLon;; lo = std::min(lo + step, maxLon)) {
        double lw = lo;
        while (lw >= 180) lw -= 360;
        while (lw < -180) lw += 360;
        int ci = cell_index(la, lw, s);
        if (std::find(ids.begin(), ids.end(), ci) == ids.end()) ids.push_back(ci);
        if (lo >= maxLon) break;
      }
      if (la >= maxLat) break;
    }
    for (int ci : ids)
      if (auto c = getCell(ci, s, cfg)) out.push_back(c);
    return out;
  }
  const auto &es = m_enc.entries();
  const double mid = 0.5 * (minLon + maxLon);
  for (size_t i : m_band[si.enc]) {
    const EncEntry &e = es[i];
    if (e.maxLat < minLat || e.minLat > maxLat) continue;
    double c = near_lon(0.5 * (e.minLon + e.maxLon), mid);
    double half = 0.5 * (e.maxLon - e.minLon);
    if (c + half < minLon || c - half > maxLon) continue;
    if (auto cell = getEnc(i)) out.push_back(cell);
  }
  return out;
}

std::vector<std::shared_ptr<CellData>> DepthEngine::cellsAround(double lat, double lon,
                                                                int slot,
                                                                const EngineConfig &cfg,
                                                                double reachNm) {
  std::vector<std::shared_ptr<CellData>> out;
  if (!slotEnabled(slot, cfg)) return out;
  const SlotInfo &si = kSlots[slot];
  if (si.cm93 >= 0) {
    // This cell and its eight neighbours
    const int s = si.cm93;
    const double sz = cell_size_deg(s);
    std::vector<int> seen;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        double la = lat + dy * sz, lo = lon + dx * sz;
        if (std::fabs(la) > 85) continue;
        int ci = cell_index(la, lo, s);
        if (std::find(seen.begin(), seen.end(), ci) != seen.end()) continue;
        seen.push_back(ci);
        if (auto c = getCell(ci, s, cfg)) out.push_back(c);
      }
    return out;
  }
  // ENC: cells within reach, and at least a neighbourhood that suits the
  // band's typical cell size, so features just across a cell edge count
  static const double kNeighbourNm[7] = {0, 300, 120, 40, 15, 5, 2};
  const double r = std::max(reachNm, kNeighbourNm[si.enc]) / 60.0;
  const double rl = r / std::max(0.05, std::cos(lat * kDeg));
  return cellsInBox(lat - r, lat + r, lon - rl, lon + rl, slot, cfg);
}

bool DepthEngine::cellCovers(const CellData &cell, double lat, double lon) {
  bool haveCovr = false;
  for (const Feature &f : cell.features) {
    if (f.cls != "M_COVR" || f.geom != Geom::Area) continue;
    haveCovr = true;
    double catcov = 1;
    f.getNum("CATCOV", catcov);
    if (catcov == 2) continue;  // "no coverage available"
    if (geo::point_in_rings(f.parts, lat, lon)) return true;
  }
  if (haveCovr) return false;
  for (const Feature &f : cell.features) {
    if ((f.cls == "DEPARE" || f.cls == "DRGARE") && f.geom == Geom::Area &&
        geo::point_in_rings(f.parts, lat, lon))
      return true;
  }
  return false;
}

// False only when (lat, lon) is certainly outside the feature's bounding box.
static bool bbox_may_contain(const Feature &f, double lat, double lon) {
  if (lat < f.minLat || lat > f.maxLat) return false;
  if (f.maxLon - f.minLon > 180.0) return true;  // spans the antimeridian
  return lon >= f.minLon && lon <= f.maxLon;
}

bool area_depth_band(const Feature &f, double &d1, double &d2) {
  d2 = std::numeric_limits<double>::quiet_NaN();
  if (f.geom != Geom::Area) return false;
  if (f.cls == "DEPARE" || f.cls == "DRGARE") {
    f.getNum("DRVAL2", d2);
    if (f.getNum("DRVAL1", d1)) return true;
    // No minimum given: the area is charted but its depth is not (a sand or
    // mud flat, an intertidal area). Treat it as drying: 0 m, and with no
    // upper limit given, 0 m at most too, so an estimate from soundings
    // around it (interpolated mode) is held at 0 m inside it.
    if (f.cm93Cls == "ITDARE" && std::isfinite(d2)) {
      d1 = d2;
      return true;
    }
    d1 = 0.0;
    if (!std::isfinite(d2)) d2 = 0.0;
    return true;
  }
  if (f.cls == "SBDARE") {
    double w;
    if (f.getNum("WATLEV", w) && (w == 1 || w == 2 || w == 4 || w == 5)) {
      d1 = 0.0;
      d2 = 0.0;
      return true;
    }
  }
  return false;
}

bool is_danger_class(const std::string &c) {
  return c == "OBSTRN" || c == "WRECKS" || c == "UWTROC";
}

DangerKind danger_depth(const Feature &f, double &v) {
  if (f.getNum("VALSOU", v)) return DangerKind::Charted;
  double w;
  if (f.getNum("WATLEV", w) && (w == 1 || w == 2 || w == 4 || w == 5)) {
    v = 0.0;
    return DangerKind::Drying;
  }
  double cat;
  if (f.cls == "OBSTRN" && f.getNum("CATOBS", cat) && (cat == 5 || cat == 6 || cat == 7))
    return DangerKind::NotDanger;
  return DangerKind::Unknown;
}

bool bare_seabed_area(const Feature &f) {
  if (f.cls != "SBDARE" || f.geom != Geom::Area) return false;
  double v;
  return !f.getNum("DRVAL1", v) && !f.getNum("VALSOU", v) && !f.getNum("WATLEV", v);
}

// "Depth area here": the minimum (DRVAL1) of the depth area containing the
// vessel, plus dangers charted inside that same area.
static void searchDepthAreaHere(const CellData &cell, double lat, double lon,
                                const EngineConfig &cfg, DepthResult &res) {
  const Feature *area = nullptr;
  const Feature *bare = nullptr;  // seabed area with no depth at all
  double d1 = 0, d2 = std::numeric_limits<double>::quiet_NaN();
  for (const Feature &f : cell.features) {
    if (f.geom != Geom::Area) continue;
    if (f.cls != "DEPARE" && f.cls != "DRGARE" && f.cls != "SBDARE") continue;
    res.objectsSearched++;
    if (!bbox_may_contain(f, lat, lon)) continue;
    if (!geo::point_in_rings(f.parts, lat, lon)) continue;
    double v, v2;
    if (!area_depth_band(f, v, v2)) {
      if (!bare && bare_seabed_area(f)) bare = &f;
      continue;
    }
    if (!area || v < d1) {  // overlapping areas: take the shallower
      area = &f;
      d1 = v;
      d2 = v2;
    }
  }
  // A seabed area (e.g. sand) with no depth, where no depth area is
  // charted: a bank the chart gives no depth for. Counts as 0 m.
  if (!area && bare) {
    area = bare;
    d1 = 0.0;
    d2 = 0.0;
  }
  if (!area) return;

  res.haveLeast = true;
  res.leastDepth = d1;
  res.leastPos = {lat, lon};
  res.leastClass = area->cls;
  res.rangeNm = 0;
  res.bearingDeg = 0;
  res.haveAtPos = true;
  res.atPosMin = d1;
  res.atPosMax = d2;
  res.atPosClass = area->cls;
  res.atPosScaleChar = cell.ch;

  if (!cfg.useHazards) return;
  bool haveMin = false;
  for (const Feature &f : cell.features) {
    if (f.cls != "OBSTRN" && f.cls != "WRECKS" && f.cls != "UWTROC") continue;
    // inside if any of its points/vertices lies in the area
    bool inside = false;
    for (const auto &p : f.points)
      if (geo::point_in_rings(area->parts, p.lat, p.lon)) { inside = true; break; }
    for (size_t i = 0; !inside && i < f.parts.size(); i++)
      for (const auto &p : f.parts[i])
        if (geo::point_in_rings(area->parts, p.lat, p.lon)) { inside = true; break; }
    if (!inside) continue;
    double v;
    DangerKind k = danger_depth(f, v);
    if (k == DangerKind::NotDanger) continue;
    if (k == DangerKind::Unknown) {
      res.unknownHazards++;
    } else if (v < d1) {
      res.shallowHazards++;
      if (!haveMin || v < res.shallowHazardMin) res.shallowHazardMin = v;
      haveMin = true;
    }
  }
}

void DepthEngine::searchCell(const CellData &cell, double lat, double lon,
                             const EngineConfig &cfg, DepthResult &res) {
  if (cfg.mode == SearchMode::DepthAreaHere) {
    searchDepthAreaHere(cell, lat, lon, cfg, res);
    return;
  }
  const bool radius = cfg.mode == SearchMode::Radius;
  const double R = std::max(0.0, cfg.radiusNm);
  const double cosLat = std::max(1e-6, std::cos(lat * kDeg));

  double bestRange = 0;
  auto consider = [&](double depth, const LatLon &p, double range,
                      const std::string &cls) {
    if (!std::isfinite(depth)) return;
    if (radius && range > R) return;
    if (!res.haveLeast || depth < res.leastDepth - 1e-9 ||
        (std::fabs(depth - res.leastDepth) <= 1e-9 && range < bestRange)) {
      res.haveLeast = true;
      res.leastDepth = depth;
      res.leastPos = p;
      res.leastClass = cls;
      bestRange = range;
    }
  };

  // Cheap rejection of objects whose bounding box is entirely out of range
  auto bboxOut = [&](const Feature &f) {
    if (!radius) return false;
    double dlat = 0;
    if (lat < f.minLat) dlat = f.minLat - lat;
    else if (lat > f.maxLat) dlat = lat - f.maxLat;
    if (dlat * 60.0 > R) return true;
    if (f.maxLon - f.minLon > 180.0) return false;  // spans the antimeridian
    double dlon = 0;
    if (lon < f.minLon) dlon = f.minLon - lon;
    else if (lon > f.maxLon) dlon = lon - f.maxLon;
    if (dlon > 180.0) dlon = 360.0 - dlon;
    return dlon * 60.0 * cosLat > R;
  };

  for (const Feature &f : cell.features) {
    const std::string &c = f.cls;
    const bool isArea = (c == "DEPARE" || c == "DRGARE" || c == "SBDARE");
    const bool isSnd = (c == "SOUNDG");
    const bool isHaz = (c == "OBSTRN" || c == "WRECKS" || c == "UWTROC");
    const bool isCnt = (c == "DEPCNT");
    if (isArea && !cfg.useAreas) continue;
    double areaD1 = 0, areaD2 = 0;
    if (isArea && !area_depth_band(f, areaD1, areaD2)) continue;
    if (isSnd && !cfg.useSoundings) continue;
    if (isHaz && !cfg.useHazards) continue;
    if (isCnt && !cfg.useContours) continue;
    if (!(isArea || isSnd || isHaz || isCnt)) continue;
    if (bboxOut(f)) continue;
    res.objectsSearched++;

    if (isSnd) {
      for (size_t i = 0; i < f.points.size() && i < f.z.size(); i++) {
        double x, y;
        geo::to_local(lat, lon, f.points[i].lat, f.points[i].lon, x, y);
        consider(f.z[i], f.points[i], std::hypot(x, y), c);
      }
      continue;
    }

    // Distance from the vessel to the object (0 when inside an area)
    LatLon nearest{lat, lon};
    double dist = std::numeric_limits<double>::infinity();
    bool inside = false;
    if (f.geom == Geom::Area) {
      inside = geo::point_in_rings(f.parts, lat, lon);
      dist = inside ? 0.0 : geo::nearest_on_parts(f.parts, lat, lon, nearest);
      if (inside) nearest = {lat, lon};
    } else if (f.geom == Geom::Line) {
      dist = geo::nearest_on_parts(f.parts, lat, lon, nearest);
    } else if (!f.points.empty()) {
      for (const auto &p : f.points) {
        double x, y;
        geo::to_local(lat, lon, p.lat, p.lon, x, y);
        double d = std::hypot(x, y);
        if (d < dist) dist = d, nearest = p;
      }
    }
    if (!std::isfinite(dist)) continue;

    if (isArea) {
      const double d1 = areaD1;
      if (inside) {
        const double d2 = areaD2;
        if (!res.haveAtPos || d1 < res.atPosMin) {
          res.haveAtPos = true;
          res.atPosMin = d1;
          res.atPosMax = d2;
          res.atPosClass = c;
        }
      }
      consider(d1, nearest, dist, c);
    } else if (isHaz) {
      double v;
      DangerKind k = danger_depth(f, v);
      if (k == DangerKind::Charted || k == DangerKind::Drying) {
        consider(v, nearest, dist, c);
      } else if (k == DangerKind::Unknown && (!radius || dist <= R)) {
        res.unknownHazards++;
      }
    } else if (isCnt) {
      double v;
      if (f.getNum("VALDCO", v)) consider(v, nearest, dist, c);
    }
  }

  if (res.haveLeast) {
    double x, y;
    geo::to_local(lat, lon, res.leastPos.lat, res.leastPos.lon, x, y);
    res.rangeNm = std::hypot(x, y);
    double b = std::atan2(x, y) / kDeg;
    if (b < 0) b += 360.0;
    res.bearingDeg = res.rangeNm < 1e-6 ? 0.0 : b;
  }
}

std::string DepthEngine::diagnostics(double lat, double lon,
                                     const EngineConfig &cfg) {
  char buf[512];
  std::string out;
  std::snprintf(buf, sizeof buf, "cm93depth_pi report\nposition %.6f %.6f\n", lat, lon);
  out += buf;
  out += "CM93 root " + (m_root.empty() ? std::string("(none)") : m_root) +
         (cfg.useCm93 ? "" : " (not used: switched off)") + "\n";
  out += m_enc.summary() + (cfg.useEnc ? "" : " (not used: switched off)") + "\n";
  if (!m_enc.firstError().empty()) out += "  first unreadable: " + m_enc.firstError() + "\n";
  if (!ready()) return out + "charts not configured\n";
  syncEnc();

  // Everything charted at the position in one chart
  auto describe = [&](const CellData &cell, const std::shared_ptr<CellData> &cptr) {
    (void)cptr;
    if (!cell.readError.empty()) out += "  READ ERROR " + cell.readError + "\n";
    const CellData *c = &cell;
    int nCovr = 0, nDep = 0, nCnt = 0, nSnd = 0;
    for (const Feature &f : c->features) {
      if (f.cls == "M_COVR") nCovr++;
      else if (f.cls == "DEPARE" || f.cls == "DRGARE") nDep++;
      else if (f.cls == "DEPCNT") nCnt++;
      else if (f.cls == "SOUNDG") nSnd++;
    }
    std::snprintf(buf, sizeof buf,
                  "  features %zu: M_COVR %d, depth areas %d, contours %d, sounding groups %d\n"
                  "  coverage contains position: %s\n",
                  c->features.size(), nCovr, nDep, nCnt, nSnd,
                  cellCovers(*c, lat, lon) ? "yes" : "no");
    out += buf;
    {  // soundings: count, and the nearest few in this chart
      struct S { double d, z, b; };
      std::vector<S> near;
      size_t nPts = 0;
      for (const Feature &f : c->features) {
        if (f.cls != "SOUNDG") continue;
        for (size_t i = 0; i < f.points.size() && i < f.z.size(); i++) {
          nPts++;
          double x, y;
          geo::to_local(lat, lon, f.points[i].lat, f.points[i].lon, x, y);
          double b = std::atan2(x, y) / kDeg;
          near.push_back({std::hypot(x, y), f.z[i], b < 0 ? b + 360 : b});
        }
      }
      std::sort(near.begin(), near.end(), [](const S &a, const S &b) { return a.d < b.d; });
      std::snprintf(buf, sizeof buf, "  sounding points %zu; nearest:", nPts);
      out += buf;
      for (size_t i = 0; i < near.size() && i < 6; i++) {
        std::snprintf(buf, sizeof buf, " %.1fm@%.2fnm/%03.0f", near[i].z, near[i].d, near[i].b);
        out += buf;
      }
      out += "\n";
    }
    for (const Feature &f : c->features) {
      if ((f.cls != "DEPARE" && f.cls != "DRGARE" && f.cls != "SBDARE") ||
          f.geom != Geom::Area)
        continue;
      bool inBox = lat >= f.minLat && lat <= f.maxLat &&
                   (f.maxLon - f.minLon > 180.0 || (lon >= f.minLon && lon <= f.maxLon));
      if (!inBox) continue;
      bool inside = geo::point_in_rings(f.parts, lat, lon);
      double d1 = NAN, d2 = NAN;
      f.getNum("DRVAL1", d1);
      f.getNum("DRVAL2", d2);
      double u1, u2;
      const bool counted = area_depth_band(f, u1, u2);
      size_t nv = 0;
      for (auto &p : f.parts) nv += p.size();
      auto num = [](double v) {
        char b[24];
        if (std::isfinite(v)) std::snprintf(b, sizeof b, "%g", v);
        else std::snprintf(b, sizeof b, "none");
        return std::string(b);
      };
      std::string used = counted ? "used as " + num(u1) + " to " + num(u2) + " m"
                                 : std::string("not a depth");
      std::snprintf(buf, sizeof buf,
                    "  %s(%s) DRVAL1 %s, DRVAL2 %s (%s), %zu rings, %zu vertices, "
                    "box %.4f..%.4f %.4f..%.4f: %s\n",
                    f.cls.c_str(), f.cm93Cls.c_str(), num(d1).c_str(), num(d2).c_str(),
                    used.c_str(), f.parts.size(), nv, f.minLat, f.maxLat, f.minLon,
                    f.maxLon, inside ? "CONTAINS position" : "box only");
      out += buf;
      std::string attrs;
      for (auto &a : f.attrs) {
        attrs += a.first;
        if (a.second.hasNum) attrs += "=" + num(a.second.num);
        else if (!a.second.str.empty()) attrs += "=\"" + a.second.str + "\"";
        attrs += " ";
      }
      out += "    attrs: " + (attrs.empty() ? std::string("(none)") : attrs) + "\n";
    }
    // Everything else charted here, every object class (decoded afresh):
    // areas containing the position, and points / lines within 0.1 nm
    CellData full;
    bool okFull = false;
    if (c->scaleIdx >= 0) {
      DecodeOptions all;
      all.offsetE = cfg.offsetE;
      all.offsetN = cfg.offsetN;
      all.openCpnSoundings = cfg.openCpnSoundings;
      okFull = load_cell(m_root, m_dict, c->cellIndex, c->scaleIdx, all, full);
    } else if (!c->files.empty()) {
      okFull = load_enc(c->files[0], DecodeOptions(), full);
    }
    if (okFull) {
      static const std::set<std::string> listed = {"DEPARE", "DRGARE", "SBDARE", "SOUNDG", "M_COVR"};
      int shown = 0;
      for (const Feature &f : full.features) {
        if (listed.count(f.cls) || shown >= 25) continue;
        std::string where;
        if (f.geom == Geom::Area) {
          if (!bbox_may_contain(f, lat, lon) || !geo::point_in_rings(f.parts, lat, lon)) continue;
          std::snprintf(buf, sizeof buf, "CONTAINS position (extent %.2f x %.2f nm)",
                        (f.maxLat - f.minLat) * 60.0,
                        (f.maxLon - f.minLon) * 60.0 * std::cos(lat * kDeg));
          where = buf;
        } else {
          LatLon n{lat, lon};
          double d = f.parts.empty() ? 1e18 : geo::nearest_on_parts(f.parts, lat, lon, n);
          for (auto &p : f.points) {
            double x, y;
            geo::to_local(lat, lon, p.lat, p.lon, x, y);
            d = std::min(d, std::hypot(x, y));
          }
          if (d > 0.1) continue;
          std::snprintf(buf, sizeof buf, "%.3f nm away", d);
          where = buf;
        }
        std::string attrs;
        for (auto &a : f.attrs) {
          attrs += a.first;
          if (a.second.hasNum) {
            std::snprintf(buf, sizeof buf, "=%g", a.second.num);
            attrs += buf;
          } else if (!a.second.str.empty()) {
            attrs += "=\"" + a.second.str + "\"";
          }
          attrs += " ";
        }
        static const char *gname[] = {"none", "point", "line", "?", "area"};
        int gi = (int)f.geom;
        std::snprintf(buf, sizeof buf, "  other: %s(%s) %s, %s: ", f.cls.c_str(),
                      f.cm93Cls.c_str(), gi <= 4 ? gname[gi] : "multipoint", where.c_str());
        out += buf + (attrs.empty() ? std::string("(no attributes)") : attrs) + "\n";
        shown++;
      }
    }
  };

  for (int slot : slotsDown(cfg)) {
    const SlotInfo &si = kSlots[slot];
    if (si.cm93 >= 0) {
      const int s = si.cm93;
      int ci = cell_index(lat, lon, s);
      std::snprintf(buf, sizeof buf, "\n[%c] CM93 cell %d\n", si.ch, ci);
      out += buf;
      for (char sub : {'0', 'A', 'B', 'C', 'D'}) {
        std::string f = find_cell_file(m_root, ci, s, sub);
        if (!f.empty()) out += "  file " + f + "\n";
      }
      auto c = getCell(ci, s, cfg);
      std::string e = loadError(ci, s);
      if (!e.empty()) out += "  READ ERROR " + e + "\n";
      if (c) describe(*c, c);
      continue;
    }
    static const char *bandName[7] = {"", "Overview", "General", "Coastal",
                                      "Approach", "Harbour", "Berthing"};
    // ENC cells of this band whose extent holds the position (and those
    // that failed to load)
    const auto &es = m_enc.entries();
    for (size_t i : m_band[si.enc]) {
      const EncEntry &en = es[i];
      if (lat < en.minLat || lat > en.maxLat) continue;
      double l = near_lon(lon, 0.5 * (en.minLon + en.maxLon));
      if (l < en.minLon || l > en.maxLon) continue;
      std::snprintf(buf, sizeof buf, "\n[%c] ENC %s, %s band, 1:%d\n  file %s\n", si.ch,
                    en.dsnm.c_str(), bandName[si.enc], en.cscl, en.path.c_str());
      out += buf;
      auto c = getEnc(i);
      if (!c) {
        auto er = m_encErrors.find(en.path);
        out += "  READ ERROR " + (er == m_encErrors.end() ? std::string("?") : er->second) + "\n";
        continue;
      }
      if (c->files.size() > 1) {
        std::snprintf(buf, sizeof buf, "  updates applied: %zu (to %s)\n", c->files.size() - 1,
                      c->files.back().c_str());
        out += buf;
      }
      describe(*c, c);
    }
  }
  {
    int hs = -1;
    double around = 0;
    if (holeAt(lat, lon, cfg, hs, around)) {
      std::snprintf(buf, sizeof buf,
                    "\ngap: the position is in a gap in the band %c depth areas "
                    "(least depth around it %g m)\n", kSlots[hs].ch, around);
      out += buf;
    }
  }
  return out;
}

bool DepthEngine::lookupAreaAcrossScales(double lat, double lon,
                                         const EngineConfig &cfg,
                                         DepthResult &res) {
  EngineConfig ac = cfg;
  ac.mode = SearchMode::DepthAreaHere;
  ac.useHazards = false;
  for (int slot : slotsDown(cfg)) {
    bool found = false;
    for (auto &c : cellsAt(lat, lon, slot, cfg)) {
      DepthResult tmp;
      searchCell(*c, lat, lon, ac, tmp);
      if (!tmp.haveAtPos) continue;
      if (found && tmp.atPosMin >= res.atPosMin) continue;  // overlapping ENCs: shallower
      found = true;
      res.haveAtPos = true;
      res.atPosMin = tmp.atPosMin;
      res.atPosMax = tmp.atPosMax;
      res.atPosClass = tmp.atPosClass;
      res.atPosScaleChar = c->ch;
    }
    if (found) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Interpolated depth from the nearest soundings
//
// Pool: soundings within cfg.radiusNm from every scale band (the cell under
// the vessel and its eight neighbours at each scale), repeats removed. Deep
// water is often charted only at small scales while a nearby coast has a
// detailed cell, so taking the "most detailed scale with any sounding"
// would favour distant inshore soundings; pooling by distance does not.
// Selection: the four nearest. With cfg.interpQuadrants, the nearest in each
// quadrant (NE, SE, SW, NW) first, then the nearest remaining up to four.
// Estimate: inverse-distance weighting, weight = 1 / distance^2. A sounding
// closer than ~2 m is taken as the answer outright. The estimate is then
// limited to the DRVAL1..DRVAL2 band of the charted depth area at the
// vessel, if there is one.
// ---------------------------------------------------------------------------
bool DepthEngine::onLandAt(double lat, double lon, const EngineConfig &cfg,
                           int &slotOut) {
  for (int slot : slotsDown(cfg)) {
    bool water = false, land = false;
    for (auto &c : cellsAt(lat, lon, slot, cfg)) {
      for (const Feature &f : c->features) {
        if (f.geom != Geom::Area) continue;
        const bool isLand = f.cls == "LNDARE";
        double a1, a2;
        if (!isLand && !area_depth_band(f, a1, a2)) continue;
        if (!bbox_may_contain(f, lat, lon)) continue;
        if (!geo::point_in_rings(f.parts, lat, lon)) continue;
        (isLand ? land : water) = true;
      }
    }
    if (land || water) {  // the first band charting the spot decides
      slotOut = slot;
      return land && !water;
    }
  }
  return false;
}

void DepthEngine::findDangerHere(double lat, double lon, const EngineConfig &cfg,
                                 DepthResult &res) {
  // From the most detailed band down to that of the depth area used: a
  // danger charted in a more detailed band than the depth area counts.
  // With coarse dangers, every band is looked at and the shallowest counts.
  const int s0 = res.haveAtPos ? slot_from_char(res.atPosScaleChar) : -1;
  const double kOnPointNm = 0.005;  // ~9 m: on top of a point danger
  for (int slot : slotsDown(cfg)) {
    if (!cfg.coarseDangers && cfg.scaleIdx < 0 && s0 >= 0 && slot < s0) break;
    bool found = false;
    for (auto &c : cellsAt(lat, lon, slot, cfg)) {
      for (const Feature &f : c->features) {
        if (!is_danger_class(f.cls)) continue;
        bool over = false;
        if (f.geom == Geom::Area) {
          over = bbox_may_contain(f, lat, lon) && geo::point_in_rings(f.parts, lat, lon);
        } else {
          for (const auto &p : f.points) {
            double x, y;
            geo::to_local(lat, lon, p.lat, p.lon, x, y);
            if (std::hypot(x, y) <= kOnPointNm) over = true;
          }
        }
        if (!over) continue;
        double v;
        DangerKind k = danger_depth(f, v);
        if (k == DangerKind::NotDanger) continue;
        {
          double bd, bn;
          char bs;
          if (dangerOverruled(f, slot, k == DangerKind::Unknown ? 0.0 : v, lat, lon, cfg, &bd,
                              &bn, &bs)) {
            if (!res.dangerOverruled) {
              res.dangerOverruled = true;
              res.overruledClass = f.cls;
              res.overruledScale = c->ch;
              res.overruledByDepth = bd;
              res.overruledByDistNm = bn;
              res.overruledByScale = bs;
            }
            continue;
          }
        }
        bool assumed = false;
        if (k == DangerKind::Unknown) {
          assumed = true;
          v = (cfg.undepthedDangerRule == 1 && res.haveAtPos) ? res.atPosMin : 0.0;
        }
        if (!res.dangerHere || v < res.dangerHereDepth) {
          res.dangerHere = true;
          res.dangerHereDepth = v;
          res.dangerHereClass = f.cls;
          res.dangerHereAssumed = assumed;
          res.dangerHereScale = c->ch;
        }
        found = true;
      }
    }
    // Without coarse dangers: the most detailed band charting one decides.
    if (found && !cfg.coarseDangers) break;
  }
}

bool DepthEngine::dangerOverruled(const Feature &f, int slot, double dangerDepth, double lat,
                                  double lon, const EngineConfig &cfg, double *byDepth,
                                  double *byDistNm, char *byScale) {
  if (f.geom != Geom::Area || cfg.coarseCheckNm <= 0) return false;
  const double R = cfg.coarseCheckNm;
  const double need = std::max(cfg.coarseClearDepthM, dangerDepth);
  bool found = false;
  double bestD = 1e18;
  for (int s : slotsDown(cfg)) {
    if (s <= slot) break;  // only more detailed bands
    for (auto &c : cellsAround(lat, lon, s, cfg, R)) {
      for (const Feature &g : c->features) {
        if (g.cls != "SOUNDG") continue;
        if (g.minLat - lat > R / 60.0 || lat - g.maxLat > R / 60.0) continue;
        for (size_t i = 0; i < g.points.size() && i < g.z.size(); i++) {
          if (!(g.z[i] >= need && g.z[i] > dangerDepth)) continue;
          double x, y;
          geo::to_local(lat, lon, g.points[i].lat, g.points[i].lon, x, y);
          double d = std::hypot(x, y);
          if (d > R || d >= bestD) continue;
          if (!geo::point_in_rings(f.parts, g.points[i].lat, g.points[i].lon)) continue;
          found = true;
          bestD = d;
          if (byDepth) *byDepth = g.z[i];
          if (byDistNm) *byDistNm = d;
          if (byScale) *byScale = c->ch;
        }
      }
    }
  }
  return found;
}

bool DepthEngine::gapAt(double lat, double lon, int slot, const EngineConfig &cfg,
                        double &surroundMin) {
    // the charts there and round it, so a gap near a chart edge is enclosed too
    std::vector<std::shared_ptr<CellData>> home = cellsAt(lat, lon, slot, cfg);
    if (home.empty()) return false;
    if (kSlots[slot].enc > 0) {
      // An ENC's extent is a rectangle round its coverage: outside the
      // coverage is not a gap
      bool covered = false;
      for (auto &c : home) covered = covered || cellCovers(*c, lat, lon);
      if (!covered) return false;
    }
    std::vector<std::shared_ptr<CellData>> cells = cellsAround(lat, lon, slot, cfg);
    auto isBoundary = [](const Feature &f, double &d1) {
      if (f.geom != Geom::Area) return false;
      if (f.cls == "LNDARE") {
        d1 = 0.0;
        return true;
      }
      double d2;
      return area_depth_band(f, d1, d2);
    };
    bool anyArea = false;
    for (auto &c : cells)
      for (const Feature &f : c->features) {
        double d1;
        if (!isBoundary(f, d1)) continue;
        anyArea = true;
        if (bbox_may_contain(f, lat, lon) && geo::point_in_rings(f.parts, lat, lon))
          return false;  // charted water (or land) here in this band: no gap
      }
    if (!anyArea) return false;  // no depth areas in this band here
    // nearest boundary met going N, E, S, W, and the DRVAL1 of what it bounds
    double hit[4] = {1e18, 1e18, 1e18, 1e18};
    double hitDepth[4] = {NAN, NAN, NAN, NAN};
    for (auto &c : cells)
      for (const Feature &f : c->features) {
        double d1;
        if (!isBoundary(f, d1)) continue;
        for (auto &ring : f.parts) {
          const size_t n = ring.size();
          if (n < 2) continue;
          double px, py;
          geo::to_local(lat, lon, ring[n - 1].lat, ring[n - 1].lon, px, py);
          for (size_t i = 0; i < n; i++) {
            double qx, qy;
            geo::to_local(lat, lon, ring[i].lat, ring[i].lon, qx, qy);
            // crossing of the +y (N) / -y (S) axis: x changes sign
            if ((px > 0) != (qx > 0)) {
              double y = py + (0 - px) * (qy - py) / (qx - px);
              int k = y >= 0 ? 0 : 2;
              if (std::fabs(y) < hit[k]) hit[k] = std::fabs(y), hitDepth[k] = d1;
            }
            // crossing of the +x (E) / -x (W) axis: y changes sign
            if ((py > 0) != (qy > 0)) {
              double x = px + (0 - py) * (qx - px) / (qy - py);
              int k = x >= 0 ? 1 : 3;
              if (std::fabs(x) < hit[k]) hit[k] = std::fabs(x), hitDepth[k] = d1;
            }
            px = qx, py = qy;
          }
        }
      }
    bool enclosed = true;
    for (double h : hit) enclosed = enclosed && h < 1e17;
    if (!enclosed) return false;  // depth areas only to one side: not a gap
    surroundMin = 1e18;
    for (double d : hitDepth)
      if (std::isfinite(d)) surroundMin = std::min(surroundMin, d);
    if (surroundMin > 1e17) surroundMin = 0.0;
    return true;
}

bool DepthEngine::holeAt(double lat, double lon, const EngineConfig &cfg,
                         int &slotOut, double &surroundMin) {
  for (int slot : slotsDown(cfg)) {
    if (chartedAt(lat, lon, slot, cfg)) return false;  // water or land here
    if (gapAt(lat, lon, slot, cfg, surroundMin)) {
      slotOut = slot;
      return true;
    }
  }
  return false;
}

// Land polygons of one scale band in the vessel's local frame (nm), for the
// "is there land between the vessel and this sounding" test.
namespace {
struct LandPoly {
  double minx, maxx, miny, maxy;
  std::vector<std::vector<std::pair<double, double>>> rings;
};
double orient(double ax, double ay, double bx, double by, double cx, double cy) {
  return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}
// Segments (0,0)-(qx,qy) and a-b intersect (touching counts)
bool seg_cross(double qx, double qy, double ax, double ay, double bx, double by) {
  double d1 = orient(ax, ay, bx, by, 0, 0), d2 = orient(ax, ay, bx, by, qx, qy);
  double d3 = orient(0, 0, qx, qy, ax, ay), d4 = orient(0, 0, qx, qy, bx, by);
  if (((d1 > 0) != (d2 > 0) || d1 == 0 || d2 == 0) &&
      ((d3 > 0) != (d4 > 0) || d3 == 0 || d4 == 0)) {
    // reject the collinear-but-apart case
    if (d1 == 0 && d2 == 0)
      return std::max(std::min(0.0, qx), std::min(ax, bx)) <=
                 std::min(std::max(0.0, qx), std::max(ax, bx)) &&
             std::max(std::min(0.0, qy), std::min(ay, by)) <=
                 std::min(std::max(0.0, qy), std::max(ay, by));
    return true;
  }
  return false;
}
}  // namespace

DepthResult DepthEngine::computeInterpolated(double lat, double lon,
                                             const EngineConfig &cfg) {
  DepthResult res;
  const double R = std::max(0.01, cfg.radiusNm);
  struct Cand { LatLon p; double z, d, x, y; int scale; char ch; std::string name; };
  std::vector<Cand> cands;
  std::vector<std::shared_ptr<CellData>> usedCells;
  std::vector<int> usedSlots;  // band of each of usedCells
  std::vector<std::shared_ptr<CellData>> scaleCells[kNumSlots];

  for (int s : slotsDown(cfg)) {
    // The chart here and those round it, so soundings just across a chart
    // edge are not missed.
    const bool any = !cellsAt(lat, lon, s, cfg).empty();
    std::vector<std::shared_ptr<CellData>> cells = cellsAround(lat, lon, s, cfg, R);
    if (kSlots[s].enc > 0 && cells.empty()) continue;  // no ENC of this band near
    if (!res.scaleTrace.empty()) res.scaleTrace += ' ';
    res.scaleTrace += kSlots[s].ch;
    std::vector<Cand> found;
    for (auto &c : cells)
      for (const Feature &f : c->features) {
        if (f.cls != "SOUNDG") continue;
        if (f.minLat - lat > R / 60.0 || lat - f.maxLat > R / 60.0) continue;
        for (size_t i = 0; i < f.points.size() && i < f.z.size(); i++) {
          double x, y;
          geo::to_local(lat, lon, f.points[i].lat, f.points[i].lon, x, y);
          double d = std::hypot(x, y);
          if (d <= R) found.push_back({f.points[i], f.z[i], d, x, y, s, c->ch, c->name});
        }
      }
    if (found.empty()) {
      res.scaleTrace += any ? 'n' : '-';
      continue;
    }
    res.scaleTrace += '+';
    res.valid = true;
    cands.insert(cands.end(), found.begin(), found.end());
    usedCells.insert(usedCells.end(), cells.begin(), cells.end());
    usedSlots.insert(usedSlots.end(), cells.size(), s);
    scaleCells[s] = cells;
  }

  if (!res.valid) {
    res.status = "No soundings within " + std::to_string(R).substr(0, 4) + " nm";
    lookupAreaAcrossScales(lat, lon, cfg, res);
    return res;
  }

  // The same sounding appears at several scales and in overlapping subcells,
  // with positions differing by the cells' coordinate resolution: keep the
  // first (more detailed scales were added first) within ~20 m.
  std::stable_sort(cands.begin(), cands.end(),
                   [](const Cand &a, const Cand &b) { return a.d < b.d; });
  const double kSameNm = 0.011;
  std::vector<Cand> uniq;
  for (const Cand &c : cands) {
    bool dup = false;
    for (const Cand &u : uniq)
      if (std::fabs(u.x - c.x) < kSameNm && std::fabs(u.y - c.y) < kSameNm &&
          std::fabs(u.z - c.z) < 0.05) {
        dup = true;
        break;
      }
    if (!dup) uniq.push_back(c);
    if (uniq.size() >= 64) break;  // sorted: nothing further can be picked
  }
  res.objectsSearched = (int)uniq.size();

  // Drop soundings with charted land between them and the vessel (the far
  // side of a headland or island). Each sounding is tested against the land
  // of its own scale band, so coastline and soundings are generalised alike.
  // A band whose land covers the vessel itself (a coarse coastline over
  // inshore water) is not used for this test.
  if (cfg.landCheck) {
    std::vector<LandPoly> land[kNumSlots];
    bool built[kNumSlots] = {false};
    bool skipScale[kNumSlots] = {false};
    auto landOf = [&](int s) -> const std::vector<LandPoly> & {
      if (built[s]) return land[s];
      built[s] = true;
      for (auto &c : scaleCells[s])
        for (const Feature &f : c->features) {
          if (f.cls != "LNDARE" || f.geom != Geom::Area) continue;
          if (f.maxLat < lat - R / 60.0 - 0.01 || f.minLat > lat + R / 60.0 + 0.01) continue;
          if (geo::point_in_rings(f.parts, lat, lon)) skipScale[s] = true;
          LandPoly lp{1e18, -1e18, 1e18, -1e18, {}};
          for (auto &ring : f.parts) {
            std::vector<std::pair<double, double>> r;
            for (auto &p : ring) {
              double x, y;
              geo::to_local(lat, lon, p.lat, p.lon, x, y);
              r.push_back({x, y});
              lp.minx = std::min(lp.minx, x), lp.maxx = std::max(lp.maxx, x);
              lp.miny = std::min(lp.miny, y), lp.maxy = std::max(lp.maxy, y);
            }
            if (r.size() >= 2) lp.rings.push_back(std::move(r));
          }
          if (!lp.rings.empty()) land[s].push_back(std::move(lp));
        }
      if (skipScale[s]) land[s].clear();
      return land[s];
    };
    auto hidden = [&](const Cand &c) {
      for (const LandPoly &lp : landOf(c.scale)) {
        if (std::max(0.0, c.x) < lp.minx || std::min(0.0, c.x) > lp.maxx ||
            std::max(0.0, c.y) < lp.miny || std::min(0.0, c.y) > lp.maxy)
          continue;
        for (auto &r : lp.rings) {
          const size_t n = r.size();
          for (size_t i = 0; i < n; i++) {
            const auto &a = r[i], &b = r[(i + 1) % n];
            if (seg_cross(c.x, c.y, a.first, a.second, b.first, b.second)) return true;
          }
        }
      }
      return false;
    };
    std::vector<Cand> vis;
    for (const Cand &c : uniq) {
      if (hidden(c)) res.landBlocked++;
      else vis.push_back(c);
    }
    uniq.swap(vis);
    if (uniq.empty()) {
      res.valid = false;
      res.status = "Every sounding within reach has land between it and the vessel";
      lookupAreaAcrossScales(lat, lon, cfg, res);
      return res;
    }
  }

  std::vector<int> pick;
  if (cfg.interpQuadrants) {
    // Nearest per quadrant, then fill up to four with the nearest remaining
    int quadBest[4] = {-1, -1, -1, -1};
    for (int i = 0; i < (int)uniq.size(); i++) {
      int q = (uniq[i].x >= 0 ? 0 : 1) + (uniq[i].y >= 0 ? 0 : 2);
      if (quadBest[q] < 0) quadBest[q] = i;  // list is sorted by distance
    }
    for (int q = 0; q < 4; q++)
      if (quadBest[q] >= 0) pick.push_back(quadBest[q]);
  }
  for (int i = 0; i < (int)uniq.size() && pick.size() < 4; i++)
    if (std::find(pick.begin(), pick.end(), i) == pick.end()) pick.push_back(i);
  std::sort(pick.begin(), pick.end(), [&](int a, int b) { return uniq[a].d < uniq[b].d; });
  {  // Do the soundings used surround the vessel? True when the vessel is
     // inside their convex hull, i.e. no angular gap of 180 deg or more.
    std::vector<double> ang;
    for (int k : pick)
      if (uniq[k].d > 1e-9) ang.push_back(std::atan2(uniq[k].y, uniq[k].x));
    std::sort(ang.begin(), ang.end());
    double maxGap = 2 * kPi;
    if (ang.size() >= 3) {
      maxGap = ang.front() + 2 * kPi - ang.back();
      for (size_t i = 1; i < ang.size(); i++) maxGap = std::max(maxGap, ang[i] - ang[i - 1]);
    }
    res.enclosed = maxGap < kPi - 1e-6;
  }
  // Report the band of the nearest sounding used
  {
    int s = uniq[pick[0]].scale;
    res.scaleIdx = s;
    res.scaleChar = uniq[pick[0]].ch;
    if (kSlots[s].cm93 >= 0) {
      const int cs = kSlots[s].cm93;
      res.cellIndex = cell_index(lat, lon, cs);
      cell_origin(res.cellIndex, res.cellSwLat, res.cellSwLon);
      if (res.cellSwLon >= 180.0) res.cellSwLon -= 360.0;
      res.cellSizeDeg = cell_size_deg(cs);
    } else {
      res.cellName = uniq[pick[0]].name;
    }
  }

  // Inverse-distance weighting (power 2)
  const double kExactNm = 0.001;  // ~1.9 m: treat as standing on the sounding
  double est;
  if (uniq[pick[0]].d < kExactNm) {
    est = uniq[pick[0]].z;
    for (int k : pick)
      res.soundings.push_back({uniq[k].p, uniq[k].z, uniq[k].d, k == pick[0] ? 1.0 : 0.0,
                               uniq[k].ch});
  } else {
    double sw = 0, swz = 0;
    for (int k : pick) {
      double w = 1.0 / (uniq[k].d * uniq[k].d);
      sw += w;
      swz += w * uniq[k].z;
    }
    est = swz / sw;
    for (int k : pick)
      res.soundings.push_back({uniq[k].p, uniq[k].z, uniq[k].d,
                               (1.0 / (uniq[k].d * uniq[k].d)) / sw,
                               uniq[k].ch});
  }
  res.soundingMin = uniq[pick[0]].z;
  for (int k : pick) res.soundingMin = std::min(res.soundingMin, uniq[k].z);
  res.rawEstimate = est;

  // Keep it inside the charted depth band at the vessel
  lookupAreaAcrossScales(lat, lon, cfg, res);
  if (res.haveAtPos) {
    if (est < res.atPosMin) { est = res.atPosMin; res.clamped = true; }
    if (std::isfinite(res.atPosMax) && est > res.atPosMax) {
      est = res.atPosMax;
      res.clamped = true;
    }
  }
  // Over a rock, wreck or reef: no deeper than it
  if (cfg.useHazards) {
    findDangerHere(lat, lon, cfg, res);
    if (res.dangerHere && est > res.dangerHereDepth) {
      est = res.dangerHereDepth;
      res.clamped = true;
    }
  }

  res.haveLeast = true;
  res.leastDepth = est;
  res.leastPos = {lat, lon};
  res.leastClass = "INTERP";
  res.rangeNm = uniq[pick[0]].d;  // nearest sounding used
  res.bearingDeg = 0;

  // Dangers within the spread of the soundings used that have no charted
  // depth, or are charted shallower than the estimate
  if (cfg.useHazards) {
    double reach = 0;
    for (int k : pick) reach = std::max(reach, uniq[k].d);
    bool haveMin = false;
    // the same danger is charted at several scales: count each place once
    std::vector<std::pair<double, double>> counted;  // local x, y
    for (size_t ci = 0; ci < usedCells.size(); ci++)
      for (const Feature &f : usedCells[ci]->features) {
        if (f.cls != "OBSTRN" && f.cls != "WRECKS" && f.cls != "UWTROC") continue;
        LatLon near{lat, lon};
        double d = std::numeric_limits<double>::infinity();
        if (f.geom == Geom::Area && geo::point_in_rings(f.parts, lat, lon)) d = 0;
        else if (!f.parts.empty()) d = geo::nearest_on_parts(f.parts, lat, lon, near);
        for (auto &p : f.points) {
          double x, y;
          geo::to_local(lat, lon, p.lat, p.lon, x, y);
          if (std::hypot(x, y) < d) {
            d = std::hypot(x, y);
            near = p;
          }
        }
        if (d > reach) continue;
        double nx, ny;
        geo::to_local(lat, lon, near.lat, near.lon, nx, ny);
        bool dup = false;
        for (auto &q : counted)
          if (std::fabs(q.first - nx) < kSameNm && std::fabs(q.second - ny) < kSameNm) {
            dup = true;
            break;
          }
        if (dup) continue;
        counted.push_back({nx, ny});
        double v;
        DangerKind k = danger_depth(f, v);
        if (k == DangerKind::NotDanger) continue;
        if (dangerOverruled(f, usedSlots[ci], k == DangerKind::Unknown ? 0.0 : v, near.lat,
                            near.lon, cfg))
          continue;
        if (k == DangerKind::Unknown) {
          res.unknownHazards++;
        } else if (v < est) {
          res.shallowHazards++;
          if (!haveMin || v < res.shallowHazardMin) res.shallowHazardMin = v;
          haveMin = true;
        }
      }
  }
  res.status = "OK";
  return res;
}

void DepthEngine::checkOffsets(const EngineConfig &cfg) {
  if (cfg.offsetE != m_cachedOffE || cfg.offsetN != m_cachedOffN ||
      cfg.openCpnSoundings != m_cachedOcpnSnd) {
    clearCache();
    m_cachedOffE = cfg.offsetE;
    m_cachedOffN = cfg.offsetN;
    m_cachedOcpnSnd = cfg.openCpnSoundings;
  }
}

DepthResult DepthEngine::compute(double lat, double lon,
                                 const EngineConfig &cfg) {
  DepthResult res;
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
  if (slots.empty()) {
    res.status = cfg.scaleIdx >= 0 ? "Fixed CM93 scale chosen but no CM93 charts"
                                   : "All chart sources switched off";
    return res;
  }

  // In a gap in the depth areas (reef, rock, islet, unsurveyed area):
  // treated like a danger with no charted depth, whatever the mode
  {
    int hs = -1;
    double around = 0;
    if (!onLandAt(lat, lon, cfg, hs) && holeAt(lat, lon, cfg, hs, around)) {
      res.valid = true;
      res.haveLeast = true;
      res.leastDepth = cfg.undepthedDangerRule == 1 ? around : 0.0;
      res.leastPos = {lat, lon};
      res.leastClass = "HOLE";
      res.dangerHere = true;
      res.dangerHereDepth = res.leastDepth;
      res.dangerHereClass = "HOLE";
      res.dangerHereAssumed = true;
      res.scaleIdx = hs;
      res.scaleChar = kSlots[hs].ch;
      res.dangerHereScale = kSlots[hs].ch;
      if (kSlots[hs].cm93 >= 0) {
        res.cellIndex = cell_index(lat, lon, kSlots[hs].cm93);
      } else {
        auto cs = cellsAt(lat, lon, hs, cfg);
        if (!cs.empty()) res.cellName = cs[0]->name;
      }
      res.scaleTrace = std::string("gap in the band ") + kSlots[hs].ch + " depth areas";
      res.status = "OK";
      return res;
    }
  }

  // On charted land: no depth, whatever the mode
  {
    int ls = -1;
    if (onLandAt(lat, lon, cfg, ls)) {
      res.valid = true;  // charts read; the answer is "land"
      res.onLand = true;
      res.scaleIdx = ls;
      res.scaleChar = kSlots[ls].ch;
      res.status = std::string("Position is on charted land (band ") + kSlots[ls].ch + ")";
      return res;
    }
  }
  if (cfg.mode == SearchMode::Interpolated) return computeInterpolated(lat, lon, cfg);

  std::shared_ptr<CellData> cell;
  int usedSlot = -1;
  const bool areaMode = cfg.mode == SearchMode::DepthAreaHere;
  // Pre-computed result for depth-area mode (searched while choosing the band)
  DepthResult areaRes;
  bool haveAreaRes = false;

  // Most detailed band first (CM93 G / ENC berthing) down to the world chart
  // (Z). Depth-area mode keeps going until a chart actually has a depth area
  // at the position: sparsely charted water often has a detailed chart whose
  // coverage claims the spot while only a smaller-scale chart carries the
  // depth area. Other modes use the first chart whose coverage contains it.
  // With a fixed CM93 scale there is only that band.
  std::shared_ptr<CellData> firstCovering;
  int firstCoveringSlot = -1;
  for (int s : slots) {
    const SlotInfo &si = kSlots[s];
    std::vector<std::shared_ptr<CellData>> cs;
    if (!cell) cs = cellsAt(lat, lon, s, cfg);
    if (si.enc > 0 && cs.empty()) continue;  // ENC bands: only where present
    if (!res.scaleTrace.empty()) res.scaleTrace += ' ';
    res.scaleTrace += si.ch;
    if (cell) {  // already chosen
      res.scaleTrace += '.';
      continue;
    }
    if (cs.empty()) {
      int ci = cell_index(lat, lon, si.cm93);
      std::string e = loadError(ci, si.cm93);
      res.scaleTrace += e.empty() ? '-' : 'x';
      if (!e.empty() && res.readError.empty())
        res.readError = std::string(1, si.ch) + ": " + e;
      continue;
    }
    if (cfg.scaleIdx >= 0) {  // fixed scale: that cell, covered or not
      cell = cs[0];
      usedSlot = s;
      res.scaleTrace += '+';
      continue;
    }
    char mark = 'c';
    for (auto &c : cs) {
      if (!c->readError.empty() && res.readError.empty())
        res.readError = std::string(1, si.ch) + ": " + c->readError;
      bool covers = cellCovers(*c, lat, lon);
      if (areaMode) {
        DepthResult tmp;
        searchCell(*c, lat, lon, cfg, tmp);
        if (tmp.haveLeast) {
          // overlapping charts of one band: the shallower depth area
          if (!cell || tmp.leastDepth < areaRes.leastDepth) {
            cell = c;
            usedSlot = s;
            areaRes = tmp;
            haveAreaRes = true;
          }
          mark = '+';
          continue;
        }
        if (covers && !firstCovering) {
          firstCovering = c;
          firstCoveringSlot = s;
        }
        if (covers && mark == 'c') mark = 'n';
      } else if (covers) {
        if (!cell) {
          cell = c;
          usedSlot = s;
        }
        mark = '+';
      }
    }
    res.scaleTrace += mark;
  }
  if (!cell && firstCovering) {  // depth-area mode, nothing found anywhere
    cell = firstCovering;
    usedSlot = firstCoveringSlot;
  }

  if (!cell) {
    res.status = cfg.scaleIdx >= 0
                     ? std::string("No CM93 ") + kScales[cfg.scaleIdx].ch +
                           " cell at this position"
                     : "No chart covers this position";
    return res;
  }

  auto setCell = [&](DepthResult &r, const std::shared_ptr<CellData> &c, int slot) {
    r.scaleIdx = slot;
    r.scaleChar = c->ch;
    r.cellIndex = c->scaleIdx >= 0 ? c->cellIndex : 0;
    r.cellName = c->name;
    r.cellSwLat = c->swLat;
    r.cellSwLon = c->swLon;
    r.cellSizeDeg = c->sizeDeg;
  };

  res.valid = true;
  setCell(res, cell, usedSlot);
  if (haveAreaRes) {
    const std::string trace = res.scaleTrace;
    const DepthResult base = res;
    res = areaRes;
    res.readError = base.readError;
    res.valid = true;
    setCell(res, cell, usedSlot);
    res.scaleTrace = trace;
  } else {
    searchCell(*cell, lat, lon, cfg, res);
    if (res.haveAtPos) res.atPosScaleChar = res.scaleChar;
  }

  if (!areaMode && cfg.scaleIdx < 0) {
    // Cell / radius modes use the most detailed covering chart. In sparsely
    // charted water that chart may hold no depth area at the vessel, or no
    // depths within the radius, while a smaller-scale chart does. Fall back
    // through the bands for each, independently.
    if (!res.haveLeast) {
      bool done = false;
      for (int s : slots) {
        if (s >= usedSlot || done) continue;
        for (auto &c : cellsAt(lat, lon, s, cfg)) {
          DepthResult tmp;
          searchCell(*c, lat, lon, cfg, tmp);
          if (!tmp.haveLeast) continue;
          res.haveLeast = true;
          res.leastDepth = tmp.leastDepth;
          res.leastPos = tmp.leastPos;
          res.rangeNm = tmp.rangeNm;
          res.bearingDeg = tmp.bearingDeg;
          res.leastClass = tmp.leastClass;
          res.unknownHazards = tmp.unknownHazards;
          res.objectsSearched = tmp.objectsSearched;
          setCell(res, c, s);
          if (!res.haveAtPos && tmp.haveAtPos) {
            res.haveAtPos = true;
            res.atPosMin = tmp.atPosMin;
            res.atPosMax = tmp.atPosMax;
            res.atPosClass = tmp.atPosClass;
            res.atPosScaleChar = c->ch;
          }
          res.scaleTrace += std::string("  depths from ") + c->ch;
          done = true;
          break;
        }
      }
    }
    if (!res.haveAtPos && lookupAreaAcrossScales(lat, lon, cfg, res))
      res.scaleTrace += std::string("  area from ") + res.atPosScaleChar;
  }

  if (areaMode && res.haveLeast && cfg.useHazards) {
    findDangerHere(lat, lon, cfg, res);
    if (res.dangerHere && res.dangerHereDepth < res.leastDepth) {
      res.leastDepth = res.dangerHereDepth;
      res.leastClass = res.dangerHereClass;
    }
  }

  if (res.haveLeast)
    res.status = "OK";
  else if (cfg.mode == SearchMode::DepthAreaHere)
    res.status = "No depth area charted at this position";
  else
    res.status = "No charted depths in search area";
  return res;
}

}  // namespace cm93
