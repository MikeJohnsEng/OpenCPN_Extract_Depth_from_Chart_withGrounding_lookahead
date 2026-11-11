// Unit tests for the wx-free core: CM93 reader, depth engine, NMEA output.
//
// A synthetic CM93 tree is written to a temp directory using the inverse of
// the decoder (encode table, header layout, feature records), then read back
// through the same code paths the plugin uses.

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cm93_reader.h"
#include "depth_engine.h"
#include "alarm_logic.h"
#include "nmea_out.h"
#include "s57_reader.h"
#include "enc_index.h"
#include "s57_writer.h"

#ifdef HAVE_LZMA
#include <lzma.h>
#endif

namespace fs = std::filesystem;
using namespace cm93;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (cond) {                                                         \
      g_pass++;                                                         \
    } else {                                                            \
      g_fail++;                                                         \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond \
                << "\n";                                                \
    }                                                                   \
  } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

// ---------------------------------------------------------------------------
// Synthetic cell writer
// ---------------------------------------------------------------------------
static const double kPi = 3.1415926535897931160;
static const double kDeg = kPi / 180.0;
static const double kR = 6378388.0;

struct Writer {
  // cell geographic bounds
  double latMin, latMax, lonMin, lonMax;
  double eMin, eMax, nMin, nMax;

  struct Edge { std::vector<std::pair<uint16_t, uint16_t>> pts; };
  struct Run { std::vector<std::array<uint16_t, 3>> pts; };
  struct Attr { uint8_t id; std::vector<uint8_t> bytes; };
  struct Feat {
    uint8_t otype = 0, geotype = 0;
    std::vector<uint16_t> segs{};  // edge index | usage<<13
    int pointIndex = -1;
    std::vector<Attr> attrs;
  };
  std::vector<Edge> edges;
  std::vector<Run> runs;
  std::vector<std::pair<uint16_t, uint16_t>> pts2;
  std::vector<Feat> feats;

  Writer(double la0, double lo0, double size) {
    latMin = la0; latMax = la0 + size; lonMin = lo0; lonMax = lo0 + size;
    eMin = lonMin * kDeg * kR; eMax = lonMax * kDeg * kR;
    nMin = kR * std::log(std::tan(kPi / 4 + latMin * kDeg / 2));
    nMax = kR * std::log(std::tan(kPi / 4 + latMax * kDeg / 2));
  }
  std::pair<uint16_t, uint16_t> xy(double lat, double lon) const {
    double e = lon * kDeg * kR;
    double n = kR * std::log(std::tan(kPi / 4 + lat * kDeg / 2));
    double x = (e - eMin) / ((eMax - eMin) / 65535);
    double y = (n - nMin) / ((nMax - nMin) / 65535);
    return {(uint16_t)std::lround(x), (uint16_t)std::lround(y)};
  }
  int addEdge(const std::vector<std::pair<double, double>> &ll) {
    Edge e;
    for (auto &p : ll) e.pts.push_back(xy(p.first, p.second));
    edges.push_back(e);
    return (int)edges.size() - 1;
  }
  static Attr attrW(uint8_t id, double v) {  // aWORD10
    uint16_t w = (uint16_t)std::lround(v * 10);
    return {id, {(uint8_t)(w & 0xff), (uint8_t)(w >> 8)}};
  }
  static Attr attrR(uint8_t id, float v) {  // aFLOAT
    Attr a{id, std::vector<uint8_t>(4)};
    std::memcpy(a.bytes.data(), &v, 4);
    return a;
  }
  static Attr attrB(uint8_t id, uint8_t v) { return {id, {v}}; }
  static Attr attrS(uint8_t id, const std::string &s) {
    Attr a{id, std::vector<uint8_t>(s.begin(), s.end())};
    a.bytes.push_back(0);
    return a;
  }
  // closed square area made of two edges, the second traversed backwards
  void addSquareArea(uint8_t otype, double clat, double clon, double half,
                     std::vector<Attr> attrs) {
    auto p0 = std::make_pair(clat - half, clon - half);
    auto p1 = std::make_pair(clat - half, clon + half);
    auto p2 = std::make_pair(clat + half, clon + half);
    auto p3 = std::make_pair(clat + half, clon - half);
    int e1 = addEdge({p0, p1, p2});
    int e2 = addEdge({p0, p3, p2});
    Feat f; f.otype = otype; f.geotype = 0x84;
    f.segs = {(uint16_t)e1, (uint16_t)(e2 | (4 << 13))};
    f.attrs = attrs;
    feats.push_back(f);
  }
  void addRectArea(uint8_t otype, double la0, double lo0, double la1,
                   double lo1, std::vector<Attr> attrs) {
    int e = addEdge({{la0, lo0}, {la0, lo1}, {la1, lo1}, {la1, lo0}, {la0, lo0}});
    Feat f; f.otype = otype; f.geotype = 0x84;
    f.segs = {(uint16_t)e};
    f.attrs = attrs;
    feats.push_back(f);
  }
  // area of several closed rings (outer + holes), each {la0, lo0, la1, lo1}
  void addRectRings(uint8_t otype, const std::vector<std::array<double, 4>> &rects,
                    std::vector<Attr> attrs) {
    Feat f; f.otype = otype; f.geotype = 0x84;
    for (auto &r : rects) {
      int e = addEdge({{r[0], r[1]}, {r[0], r[3]}, {r[2], r[3]}, {r[2], r[1]}, {r[0], r[1]}});
      f.segs.push_back((uint16_t)e);
    }
    f.attrs = attrs;
    feats.push_back(f);
  }
  void addPoint(uint8_t otype, double lat, double lon, std::vector<Attr> attrs) {
    pts2.push_back(xy(lat, lon));
    Feat f; f.otype = otype; f.geotype = (uint8_t)(attrs.empty() ? 0x01 : 0x81);
    f.pointIndex = (int)pts2.size() - 1;
    f.attrs = attrs;
    feats.push_back(f);
  }
  // one record (and feature) per sounding: decodes the same either way
  void addSoundings(uint8_t otype,
                    const std::vector<std::array<double, 3>> &s) {
    for (auto &p : s) addSoundingRecord(otype, {p});
  }
  // several soundings in ONE record
  void addSoundingRecord(uint8_t otype,
                         const std::vector<std::array<double, 3>> &s) {
    Run r;
    for (auto &p : s) {
      auto q = xy(p[0], p[1]);
      double d = p[2];
      uint16_t z = d >= 1200 ? (uint16_t)(12000 + std::lround(d))
                             : (uint16_t)std::lround(d * 10);
      r.pts.push_back({q.first, q.second, z});
    }
    runs.push_back(r);
    Feat f; f.otype = otype; f.geotype = 0x08;
    f.pointIndex = (int)runs.size() - 1;
    feats.push_back(f);
  }
  void addLine(uint8_t otype, const std::vector<std::pair<double, double>> &ll,
               std::vector<Attr> attrs) {
    int e = addEdge(ll);
    Feat f; f.otype = otype; f.geotype = 0x82;
    f.segs = {(uint16_t)e};
    f.attrs = attrs;
    feats.push_back(f);
  }
  // raw z for magic-number testing
  void addRawSounding(uint8_t otype, double lat, double lon, uint16_t z) {
    Run r;
    auto q = xy(lat, lon);
    r.pts.push_back({q.first, q.second, z});
    runs.push_back(r);
    Feat f; f.otype = otype; f.geotype = 0x08;
    f.pointIndex = (int)runs.size() - 1;
    feats.push_back(f);
  }

  std::vector<uint8_t> build() const {
    std::vector<uint8_t> b;
    auto u8 = [&](uint8_t v) { b.push_back(v); };
    auto u16 = [&](uint16_t v) { u8(v & 0xff); u8(v >> 8); };
    auto i32 = [&](int32_t v) { for (int i = 0; i < 4; i++) u8((v >> (8 * i)) & 0xff); };
    auto f64 = [&](double v) { uint8_t t[8]; std::memcpy(t, &v, 8); for (auto c : t) u8(c); };

    int nVecPts = 0; for (auto &e : edges) nVecPts += (int)e.pts.size();
    int n3dPts = 0; for (auto &r : runs) n3dPts += (int)r.pts.size();
    int nSegs = 0, attrBytes = 0;
    for (auto &f : feats) {
      nSegs += (int)f.segs.size();
      for (auto &a : f.attrs) attrBytes += 1 + (int)a.bytes.size();
    }

    u16(138); i32(0); i32(0);  // prolog, lengths patched below
    f64(lonMin); f64(latMin); f64(lonMax); f64(latMax);
    f64(eMin); f64(nMin); f64(eMax); f64(nMax);
    u16((uint16_t)edges.size()); i32(nVecPts); i32(nSegs); i32(0);
    u16((uint16_t)runs.size()); i32(n3dPts); i32(0);
    u16((uint16_t)pts2.size()); u16(0); u16(0);
    u16((uint16_t)feats.size());
    i32(0); i32(0); u16(0); u16(0); u16(0); i32(0);
    i32(0); u16(0); i32(attrBytes); i32(0);
    if (b.size() != 138) { std::cerr << "header size " << b.size() << "\n"; std::abort(); }

    for (auto &e : edges) { u16((uint16_t)e.pts.size()); for (auto &p : e.pts) { u16(p.first); u16(p.second); } }
    for (auto &r : runs) { u16((uint16_t)r.pts.size()); for (auto &p : r.pts) { u16(p[0]); u16(p[1]); u16(p[2]); } }
    for (auto &p : pts2) { u16(p.first); u16(p.second); }
    for (auto &f : feats) {
      int geom = 0;
      int g = f.geotype & 0x0f;
      if (g == 4 || g == 2) geom = 2 + 2 * (int)f.segs.size();
      else if (g == 1 || g == 8) geom = 2;
      int alen = 0; for (auto &a : f.attrs) alen += 1 + (int)a.bytes.size();
      int total = 4 + geom + ((f.geotype & 0x80) ? 1 + alen : 0);
      u8(f.otype); u8(f.geotype); u16((uint16_t)total);
      if (g == 4 || g == 2) { u16((uint16_t)f.segs.size()); for (auto s : f.segs) u16(s); }
      else if (g == 1 || g == 8) u16((uint16_t)f.pointIndex);
      if (f.geotype & 0x80) {
        u8((uint8_t)f.attrs.size());
        for (auto &a : f.attrs) { u8(a.id); for (auto c : a.bytes) u8(c); }
      }
    }
    int32_t rest = (int32_t)b.size() - 138;
    std::memcpy(&b[2], &rest, 4);
    for (auto &c : b) c = encode_byte(c);
    return b;
  }
};

// Dictionary: class ids and attribute ids used by the writer
enum { C_DEPARE = 1, C_SOUNDG, C_OBSTRN, C_MSOR, C_ITDARE, C_DEPCNT, C_WRECKS, C_LNDARE, C_SBDARE };
enum { A_DRVAL1 = 0, A_DRVAL2, A_VALSOU, A_VALDCO, A_CATCOV, A_OBJNAM, A_WATLEV, A_CATOBS };

static void write_dictionary(const fs::path &dir) {
  std::ofstream o(dir / "CM93OBJ.DIC");
  o << "NULLNM|0|P\nDEPARE|1|A\nSOUNDG|2|P\nOBSTRN|3|P\n_m_sor|4|A\n"
       "ITDARE|5|A\nDEPCNT|6|L\nWRECKS|7|P\nLNDARE|8|A\nSBDARE|9|A\n";
  std::ofstream a(dir / "CM93ATTR.DIC");
  a << "; comment line\nDRVAL1|0|aFLOAT\nDRVAL2|1|aWORD10\nVALSOU|2|aWORD10\n"
       "VALDCO|3|aFLOAT\nCATCOV|4|aBYTE\nOBJNAM|5|aSTRING\nWATLEV|6|aBYTE\nCATOBS|7|aBYTE\n";
}

