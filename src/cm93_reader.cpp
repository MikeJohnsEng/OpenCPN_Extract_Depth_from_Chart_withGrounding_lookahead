/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * cm93_reader.cpp : standalone CM93 v2 cell reader.
 *
 * Format knowledge comes from OpenCPN gui/src/cm93.cpp (GPLv2+).
 * One deliberate difference: OpenCPN advances its 3-D point buffer by one
 * point per record (read_3dpoint_table: "q++"), which lets consecutive
 * multipoint records overwrite each other in memory. Here each record gets
 * its own run of points, which is what the header's total-point count
 * (m_50) implies. Parsing of the file itself is identical either way.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "cm93_reader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#ifdef HAVE_LZMA
#include <lzma.h>
#endif

namespace fs = std::filesystem;

namespace cm93 {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const double kPi = 3.1415926535897931160;
static const double kDeg = kPi / 180.0;
// "International 1924" semi-major axis, found empirically by OpenCPN to match
// the CM93 point transform coefficients.
static const double kCM93SemiMajor = 6378388.0;

const ScaleInfo kScales[kNumScales] = {
    {'Z', 120, 20000000}, {'A', 60, 3000000}, {'B', 30, 1000000},
    {'C', 12, 200000},    {'D', 3, 100000},   {'E', 1, 50000},
    {'F', 1, 20000},      {'G', 1, 7500},
};

int scale_index_from_char(char c) {
  c = (char)std::toupper((unsigned char)c);
  for (int i = 0; i < kNumScales; i++)
    if (kScales[i].ch == c) return i;
  return -1;
}

// ---------------------------------------------------------------------------
// Encode / decode tables (OpenCPN cm93.cpp Table_0)
// ---------------------------------------------------------------------------
static const unsigned char Table_0[256] = {
    0x0CD, 0x0EA, 0x0DC, 0x048, 0x03E, 0x06D, 0x0CA, 0x07B, 0x052, 0x0E1, 0x0A4,
    0x08E, 0x0AB, 0x005, 0x0A7, 0x097, 0x0B9, 0x060, 0x039, 0x085, 0x07C, 0x056,
    0x07A, 0x0BA, 0x068, 0x06E, 0x0F5, 0x05D, 0x002, 0x04E, 0x00F, 0x0A1, 0x027,
    0x024, 0x041, 0x034, 0x000, 0x05A, 0x0FE, 0x0CB, 0x0D0, 0x0FA, 0x0F8, 0x06C,
    0x074, 0x096, 0x09E, 0x00E, 0x0C2, 0x049, 0x0E3, 0x0E5, 0x0C0, 0x03B, 0x059,
    0x018, 0x0A9, 0x086, 0x08F, 0x030, 0x0C3, 0x0A8, 0x022, 0x00A, 0x014, 0x01A,
    0x0B2, 0x0C9, 0x0C7, 0x0ED, 0x0AA, 0x029, 0x094, 0x075, 0x00D, 0x0AC, 0x00C,
    0x0F4, 0x0BB, 0x0C5, 0x03F, 0x0FD, 0x0D9, 0x09C, 0x04F, 0x0D5, 0x084, 0x01E,
    0x0B1, 0x081, 0x069, 0x0B4, 0x009, 0x0B8, 0x03C, 0x0AF, 0x0A3, 0x008, 0x0BF,
    0x0E0, 0x09A, 0x0D7, 0x0F7, 0x08C, 0x067, 0x066, 0x0AE, 0x0D4, 0x04C, 0x0A5,
    0x0EC, 0x0F9, 0x0B6, 0x064, 0x078, 0x006, 0x05B, 0x09B, 0x0F2, 0x099, 0x0CE,
    0x0DB, 0x053, 0x055, 0x065, 0x08D, 0x007, 0x033, 0x004, 0x037, 0x092, 0x026,
    0x023, 0x0B5, 0x058, 0x0DA, 0x02F, 0x0B3, 0x040, 0x05E, 0x07F, 0x04B, 0x062,
    0x080, 0x0E4, 0x06F, 0x073, 0x01D, 0x0DF, 0x017, 0x0CC, 0x028, 0x025, 0x02D,
    0x0EE, 0x03A, 0x098, 0x0E2, 0x001, 0x0EB, 0x0DD, 0x0BC, 0x090, 0x0B0, 0x0FC,
    0x095, 0x076, 0x093, 0x046, 0x057, 0x02C, 0x02B, 0x050, 0x011, 0x00B, 0x0C1,
    0x0F0, 0x0E7, 0x0D6, 0x021, 0x031, 0x0DE, 0x0FF, 0x0D8, 0x012, 0x0A6, 0x04D,
    0x08A, 0x013, 0x043, 0x045, 0x038, 0x0D2, 0x087, 0x0A0, 0x0EF, 0x082, 0x0F1,
    0x047, 0x089, 0x06A, 0x0C8, 0x054, 0x01B, 0x016, 0x07E, 0x079, 0x0BD, 0x06B,
    0x091, 0x0A2, 0x071, 0x036, 0x0B7, 0x003, 0x03D, 0x072, 0x0C6, 0x044, 0x08B,
    0x0CF, 0x015, 0x09F, 0x032, 0x0C4, 0x077, 0x083, 0x063, 0x020, 0x088, 0x0F6,
    0x0AD, 0x0F3, 0x0E8, 0x04A, 0x0E9, 0x035, 0x01C, 0x05F, 0x019, 0x01F, 0x07D,
    0x070, 0x0FB, 0x0D1, 0x051, 0x010, 0x0D3, 0x02E, 0x061, 0x09D, 0x05C, 0x02A,
    0x042, 0x0BE, 0x0E6};

namespace {
struct Tables {
  uint8_t enc[256];
  uint8_t dec[256];
  Tables() {
    for (int i = 0; i < 256; i++) enc[i] = (uint8_t)(Table_0[i] ^ 8);
    for (int i = 0; i < 256; i++) dec[enc[i]] = (uint8_t)i;
  }
};
const Tables &tables() {
  static Tables t;
  return t;
}
}  // namespace

uint8_t decode_byte(uint8_t b) { return tables().dec[b]; }
uint8_t encode_byte(uint8_t b) { return tables().enc[b]; }

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static std::string lower(std::string s) {
  for (auto &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

static std::vector<std::string> split(const std::string &s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

static double normalize_lon(double lon) {
  while (lon >= 180.0) lon -= 360.0;
  while (lon < -180.0) lon += 360.0;
  return lon;
}

std::string find_file_ci(const std::string &dir, const std::string &name) {
  std::error_code ec;
  fs::path p = fs::u8path(dir) / fs::u8path(name);
  if (fs::is_regular_file(p, ec)) return p.u8string();
  const std::string want = lower(name);
  for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end;
       it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    if (lower(it->path().filename().u8string()) == want)
      return it->path().u8string();
  }
  return "";
}

std::string find_dir_ci(const std::string &dir, const std::string &name) {
  std::error_code ec;
  fs::path p = fs::u8path(dir) / fs::u8path(name);
  if (fs::is_directory(p, ec)) return p.u8string();
  const std::string want = lower(name);
  for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end;
       it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    if (lower(it->path().filename().u8string()) == want)
      return it->path().u8string();
  }
  return "";
}

static bool read_text_lines(const std::string &path,
                            std::vector<std::string> &lines) {
  std::ifstream f(fs::u8path(path), std::ios::binary);
  if (!f) return false;
  std::string line;
  while (std::getline(f, line)) lines.push_back(line);
  return true;
}

// ---------------------------------------------------------------------------
// Feature
// ---------------------------------------------------------------------------
bool Feature::getNum(const char *name, double &v) const {
  auto it = attrs.find(name);
  if (it == attrs.end() || !it->second.hasNum) return false;
  v = it->second.num;
  return true;
}

// ---------------------------------------------------------------------------
// Dictionary
// ---------------------------------------------------------------------------
static char attr_type_from_token(const std::string &tok) {
  if (tok == "aFLOAT") return 'R';
  if (tok == "aBYTE") return 'B';
  if (tok == "aSTRING") return 'S';
  if (tok == "aCMPLX") return 'C';
  if (tok == "aLIST") return 'L';
  if (tok == "aWORD10") return 'W';
  if (tok == "aLONG") return 'G';
  return '?';
}

bool Dictionary::load(const std::string &dir, std::string *err) {
  m_ok = false;
  m_class.clear();
  m_attr.clear();
  m_attrType.clear();

  std::string objFile = find_file_ci(dir, "CM93OBJ.DIC");
  if (objFile.empty()) {
    if (err) *err = "CM93OBJ.DIC not found in " + dir;
    return false;
  }
  std::vector<std::string> lines;
  if (!read_text_lines(objFile, lines) || lines.empty()) {
    if (err) *err = "cannot read " + objFile;
    return false;
  }
  for (const auto &l : lines) {
    auto tok = split(l, '|');
    if (tok.size() < 2) continue;
    int iclass = std::atoi(tok[1].c_str());
    if (iclass < 0 || iclass > 4095) continue;
    if ((int)m_class.size() <= iclass) m_class.resize(iclass + 1, "NULLNM");
    m_class[iclass] = trim(tok[0]);
  }

  // Attribute dictionary: ATTRLUT.DIC (6 fields) preferred, else CM93ATTR.DIC
  std::string attrFile = find_file_ci(dir, "ATTRLUT.DIC");
  bool lut = !attrFile.empty();
  if (!lut) attrFile = find_file_ci(dir, "CM93ATTR.DIC");
  if (attrFile.empty()) {
    if (err) *err = "ATTRLUT.DIC / CM93ATTR.DIC not found in " + dir;
    return false;
  }
  lines.clear();
  if (!read_text_lines(attrFile, lines)) {
    if (err) *err = "cannot read " + attrFile;
    return false;
  }
  for (const auto &l0 : lines) {
    std::string l = l0;
    if (l.empty() || l[0] == ';') continue;
    auto tok = split(l, '|');
    size_t typeField = lut ? 5 : 2;
    if (tok.size() < 2) continue;
    int iattr = std::atoi(tok[1].c_str());
    if (iattr < 0 || iattr > 255) continue;  // attribute ids are one byte
    if ((int)m_attr.size() <= iattr) {
      m_attr.resize(iattr + 1, "NULLNM");
      m_attrType.resize(iattr + 1, '?');
    }
    m_attr[iattr] = trim(tok[0]);
    m_attrType[iattr] =
        tok.size() > typeField ? attr_type_from_token(trim(tok[typeField])) : '?';
  }

  m_dir = dir;
  m_ok = !m_class.empty() && !m_attr.empty();
  if (!m_ok && err) *err = "dictionary files are empty";
  return m_ok;
}

bool Dictionary::find_and_load(const std::string &root, int maxDepth,
                               std::string *err) {
  if (load(root, nullptr)) return true;
  // Breadth-limited recursive search for cm93obj.dic
  std::vector<std::pair<fs::path, int>> stack{{fs::u8path(root), 0}};
  std::error_code ec;
  while (!stack.empty()) {
    auto [p, depth] = stack.back();
    stack.pop_back();
    for (fs::directory_iterator it(p, ec), end; !ec && it != end;
         it.increment(ec)) {
      if (it->is_regular_file(ec) &&
          lower(it->path().filename().u8string()) == "cm93obj.dic") {
        if (load(p.u8string(), err)) return true;
      } else if (depth < maxDepth && it->is_directory(ec)) {
        // CM93 cell directories are 8-digit names holding thousands of
        // files; the dictionary never lives there, so skip them.
        std::string n = it->path().filename().u8string();
        bool digits = n.size() == 8 &&
                      std::all_of(n.begin(), n.end(), [](char c) {
                        return std::isdigit((unsigned char)c);
                      });
        if (!digits) stack.push_back({it->path(), depth + 1});
      }
    }
  }
  if (err && err->empty()) *err = "CM93 dictionary (CM93OBJ.DIC) not found under " + root;
  return false;
}

std::string Dictionary::className(int i) const {
  if (i < 0 || i >= (int)m_class.size()) return "Unknown";
  return m_class[i];
}
std::string Dictionary::attrName(int i) const {
  if (i < 0 || i >= (int)m_attr.size()) return "UnknownAttr";
  return m_attr[i];
}
char Dictionary::attrType(int i) const {
  if (i < 0 || i >= (int)m_attrType.size()) return '?';
  return m_attrType[i];
}

void Dictionary::setForTest(
    const std::vector<std::string> &classes,
    const std::vector<std::pair<std::string, char>> &attrs) {
  m_class = classes;
  m_attr.clear();
  m_attrType.clear();
  for (auto &a : attrs) {
    m_attr.push_back(a.first);
    m_attrType.push_back(a.second);
  }
  m_ok = true;
  m_dir = "<test>";
}

// ---------------------------------------------------------------------------
// Cell indexing (OpenCPN Get_CM93_CellIndex / Get_CM93_Cell_Origin)
// ---------------------------------------------------------------------------
int cell_index(double lat, double lon, int scaleIdx) {
  int dval = kScales[scaleIdx].dval;

  double lon1 = (lon + 360.) * 3.;  // basic cell size is 20 minutes
  while (lon1 >= 1080.0) lon1 -= 1080.0;
  while (lon1 < 0.0) lon1 += 1080.0;
  unsigned short lon2 = (unsigned short)std::floor(lon1 / dval);
  unsigned short lon3 = (unsigned short)(lon2 * dval);

  double lat1 = (lat * 3.) + 270. - 30;
  unsigned short lat2 = (unsigned short)std::floor(lat1 / dval);
  unsigned short lat3 = (unsigned short)(lat2 * dval);

  return (int)lon3 + ((int)lat3 + 30) * 10000;
}

void cell_origin(int cellIndex, double &lat, double &lon) {
  lon = (cellIndex % 10000) / 3.;
  lat = ((cellIndex / 10000) - 270.) / 3.;
}

double cell_size_deg(int scaleIdx) { return kScales[scaleIdx].dval / 3.0; }

CellPathParts cell_path_parts(int cellIndex, int scaleIdx) {
  int dval = kScales[scaleIdx].dval;
  int ilat = cellIndex / 10000;
  int ilon = cellIndex % 10000;
  int jlat = (((ilat - 30) / dval) * dval) + 30;
  int jlon = (ilon / dval) * dval;
  int ilatroot = (((ilat - 30) / 60) * 60) + 30;
  int ilonroot = (ilon / 60) * 60;

  char buf[32];
  CellPathParts p;
  std::snprintf(buf, sizeof buf, "%04d%04d", ilatroot, ilonroot);
  p.rootDir = buf;
  // OpenCPN prints "%04d%04d" then overwrites the first character with the
  // subcell letter, so only the last three digits of jlat survive.
  std::snprintf(buf, sizeof buf, "%04d%04d", jlat, jlon);
  p.baseName = std::string(buf + 1);
  return p;
}

// ---------------------------------------------------------------------------
// File reading (+ optional xz)
// ---------------------------------------------------------------------------
bool read_file(const std::string &path, std::vector<uint8_t> &data,
               std::string *err) {
  std::ifstream f(fs::u8path(path), std::ios::binary);
  if (!f) {
    if (err) *err = "cannot open " + path;
    return false;
  }
  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
  bool isXz = path.size() > 3 && lower(path.substr(path.size() - 3)) == ".xz";
  if (!isXz) {
    data.swap(raw);
    return true;
  }
#ifdef HAVE_LZMA
  lzma_stream strm = LZMA_STREAM_INIT;
  if (lzma_stream_decoder(&strm, UINT64_MAX, 0) != LZMA_OK) {
    if (err) *err = "lzma init failed";
    return false;
  }
  data.clear();
  std::vector<uint8_t> buf(1 << 16);
  strm.next_in = raw.data();
  strm.avail_in = raw.size();
  lzma_ret ret;
  do {
    strm.next_out = buf.data();
    strm.avail_out = buf.size();
    ret = lzma_code(&strm, LZMA_FINISH);
    data.insert(data.end(), buf.data(), buf.data() + (buf.size() - strm.avail_out));
  } while (ret == LZMA_OK);
  lzma_end(&strm);
  if (ret != LZMA_STREAM_END) {
    if (err) *err = "xz decompression failed for " + path;
    return false;
  }
  return true;
#else
  if (err) *err = "built without liblzma, cannot read " + path;
  return false;
#endif
}

// ---------------------------------------------------------------------------
// Binary parsing of a decoded cell image
// ---------------------------------------------------------------------------
namespace {

class Reader {
public:
  Reader(const uint8_t *p, size_t n) : m_p(p), m_n(n) {}
  bool ok() const { return m_ok; }
  size_t pos() const { return m_pos; }
  size_t size() const { return m_n; }
  bool has(size_t n) const { return m_ok && m_pos + n <= m_n; }

  template <typename T>
  T get() {
    T v{};
    if (!has(sizeof(T))) {
      m_ok = false;
      return v;
    }
    std::memcpy(&v, m_p + m_pos, sizeof(T));  // little endian, as CM93 is
    m_pos += sizeof(T);
    return v;
  }
  const uint8_t *take(size_t n) {
    if (!has(n)) {
      m_ok = false;
      return nullptr;
    }
    const uint8_t *r = m_p + m_pos;
    m_pos += n;
    return r;
  }

private:
  const uint8_t *m_p;
  size_t m_n;
  size_t m_pos = 0;
  bool m_ok = true;
};

struct Pt2 {
  uint16_t x, y;
};
struct Pt3 {
  uint16_t x, y, z;
};
struct Edge {
  uint32_t first;  // index into edge point pool
  uint16_t n;
};
struct Run3 {
  uint32_t first;
  uint16_t n;
};
struct SegRef {
  uint16_t edge;
  uint8_t usage;  // bit 2 (value 4) = traverse backwards
};
struct RawFeature {
  uint8_t otype = 0;
  uint8_t geotype = 0;
  std::vector<SegRef> segs;  // area / line
  int pointIndex = -1;       // 2D point or 3D record index
  const uint8_t *attrBlock = nullptr;
  int attrLen = 0;
  int nattr = 0;
};

struct Transform {
  double xRate, yRate, xOrigin, yOrigin, offE, offN;
  LatLon operator()(uint16_t x, uint16_t y) const {
    double valx = x * xRate + xOrigin - offE;
    double valy = y * yRate + yOrigin - offN;
    LatLon ll;
    ll.lat = (2.0 * std::atan(std::exp(valy / kCM93SemiMajor)) - kPi / 2.) / kDeg;
    ll.lon = normalize_lon(valx / (kDeg * kCM93SemiMajor));
    return ll;
  }
};

void parse_attrs(const RawFeature &rf, const Dictionary &dict, Feature &f) {
  const uint8_t *b = rf.attrBlock;
  int n = rf.attrLen;
  int c = 0;
  for (int j = 0; j < rf.nattr && c < n; j++) {
    int iattr = b[c++];
    char t = dict.attrType(iattr);
    AttrValue v;
    v.type = t;
    auto need = [&](int k) { return c + k <= n; };
    switch (t) {
      case 'B':
        if (!need(1)) return;
        v.hasNum = true;
        v.num = b[c];
        c += 1;
        break;
      case 'I': {
        if (!need(2)) return;
        int16_t s;
        std::memcpy(&s, b + c, 2);
        v.hasNum = true;
        v.num = s;
        c += 2;
        break;
      }
      case 'W': {
        if (!need(2)) return;
        uint16_t w;
        std::memcpy(&w, b + c, 2);
        v.hasNum = true;
        v.num = w / 10.0;
        c += 2;
        break;
      }
      case 'G': {
        if (!need(4)) return;
        int32_t g;
        std::memcpy(&g, b + c, 4);
        v.hasNum = true;
        v.num = g;
        c += 4;
        break;
      }
      case 'R': {
        if (!need(4)) return;
        float r;
        std::memcpy(&r, b + c, 4);
        v.hasNum = std::isfinite(r);
        v.num = r;
        c += 4;
        break;
      }
      case 'S': {
        int s = c;
        while (c < n && b[c]) c++;
        v.str.assign((const char *)b + s, c - s);
        c++;  // terminator
        char *end = nullptr;
        double d = std::strtod(v.str.c_str(), &end);
        if (end && end != v.str.c_str()) {
          v.hasNum = true;
          v.num = d;
        }
        break;
      }
      case 'C': {
        if (!need(3)) return;
        c += 3;
        int s = c;
        while (c < n && b[c]) c++;
        v.str.assign((const char *)b + s, c - s);
        c++;
        break;
      }
      case 'L': {
        if (!need(1)) return;
        int nl = b[c++];
        if (!need(nl)) return;
        for (int i = 0; i < nl; i++) {
          if (i) v.str += ",";
          v.str += std::to_string(b[c + i]);
        }
        c += nl;
        break;
      }
      default:
        // Unknown value width: the rest of this block cannot be parsed.
        return;
    }
    f.attrs[dict.attrName(iattr)] = v;
  }
}

std::string substitute_class(const std::string &cls) {
  if (cls == "ITDARE") return "DEPARE";  // intertidal area
  if (cls == "_m_sor") return "M_COVR";
  if (cls == "SPOGRD") return "DMPGRD";
  if (cls == "FSHHAV") return "FSHFAC";
  if (cls == "OFSPRD") return "CTNARE";
  return cls;
}

void grow_bbox(Feature &f, const LatLon &p) {
  f.minLat = std::min(f.minLat, p.lat);
  f.maxLat = std::max(f.maxLat, p.lat);
  f.minLon = std::min(f.minLon, p.lon);
  f.maxLon = std::max(f.maxLon, p.lon);
}

}  // namespace

bool decode_cell_buffer(const std::vector<uint8_t> &rawIn,
                        const Dictionary &dict, const DecodeOptions &opt,
                        std::vector<Feature> &out, CellHeader *hdrOut,
                        std::string *err) {
  auto fail = [&](const char *m) {
    if (err) *err = m;
    return false;
  };
  if (rawIn.size() < 10 + 128) return fail("cell file too short");

  std::vector<uint8_t> d(rawIn.size());
  for (size_t i = 0; i < rawIn.size(); i++) d[i] = decode_byte(rawIn[i]);

  Reader r(d.data(), d.size());

  // Prolog: lengths must add up to the file size
  uint16_t word0 = r.get<uint16_t>();
  int32_t int0 = r.get<int32_t>();
  int32_t int1 = r.get<int32_t>();
  if ((int64_t)word0 + int0 + int1 != (int64_t)d.size())
    return fail("cell file length check failed (corrupt or not CM93)");

  // 128-byte header
  double lonMin = r.get<double>(), latMin = r.get<double>();
  double lonMax = r.get<double>(), latMax = r.get<double>();
  double eMin = r.get<double>(), nMin = r.get<double>();
  double eMax = r.get<double>(), nMax = r.get<double>();
  uint16_t nVec = r.get<uint16_t>();
  int32_t nVecPts = r.get<int32_t>();
  r.get<int32_t>();  // m_46
  r.get<int32_t>();  // m_4a
  uint16_t n3d = r.get<uint16_t>();
  int32_t n3dPts = r.get<int32_t>();  // m_50
  r.get<int32_t>();                   // m_54
  uint16_t n2d = r.get<uint16_t>();
  r.get<uint16_t>();  // m_5a
  r.get<uint16_t>();  // m_5c
  uint16_t nFeat = r.get<uint16_t>();
  r.get<int32_t>();   // m_60
  r.get<int32_t>();   // m_64
  r.get<uint16_t>();  // m_68
  r.get<uint16_t>();  // m_6a
  r.get<uint16_t>();  // m_6c
  r.get<int32_t>();   // related object pointers
  r.get<int32_t>();   // m_72
  r.get<uint16_t>();  // m_76
  r.get<int32_t>();   // m_78 attribute bytes
  r.get<int32_t>();   // m_7c
  if (!r.ok()) return fail("truncated header");
  (void)nVecPts;
  (void)n3dPts;

  if (hdrOut) {
    hdrOut->lonMin = lonMin;
    hdrOut->latMin = latMin;
    hdrOut->lonMax = lonMax;
    hdrOut->latMax = latMax;
    hdrOut->nFeatures = nFeat;
  }

  Transform tf;
  double dx = eMax - eMin;
  if (dx < 0) dx += kCM93SemiMajor * 2.0 * kPi;  // one trip around
  tf.xRate = dx / 65535;
  tf.yRate = (nMax - nMin) / 65535;
  tf.xOrigin = eMin;
  tf.yOrigin = nMin;
  tf.offE = opt.offsetE;
  tf.offN = opt.offsetN;

  // Edge (vector record) table
  std::vector<Pt2> edgePts;
  std::vector<Edge> edges(nVec);
  for (int i = 0; i < nVec; i++) {
    uint16_t np = r.get<uint16_t>();
    if (!r.has((size_t)np * 4)) return fail("truncated edge table");
    edges[i].first = (uint32_t)edgePts.size();
    edges[i].n = np;
    for (int k = 0; k < np; k++) {
      Pt2 p;
      p.x = r.get<uint16_t>();
      p.y = r.get<uint16_t>();
      edgePts.push_back(p);
    }
  }

  // 3-D point table (soundings)
  std::vector<Pt3> pts3;
  std::vector<Run3> runs3(n3d);
  for (int i = 0; i < n3d; i++) {
    uint16_t np = r.get<uint16_t>();
    if (!r.has((size_t)np * 6)) return fail("truncated 3D point table");
    runs3[i].first = (uint32_t)pts3.size();
    runs3[i].n = np;
    for (int k = 0; k < np; k++) {
      Pt3 p;
      p.x = r.get<uint16_t>();
      p.y = r.get<uint16_t>();
      p.z = r.get<uint16_t>();
      pts3.push_back(p);
    }
  }

  // The same table as OpenCPN's read_3dpoint_table() leaves it: record i
  // is written starting at slot i (its pointer moves one point per record),
  // so later records overwrite the tails of earlier ones, and record i is
  // read back from slots i .. i+n-1.
  std::vector<Pt3> pts3ocpn(pts3.size());
  for (int i = 0; i < n3d; i++)
    for (int k = 0; k < runs3[i].n; k++)
      if ((size_t)i + k < pts3ocpn.size()) pts3ocpn[i + k] = pts3[runs3[i].first + k];

  // 2-D point table
  std::vector<Pt2> pts2(n2d);
  for (int i = 0; i < n2d; i++) {
    pts2[i].x = r.get<uint16_t>();
    pts2[i].y = r.get<uint16_t>();
  }
  if (!r.ok()) return fail("truncated point tables");

  // Feature table
  std::vector<RawFeature> raws(nFeat);
  for (int io = 0; io < nFeat; io++) {
    RawFeature &rf = raws[io];
    rf.otype = r.get<uint8_t>();
    rf.geotype = r.get<uint8_t>();
    int descBytes = r.get<uint16_t>();
    if (!r.ok()) return fail("truncated feature table");

    switch (rf.geotype & 0x0f) {
      case 4:    // area
      case 2: {  // line
        uint16_t ne = r.get<uint16_t>();
        descBytes -= ne * 2 + 2;
        for (int k = 0; k < ne; k++) {
          uint16_t idx = r.get<uint16_t>();
          // OpenCPN rejects the whole cell only for index > count; an index
          // equal to the count points past the table, so just skip that
          // edge instead of dropping every feature in the cell.
          if ((idx & 0x1fff) > nVec) return fail("bad edge reference");
          if ((idx & 0x1fff) == nVec) continue;
          rf.segs.push_back({(uint16_t)(idx & 0x1fff), (uint8_t)(idx >> 13)});
        }
        break;
      }
      case 1:
      case 8: {
        rf.pointIndex = r.get<uint16_t>();
        descBytes -= 2;
        break;
      }
      default:
        break;
    }
    if (rf.geotype & 0x10) {  // related (child) objects
      uint8_t nrel = r.get<uint8_t>();
      descBytes -= nrel * 2 + 1;
      for (int k = 0; k < nrel; k++) r.get<uint16_t>();  // not needed here
    }
    if (rf.geotype & 0x20) {
      r.get<uint16_t>();
      descBytes -= 2;
    }
    if (rf.geotype & 0x80) {  // attributes
      rf.nattr = r.get<uint8_t>();
      descBytes -= 5;  // 4-byte record header + attribute count byte
      if (descBytes < 0) return fail("bad attribute block length");
      rf.attrLen = descBytes;
      rf.attrBlock = r.take((size_t)descBytes);
    }
    if (!r.ok()) return fail("truncated feature record");
  }

  // Build output features
  for (int io = 0; io < nFeat; io++) {
    const RawFeature &rf = raws[io];
    std::string rawCls = dict.className(rf.otype);
    std::string cls = substitute_class(rawCls);
    if (!opt.keepClasses.empty() && !opt.keepClasses.count(cls)) continue;

    Feature f;
    f.cls = cls;
    f.cm93Cls = rawCls;
    int g = rf.geotype & 0x0f;

    if (g == 4) {
      // Ring assembly exactly as OpenCPN cm93chart::BuildGeom()
      f.geom = Geom::Area;
      std::vector<LatLon> ring;
      bool newRing = true;
      Pt2 start{0, 0};
      for (const SegRef &s : rf.segs) {
        const Edge &e = edges[s.edge];
        if (e.n == 0) continue;
        const Pt2 *ep = &edgePts[e.first];
        bool back = (s.usage & 4) != 0;
        if (newRing) {
          newRing = false;
          start = back ? ep[e.n - 1] : ep[0];
        }
        Pt2 curEnd = back ? ep[0] : ep[e.n - 1];
        for (int k = 0; k < e.n; k++) {
          const Pt2 &p = back ? ep[e.n - 1 - k] : ep[k];
          ring.push_back(tf(p.x, p.y));
        }
        if (curEnd.x == start.x && curEnd.y == start.y) {
          // ring closed: keep the closure point
          for (auto &ll : ring) grow_bbox(f, ll);
          if (ring.size() >= 4) f.parts.push_back(std::move(ring));
          ring.clear();
          newRing = true;
        } else {
          ring.pop_back();  // next edge starts with this same node
        }
      }
      if (ring.size() >= 3) {  // unterminated ring: close it, don't drop it
        ring.push_back(ring.front());
        for (auto &ll : ring) grow_bbox(f, ll);
        f.parts.push_back(std::move(ring));
      }
    } else if (g == 2) {
      f.geom = Geom::Line;
      for (const SegRef &s : rf.segs) {
        const Edge &e = edges[s.edge];
        bool back = (s.usage & 4) != 0;
        std::vector<LatLon> part;
        for (int k = 0; k < e.n; k++) {
          const Pt2 &p = edgePts[e.first + (back ? e.n - 1 - k : k)];
          part.push_back(tf(p.x, p.y));
          grow_bbox(f, part.back());
        }
        if (part.size() >= 2) f.parts.push_back(std::move(part));
      }
    } else if (g == 1) {
      f.geom = Geom::Point;
      if (rf.pointIndex < 0 || rf.pointIndex >= (int)pts2.size()) continue;
      const Pt2 &p = pts2[rf.pointIndex];
      f.points.push_back(tf(p.x, p.y));
      grow_bbox(f, f.points.back());
    } else if (g == 8) {
      f.geom = Geom::MultiPoint3D;
      if (rf.pointIndex < 0 || rf.pointIndex >= (int)runs3.size()) continue;
      const Run3 &run = runs3[rf.pointIndex];
      const std::vector<Pt3> &pool = opt.openCpnSoundings ? pts3ocpn : pts3;
      const uint32_t first = opt.openCpnSoundings ? (uint32_t)rf.pointIndex : run.first;
      for (int k = 0; k < run.n; k++) {
        if (first + k >= pool.size()) break;
        const Pt3 &p = pool[first + k];
        f.points.push_back(tf(p.x, p.y));
        grow_bbox(f, f.points.back());
        // OpenCPN's "magic number": values >= 12000 are whole metres + 12000
        f.z.push_back(p.z >= 12000 ? double(p.z - 12000) : p.z / 10.0);
      }
    } else {
      continue;  // parent objects without geometry are of no use here
    }

    if (rf.attrBlock) parse_attrs(rf, dict, f);
    out.push_back(std::move(f));
  }
  return true;
}

// ---------------------------------------------------------------------------
// Locating and loading cells
// ---------------------------------------------------------------------------
std::string find_cell_file(const std::string &root, int cellIndex,
                           int scaleIdx, char subcell) {
  CellPathParts pp = cell_path_parts(cellIndex, scaleIdx);
  std::string rootDir = find_dir_ci(root, pp.rootDir);
  if (rootDir.empty()) return "";

  const char up = kScales[scaleIdx].ch;
  const char lo = (char)std::tolower((unsigned char)up);
  std::string scaleDir = find_dir_ci(rootDir, std::string(1, up));
  if (scaleDir.empty()) return "";

  std::string stem = std::string(1, subcell) + pp.baseName + ".";
  for (char ext : {up, lo}) {
    std::string name = stem + ext;
    std::error_code ec;
    fs::path p = fs::u8path(scaleDir) / fs::u8path(name);
    if (fs::is_regular_file(p, ec)) return p.u8string();
    fs::path px = fs::u8path(scaleDir) / fs::u8path(name + ".xz");
    if (fs::is_regular_file(px, ec)) return px.u8string();
  }
  // Fall back to a case-insensitive scan (e.g. lower-case subcell letters)
  std::string f = find_file_ci(scaleDir, stem + up);
  if (!f.empty()) return f;
  f = find_file_ci(scaleDir, stem + up + std::string(".xz"));
  return f;
}

bool load_cell(const std::string &root, const Dictionary &dict, int cellIndex,
               int scaleIdx, const DecodeOptions &opt, CellData &out,
               std::string *err) {
  out = CellData();
  out.cellIndex = cellIndex;
  out.scaleIdx = scaleIdx;
  out.ch = kScales[scaleIdx].ch;
  cell_origin(cellIndex, out.swLat, out.swLon);
  out.swLon = normalize_lon(out.swLon);
  out.sizeDeg = cell_size_deg(scaleIdx);

  std::string firstErr;
  // Base cell '0', then subcells 'A', 'B', ... until one is missing. As in
  // OpenCPN, a missing base cell does not stop the subcells being read.
  for (char sub = '0';; sub = (sub == '0') ? 'A' : (char)(sub + 1)) {
    std::string path = find_cell_file(root, cellIndex, scaleIdx, sub);
    if (path.empty()) {
      if (sub == '0') continue;  // try 'A' onwards
      break;
    }
    std::vector<uint8_t> raw;
    std::string e;
    if (read_file(path, raw, &e) &&
        decode_cell_buffer(raw, dict, opt, out.features, nullptr, &e)) {
      out.filesLoaded++;
      out.files.push_back(path);
    } else if (firstErr.empty()) {
      firstErr = path + ": " + e;
    }
    if (sub == 'Z') break;
  }
  out.readError = firstErr;
  if (out.filesLoaded == 0) {
    if (err) *err = firstErr.empty() ? "cell not present" : firstErr;
    return false;
  }
  return true;
}

bool looks_like_cm93_root(const std::string &dir) {
  std::error_code ec;
  if (!fs::is_directory(fs::u8path(dir), ec)) return false;
  int nCellDirs = 0;
  for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end;
       it.increment(ec)) {
    std::string n = it->path().filename().u8string();
    if (it->is_directory(ec) && n.size() == 8 &&
        std::all_of(n.begin(), n.end(),
                    [](char c) { return std::isdigit((unsigned char)c); }))
      nCellDirs++;
    if (nCellDirs > 0) return true;
  }
  return false;
}

std::string find_cm93_root(const std::string &dir, int maxDepth) {
  if (looks_like_cm93_root(dir)) return dir;
  if (maxDepth <= 0) return "";
  std::error_code ec;
  for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end;
       it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    std::string r = find_cm93_root(it->path().u8string(), maxDepth - 1);
    if (!r.empty()) return r;
  }
  return "";
}

}  // namespace cm93
