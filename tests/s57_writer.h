// Minimal S-57 / ISO 8211 writer for the unit tests: builds base cells and
// update files the way producers lay them out (DDR + one data record per
// vector or feature record), so they can be read back through the plugin's
// S-57 reader. Only what the tests need.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace s57w {

inline std::string b(uint64_t v, int n) {  // little-endian binary
  std::string s;
  for (int i = 0; i < n; i++) s += (char)((v >> (8 * i)) & 0xff);
  return s;
}
inline std::string A(const std::string &s) { return s + '\x1f'; }
inline std::string name(int rcnm, uint32_t rcid) { return b(rcnm, 1) + b(rcid, 4); }

using Fields = std::vector<std::pair<std::string, std::string>>;

struct Def {
  std::string tag, ctrl, name, desc, fmt;
};

inline const std::vector<Def> &defs() {
  static const std::vector<Def> d = {
      {"0001", "0000;&   ", "ISO/IEC 8211 Record Identifier", "", "(b12)"},
      {"DSID", "1600;&   ", "Data set identification field",
       "RCNM!RCID!EXPP!INTU!DSNM!EDTN!UPDN!UADT!ISDT!STED!PRSP!PSDN!PRED!PROF!AGEN!COMT",
       "(b11,b14,2b11,3A,2A(8),R(4),b11,2A,b11,b12,A)"},
      {"DSPM", "1600;&   ", "Data set parameter field",
       "RCNM!RCID!HDAT!VDAT!SDAT!CSCL!DUNI!HUNI!PUNI!COUN!COMF!SOMF!COMT",
       "(b11,b14,3b11,b14,4b11,2b14,A)"},
      {"VRID", "1600;&   ", "Vector record identifier field", "RCNM!RCID!RVER!RUIN",
       "(b11,b14,b12,b11)"},
      {"VRPC", "1600;&   ", "Vector record pointer control field", "VPUI!VPIX!NVPT",
       "(b11,2b12)"},
      {"VRPT", "2600;&   ", "Vector record pointer field", "*NAME!ORNT!USAG!TOPI!MASK",
       "(B(40),4b11)"},
      {"SGCC", "1600;&   ", "Coordinate control field", "CCUI!CCIX!CCNC", "(b11,2b12)"},
      {"SG2D", "2500;&   ", "2-D coordinate field", "*YCOO!XCOO", "(2b24)"},
      {"SG3D", "2500;&   ", "3-D coordinate (sounding array) field", "*YCOO!XCOO!VE3D",
       "(b24,b24,b24)"},
      {"FRID", "1600;&   ", "Feature record identifier field",
       "RCNM!RCID!PRIM!GRUP!OBJL!RVER!RUIN", "(b11,b14,2b11,2b12,b11)"},
      {"FOID", "1600;&   ", "Feature object identifier field", "AGEN!FIDN!FIDS",
       "(b12,b14,b12)"},
      {"ATTF", "2600;&   ", "Feature record attribute field", "*ATTL!ATVL", "(b12,A)"},
      {"FSPC", "1600;&   ", "Feature record to spatial record pointer control field",
       "FSUI!FSIX!NSPT", "(b11,2b12)"},
      {"FSPT", "2600;&   ", "Feature record to spatial record pointer field",
       "*NAME!ORNT!USAG!MASK", "(B(40),3b11)"},
      {"CATD", "1600;&   ", "Catalogue directory field",
       "RCNM!RCID!FILE!LFIL!VOLM!IMPL!SLAT!WLON!NLAT!ELON!CRCS!COMT",
       "(A(2),I(10),3A,A(3),4R,2A)"},
  };
  return d;
}

// One ISO 8211 record: leader, directory, field area
inline std::string record(char leaderId, const Fields &fields, const std::string &ddrExtra = "") {
  const int sLen = 4, sPos = 5, sTag = 4;
  std::string dir, area;
  for (auto &f : fields) {
    std::string data = f.second + '\x1e';
    char e[32];
    std::snprintf(e, sizeof e, "%s%04d%05d", f.first.c_str(), (int)data.size(), (int)area.size());
    dir += e;
    area += data;
  }
  dir += '\x1e';
  const int fieldArea = 24 + (int)dir.size();
  const int len = fieldArea + (int)area.size();
  char L[32];
  if (leaderId == 'L')
    std::snprintf(L, sizeof L, "%05d3LE1 09%05d ! %d%d0%d", len, fieldArea, sLen, sPos, sTag);
  else
    std::snprintf(L, sizeof L, "%05d D     %05d   %d%d0%d", len, fieldArea, sLen, sPos, sTag);
  (void)ddrExtra;
  return std::string(L, 24) + dir + area;
}

inline std::string ddr() {
  Fields f;
  f.push_back({"0000", "0000;&   test"});
  for (auto &d : defs())
    f.push_back({d.tag, d.ctrl + d.name + '\x1f' + d.desc + '\x1f' + d.fmt});
  return record('L', f);
}