static void write_cell(const fs::path &root, int ci, int scaleIdx, char sub,
                       const Writer &w, bool lowerExt = false) {
  CellPathParts pp = cell_path_parts(ci, scaleIdx);
  char sc = kScales[scaleIdx].ch;
  fs::path dir = root / pp.rootDir / std::string(1, sc);
  fs::create_directories(dir);
  std::string name = std::string(1, sub) + pp.baseName + "." +
                     std::string(1, lowerExt ? (char)std::tolower(sc) : sc);
  auto data = w.build();
  std::ofstream f(dir / name, std::ios::binary);
  f.write((const char *)data.data(), data.size());
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Look-ahead. Ship at the centre of a G cell, course 090, so the track frame
// (u ahead = east, v to starboard = south) lines up with lat/lon and every
// object can be placed by its (u, v) in nautical miles.
// ---------------------------------------------------------------------------
static void test_lookahead() {
  fs::path root = fs::temp_directory_path() / "cm93depth_look_tree";
  fs::remove_all(root);
  fs::create_directories(root);
  write_dictionary(root);
  const int G = 7, E = 5;
  int ciG = cell_index(-43.10, 148.10, G);
  double gLat, gLon;
  cell_origin(ciG, gLat, gLon);
  const double sz = cell_size_deg(G);
  const double lat0 = gLat + sz / 2, lon0 = gLon + sz / 2;
  const double cl = std::cos(lat0 * kDeg);
  auto LA = [&](double v) { return lat0 - v / 60.0; };
  auto LO = [&](double u) { return lon0 + u / (60.0 * cl); };
  // rectangle given in (u, v)
  auto rect = [&](Writer &w, uint8_t ot, double u0, double v0, double u1, double v1,
                  std::vector<Writer::Attr> a) {
    w.addRectArea(ot, LA(v1), LO(u0), LA(v0), LO(u1), a);
  };
  {
    Writer w(gLat, gLon, sz);
    w.addRectArea(C_MSOR, gLat, gLon, gLat + sz, gLon + sz, {});
    // G charts u -1..1.75 only (20-30 m), plus the land beyond
    rect(w, C_DEPARE, -1, -1, 1.75, 1, {Writer::attrR(A_DRVAL1, 20.0f), Writer::attrW(A_DRVAL2, 30.0)});
    rect(w, C_DEPARE, 1.4, -0.1, 1.6, 0.1, {Writer::attrR(A_DRVAL1, 3.0f), Writer::attrW(A_DRVAL2, 5.0)});
    // clips the starboard edge of the corridor from u = 1.05
    rect(w, C_DEPARE, 1.05, 0.2, 1.45, 0.6, {Writer::attrR(A_DRVAL1, 2.0f), Writer::attrW(A_DRVAL2, 3.0)});
    w.addSoundings(C_SOUNDG, {{{LA(0.1), LO(0.5), 12.0}}, {{LA(-0.4), LO(0.8), 4.0}}});
    w.addPoint(C_WRECKS, LA(-0.2), LO(1.0), {Writer::attrS(A_OBJNAM, "Wreck")});
    w.addLine(C_DEPCNT, {{LA(-1), LO(0.3)}, {LA(1), LO(0.3)}}, {Writer::attrR(A_VALDCO, 10.0f)});
    rect(w, C_LNDARE, 2.8, -0.2, 3.2, 0.2, {});
    // behind the land, seen from u = 2.5
    w.addSoundings(C_SOUNDG, {{{LA(0.0), LO(3.5), 3.0}}});
    write_cell(root, ciG, G, '0', w);
  }
  {
    int ciE = cell_index(lat0, lon0, E);
    double eLat, eLon;
    cell_origin(ciE, eLat, eLon);
    double esz = cell_size_deg(E);
    Writer w(eLat, eLon, esz);
    w.addRectArea(C_MSOR, eLat, eLon, eLat + esz, eLon + esz, {});
    w.addRectArea(C_DEPARE, eLat, eLon, eLat + esz, eLon + esz,
                  {Writer::attrR(A_DRVAL1, 8.0f), Writer::attrW(A_DRVAL2, 20.0)});
    // generalised shoal where G is charted: must be ignored
    rect(w, C_DEPARE, 0.4, -0.1, 0.6, 0.1, {Writer::attrR(A_DRVAL1, 0.0f), Writer::attrW(A_DRVAL2, 2.0)});
    // E-only sounding beyond G's depth area: must be used
    w.addSoundings(C_SOUNDG, {{{LA(0.0), LO(1.92), 1.0}}});
    rect(w, C_LNDARE, 2.8, -0.2, 3.2, 0.2, {});
    write_cell(root, ciE, E, '0', w);
  }

  DepthEngine eng;
  std::string err;
  CHECK(eng.setRoot(root.u8string(), &err));
  EngineConfig ec;
  LookAheadConfig lc;
  lc.zone = LookZone::Corridor;
  lc.basis = LookBasis::Distance;
  lc.distanceNm = 2.0;
  lc.halfWidthNm = 0.25;
  lc.safetyDepthM = 5.0;
  lc.undepthedAsShoal = false;
  const double tolNm = 0.004;  // cell coordinate resolution and flat projection

  LookAheadResult r = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, ec);
  CHECK(r.valid);
  CHECK_NEAR(r.lengthNm, 2.0, 1e-9);
  CHECK(r.haveLeast);
  CHECK_NEAR(r.least.depth, 1.0, 1e-6);  // E sounding beyond G's coverage
  CHECK(r.least.cls == "SOUNDG" && r.least.scaleChar == 'E');
  CHECK_NEAR(r.least.rangeNm, 1.92, tolNm);
  CHECK_NEAR(r.least.bearingDeg, 90.0, 0.3);
  CHECK(r.haveShoal);
  CHECK(r.shoal.cls == "DEPARE");          // corner clipper, 2 m
  CHECK_NEAR(r.shoal.depth, 2.0, 1e-6);
  CHECK_NEAR(r.shoal.aheadNm, 1.05, tolNm);
  CHECK_NEAR(r.shoal.timeMin, 10.5, 0.05);  // 1.05 nm at 6 kn
  CHECK(r.shoal.bearingDeg > 90.0 && r.shoal.bearingDeg < 104.0);  // starboard side
  CHECK(r.undepthed == 1 && r.haveUndepthed);
  CHECK_NEAR(r.nearestUndepthed.aheadNm, 1.0, tolNm);
  CHECK(!r.landInZone);
  CHECK(r.coarsestScale == 'E');
  CHECK(r.scaleTrace.find('G') != std::string::npos && r.scaleTrace.find('E') != std::string::npos);
  std::cout << "look-ahead corridor: least " << r.least.depth << " m (" << r.least.cls
            << "), shoal at " << r.shoal.aheadNm << " nm, scales " << r.scaleTrace << "\n";

  // the E cell's generalised 0-2 m area at u = 0.5 lies where G is charted
  bool sawZero = r.least.depth < 0.5;
  CHECK(!sawZero);

  // undepthed dangers counted as shoals: the wreck at 1.0 nm comes first
  lc.undepthedAsShoal = true;
  r = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, ec);
  CHECK(r.haveShoal && r.shoal.cls == "WRECKS" && r.shoal.undepthed);
  CHECK_NEAR(r.shoal.aheadNm, 1.0, tolNm);
  CHECK(r.shoal.bearingDeg < 90.0 && r.shoal.bearingDeg > 75.0);  // port side
  {
    std::string s = build_lookahead_sentence(r, "PCMLA", true);
    std::cout << "look-ahead sentence: " << s;
    CHECK(s.rfind("$PCMLA,1.0,1.92,090,S,5.0,1.00,10.0,", 0) == 0);
    CHECK(s.find(",W,1,1.00,C,2.00,0.25,E,A*") != std::string::npos);
    std::string body = s.substr(1, s.find('*') - 1);
    CHECK(s.substr(s.find('*') + 1, 2) == nmea_checksum(body));
    CHECK(s.size() <= 82);
  }

  // safety depth 1.5 m: nothing shallower except the undepthed wreck
  lc.safetyDepthM = 1.5;
  lc.undepthedAsShoal = false;
  r = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, ec);
  CHECK(r.haveShoal && r.shoal.cls == "SOUNDG");
  CHECK_NEAR(r.shoal.aheadNm, 1.92, tolNm);
  lc.safetyDepthM = 0.5;
  r = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, ec);
  CHECK(!r.haveShoal && r.status == "Clear");
  {
    std::string s = build_lookahead_sentence(r, "PCMLA", true);
    CHECK(s.find(",0.5,,,,,1,1.00,C,") != std::string::npos);
  }

  // longer zone reaches the land: shoal source L at 2.8 nm
  lc.safetyDepthM = 0.5;
  lc.distanceNm = 4.0;
  r = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, ec);
  CHECK(r.landInZone && r.haveShoal && r.shoal.land);
  CHECK_NEAR(r.shoal.aheadNm, 2.8, tolNm);
  CHECK(build_lookahead_sentence(r, "PCMLA", true).find(",L,") != std::string::npos);

  // course the other way: everything is astern. G's 20-30 m area ends
  // 1 nm west; beyond it the E cell's 8-20 m area applies
  lc.safetyDepthM = 5.0;
  lc.distanceNm = 2.0;
  r = eng.lookAhead(lat0, lon0, 270.0, 6.0, NAN, lc, ec);
  CHECK(r.valid && !r.haveShoal && r.undepthed == 0);
  CHECK(r.haveLeast && std::fabs(r.least.depth - 8.0) < 1e-6 && r.least.scaleChar == 'E');

  // time basis: 20 min at 6 kn = 2 nm; at 1 kn the minimum length applies
  lc.basis = LookBasis::Time;
  lc.minutes = 20;
  lc.minLengthNm = 0.5;
  r = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, ec);
  CHECK_NEAR(r.lengthNm, 2.0, 1e-9);
  r = eng.lookAhead(lat0, lon0, 90.0, 1.0, NAN, lc, ec);
  CHECK_NEAR(r.lengthNm, 0.5, 1e-9);
  lc.maxLengthNm = 1.5;
  r = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, ec);
  CHECK_NEAR(r.lengthNm, 1.5, 1e-9);
  lc.maxLengthNm = 20;

  // below the minimum SOG: heading used, no times; no heading -> invalid
  r = eng.lookAhead(lat0, lon0, 0.0, 0.2, 90.0, lc, ec);
  CHECK(r.valid && r.courseFromHeading && std::fabs(r.courseDeg - 90.0) < 1e-9);
  CHECK_NEAR(r.lengthNm, 0.5, 1e-9);
  CHECK(!r.haveShoal || std::isnan(r.shoal.timeMin));
  r = eng.lookAhead(lat0, lon0, 0.0, 0.2, NAN, lc, ec);
  CHECK(!r.valid && r.status.rfind("No course", 0) == 0);
  CHECK(build_lookahead_sentence(r, "PCMLA", true).find(",V*") != std::string::npos);

  // sector, 2 nm, +-30 deg: the 4 m sounding north of the corridor is now
  // inside (26.6 deg off the course) and is the first shoal at 0.894 nm
  lc.basis = LookBasis::Distance;
  lc.zone = LookZone::Sector;
  lc.halfAngleDeg = 30;
  lc.distanceNm = 2.0;
  r = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, ec);
  CHECK(r.valid && r.haveShoal && r.shoal.cls == "SOUNDG");
  CHECK_NEAR(r.shoal.depth, 4.0, 1e-6);
  CHECK_NEAR(r.shoal.aheadNm, std::hypot(0.8, 0.4), tolNm);
  CHECK_NEAR(r.shoal.bearingDeg, 90.0 - std::atan2(0.4, 0.8) / kDeg, 0.4);
  // narrower sector excludes it; the corner clipper's nearest point
  // (1.05, 0.2) is 10.8 deg off and is found at its range 1.069 nm
  lc.halfAngleDeg = 20;
  r = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, ec);
  CHECK(r.haveShoal && r.shoal.cls == "DEPARE");
  CHECK_NEAR(r.shoal.aheadNm, std::hypot(1.05, 0.2), tolNm);
  // a sector so narrow it passes between: 3-5 m square straight ahead at 1.4
  lc.halfAngleDeg = 2;
  r = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, ec);
  CHECK(r.haveShoal && r.shoal.depth == 3.0);
  CHECK_NEAR(r.shoal.aheadNm, 1.4, tolNm);
  {
    std::string s = build_lookahead_sentence(r, "PCMLA", true);
    CHECK(s.find(",S,2.00,2.0,") != std::string::npos);
  }
  // outline is closed and starts at the vessel side
  CHECK(r.outline.size() >= 4);

  // ---- land: interpolation and position checks
  {
    EngineConfig ic;
    ic.mode = SearchMode::Interpolated;
    ic.radiusNm = 2.0;
    // west of the island: the 3.0 m sounding east of it is hidden
    DepthResult ri = eng.compute(lat0, LO(2.5), ic);
    CHECK(ri.valid && ri.haveLeast && !ri.onLand);
    CHECK(ri.landBlocked >= 1);
    bool used3 = false;
    for (auto &sd : ri.soundings) used3 = used3 || std::fabs(sd.depth - 3.0) < 1e-6;
    CHECK(!used3);
    // with the check off it is the nearest after the E 1.0 m one
    ic.landCheck = false;
    ri = eng.compute(lat0, LO(2.5), ic);
    used3 = false;
    for (auto &sd : ri.soundings) used3 = used3 || std::fabs(sd.depth - 3.0) < 1e-6;
    CHECK(used3 && ri.landBlocked == 0);
    ic.landCheck = true;
    // on the island: no depth in any mode, sentence status V
    ri = eng.compute(lat0, LO(3.0), ic);
    CHECK(ri.valid && ri.onLand && !ri.haveLeast);
    CHECK(ri.status.rfind("Position is on charted land", 0) == 0);
    CHECK(build_least_depth_sentence(ri, "PCMLD", SearchMode::Interpolated, 2.0, true)
              .find(",V*") != std::string::npos);
    EngineConfig hc;
    hc.mode = SearchMode::DepthAreaHere;
    CHECK(eng.compute(lat0, LO(3.0), hc).onLand);
    std::cout << "land: blocked " << eng.compute(lat0, LO(2.5), ic).landBlocked
              << ", on land: " << ri.status << "\n";
  }

  // worst-case sentence length
  {
    LookAheadResult w;
    w.valid = true;
    w.haveLeast = w.haveShoal = w.haveUndepthed = true;
    w.least.depth = 9999.0; w.least.rangeNm = 49.99; w.least.bearingDeg = 359; w.least.cls = "SOUNDG";
    w.safetyDepthM = 99.9;
    w.shoal.aheadNm = 49.99; w.shoal.rangeNm = 49.99; w.shoal.timeMin = 9999; w.shoal.bearingDeg = 359;
    w.shoal.cls = "LNDARE";
    w.undepthed = 250; w.nearestUndepthed.aheadNm = 49.99;
    w.zone = LookZone::Sector; w.lengthNm = 50; w.halfAngleDeg = 90; w.coarsestScale = 'Z';
    std::string s = build_lookahead_sentence(w, "PCMLA", true);
    std::cout << "worst-case look-ahead (" << s.size() << " incl CR LF): " << s;
    CHECK(s.size() <= 82);
    w.least.depth = -12.3; w.zone = LookZone::Corridor; w.halfWidthNm = 5.0;
    CHECK(build_lookahead_sentence(w, "PCMLA", true).size() <= 82);
  }
  fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// Grounding alarm: state machine, evaluation, $IIALR
// ---------------------------------------------------------------------------
static void test_alarm() {
  using S = AlarmLogic::State;
  AlarmLogic a;
  const int64_t rep = 5000;
  CHECK(!a.update(0, AlarmKind::None, NAN, rep) && a.state() == S::Clear);
  CHECK(a.update(1000, AlarmKind::ShoalAhead, 6.0, rep) && a.state() == S::Active);  // sounds
  CHECK(!a.update(3000, AlarmKind::ShoalAhead, 5.8, rep));   // not yet due
  CHECK(a.update(6000, AlarmKind::ShoalAhead, 5.5, rep));    // repeat
  a.silence(6500, 30000);
  CHECK(a.state() == S::Silenced);
  CHECK(!a.update(20000, AlarmKind::ShoalAhead, 5.0, rep));  // muted
  CHECK(a.update(36500, AlarmKind::ShoalAhead, 4.5, rep) && a.state() == S::Active);  // back
  a.acknowledge();
  CHECK(a.state() == S::Acknowledged);
  CHECK(!a.update(40000, AlarmKind::ShoalAhead, 3.0, rep));  // quiet
  CHECK(a.update(45000, AlarmKind::ShoalAhead, 2.2, rep) && a.state() == S::Active);  // halved
  a.acknowledge();
  CHECK(a.update(46000, AlarmKind::DepthHere, 0, rep) && a.state() == S::Active);  // worse kind
  CHECK(!a.update(47000, AlarmKind::None, NAN, rep) && a.state() == S::Clear);  // clears
  CHECK(a.update(48000, AlarmKind::ShoalAhead, 5.0, rep));  // new alarm sounds at once

  // evaluation
  DepthResult here;
  here.valid = true;
  here.haveLeast = true;
  here.leastDepth = 12.0;
  LookAheadResult la;
  la.valid = true;
  la.haveShoal = true;
  la.shoal.aheadNm = 1.0;
  la.shoal.timeMin = 10.0;
  la.shoal.depth = 2.0;
  la.shoal.bearingDeg = 88;
  AlarmConfig c;  // 6 min / 0.2 nm / safety 5 m
  double sev;
  std::string txt;
  CHECK(evaluate_alarm(here, la, c, true, sev, txt) == AlarmKind::None);  // 10 min away
  la.shoal.timeMin = 5.0;
  CHECK(evaluate_alarm(here, la, c, true, sev, txt) == AlarmKind::ShoalAhead);
  CHECK(txt.rfind("SHOAL AHEAD: 1.00 nm, 5.0 min, 088 T", 0) == 0 && sev == 5.0);
  CHECK(evaluate_alarm(here, la, c, false, sev, txt) == AlarmKind::None);  // no fix
  la.shoal.timeMin = NAN;  // stopped / heading only: distance rule
  CHECK(evaluate_alarm(here, la, c, true, sev, txt) == AlarmKind::None);
  la.shoal.aheadNm = 0.15;
  CHECK(evaluate_alarm(here, la, c, true, sev, txt) == AlarmKind::ShoalAhead);
  here.leastDepth = 3.0;  // depth here below safety depth wins
  CHECK(evaluate_alarm(here, la, c, true, sev, txt) == AlarmKind::DepthHere);
  c.depthIsHere = false;  // cell / radius mode: not the depth at the vessel
  CHECK(evaluate_alarm(here, la, c, true, sev, txt) == AlarmKind::ShoalAhead);
  here.onLand = true;
  CHECK(evaluate_alarm(here, la, c, true, sev, txt) == AlarmKind::DepthHere && txt == "ON CHARTED LAND");

  std::string alr = build_alr_sentence(3600 * 13 + 54 * 60 + 7.25, 1, true, false,
                                       "SHOAL AHEAD: 1.00 nm, 5.0 min, 088 T");
  std::cout << "ALR: " << alr;
  CHECK(alr.rfind("$IIALR,135407.25,001,A,V,SHOAL AHEAD: 1.00 nm  5.0 min  088 T*", 0) == 0);
  CHECK(alr.size() <= 82);
  std::string longText(200, 'X');
  CHECK(build_alr_sentence(0, 1, false, true, longText).size() == 82);
}


// ---------------------------------------------------------------------------
// S-57 ENC cells: ISO 8211 + S-57 decoding, updates, the index, and the
// engine with ENC alone and together with CM93 ("most detailed wins").
// ---------------------------------------------------------------------------
enum { O_DEPARE = 42, O_DEPCNT = 43, O_LNDARE = 71, O_OBSTRN = 86, O_SOUNDG = 129,
       O_UWTROC = 153, O_WRECKS = 159, O_M_COVR = 302 };
enum { T_CATCOV = 18, T_DRVAL1 = 87, T_DRVAL2 = 88, T_VALDCO = 174, T_VALSOU = 179,
       T_WATLEV = 187 };

static void write_bytes(const fs::path &p, const std::vector<uint8_t> &d) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write((const char *)d.data(), d.size());
}

// Harbour cell round (lat0, lon0):
//  outer DEPARE 10-20 m over the whole extent, with holes for: the inner
//  3-6 m DEPARE round the ship (+-0.01 deg), an island (LNDARE) ~0.88 nm
//  east, and an uncharted gap (a pontoon, say) ~1.2 nm north-west
//  an undepthed obstruction area 0.2 nm north, a 1.5 m rock 1.5 nm SW,
//  a 2 m wreck far SW, soundings 4.2 / 5.5 m close by and 12 m away
struct HarbourIds { uint32_t inner, rock, wreck, wreckNode; };
static HarbourIds harbour_cell(s57w::Cell &c, double la, double lo) {
  using s57w::Cell;
  HarbourIds ids{};
  const std::string sp = "";
  c.area(O_M_COVR, {{T_CATCOV, "1"}}, {Cell::rect(la - 0.05, lo - 0.05, la + 0.05, lo + 0.05)});
  c.area(O_DEPARE, {{T_DRVAL1, "10"}, {T_DRVAL2, "20"}},
         {Cell::rect(la - 0.05, lo - 0.05, la + 0.05, lo + 0.05),
          Cell::rect(la - 0.01, lo - 0.01, la + 0.01, lo + 0.01),      // inner area
          Cell::rect(la - 0.005, lo + 0.02, la + 0.005, lo + 0.03),    // island
          Cell::rect(la + 0.02, lo - 0.01, la + 0.025, lo - 0.005)});  // gap
  ids.inner = c.rectTwoEdges(O_DEPARE, {{T_DRVAL1, "3"}, {T_DRVAL2, "6"}}, la - 0.01, lo - 0.01,
                             la + 0.01, lo + 0.01);
  c.area(O_LNDARE, {}, {Cell::rect(la - 0.005, lo + 0.02, la + 0.005, lo + 0.03)});
  c.area(O_OBSTRN, {}, {Cell::rect(la + 0.0030, lo - 0.001, la + 0.0040, lo + 0.001)});
  ids.rock = c.point(O_UWTROC, {{T_VALSOU, "1.5"}, {T_WATLEV, "3"}}, la - 0.02, lo - 0.02);
  ids.wreckNode = c.isolated(la - 0.03, lo - 0.04);
  ids.wreck = c.feature(1, O_WRECKS, {{T_VALSOU, "2"}}, {{110, ids.wreckNode, 255}});
  c.sounding({{la + 0.002, lo, 4.2}, {la - 0.003, lo + 0.002, 5.5}});
  c.sounding({{la - 0.02, lo - 0.03, 12.0}});
  c.line(O_DEPCNT, {{T_VALDCO, "5"}}, {{la - 0.01, lo - 0.01}, {la - 0.01, lo + 0.01}});
  return ids;
}