// CATALOG.031 of an exchange set: (file relative to it with '\\', S, W, N, E)
struct CatEntry {
  std::string file;
  double s, w, n, e;
};
inline std::vector<uint8_t> catalog(const std::vector<CatEntry> &cells) {
  std::string out = ddr();
  int id = 1;
  for (auto &c : cells) {
    char num[16], v[4][32];
    std::snprintf(num, sizeof num, "%10d", id);
    std::snprintf(v[0], 32, "%.7f", c.s);
    std::snprintf(v[1], 32, "%.7f", c.w);
    std::snprintf(v[2], 32, "%.7f", c.n);
    std::snprintf(v[3], 32, "%.7f", c.e);
    out += record('D', {{"0001", b(id, 2)},
                        {"CATD", std::string("CD") + num + A(c.file) + A("") + A("V01X01") +
                                     "BIN" + A(v[0]) + A(v[1]) + A(v[2]) + A(v[3]) +
                                     A("00000000") + A("")}});
    id++;
  }
  return std::vector<uint8_t>(out.begin(), out.end());
}

// A data set (base cell or update) under construction
struct Cell {
  int intu = 5, cscl = 20000, comf = 10000000, somf = 10, duni = 1;
  std::string dsnm = "TEST0001.000", edtn = "1", updn = "0";
  std::vector<Fields> recs;
  uint32_t nextV = 1, nextF = 1;
  int ruin = 1;  // RUIN written for new records (1 insert)

  std::string yx(double lat, double lon) const {
    return b((uint32_t)(int32_t)std::lround(lat * comf), 4) +
           b((uint32_t)(int32_t)std::lround(lon * comf), 4);
  }
  uint32_t vector(int rcnm, const std::vector<std::pair<double, double>> &pts,
                  const std::vector<std::array<uint32_t, 2>> &vrpt = {}) {
    uint32_t id = nextV++;
    Fields f;
    f.push_back({"VRID", b(rcnm, 1) + b(id, 4) + b(1, 2) + b(ruin, 1)});
    if (!vrpt.empty()) {
      std::string p;
      for (size_t i = 0; i < vrpt.size(); i++)  // begin node TOPI 1, end node TOPI 2
        p += name(120, vrpt[i][0]) + b(255, 1) + b(255, 1) + b(vrpt[i][1], 1) + b(255, 1);
      f.push_back({"VRPT", p});
    }
    if (!pts.empty()) {
      std::string c;
      for (auto &q : pts) c += yx(q.first, q.second);
      f.push_back({"SG2D", c});
    }
    recs.push_back(f);
    return id;
  }
  uint32_t node(double lat, double lon) { return vector(120, {{lat, lon}}); }
  uint32_t isolated(double lat, double lon) { return vector(110, {{lat, lon}}); }
  uint32_t edge(uint32_t n0, uint32_t n1, const std::vector<std::pair<double, double>> &mid) {
    return vector(130, mid, {{n0, 1}, {n1, 2}});
  }
  uint32_t soundings(const std::vector<std::array<double, 3>> &s) {
    uint32_t id = nextV++;
    std::string c;
    for (auto &p : s)
      c += yx(p[0], p[1]) + b((uint32_t)(int32_t)std::lround(p[2] * somf), 4);
    recs.push_back({{"VRID", b(110, 1) + b(id, 4) + b(1, 2) + b(ruin, 1)}, {"SG3D", c}});
    return id;
  }
  // attrs: (ATTL code, value)
  uint32_t feature(int prim, int objl, const std::vector<std::pair<int, std::string>> &attrs,
                   const std::vector<std::array<uint32_t, 3>> &fspt /* rcnm, rcid, ornt */) {
    uint32_t id = nextF++;
    Fields f;
    f.push_back({"FRID", b(100, 1) + b(id, 4) + b(prim, 1) + b(2, 1) + b(objl, 2) + b(1, 2) +
                             b(ruin, 1)});
    f.push_back({"FOID", b(550, 2) + b(id, 4) + b(1, 2)});
    if (!attrs.empty()) {
      std::string a;
      for (auto &p : attrs) a += b(p.first, 2) + A(p.second);
      f.push_back({"ATTF", a});
    }
    if (!fspt.empty()) {
      std::string p;
      for (auto &q : fspt) p += name(q[0], q[1]) + b(q[2], 1) + b(1, 1) + b(2, 1);
      f.push_back({"FSPT", p});
    }
    recs.push_back(f);
    return id;
  }
  // Closed ring as one edge from a node back to itself
  uint32_t ringEdge(const std::vector<std::pair<double, double>> &pts) {
    uint32_t n = node(pts[0].first, pts[0].second);
    std::vector<std::pair<double, double>> mid(pts.begin() + 1, pts.end());
    return edge(n, n, mid);
  }
  uint32_t area(int objl, const std::vector<std::pair<int, std::string>> &attrs,
                const std::vector<std::vector<std::pair<double, double>>> &rings) {
    std::vector<std::array<uint32_t, 3>> fs;
    for (auto &r : rings) fs.push_back({130, ringEdge(r), 1});
    return feature(3, objl, attrs, fs);
  }
  static std::vector<std::pair<double, double>> rect(double la0, double lo0, double la1,
                                                      double lo1) {
    return {{la0, lo0}, {la0, lo1}, {la1, lo1}, {la1, lo0}};
  }
  // Rectangle split into two edges between two nodes (the second reversed in
  // the feature, as producers share edges between neighbouring areas)
  uint32_t rectTwoEdges(int objl, const std::vector<std::pair<int, std::string>> &attrs,
                        double la0, double lo0, double la1, double lo1) {
    uint32_t nA = node(la0, lo0), nB = node(la1, lo1);
    uint32_t e1 = edge(nA, nB, {{la0, lo1}});  // SW -> SE -> NE
    uint32_t e2 = edge(nA, nB, {{la1, lo0}});  // SW -> NW -> NE, used reversed
    return feature(3, objl, attrs, {{130, e1, 1}, {130, e2, 2}});
  }
  uint32_t point(int objl, const std::vector<std::pair<int, std::string>> &attrs, double lat,
                 double lon) {
    return feature(1, objl, attrs, {{110, isolated(lat, lon), 255}});
  }
  uint32_t sounding(const std::vector<std::array<double, 3>> &s) {
    return feature(1, 129, {}, {{110, soundings(s), 255}});
  }
  uint32_t line(int objl, const std::vector<std::pair<int, std::string>> &attrs,
                const std::vector<std::pair<double, double>> &pts) {
    uint32_t n0 = node(pts.front().first, pts.front().second);
    uint32_t n1 = node(pts.back().first, pts.back().second);
    std::vector<std::pair<double, double>> mid(pts.begin() + 1, pts.end() - 1);
    return feature(2, objl, attrs, {{130, edge(n0, n1, mid), 1}});
  }