static void test_enc(const fs::path &cm93root, double shipLat, double shipLon, double swLon,
                     double sz) {
  // ---- format expansion and catalogue
  {
    std::vector<std::pair<char, int>> f;
    CHECK(iso8211::expand_format("(b11,b14,2b11,3A,2A(8),R(4),b11,2A,b11,b12,A)", f));
    CHECK(f.size() == 16 && f[0] == std::make_pair('u', 1) && f[1] == std::make_pair('u', 4) &&
          f[4] == std::make_pair('A', 0) && f[7] == std::make_pair('A', 8) &&
          f[9] == std::make_pair('R', 4) && f[14] == std::make_pair('u', 2));
    CHECK(iso8211::expand_format("(B(40),4b11)", f) && f.size() == 5 && f[0].first == 'B' &&
          f[0].second == 5);
    CHECK(iso8211::expand_format("(2b24)", f) && f.size() == 2 && f[1] == std::make_pair('s', 4));
    CHECK(iso8211::expand_format("(A,2(I(2),R))", f) && f.size() == 5 && f[3].first == 'I');
    CHECK(s57_object_code("DEPARE") == 42 && s57_object_code("SOUNDG") == 129);
    CHECK(s57_attribute_code("DRVAL1") == 87 && s57_attribute(179)->type == 'F');
    CHECK(std::string(s57_object(302)->acronym) == "M_COVR");
    CHECK(enc_band(5, 12000) == 5 && enc_band(0, 12000) == 5 && enc_band(0, 2000000) == 1 &&
          enc_band(0, 50000) == 4 && enc_band(9, 3000) == 6);
    CHECK(slot_from_char('5') > slot_from_char('F') && slot_from_char('5') < slot_from_char('G'));
    CHECK(slot_from_char('3') > slot_from_char('C') && slot_from_char('3') < slot_from_char('D'));
    CHECK(slot_from_char('6') == kNumSlots - 1 && slot_from_char('Z') == 0);
  }

  const double la = shipLat, lo = shipLon;
  fs::path dir = fs::temp_directory_path() / "cm93depth_test_enc";
  fs::remove_all(dir);
  fs::create_directories(dir / "ENC_ROOT" / "ZZ5HARB1");
  const fs::path base = dir / "ENC_ROOT" / "ZZ5HARB1" / "ZZ5HARB1.000";
  s57w::Cell hc;
  hc.dsnm = "ZZ5HARB1.000";
  hc.intu = 5;
  hc.cscl = 12000;
  HarbourIds ids = harbour_cell(hc, la, lo);
  write_bytes(base, hc.build());

  // ---- reader
  {
    CellData cell;
    EncHeader h;
    std::string err;
    DecodeOptions opt;
    CHECK(load_enc(base.u8string(), opt, cell, &h, &err));
    CHECK(h.intu == 5 && h.cscl == 12000 && h.comf == 1e7 && h.somf == 10);
    CHECK(cell.ch == '5' && cell.name == "ZZ5HARB1");
    CHECK_NEAR(h.minLat, la - 0.05, 1e-6);
    CHECK_NEAR(h.maxLon, lo + 0.05, 1e-6);
    int nDep = 0, nSnd = 0, nPts = 0;
    const Feature *inner = nullptr, *outer = nullptr, *cnt = nullptr;
    for (auto &f : cell.features) {
      if (f.cls == "DEPARE") {
        nDep++;
        double v;
        if (f.getNum("DRVAL1", v) && v == 3) inner = &f;
        if (f.getNum("DRVAL1", v) && v == 10) outer = &f;
      }
      if (f.cls == "SOUNDG") {
        nSnd++;
        nPts += (int)f.points.size();
        CHECK(f.geom == Geom::MultiPoint3D && f.z.size() == f.points.size());
      }
      if (f.cls == "DEPCNT") cnt = &f;
    }
    CHECK(nDep == 2 && nSnd == 2 && nPts == 3);
    // two edges, the second reversed: one closed ring of 4 corners
    CHECK(inner && inner->parts.size() == 1 && inner->parts[0].size() == 4);
    CHECK(inner && geo::point_in_rings(inner->parts, la, lo));
    CHECK(outer && outer->parts.size() == 4);
    CHECK(outer && !geo::point_in_rings(outer->parts, la, lo));         // in a hole
    CHECK(outer && geo::point_in_rings(outer->parts, la - 0.03, lo));  // outer water
    CHECK(cnt && cnt->geom == Geom::Line && cnt->parts.size() == 1 && cnt->parts[0].size() == 2);
    bool saw42 = false;
    for (auto &f : cell.features)
      for (double z : f.z) saw42 = saw42 || std::fabs(z - 4.2) < 1e-9;
    CHECK(saw42);
    // only the classes asked for
    DecodeOptions few;
    few.keepClasses = {"SOUNDG"};
    CellData c2;
    CHECK(load_enc(base.u8string(), few, c2) && c2.features.size() == 2);
    // header only
    EncHeader h2;
    CHECK(read_enc_header(base.u8string(), h2) && h2.dsnm == "ZZ5HARB1.000" &&
          std::fabs(h2.minLon - (lo - 0.05)) < 1e-6);
    // not an S-57 file (an encrypted S-63 cell reads like this)
    fs::path junk = dir / "JUNK.000";
    write_bytes(junk, std::vector<uint8_t>(200, 0x5a));
    CHECK(!read_enc_header(junk.u8string(), h2, &err));
  }

  // ---- damaged files must not crash the reader (truncated, bytes changed)
  {
    std::vector<uint8_t> good = hc.build();
    int decoded = 0;
    for (size_t n = 0; n < good.size(); n += 7) {
      std::vector<uint8_t> t(good.begin(), good.begin() + n);
      std::vector<Feature> out;
      EncHeader h;
      if (decode_enc_buffers(t, {}, DecodeOptions(), out, &h)) decoded++;
      read_enc_header_buffer(t, h);
    }
    uint32_t seed = 12345;
    for (int k = 0; k < 300; k++) {
      std::vector<uint8_t> t = good;
      for (int j = 0; j < 8; j++) {
        seed = seed * 1103515245u + 12345u;
        t[(seed >> 8) % t.size()] = (uint8_t)(seed >> 3);
      }
      std::vector<Feature> out;
      EncHeader h;
      decode_enc_buffers(t, {t}, DecodeOptions(), out, &h);
    }
    CHECK(decoded > 0);  // got here without crashing
  }

  // ---- index (with a cache file)
  const std::string cache = (dir / "cache" / "enc_index.txt").u8string();
  {
    EncIndex ix;
    ix.configure({dir.u8string()}, cache);
    CHECK(!ix.complete());
    ix.runToCompletion();
    CHECK(ix.complete() && ix.entries().size() == 1 && ix.unusable() == 1);
    CHECK(ix.entries().size() == 1 && ix.entries()[0].band == 5 &&
          ix.entries()[0].dsnm == "ZZ5HARB1.000");
    CHECK(fs::exists(fs::u8path(cache)));
    EncIndex ix2;  // from the cache
    ix2.configure({dir.u8string()}, cache);
    ix2.runToCompletion();
    CHECK(ix2.entries().size() == 1 && std::fabs(ix2.entries()[0].maxLat - (la + 0.05)) < 1e-6);
    std::cout << ix2.summary() << "\n";
    // an exchange set's catalogue: the cell is indexed without reading it
    write_bytes(dir / "ENC_ROOT" / "CATALOG.031",
                s57w::catalog({{"ZZ5HARB1\\ZZ5HARB1.000", la - 0.05, lo - 0.05, la + 0.05,
                                lo + 0.05},
                               {"ZZ5HARB1\\ZZ5HARB1.TXT", 0, 0, 1, 1}}));
    EncIndex ix3;
    ix3.configure({dir.u8string()}, "");
    ix3.runToCompletion();
    CHECK(ix3.entries().size() == 1 && ix3.entries()[0].band == 5 &&
          ix3.entries()[0].cscl == 0 /* not read */ &&
          std::fabs(ix3.entries()[0].minLon - (lo - 0.05)) < 1e-6);
    fs::remove(dir / "ENC_ROOT" / "CATALOG.031");
  }

  // ---- engine with ENC only
  DepthEngine eng;
  eng.encIndex().configure({dir.u8string()}, "");
  eng.encIndex().runToCompletion();
  CHECK(!eng.cm93Ready() && eng.encReady() && eng.ready());
  {
    EngineConfig h;
    h.mode = SearchMode::DepthAreaHere;
    DepthResult r = eng.compute(la, lo, h);
    CHECK(r.valid && r.haveLeast && r.scaleChar == '5' && r.cellName == "ZZ5HARB1");
    CHECK_NEAR(r.leastDepth, 3.0, 1e-9);
    CHECK(r.atPosScaleChar == '5' && std::fabs(r.atPosMax - 6.0) < 1e-9);
    CHECK(r.unknownHazards == 1);  // the obstruction inside the area
    // on the obstruction: no deeper than 0 m (rule 0) / the area's 3 m (rule 1)
    r = eng.compute(la + 0.0035, lo, h);
    CHECK(r.dangerHere && r.dangerHereClass == "OBSTRN" && r.dangerHereAssumed);
    CHECK_NEAR(r.leastDepth, 0.0, 1e-9);
    h.undepthedDangerRule = 1;
    r = eng.compute(la + 0.0035, lo, h);
    CHECK_NEAR(r.leastDepth, 3.0, 1e-9);
    h.undepthedDangerRule = 0;
    // on the island
    r = eng.compute(la, lo + 0.025, h);
    CHECK(r.valid && r.onLand && r.scaleChar == '5');
    // in the gap (uncharted hole between depth areas)
    r = eng.compute(la + 0.0225, lo - 0.0075, h);
    CHECK(r.valid && r.leastClass == "HOLE" && r.scaleChar == '5');
    CHECK_NEAR(r.leastDepth, 0.0, 1e-9);
    // outside the cell
    r = eng.compute(la + 0.2, lo, h);
    CHECK(!r.valid);
    // ENC switched off
    h.useEnc = false;
    r = eng.compute(la, lo, h);
    CHECK(!r.valid && r.status == "All chart sources switched off");
    h.useEnc = true;

    // whole cell: the 1.5 m rock is the least
    EngineConfig c;
    c.mode = SearchMode::Cell;
    r = eng.compute(la, lo, c);
    CHECK(r.haveLeast && r.leastClass == "UWTROC" && r.scaleChar == '5');
    CHECK_NEAR(r.leastDepth, 1.5, 1e-9);

    // interpolated
    EngineConfig in;
    in.mode = SearchMode::Interpolated;
    in.radiusNm = 2.0;
    r = eng.compute(la, lo, in);
    CHECK(r.valid && r.haveLeast && r.scaleChar == '5' && r.cellName == "ZZ5HARB1");
    CHECK(!r.soundings.empty() && r.soundings[0].scaleChar == '5');
    CHECK_NEAR(r.soundings[0].depth, 4.2, 1e-9);
    CHECK(r.leastDepth >= 3.0 - 1e-9 && r.leastDepth <= 6.0 + 1e-9);

    // look-ahead east: the island 0.88 nm ahead
    LookAheadConfig lc;
    lc.basis = LookBasis::Distance;
    lc.distanceNm = 2.0;
    lc.halfWidthNm = 0.05;
    lc.safetyDepthM = 2.5;
    LookAheadResult a = eng.lookAhead(la, lo, 90.0, 6.0, NAN, lc, c);
    const double toIsland = 0.01 * 60.0 * std::cos(la * kDeg) * 2.0;
    CHECK(a.valid && a.haveShoal && a.shoal.land && a.shoal.scaleChar == '5');
    CHECK_NEAR(a.shoal.aheadNm, toIsland, 0.01);
    CHECK(a.coarsestScale == '5');
    // look-ahead north from inside the inner area, past the gap
    LookAheadResult g = eng.lookAhead(la, lo - 0.0075, 0.0, 6.0, NAN, lc, c);
    CHECK(g.valid && g.haveUndepthed && g.nearestUndepthed.cls == "HOLE");
    CHECK_NEAR(g.nearestUndepthed.aheadNm, 0.02 * 60.0, 0.01);
    std::cout << "ENC look-ahead: shoal " << a.shoal.cls << " at " << a.shoal.aheadNm
              << " nm, gap at " << g.nearestUndepthed.aheadNm << " nm, " << a.scaleTrace << "\n";

    std::string diag = eng.diagnostics(la, lo, c);
    CHECK(diag.find("[5] ENC ZZ5HARB1.000, Harbour band, 1:12000") != std::string::npos);
    CHECK(diag.find("CONTAINS position") != std::string::npos);
  }

  // ---- updates: .001 inner area 3 -> 2.5 m; .002 deletes the rock;
  //      .003 moves the wreck next to the ship; .005 (out of sequence) would
  //      delete the inner area and must not be applied
  {
    s57w::Cell u1;
    u1.dsnm = "ZZ5HARB1.001";
    u1.updn = "1";
    u1.modifyAttrs(ids.inner, 3, O_DEPARE, 2, {{T_DRVAL1, "2.5"}});
    write_bytes(base.parent_path() / "ZZ5HARB1.001", u1.build());
    s57w::Cell u2;
    u2.dsnm = "ZZ5HARB1.002";
    u2.updn = "2";
    u2.deleteFeature(ids.rock, 1, O_UWTROC, 2);
    write_bytes(base.parent_path() / "ZZ5HARB1.002", u2.build());
    s57w::Cell u3;
    u3.dsnm = "ZZ5HARB1.003";
    u3.updn = "3";
    u3.modifyCoords(110, ids.wreckNode, 2, 3, 1, {{la - 0.001, lo + 0.001}});
    write_bytes(base.parent_path() / "zz5harb1.003", u3.build());  // any case
    s57w::Cell u5;
    u5.dsnm = "ZZ5HARB1.005";
    u5.updn = "5";
    u5.deleteFeature(ids.inner, 3, O_DEPARE, 3);
    write_bytes(base.parent_path() / "ZZ5HARB1.005", u5.build());

    CHECK(find_enc_updates(base.u8string()).size() == 4);
    CellData cell;
    EncHeader h;
    CHECK(load_enc(base.u8string(), DecodeOptions(), cell, &h));
    CHECK(h.updatesApplied == 3 && h.updn == 3 && !h.updateError.empty());

    eng.clearCache();
    EngineConfig hh;
    hh.mode = SearchMode::DepthAreaHere;
    DepthResult r = eng.compute(la + 0.002, lo - 0.002, hh);
    CHECK(r.haveLeast && std::fabs(r.leastDepth - 2.5) < 1e-9);
    EngineConfig c;
    c.mode = SearchMode::Cell;
    r = eng.compute(la, lo, c);
    CHECK(r.haveLeast && r.leastClass == "WRECKS");  // rock gone; wreck 2 m is now least
    CHECK_NEAR(r.leastDepth, 2.0, 1e-9);
    // on the moved wreck
    r = eng.compute(la - 0.001, lo + 0.001, hh);
    CHECK(r.dangerHere && r.dangerHereClass == "WRECKS" && std::fabs(r.leastDepth - 2.0) < 1e-9);
    std::string diag = eng.diagnostics(la, lo, c);
    CHECK(diag.find("updates applied: 3") != std::string::npos);
  }

  // ---- together with CM93: the most detailed band wins
  {
    DepthEngine both;
    CHECK(both.setRoot(cm93root.u8string()));
    EngineConfig h;
    h.mode = SearchMode::DepthAreaHere;
    DepthResult r = both.compute(la, lo, h);
    CHECK(r.scaleChar == 'G' && std::fabs(r.leastDepth - 5.0) < 1e-9);  // CM93 alone

    // harbour ENC (band 5) is less detailed than CM93 G: G still wins
    both.encIndex().configure({dir.u8string()}, "");
    both.encIndex().runToCompletion();
    r = both.compute(la, lo, h);
    CHECK(r.scaleChar == 'G' && std::fabs(r.leastDepth - 5.0) < 1e-9);
    // where G's coverage stops, the harbour ENC beats CM93 E
    const double eastLon = swLon + 0.9 * sz;
    fs::path d2 = dir / "ENC_ROOT" / "ZZ4APPR1";
    fs::create_directories(d2);
    s57w::Cell ap;  // approach band 4 cell round the east point, 12-15 m
    ap.dsnm = "ZZ4APPR1.000";
    ap.intu = 4;
    ap.cscl = 45000;
    ap.area(O_M_COVR, {{T_CATCOV, "1"}},
            {s57w::Cell::rect(la - 0.05, eastLon - 0.02, la + 0.05, eastLon + 0.02)});
    ap.area(O_DEPARE, {{T_DRVAL1, "12"}, {T_DRVAL2, "15"}},
            {s57w::Cell::rect(la - 0.05, eastLon - 0.02, la + 0.05, eastLon + 0.02)});
    write_bytes(d2 / "ZZ4APPR1.000", ap.build());
    both.encIndex().rescan();
    both.encIndex().runToCompletion();
    CHECK(both.encIndex().entries().size() == 2);
    r = both.compute(la, eastLon, h);
    CHECK(r.scaleChar == '4' && std::fabs(r.leastDepth - 12.0) < 1e-9);
    // ENC off: CM93 E's 8 m as before
    EngineConfig noEnc = h;
    noEnc.useEnc = false;
    r = both.compute(la, eastLon, noEnc);
    CHECK(r.scaleChar == 'E' && std::fabs(r.leastDepth - 8.0) < 1e-9);
    // a fixed CM93 scale uses CM93 only
    EngineConfig fixedE = h;
    fixedE.scaleIdx = 5;
    r = both.compute(la, eastLon, fixedE);
    CHECK(r.scaleChar == 'E');
    // berthing ENC (band 6) at the ship beats CM93 G
    fs::path d3 = dir / "ENC_ROOT" / "ZZ6BERT1";
    fs::create_directories(d3);
    s57w::Cell be;
    be.dsnm = "ZZ6BERT1.000";
    be.intu = 6;
    be.cscl = 2500;
    be.area(O_M_COVR, {{T_CATCOV, "1"}}, {s57w::Cell::rect(la - 0.004, lo - 0.004, la + 0.004, lo + 0.004)});
    be.area(O_DEPARE, {{T_DRVAL1, "1"}, {T_DRVAL2, "2"}},
            {s57w::Cell::rect(la - 0.004, lo - 0.004, la + 0.004, lo + 0.004)});
    write_bytes(d3 / "ZZ6BERT1.000", be.build());
    both.encIndex().rescan();
    both.encIndex().runToCompletion();
    r = both.compute(la, lo, h);
    CHECK(r.scaleChar == '6' && r.cellName == "ZZ6BERT1" && std::fabs(r.leastDepth - 1.0) < 1e-9);
    CHECK(r.scaleTrace.rfind("6+ G.", 0) == 0);
    // NMEA: the ENC band digit in field 14
    std::string s = build_least_depth_sentence(r, "PCMLD", SearchMode::DepthAreaHere, 1.0, true);
    CHECK(s.find(",H,,6,A*") != std::string::npos);
    std::cout << "ENC + CM93: " << r.scaleTrace << " -> " << s;
  }
  fs::remove_all(dir);
}

int main() {
  // ---- cell index arithmetic -------------------------------------------
  {
    int ci = cell_index(-43.0 + 1e-9, 147.5, 7);
    CHECK(ci == 1410442);
    double la, lo;
    cell_origin(ci, la, lo);
    CHECK_NEAR(la, -43.0, 1e-9);
    CHECK_NEAR(lo, 147.3333333, 1e-6);
    CellPathParts pp = cell_path_parts(ci, 7);
    CHECK(pp.rootDir == "00900420");
    CHECK(pp.baseName == "1410442");
    // western hemisphere: lon -70.1 -> (289.9*3) = 869.7 -> 869
    int ci2 = cell_index(41.5, -70.1, 7);
    CHECK(ci2 % 10000 == 869);
    CHECK(scale_index_from_char('g') == 7 && scale_index_from_char('Z') == 0);
    CHECK(scale_index_from_char('Q') == -1);
    // D scale (dval 3) normalises to multiples of 3
    int cid = cell_index(-43.1, 147.5, 4);
    CHECK((cid % 10000) % 3 == 0);
  }

  // ---- encode/decode tables are inverse ---------------------------------
  {
    bool inv = true;
    for (int i = 0; i < 256; i++)
      if (decode_byte(encode_byte((uint8_t)i)) != i) inv = false;
    CHECK(inv);
  }

  // ---- NMEA --------------------------------------------------------------
  {
    CHECK(nmea_checksum("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,") == "47");
    CHECK(valid_proprietary_address("PCMLD"));
    CHECK(!valid_proprietary_address("GPDPT"));
    CHECK(!valid_proprietary_address("PCML"));
    CHECK(!valid_proprietary_address("Pcmld"));

    DepthResult r;
    r.valid = true; r.haveLeast = true;
    r.leastDepth = 2.34; r.leastPos = {-42.79, 147.512};
    r.rangeNm = 0.567; r.bearingDeg = 359.7; r.leastClass = "SOUNDG";
    r.haveAtPos = true; r.atPosMin = 5; r.atPosMax = 10; r.unknownHazards = 1;
    r.scaleChar = 'G';
    std::string s = build_least_depth_sentence(r, "PCMLD", SearchMode::Radius, 1.0, true);
    std::string body = s.substr(1, s.find('*') - 1);
    CHECK(s.rfind("$PCMLD,2.3,4247.400,S,14730.720,E,0.57,000,5.0,10.0,S,1,R,1.00,G,A*", 0) == 0);
    CHECK(s.substr(s.find('*') + 1, 2) == nmea_checksum(body));
    CHECK(s.size() >= 2 && s.substr(s.size() - 2) == "\r\n");

    // worst-case length must fit NMEA 0183's 82 characters
    r.leastDepth = -1234.5; r.leastPos = {-89.99999, -179.99999}; r.rangeNm = 999.99;
    r.atPosMin = -1234.5; r.atPosMax = 1234.5; r.unknownHazards = 99;
    s = build_least_depth_sentence(r, "PCMLD", SearchMode::Radius, 99.99, true);
    CHECK(s.size() <= 82);
    std::cout << "worst case sentence (" << s.size() << " chars): " << s;

    // invalid result
    DepthResult bad;
    s = build_least_depth_sentence(bad, "PCMLD", SearchMode::Cell, 1.0, false);
    CHECK(s.rfind("$PCMLD,,,,,,,,,,,,C,,,V*", 0) == 0);
    // minutes rounding must not produce 60.000
    r.leastPos = {-42.9999999, 147.9999999};
    r.leastDepth = 1; r.rangeNm = 1; r.atPosMin = 1; r.atPosMax = 2; r.unknownHazards = 0;
    s = build_least_depth_sentence(r, "PCMLD", SearchMode::Cell, 1.0, true);
    CHECK(s.find("4300.000,S,14800.000,E") != std::string::npos);
  }

  // ---- geometry ----------------------------------------------------------
  {
    std::vector<std::vector<LatLon>> sq = {{{-1, -1}, {-1, 1}, {1, 1}, {1, -1}, {-1, -1}}};
    CHECK(geo::point_in_rings(sq, 0, 0));
    CHECK(!geo::point_in_rings(sq, 0, 2));
    // ring with a hole
    auto holed = sq;
    holed.push_back({{-0.5, -0.5}, {-0.5, 0.5}, {0.5, 0.5}, {0.5, -0.5}, {-0.5, -0.5}});
    CHECK(!geo::point_in_rings(holed, 0, 0));
    CHECK(geo::point_in_rings(holed, 0.75, 0));
    // antimeridian-spanning square
    std::vector<std::vector<LatLon>> am = {{{-1, 179}, {-1, -179}, {1, -179}, {1, 179}, {-1, 179}}};
    CHECK(geo::point_in_rings(am, 0, 179.9));
    CHECK(geo::point_in_rings(am, 0, -179.9));
    CHECK(!geo::point_in_rings(am, 0, 178.5));
    LatLon n;
    double d = geo::nearest_on_parts(sq, 0, 2, n);
    CHECK_NEAR(d, 60.0, 0.05);  // 1 degree of longitude at the equator
    CHECK_NEAR(n.lat, 0, 1e-9);
    CHECK_NEAR(n.lon, 1, 1e-9);
  }

  // ---- synthetic CM93 tree ----------------------------------------------
  fs::path root = fs::temp_directory_path() / "cm93depth_test_tree";
  fs::remove_all(root);
  fs::create_directories(root);
  write_dictionary(root);

  const double shipLat = -42.80, shipLon = 147.50;
  const int G = 7, E = 5;
  int ciG = cell_index(shipLat, shipLon, G);
  double swLat, swLon;
  cell_origin(ciG, swLat, swLon);
  const double sz = cell_size_deg(G);

  {
    Writer w(swLat, swLon, sz);
    // coverage only for the western 3/4 of the cell
    w.addRectArea(C_MSOR, swLat, swLon, swLat + sz, swLon + 0.75 * sz, {});
    // depth area around the ship: 5..10 m
    w.addSquareArea(C_DEPARE, shipLat, shipLon, 0.02,
                    {Writer::attrR(A_DRVAL1, 5.0f), Writer::attrW(A_DRVAL2, 10.0)});
    // shallower area ~1.9 nm east: 2..5 m
    w.addSquareArea(C_DEPARE, shipLat, shipLon + 0.06, 0.01,
                    {Writer::attrR(A_DRVAL1, 2.0f), Writer::attrW(A_DRVAL2, 5.0)});
    // soundings: 7.3 m close by, 1.2 m ~3.7 nm NW, 30 m via the 12000 code
    w.addSoundings(C_SOUNDG, {{{shipLat + 0.01, shipLon, 7.3}},
                              {{shipLat + 0.05, shipLon - 0.05, 1.2}}});
    w.addRawSounding(C_SOUNDG, shipLat - 0.01, shipLon + 0.01, 12030);
    // obstruction with depth 0.8 m about 5 nm south
    w.addPoint(C_OBSTRN, shipLat - 0.083, shipLon,
               {Writer::attrW(A_VALSOU, 0.8), Writer::attrS(A_OBJNAM, "Rock X")});
    // charted 4 m obstruction inside the 5-10 m area, ~1.08 nm north
    w.addPoint(C_OBSTRN, shipLat + 0.018, shipLon, {Writer::attrW(A_VALSOU, 4.0)});
    // obstruction with no depth 0.3 nm north-east
    w.addPoint(C_OBSTRN, shipLat + 0.004, shipLon + 0.004, {Writer::attrS(A_OBJNAM, "Unknown")});
    // 3 m contour 0.5 nm south of the ship
    w.addLine(C_DEPCNT, {{shipLat - 0.00833, shipLon - 0.03}, {shipLat - 0.00833, shipLon + 0.03}},
              {Writer::attrR(A_VALDCO, 3.0f)});
    // intertidal area (CM93 ITDARE -> DEPARE), drying 1.5 m, far corner
    w.addSquareArea(C_ITDARE, swLat + 0.02, swLon + 0.02, 0.005,
                    {Writer::attrR(A_DRVAL1, -1.5f), Writer::attrW(A_DRVAL2, 0.0)});
    write_cell(root, ciG, G, '0', w);

    // subcell 'A': 2.5 m sounding 0.2 nm east (lower-case extension on disk)
    Writer wa(swLat, swLon, sz);
    wa.addSoundings(C_SOUNDG, {{{shipLat, shipLon + 0.0045, 2.5}}});
    write_cell(root, ciG, G, 'A', wa, true);
  }
  {
    // E-scale cell covering the whole cell area, one 8 m depth area
    int ciE = cell_index(shipLat, shipLon, E);
    double la, lo;
    cell_origin(ciE, la, lo);
    Writer w(la, lo, cell_size_deg(E));
    w.addRectArea(C_MSOR, la, lo, la + cell_size_deg(E), lo + cell_size_deg(E),
                  {Writer::attrB(A_CATCOV, 1)});
    w.addRectArea(C_DEPARE, la, lo, la + cell_size_deg(E), lo + cell_size_deg(E),
                  {Writer::attrR(A_DRVAL1, 8.0f), Writer::attrW(A_DRVAL2, 20.0)});
    // deeper soundings 0.6 nm around P = (shipLat+0.1, shipLon), charted only
    // in this E cell; the G cell's shallow soundings are 3.7+ nm from P
    const double dLon = 0.6 / (60.0 * std::cos((shipLat + 0.1) * kDeg));
    w.addSoundings(C_SOUNDG, {{{shipLat + 0.11, shipLon, 12.0}},
                              {{shipLat + 0.1, shipLon + dLon, 14.0}},
                              {{shipLat + 0.09, shipLon, 16.0}},
                              {{shipLat + 0.1, shipLon - dLon, 14.0}}});
    write_cell(root, ciE, E, '0', w);
  }

  // ---- dictionary --------------------------------------------------------
  {
    Dictionary d;
    std::string err;
    CHECK(d.load(root.u8string(), &err));
    CHECK(d.className(5) == "ITDARE");
    CHECK(d.attrType(A_DRVAL2) == 'W');
    CHECK(d.attrType(A_DRVAL1) == 'R');
    // find_and_load from a parent directory
    Dictionary d2;
    CHECK(d2.find_and_load(root.parent_path().u8string() + "/cm93depth_test_tree", 2, &err));
    CHECK(looks_like_cm93_root(root.u8string()));
  }

  // ---- reader ------------------------------------------------------------
  {
    Dictionary d;
    d.load(root.u8string());
    CellData cell;
    std::string err;
    DecodeOptions opt;
    CHECK(load_cell(root.u8string(), d, ciG, G, opt, cell, &err));
    CHECK(cell.filesLoaded == 2);  // base + subcell A
    int nDep = 0, nSnd = 0, nCovr = 0, nObs = 0, nCnt = 0;
    for (auto &f : cell.features) {
      if (f.cls == "DEPARE") nDep++;
      if (f.cls == "SOUNDG") nSnd++;
      if (f.cls == "M_COVR") nCovr++;
      if (f.cls == "OBSTRN") nObs++;
      if (f.cls == "DEPCNT") nCnt++;
    }
    CHECK(nDep == 3);  // two DEPARE + ITDARE substituted
    CHECK(nSnd == 4);
    CHECK(nCovr == 1);
    CHECK(nObs == 3);
    CHECK(nCnt == 1);

    // first DEPARE: ring of 5 points (closed), attrs decoded both types
    const Feature *dep = nullptr;
    for (auto &f : cell.features)
      if (f.cls == "DEPARE") { dep = &f; break; }
    CHECK(dep && dep->parts.size() == 1 && dep->parts[0].size() == 5);
    double v;
    CHECK(dep && dep->getNum("DRVAL1", v) && std::fabs(v - 5.0) < 1e-6);
    CHECK(dep && dep->getNum("DRVAL2", v) && std::fabs(v - 10.0) < 1e-6);
    // ring geometry round trip within 1/65535 of a 20' cell (~0.6 m)
    if (dep) {
      CHECK_NEAR(dep->parts[0][0].lat, shipLat - 0.02, 2e-5);
      CHECK_NEAR(dep->parts[0][0].lon, shipLon - 0.02, 2e-5);
      CHECK_NEAR(dep->parts[0].front().lat, dep->parts[0].back().lat, 1e-12);
      CHECK(geo::point_in_rings(dep->parts, shipLat, shipLon));
    }
    // soundings decoded incl. 12000 magic number
    bool saw30 = false, saw73 = false;
    for (auto &f : cell.features)
      if (f.cls == "SOUNDG")
        for (double z : f.z) {
          if (std::fabs(z - 30.0) < 1e-9) saw30 = true;
          if (std::fabs(z - 7.3) < 1e-9) saw73 = true;
        }
    CHECK(saw30 && saw73);
    // string attribute
    bool sawName = false;
    for (auto &f : cell.features)
      if (f.cls == "OBSTRN" && f.attrs.count("OBJNAM") && f.attrs.at("OBJNAM").str == "Rock X")
        sawName = true;
    CHECK(sawName);

    // keepClasses filter
    DecodeOptions only;
    only.keepClasses = {"SOUNDG"};
    CellData c2;
    load_cell(root.u8string(), d, ciG, G, only, c2);
    bool allSnd = !c2.features.empty();
    for (auto &f : c2.features) allSnd = allSnd && f.cls == "SOUNDG";
    CHECK(allSnd);

    // offsets move geometry: +100 m easting offset shifts points west
    DecodeOptions off;
    off.offsetE = 100.0;
    CellData c3;
    load_cell(root.u8string(), d, ciG, G, off, c3);
    const Feature *dep3 = nullptr;
    for (auto &f : c3.features)
      if (f.cls == "DEPARE") { dep3 = &f; break; }
    if (dep && dep3) {
      double shiftDeg = dep->parts[0][0].lon - dep3->parts[0][0].lon;
      CHECK_NEAR(shiftDeg * kDeg * kR, 100.0, 0.5);
    }

    // A record holding several soundings, followed by another record:
    // decoded as stored, or as OpenCPN reads (and draws) it.
    {
      Writer wm(swLat, swLon, sz);
      wm.addSoundingRecord(C_SOUNDG, {{{shipLat, shipLon, 11.0}},
                                      {{shipLat + 0.01, shipLon, 22.0}},
                                      {{shipLat + 0.02, shipLon, 33.0}}});
      wm.addSoundingRecord(C_SOUNDG, {{{shipLat + 0.03, shipLon, 44.0}}});
      auto raw = wm.build();
      auto depths = [&](bool ocpn) {
        DecodeOptions o;
        o.openCpnSoundings = ocpn;
        std::vector<Feature> fs;
        decode_cell_buffer(raw, d, o, fs);
        std::vector<double> z;
        for (auto &f : fs) z.insert(z.end(), f.z.begin(), f.z.end());
        return z;
      };
      auto asStored = depths(false);
      CHECK((asStored == std::vector<double>{11, 22, 33, 44}));
      // OpenCPN: slots 0..2 <- record 0, slot 1 <- record 1 -> 11, 44, 33;
      // record 0 reads slots 0..2, record 1 reads slot 1
      auto asOcpn = depths(true);
      CHECK((asOcpn == std::vector<double>{11, 44, 33, 44}));
    }

    // corrupt file must be rejected cleanly
    std::vector<uint8_t> junk(500, 0x55);
    std::vector<Feature> out;
    CHECK(!decode_cell_buffer(junk, d, opt, out, nullptr, &err));
    // truncated (length fields no longer match) must be rejected
    Writer wt(swLat, swLon, sz);
    wt.addSoundings(C_SOUNDG, {{{shipLat, shipLon, 1.0}}});
    auto data = wt.build();
    data.resize(data.size() - 3);
    CHECK(!decode_cell_buffer(data, d, opt, out, nullptr, &err));
  }

  // ---- engine ------------------------------------------------------------
  {
    DepthEngine eng;
    std::string err;
    CHECK(eng.setRoot(root.u8string(), &err));

    EngineConfig cfg;  // auto scale, cell mode
    DepthResult r = eng.compute(shipLat, shipLon, cfg);
    CHECK(r.valid && r.haveLeast);
    CHECK(r.scaleChar == 'G');
    CHECK_NEAR(r.leastDepth, -1.5, 1e-6);  // the drying intertidal area
    CHECK(r.leastClass == "DEPARE");
    CHECK(r.haveAtPos && std::fabs(r.atPosMin - 5.0) < 1e-6 && std::fabs(r.atPosMax - 10.0) < 1e-6);
    CHECK(r.unknownHazards == 1);
    // bearing roughly south-west toward the cell corner
    CHECK(r.bearingDeg > 180 && r.bearingDeg < 270);

    // radius 1 nm: candidates 5 m (inside area), 7.3 m sounding 0.6 nm,
    // 3 m contour 0.5 nm, 2.5 m sounding 0.2 nm (subcell A) -> 2.5
    cfg.mode = SearchMode::Radius;
    cfg.radiusNm = 1.0;
    r = eng.compute(shipLat, shipLon, cfg);
    CHECK(r.haveLeast);
    CHECK_NEAR(r.leastDepth, 2.5, 1e-6);
    CHECK(r.leastClass == "SOUNDG");
    CHECK_NEAR(r.rangeNm, 0.0045 * std::cos(shipLat * kDeg) * 60.0, 0.01);
    CHECK(std::fabs(r.bearingDeg - 90.0) < 2.0);
    CHECK(r.unknownHazards == 1);

    // soundings off: contour 3 m at 0.5 nm due south wins
    cfg.useSoundings = false;
    r = eng.compute(shipLat, shipLon, cfg);
    CHECK_NEAR(r.leastDepth, 3.0, 1e-6);
    CHECK(r.leastClass == "DEPCNT");
    CHECK_NEAR(r.rangeNm, 0.5, 0.01);
    CHECK(std::fabs(r.bearingDeg - 180.0) < 1.0);

    // contours off too: 5 m area containing the vessel, range 0
    cfg.useContours = false;
    r = eng.compute(shipLat, shipLon, cfg);
    CHECK_NEAR(r.leastDepth, 5.0, 1e-6);
    CHECK_NEAR(r.rangeNm, 0.0, 1e-9);

    // radius 3 nm with soundings back: 2 m area edge ~1.9 nm east beats 2.5
    cfg.useSoundings = true;
    cfg.useContours = true;
    cfg.radiusNm = 3.0;
    r = eng.compute(shipLat, shipLon, cfg);
    CHECK_NEAR(r.leastDepth, 2.0, 1e-6);
    CHECK(r.leastClass == "DEPARE");
    CHECK(std::fabs(r.bearingDeg - 90.0) < 2.0);

    // radius 4 nm: 1.2 m sounding ~3.7 nm NW
    cfg.radiusNm = 4.0;
    r = eng.compute(shipLat, shipLon, cfg);
    CHECK_NEAR(r.leastDepth, 1.2, 1e-6);
    CHECK(r.bearingDeg > 270 && r.bearingDeg < 360);

    // radius 6 nm: obstruction 0.8 m ~5 nm south
    cfg.radiusNm = 6.0;
    r = eng.compute(shipLat, shipLon, cfg);
    CHECK_NEAR(r.leastDepth, 0.8, 1e-6);
    CHECK(r.leastClass == "OBSTRN");

    // hazards off: back to 1.2
    cfg.useHazards = false;
    r = eng.compute(shipLat, shipLon, cfg);
    CHECK_NEAR(r.leastDepth, 1.2, 1e-6);
    CHECK(r.unknownHazards == 0);
    cfg.useHazards = true;

    // position in the uncovered east quarter of the G cell -> falls back to E
    double eastLon = swLon + 0.9 * sz;
    cfg.mode = SearchMode::Cell;
    r = eng.compute(shipLat, eastLon, cfg);
    CHECK(r.valid && r.scaleChar == 'E');
    CHECK_NEAR(r.leastDepth, 8.0, 1e-6);
    CHECK(r.haveAtPos && std::fabs(r.atPosMin - 8.0) < 1e-6);

    // fixed scale G at the same point uses G even without coverage
    cfg.scaleIdx = G;
    r = eng.compute(shipLat, eastLon, cfg);
    CHECK(r.valid && r.scaleChar == 'G');
    CHECK(!r.haveAtPos);

    // fixed scale with no cell on disk
    cfg.scaleIdx = 3;  // C
    r = eng.compute(shipLat, shipLon, cfg);
    CHECK(!r.valid);

    // position with no chart at all
    cfg.scaleIdx = -1;
    r = eng.compute(10.0, 10.0, cfg);
    CHECK(!r.valid);
    CHECK(r.status == "No chart covers this position");

    // ---- depth area here ----
    EngineConfig h;
    h.mode = SearchMode::DepthAreaHere;
    r = eng.compute(shipLat, shipLon, h);
    CHECK(r.valid && r.haveLeast);
    CHECK_NEAR(r.leastDepth, 5.0, 1e-6);  // DRVAL1 of the area at the ship
    CHECK(r.leastClass == "DEPARE");
    CHECK_NEAR(r.rangeNm, 0.0, 1e-12);
    CHECK(r.haveAtPos && std::fabs(r.atPosMax - 10.0) < 1e-6);
    CHECK(r.unknownHazards == 1);   // undepthed obstruction inside the area
    CHECK(r.shallowHazards == 1);   // the 4 m obstruction inside
    CHECK_NEAR(r.shallowHazardMin, 4.0, 1e-6);
    CHECK(r.status == "OK");
    // hazards switched off
    h.useHazards = false;
    r = eng.compute(shipLat, shipLon, h);
    CHECK(r.unknownHazards == 0 && r.shallowHazards == 0);
    h.useHazards = true;
    // in the shallower 2-5 m area
    r = eng.compute(shipLat, shipLon + 0.06, h);
    CHECK_NEAR(r.leastDepth, 2.0, 1e-6);
    CHECK(r.unknownHazards == 0 && r.shallowHazards == 0);
    // G cell has no area at the east point when forced -> nothing
    h.scaleIdx = G;
    r = eng.compute(shipLat, swLon + 0.9 * sz, h);
    CHECK(r.valid && !r.haveLeast);
    CHECK(r.status == "No depth area charted at this position");
    // auto scale there falls back to the E cell's 8 m area
    h.scaleIdx = -1;
    r = eng.compute(shipLat, swLon + 0.9 * sz, h);
    CHECK(r.scaleChar == 'E');
    CHECK_NEAR(r.leastDepth, 8.0, 1e-6);
    // inside G coverage but no G depth area there -> falls through to E's 8 m
    r = eng.compute(shipLat + 0.1, shipLon, h);
    CHECK(r.valid && r.haveLeast && r.scaleChar == 'E');
    CHECK_NEAR(r.leastDepth, 8.0, 1e-6);
    CHECK(r.scaleTrace.rfind("Gn F- E+ D. C. B. A. Z.", 0) == 0);
    std::cout << "trace: " << r.scaleTrace << "\n";
    // cell mode still searches the first covering cell (G) for the least
    // depth, but the "depth area here" line falls back to E's 8-20 m area
    {
      EngineConfig cm;
      DepthResult rc = eng.compute(shipLat + 0.1, shipLon, cm);
      CHECK(rc.scaleChar == 'G');
      CHECK(rc.scaleTrace.rfind("G+ F. E.", 0) == 0);
      CHECK(rc.haveAtPos && std::fabs(rc.atPosMin - 8.0) < 1e-6);
      CHECK(rc.atPosScaleChar == 'E');
      // radius mode, 0.5 nm: nothing in G that close -> depths from E (8 m)
      EngineConfig rm;
      rm.mode = SearchMode::Radius;
      rm.radiusNm = 0.5;
      DepthResult rr = eng.compute(shipLat + 0.1, shipLon, rm);
      CHECK(rr.haveLeast && rr.scaleChar == 'E');
      CHECK_NEAR(rr.leastDepth, 8.0, 1e-6);
      CHECK(rr.haveAtPos && rr.atPosScaleChar == 'E');
      std::cout << "radius fallback trace: " << rr.scaleTrace << "\n";
      // where G does have an area, radius mode keeps using G for both
      rr = eng.compute(shipLat, shipLon, rm);
      CHECK(rr.scaleChar == 'G' && rr.atPosScaleChar == 'G');
    }
    // ---- interpolated from soundings ----
    {
      EngineConfig ic;
      ic.mode = SearchMode::Interpolated;
      ic.radiusNm = 1.0;
      DepthResult ri = eng.compute(shipLat, shipLon, ic);
      CHECK(ri.valid && ri.haveLeast && ri.scaleChar == 'G');
      CHECK(ri.soundings.size() == 3);  // 2.5, 7.3, 30 within 1 nm; 1.2 is 3.7 nm
      CHECK(!ri.enclosed);              // nothing to the west
      CHECK_NEAR(ri.soundingMin, 2.5, 1e-9);
      // independent IDW check from the reported distances
      double sw = 0, swz = 0, wsum = 0;
      for (auto &sd : ri.soundings) {
        sw += 1 / (sd.distNm * sd.distNm);
        swz += sd.depth / (sd.distNm * sd.distNm);
        wsum += sd.weight;
      }
      CHECK_NEAR(ri.rawEstimate, swz / sw, 1e-9);
      CHECK_NEAR(wsum, 1.0, 1e-9);
      CHECK(ri.rawEstimate > 4.0 && ri.rawEstimate < 5.0);  // ~4.6 m
      CHECK(ri.clamped);                                    // below the 5-10 m band
      CHECK_NEAR(ri.leastDepth, 5.0, 1e-9);
      CHECK_NEAR(ri.rangeNm, ri.soundings[0].distNm, 1e-12);
      CHECK(ri.unknownHazards == 1 && ri.shallowHazards == 0);
      std::cout << "interp raw " << ri.rawEstimate << " -> " << ri.leastDepth
                << "  trace " << ri.scaleTrace << "\n";
      std::string is = build_least_depth_sentence(ri, "PCMLD", SearchMode::Interpolated, 1.0, true);
      CHECK(is.rfind("$PCMLD,5.0,4248.000,S,14730.000,E,0.20,,5.0,10.0,I,1,I,0.74,G,A*", 0) == 0);
      std::cout << "interpolated sentence: " << is;

      // standing on the 7.3 m sounding
      ri = eng.compute(shipLat + 0.01, shipLon, ic);
      CHECK_NEAR(ri.leastDepth, 7.3, 1e-9);
      CHECK(!ri.clamped);

      // wider radius picks up the 1.2 m sounding to the north-west: 4 used
      ic.radiusNm = 4.0;
      ri = eng.compute(shipLat, shipLon, ic);
      CHECK(ri.soundings.size() == 4);
      CHECK_NEAR(ri.soundingMin, 1.2, 1e-9);

      // Large radius, boat at P among E-cell soundings: the far shallow
      // G-cell soundings (more detailed scale) must not be used
      ic.radiusNm = 50.0;
      ri = eng.compute(shipLat + 0.1, shipLon, ic);
      CHECK(ri.valid && ri.soundings.size() == 4);
      bool allE = true;
      for (auto &sd : ri.soundings) allE = allE && sd.depth >= 12.0 && sd.distNm < 0.62;
      CHECK(allE);
      CHECK(ri.enclosed);
      CHECK_NEAR(ri.leastDepth, 14.0, 0.05);  // symmetric -> mean of 12,14,16,14
      CHECK(ri.scaleChar == 'E');
      CHECK_NEAR(ri.soundingMin, 12.0, 1e-9);
      std::cout << "50 nm radius at P: " << ri.leastDepth << " m from "
                << ri.soundings.size() << " soundings, trace " << ri.scaleTrace << "\n";

      // quadrant option: at the ship with 50 nm, the SW quadrant is empty
      // nearby so it may reach further, but never further than needed
      ic.interpQuadrants = true;
      ri = eng.compute(shipLat, shipLon, ic);
      CHECK(ri.valid && ri.soundings.size() == 4);
      ic.interpQuadrants = false;

      // nothing within reach
      ic.radiusNm = 0.2;
      ri = eng.compute(shipLat, swLon + 0.9 * sz, ic);
      CHECK(!ri.valid && !ri.haveLeast);
      CHECK(ri.status.rfind("No soundings within 0.2", 0) == 0);
      CHECK(ri.haveAtPos);  // the depth band is still reported (E's 8-20 m)
    }

    // sentence: mode H, range 0, no bearing, field 11 = 1 unknown + 1 shallower
    r = eng.compute(shipLat, shipLon, h);
    std::string hs = build_least_depth_sentence(r, "PCMLD", SearchMode::DepthAreaHere, 1.0, true);
    CHECK(hs.find(",0.00,,5.0,10.0,A,2,H,,G,A*") != std::string::npos);
    std::cout << "depth-area-here sentence: " << hs;
    std::string diag = eng.diagnostics(shipLat, shipLon, h);
    CHECK(diag.find("CONTAINS position") != std::string::npos);
    CHECK(diag.find("[G] CM93 cell") != std::string::npos);
    std::cout << diag.substr(0, 700) << "...\n";

    // bad root
    DepthEngine bad;
    CHECK(!bad.setRoot((root / "nope").u8string(), &err));
    r = bad.compute(shipLat, shipLon, cfg);
    CHECK(!r.valid);
  }

  // ---- unreadable cell file is reported, not treated as absent -------------
  {
    const double la = -42.4, lo = 147.9;
    int ci = cell_index(la, lo, G);
    CellPathParts pp = cell_path_parts(ci, G);
    fs::path dir = root / pp.rootDir / "G";
    fs::create_directories(dir);
    std::ofstream(dir / ("0" + pp.baseName + ".G"), std::ios::binary) << "garbage";
    DepthEngine eng;
    eng.setRoot(root.u8string());
    EngineConfig h;
    h.mode = SearchMode::DepthAreaHere;
    DepthResult r = eng.compute(la, lo, h);
    CHECK(r.scaleTrace.rfind("Gx", 0) == 0);
    CHECK(r.readError.rfind("G: ", 0) == 0);
    std::cout << "read error: " << r.readError << "\n";
  }

  // ---- areas with no depth: sand flats, drying seabed areas ---------------
  {
    const double la = -42.30, lo = 147.40;
    int ci = cell_index(la, lo, G);
    double cla, clo;
    cell_origin(ci, cla, clo);
    Writer w(cla, clo, sz);
    w.addRectArea(C_MSOR, cla, clo, cla + sz, clo + sz, {});
    const double q = sz / 8;
    // west half: 10-20 m, with a sand patch (no depth) inside it
    w.addRectArea(C_DEPARE, cla, clo, cla + sz, clo + 4 * q,
                  {Writer::attrR(A_DRVAL1, 10.0f), Writer::attrW(A_DRVAL2, 20.0)});
    w.addRectArea(C_SBDARE, cla + q, clo + q, cla + 2 * q, clo + 2 * q,
                  {Writer::attrS(A_OBJNAM, "sand")});
    // a depth area with no DRVAL1 at all
    w.addRectArea(C_DEPARE, cla + 3 * q, clo + 5 * q, cla + 4 * q, clo + 6 * q, {});
    // a sand bank that covers and uncovers (WATLEV 4), no depth area there
    w.addRectArea(C_SBDARE, cla + 5 * q, clo + 5 * q, cla + 6 * q, clo + 6 * q,
                  {Writer::attrB(A_WATLEV, 4)});
    // a sand area with no depth and no water level, no depth area there
    w.addRectArea(C_SBDARE, cla + 7 * q, clo + 6 * q, cla + 7.8 * q, clo + 7 * q,
                  {Writer::attrS(A_OBJNAM, "sand")});
    // a submerged reef charted as an obstruction with no depth, and a foul
    // area, both inside the 10-20 m area
    w.addRectArea(C_OBSTRN, cla + 0.2 * q, clo + 2.5 * q, cla + 0.8 * q, clo + 3.5 * q,
                  {Writer::attrS(A_OBJNAM, "reef")});
    w.addRectArea(C_OBSTRN, cla + 6.2 * q, clo + 2.5 * q, cla + 6.8 * q, clo + 3.5 * q,
                  {Writer::attrB(A_CATOBS, 6)});
    // a 4 m sounding just outside the depthless area
    w.addSoundings(C_SOUNDG, {{{cla + 3.5 * q, clo + 4.8 * q, 4.0}}});
    write_cell(root, ci, G, '0', w);

    DepthEngine eng;
    eng.setRoot(root.u8string());
    EngineConfig h;
    h.mode = SearchMode::DepthAreaHere;
    auto at = [&](double fy, double fx) { return eng.compute(cla + fy * q, clo + fx * q, h); };
    {  // interpolated inside the depthless area: held at 0 m
      EngineConfig ic;
      ic.mode = SearchMode::Interpolated;
      ic.radiusNm = 5;
      DepthResult ri = eng.compute(cla + 3.5 * q, clo + 5.5 * q, ic);
      CHECK(ri.valid && ri.haveLeast && ri.clamped && std::fabs(ri.leastDepth) < 1e-9);
      CHECK(ri.rawEstimate > 3.9);
      CHECK(ri.haveAtPos && ri.atPosMin == 0.0 && ri.atPosMax == 0.0);
    }
    DepthResult r = at(1.5, 1.5);  // sand patch inside the 10-20 m area: ignored
    CHECK(r.haveLeast && std::fabs(r.leastDepth - 10.0) < 1e-6 && r.leastClass == "DEPARE");
    r = at(3.5, 5.5);  // depth area with no DRVAL1
    CHECK(r.haveLeast && std::fabs(r.leastDepth) < 1e-9 && r.leastClass == "DEPARE");
    r = at(5.5, 5.5);  // drying sand bank
    CHECK(r.haveLeast && std::fabs(r.leastDepth) < 1e-9 && r.leastClass == "SBDARE");
    // over the reef: 0 m (default), or the area's 10 m with the ECDIS rule
    r = at(0.5, 3.0);
    CHECK(r.haveLeast && r.dangerHere && r.dangerHereAssumed);
    CHECK(std::fabs(r.leastDepth) < 1e-9 && r.leastClass == "OBSTRN");
    h.undepthedDangerRule = 1;
    r = at(0.5, 3.0);
    CHECK(r.dangerHere && std::fabs(r.leastDepth - 10.0) < 1e-9);
    h.undepthedDangerRule = 0;
    // over the foul area: not a danger to navigation, area depth stands
    r = at(6.5, 3.0);
    CHECK(r.haveLeast && !r.dangerHere && std::fabs(r.leastDepth - 10.0) < 1e-9);
    {  // interpolated over the reef: held at 0 m
      EngineConfig ic;
      ic.mode = SearchMode::Interpolated;
      ic.radiusNm = 20;
      DepthResult ri = eng.compute(cla + 0.5 * q, clo + 3.0 * q, ic);
      CHECK(ri.valid && ri.haveLeast && ri.dangerHere && std::fabs(ri.leastDepth) < 1e-9);
    }
    r = at(7.4, 6.5);  // sand with no depth and no depth area: 0 m
    CHECK(r.haveLeast && std::fabs(r.leastDepth) < 1e-9 && r.leastClass == "SBDARE");
    r = at(7.4, 7.5);  // nothing charted there at all
    CHECK(!r.haveLeast);
    // radius mode near the sand patch in deep water: the patch is not a depth
    EngineConfig rm;
    rm.mode = SearchMode::Radius;
    rm.radiusNm = 0.3;
    r = eng.compute(cla + 1.5 * q, clo + 1.5 * q, rm);
    CHECK(r.haveLeast && std::fabs(r.leastDepth - 10.0) < 1e-6);
    // radius mode reaching the drying bank: 0 m from SBDARE
    rm.radiusNm = 50;
    r = eng.compute(cla + 5.5 * q, clo + 3.5 * q, rm);
    CHECK(r.haveLeast && std::fabs(r.leastDepth) < 1e-9);
    CHECK(source_code("SBDARE") == 'A');
  }

  // ---- a gap in the depth areas (a reef the chart leaves as a hole) ---------
  {
    const double la = -42.20, lo = 147.75;
    int ci = cell_index(la, lo, G);
    double cla, clo;
    cell_origin(ci, cla, clo);
    Writer w(cla, clo, sz);
    w.addRectArea(C_MSOR, cla, clo, cla + sz, clo + sz, {});
    const double q = sz / 8;
    w.addRectRings(C_DEPARE,
                   {{{cla + q, clo + q, cla + 7 * q, clo + 7 * q}},
                    {{cla + 3 * q, clo + 3 * q, cla + 4 * q, clo + 4 * q}},
                    // a reef only ~9 m across
                    {{cla + 2.5 * q - 0.00004, clo + 5 * q, cla + 2.5 * q + 0.00004,
                      clo + 5 * q + 0.00011}}},
                   {Writer::attrR(A_DRVAL1, 50.0f), Writer::attrW(A_DRVAL2, 100.0)});
    w.addSoundings(C_SOUNDG, {{{cla + 2 * q, clo + 2 * q, 60.0}}});
    write_cell(root, ci, G, '0', w);
    DepthEngine eng;
    eng.setRoot(root.u8string());
    EngineConfig ic;
    ic.mode = SearchMode::Interpolated;
    ic.radiusNm = 50;
    DepthResult r = eng.compute(cla + 3.5 * q, clo + 3.5 * q, ic);
    CHECK(r.valid && r.haveLeast && r.leastClass == "HOLE" && std::fabs(r.leastDepth) < 1e-9);
    CHECK(r.dangerHere && r.dangerHereAssumed);
    CHECK(build_least_depth_sentence(r, "PCMLD", SearchMode::Interpolated, 50, true)
              .find(",U,") != std::string::npos);
    ic.undepthedDangerRule = 1;
    r = eng.compute(cla + 3.5 * q, clo + 3.5 * q, ic);
    CHECK(std::fabs(r.leastDepth - 50.0) < 1e-9);
    // in the 50-100 m area itself: interpolated normally
    ic.undepthedDangerRule = 0;
    r = eng.compute(cla + 2.5 * q, clo + 2.5 * q, ic);
    CHECK(r.valid && r.leastClass == "INTERP" && r.leastDepth >= 50.0);
    // outside the area, open to the east: not a gap
    EngineConfig h;
    h.mode = SearchMode::DepthAreaHere;
    r = eng.compute(cla + 3.5 * q, clo + 7.5 * q, h);
    CHECK(r.leastClass != "HOLE");
    std::cout << "gap: " << eng.compute(cla + 3.5 * q, clo + 3.5 * q, h).scaleTrace << "\n";
    {  // an obstruction charted only on the smaller-scale E chart
      int ciE = cell_index(cla + 2.5 * q, clo + 2.5 * q, E);
      double ela, elo;
      cell_origin(ciE, ela, elo);
      Writer we(ela, elo, cell_size_deg(E));
      we.addRectArea(C_MSOR, ela, elo, ela + cell_size_deg(E), elo + cell_size_deg(E), {});
      we.addRectArea(C_OBSTRN, cla + 2.3 * q, clo + 2.3 * q, cla + 2.7 * q, clo + 2.7 * q,
                     {Writer::attrB(A_WATLEV, 3)});
      write_cell(root, ciE, E, '0', we);
      DepthEngine e2;
      e2.setRoot(root.u8string());
      EngineConfig hc;
      hc.mode = SearchMode::DepthAreaHere;
      DepthResult rr = e2.compute(cla + 2.5 * q, clo + 2.5 * q, hc);
      CHECK(rr.haveLeast && rr.dangerHere && rr.dangerHereScale == 'E');
      CHECK(std::fabs(rr.leastDepth) < 1e-9 && rr.leastClass == "OBSTRN");
      hc.coarseDangers = false;  // only the G chart's own: 50 m
      rr = e2.compute(cla + 2.5 * q, clo + 2.5 * q, hc);
      CHECK(!rr.dangerHere && std::fabs(rr.leastDepth - 50.0) < 1e-9);
      // look-ahead toward it from the west finds it as an undepthed danger
      LookAheadConfig lc2;
      lc2.basis = LookBasis::Distance;
      lc2.distanceNm = 3.0;
      LookAheadResult l2 = e2.lookAhead(cla + 2.5 * q, clo + 1.5 * q, 90.0, 6.0, NAN, lc2,
                                        EngineConfig());
      CHECK(l2.haveShoal && l2.shoal.cls == "OBSTRN" && l2.shoal.scaleChar == 'E');
      EngineConfig noc;
      noc.coarseDangers = false;
      l2 = e2.lookAhead(cla + 2.5 * q, clo + 1.5 * q, 90.0, 6.0, NAN, lc2, noc);
      CHECK(!l2.haveShoal || l2.shoal.cls != "OBSTRN");
    }
    {  // a large undepthed obstruction AREA on the smaller-scale E chart (an
       // "obstructions may exist" zone) over water the G chart sounds at
       // 10.8 m 0.12 nm away: the detailed soundings overrule it
      const double vla = -42.55, vlo = 148.25;  // the vessel
      int cg = cell_index(vla, vlo, G), ceE = cell_index(vla, vlo, E);
      double gla, glo, ela, elo;
      cell_origin(cg, gla, glo);
      cell_origin(ceE, ela, elo);
      Writer wg(gla, glo, sz);
      wg.addRectArea(C_MSOR, gla, glo, gla + sz, glo + sz, {});
      wg.addRectArea(C_DEPARE, gla, glo, gla + sz, glo + sz,
                     {Writer::attrR(A_DRVAL1, 10.0f), Writer::attrW(A_DRVAL2, 15.0)});
      const double nm = 1.0 / 60.0, nmLon = nm / std::cos(vla * kDeg);
      wg.addSoundings(C_SOUNDG, {{{vla - 0.05 * nm, vlo - 0.11 * nmLon, 10.8}},
                                 {{vla + 0.08 * nm, vlo + 0.11 * nmLon, 7.4}},
                                 {{vla + 0.20 * nm, vlo + 0.12 * nmLon, 3.4}}});
      write_cell(root, cg, G, '0', wg);
      Writer we(ela, elo, cell_size_deg(E));
      we.addRectArea(C_MSOR, ela, elo, ela + cell_size_deg(E), elo + cell_size_deg(E), {});
      we.addRectArea(C_DEPARE, ela, elo, ela + cell_size_deg(E), elo + cell_size_deg(E),
                     {Writer::attrR(A_DRVAL1, 10.0f), Writer::attrW(A_DRVAL2, 20.0)});
      we.addRectArea(C_OBSTRN, vla - 0.05, vlo - 0.05, vla + 0.05, vlo + 0.05,
                     {Writer::attrB(A_WATLEV, 3)});
      write_cell(root, ceE, E, '0', we);
      DepthEngine e3;
      e3.setRoot(root.u8string());
      EngineConfig ic3;
      ic3.mode = SearchMode::Interpolated;
      ic3.radiusNm = 50;
      DepthResult r3 = e3.compute(vla, vlo, ic3);
      CHECK(r3.valid && !r3.dangerHere && r3.dangerOverruled && r3.overruledClass == "OBSTRN");
      CHECK(r3.overruledScale == 'E' && r3.overruledByScale == 'G' &&
            std::fabs(r3.overruledByDepth - 10.8) < 1e-6);
      CHECK(r3.leastDepth >= 7.0 && r3.leastClass == "INTERP");
      std::cout << "overruled coarse obstruction: " << r3.leastDepth << " m (by "
                << r3.overruledByDepth << " m at " << r3.overruledByDistNm << " nm)\n";
      EngineConfig h3 = ic3;
      h3.mode = SearchMode::DepthAreaHere;
      r3 = e3.compute(vla, vlo, h3);
      CHECK(r3.haveLeast && std::fabs(r3.leastDepth - 10.0) < 1e-9 && !r3.dangerHere);
      LookAheadConfig lc3;
      lc3.basis = LookBasis::Distance;
      lc3.distanceNm = 0.5;
      LookAheadResult l3 = e3.lookAhead(vla, vlo, 90.0, 5.0, NAN, lc3, h3);
      CHECK(l3.valid && !l3.haveUndepthed);
      // switched off (0 nm): the zone counts as before, 0 m
      ic3.coarseCheckNm = 0;
      r3 = e3.compute(vla, vlo, ic3);
      CHECK(r3.dangerHere && std::fabs(r3.leastDepth) < 1e-9);
      // deep soundings only farther than the check radius: still counted
      ic3.coarseCheckNm = 0.1;
      r3 = e3.compute(vla, vlo, ic3);
      CHECK(r3.dangerHere && std::fabs(r3.leastDepth) < 1e-9);
      // ... or shallower than the clear depth
      ic3.coarseCheckNm = 0.25;
      ic3.coarseClearDepthM = 12.0;
      r3 = e3.compute(vla, vlo, ic3);
      CHECK(r3.dangerHere && std::fabs(r3.leastDepth) < 1e-9);
    }
    // look-ahead toward the gap from the west, along 090: the gap's west
    // edge is 1 q of longitude ahead
    LookAheadConfig lc;
    lc.basis = LookBasis::Distance;
    lc.distanceNm = 4.0;
    lc.halfWidthNm = 0.1;
    const double lat0 = cla + 3.5 * q, lon0 = clo + 2.0 * q;
    const double edgeNm = q * 60.0 * std::cos(lat0 * kDeg);
    LookAheadResult lr = eng.lookAhead(lat0, lon0, 90.0, 6.0, NAN, lc, EngineConfig());
    CHECK(lr.valid && lr.haveShoal && lr.shoal.cls == "HOLE" && lr.shoal.undepthed);
    CHECK(std::fabs(lr.shoal.aheadNm - edgeNm) < 0.03);
    CHECK(lr.undepthed == 1);
    CHECK(build_lookahead_sentence(lr, "PCMLA", true).find(",U,") != std::string::npos);
    std::cout << "look-ahead gap at " << lr.shoal.aheadNm << " nm (edge " << edgeNm << ")\n";
    // heading away from it: nothing
    lr = eng.lookAhead(lat0, lon0, 270.0, 6.0, NAN, lc, EngineConfig());
    CHECK(lr.valid && !lr.haveShoal);
    // the 9 m reef, 0.036 nm to starboard of the track, 0.93 nm ahead
    {
      const double la1 = cla + 2.5 * q + 0.0006, lo1 = clo + 4.5 * q;
      const double edge1 = 0.5 * q * 60.0 * std::cos(la1 * kDeg);
      lr = eng.lookAhead(la1, lo1, 90.0, 6.0, NAN, lc, EngineConfig());
      CHECK(lr.haveShoal && lr.shoal.cls == "HOLE");
      CHECK(std::fabs(lr.shoal.aheadNm - edge1) < 0.003);
      std::cout << "9 m reef found at " << lr.shoal.aheadNm << " nm (edge " << edge1 << ")\n";
      lc.zone = LookZone::Sector;
      lc.halfAngleDeg = 10;
      lr = eng.lookAhead(la1, lo1, 90.0, 6.0, NAN, lc, EngineConfig());
      CHECK(lr.haveShoal && lr.shoal.cls == "HOLE");
      lc.zone = LookZone::Corridor;
      lc.halfWidthNm = 0.02;  // too narrow to reach it
      lr = eng.lookAhead(la1, lo1, 90.0, 6.0, NAN, lc, EngineConfig());
      CHECK(!lr.haveShoal);
    }
  }

  // ---- cell stored only as subcell 'A' (no base '0' file) ------------------
  {
    const double la = -42.45, lo = 147.62;
    int ci = cell_index(la, lo, G);
    double cla, clo;
    cell_origin(ci, cla, clo);
    Writer w(cla, clo, sz);
    w.addRectArea(C_MSOR, cla, clo, cla + sz, clo + sz, {});
    w.addRectArea(C_DEPARE, cla, clo, cla + sz, clo + sz,
                  {Writer::attrR(A_DRVAL1, 9.0f), Writer::attrW(A_DRVAL2, 11.0)});
    write_cell(root, ci, G, 'A', w);
    DepthEngine eng;
    eng.setRoot(root.u8string());
    EngineConfig h;
    h.mode = SearchMode::DepthAreaHere;
    DepthResult r = eng.compute(la, lo, h);
    CHECK(r.valid && r.scaleChar == 'G');
    CHECK_NEAR(r.leastDepth, 9.0, 1e-6);
  }

#ifdef HAVE_LZMA
  // ---- xz-compressed cell (as written by OpenCPN's chart downloader) -------
  {
    const double la = -42.5, lo = 147.2;  // a different G cell
    int ci = cell_index(la, lo, G);
    double cla, clo;
    cell_origin(ci, cla, clo);
    Writer w(cla, clo, sz);
    w.addRectArea(C_MSOR, cla, clo, cla + sz, clo + sz, {});
    w.addRectArea(C_DEPARE, cla, clo, cla + sz, clo + sz,
                  {Writer::attrR(A_DRVAL1, 12.0f), Writer::attrW(A_DRVAL2, 15.0)});
    // intertidal area with no attributes -> counts as 0 m (drying)
    w.addSquareArea(C_ITDARE, cla + 0.05, clo + 0.05, 0.005, {});
    auto plain = w.build();
    std::vector<uint8_t> comp(plain.size() + 1024);
    size_t outPos = 0;
    lzma_ret lr = lzma_easy_buffer_encode(6, LZMA_CHECK_CRC64, nullptr, plain.data(),
                                          plain.size(), comp.data(), &outPos, comp.size());
    CHECK(lr == LZMA_OK);
    comp.resize(outPos);
    CellPathParts pp = cell_path_parts(ci, G);
    fs::path dir = root / pp.rootDir / "G";
    fs::create_directories(dir);
    std::ofstream f(dir / ("0" + pp.baseName + ".G.xz"), std::ios::binary);
    f.write((const char *)comp.data(), comp.size());
    f.close();

    DepthEngine eng;
    eng.setRoot(root.u8string());
    EngineConfig cfg;
    DepthResult r = eng.compute(la, lo, cfg);
    CHECK(r.valid && r.scaleChar == 'G');
    CHECK_NEAR(r.leastDepth, 0.0, 1e-6);  // attribute-less ITDARE
    CHECK(r.haveAtPos && std::fabs(r.atPosMin - 12.0) < 1e-6);
  }
#endif

  test_enc(root, shipLat, shipLon, swLon, sz);
  fs::remove_all(root);
  test_lookahead();
  test_alarm();
  std::cout << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail ? 1 : 0;
}