  // ---- update records
  void modifyAttrs(uint32_t rcid, int prim, int objl, int rver,
                   const std::vector<std::pair<int, std::string>> &attrs) {
    std::string a;
    for (auto &p : attrs) a += b(p.first, 2) + A(p.second);
    recs.push_back({{"FRID", b(100, 1) + b(rcid, 4) + b(prim, 1) + b(2, 1) + b(objl, 2) +
                                 b(rver, 2) + b(3, 1)},
                    {"ATTF", a}});
  }
  void deleteFeature(uint32_t rcid, int prim, int objl, int rver) {
    recs.push_back({{"FRID", b(100, 1) + b(rcid, 4) + b(prim, 1) + b(2, 1) + b(objl, 2) +
                                 b(rver, 2) + b(2, 1)}});
  }
  // Coordinate update of a vector: ccui 1 insert, 2 delete, 3 modify
  void modifyCoords(int rcnm, uint32_t rcid, int rver, int ccui, int ccix,
                    const std::vector<std::pair<double, double>> &pts, int n = -1) {
    std::string c;
    for (auto &q : pts) c += yx(q.first, q.second);
    Fields f = {{"VRID", b(rcnm, 1) + b(rcid, 4) + b(rver, 2) + b(3, 1)},
                {"SGCC", b(ccui, 1) + b(ccix, 2) + b(n < 0 ? pts.size() : n, 2)}};
    if (!pts.empty()) f.push_back({"SG2D", c});
    recs.push_back(f);
  }

  std::vector<uint8_t> build() const {
    std::string out = ddr();
    Fields head = {{"0001", b(1, 2)},
                   {"DSID", b(10, 1) + b(1, 4) + b(updn == "0" ? 1 : 2, 1) + b(intu, 1) +
                                A(dsnm) + A(edtn) + A(updn) + "20260101" + "20260101" +
                                "03.1" + b(1, 1) + A("") + A("2.0") + b(1, 1) + b(550, 2) +
                                A("")}};
    out += record('D', head);
    out += record('D', {{"0001", b(2, 2)},
                        {"DSPM", b(20, 1) + b(1, 4) + b(2, 1) + b(17, 1) + b(23, 1) +
                                     b(cscl, 4) + b(duni, 1) + b(1, 1) + b(1, 1) + b(1, 1) +
                                     b(comf, 4) + b(somf, 4) + A("")}});
    int n = 3;
    for (auto &r : recs) {
      Fields f = {{"0001", b(n++, 2)}};
      f.insert(f.end(), r.begin(), r.end());
      out += record('D', f);
    }
    return std::vector<uint8_t>(out.begin(), out.end());
  }
};

}  // namespace s57w
