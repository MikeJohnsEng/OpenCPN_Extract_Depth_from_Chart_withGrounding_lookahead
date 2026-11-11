/***************************************************************************
 * cm93depth_pi - CM93 / S-57 least-depth plugin for OpenCPN
 *
 * s57_reader.cpp : ISO 8211 + S-57 ENC reader (see s57_reader.h)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "s57_reader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace cm93 {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Catalogue lookups
// ---------------------------------------------------------------------------
static const S57Code *find_code(const S57Code *t, size_t n, int code) {
  size_t lo = 0, hi = n;  // tables are sorted by code
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (t[mid].code < code) lo = mid + 1;
    else hi = mid;
  }
  return (lo < n && t[lo].code == code) ? &t[lo] : nullptr;
}
const S57Code *s57_object(int code) { return find_code(kS57Objects, kS57ObjectCount, code); }
const S57Code *s57_attribute(int code) {
  return find_code(kS57Attributes, kS57AttributeCount, code);
}
int s57_object_code(const std::string &a) {
  for (size_t i = 0; i < kS57ObjectCount; i++)
    if (a == kS57Objects[i].acronym) return kS57Objects[i].code;
  return -1;
}
int s57_attribute_code(const std::string &a) {
  for (size_t i = 0; i < kS57AttributeCount; i++)
    if (a == kS57Attributes[i].acronym) return kS57Attributes[i].code;
  return -1;
}

// ---------------------------------------------------------------------------
// ISO 8211
// ---------------------------------------------------------------------------
namespace iso8211 {

static const uint8_t kUT = 0x1f;  // unit (subfield) terminator
static const uint8_t kFT = 0x1e;  // field terminator

int FieldDefn::index(const std::string &sub) const {
  for (size_t i = 0; i < subs.size(); i++)
    if (subs[i].name == sub) return (int)i;
  return -1;
}

const Value *Field::get(const char *sub, size_t i) const {
  if (!defn || i >= inst.size()) return nullptr;
  int k = defn->index(sub);
  if (k < 0 || k >= (int)inst[i].size()) return nullptr;
  return &inst[i][k];
}
long long Field::getInt(const char *sub, size_t i, long long dflt) const {
  const Value *v = get(sub, i);
  return (v && v->isNum) ? (long long)v->num : dflt;
}
double Field::getNum(const char *sub, size_t i, double dflt) const {
  const Value *v = get(sub, i);
  return (v && v->isNum) ? v->num : dflt;
}
std::string Field::getStr(const char *sub, size_t i) const {
  const Value *v = get(sub, i);
  return v ? v->str : std::string();
}

const Field *Record::find(const char *tag) const {
  for (const Field &f : fields)
    if (f.tag == tag) return &f;
  return nullptr;
}

static int num_at(const uint8_t *p, int n) {  // ASCII digits, -1 if not digits
  int v = 0;
  for (int i = 0; i < n; i++) {
    if (p[i] == ' ') continue;
    if (p[i] < '0' || p[i] > '9') return -1;
    v = v * 10 + (p[i] - '0');
  }
  return v;
}

// One format item ("b14", "A(8)", "B(40)", "R") -> (type, width)
static bool parse_item(const std::string &s, char &type, int &width) {
  if (s.empty()) return false;
  width = 0;
  char c = s[0];
  if (c == 'b') {
    if (s.size() < 3 || (s[1] != '1' && s[1] != '2')) return false;
    type = s[1] == '1' ? 'u' : 's';
    width = std::atoi(s.c_str() + 2);
    return width >= 1 && width <= 8;
  }
  if (c == 'B') {
    size_t a = s.find('('), b = s.find(')');
    if (a == std::string::npos || b == std::string::npos) return false;
    int bits = std::atoi(s.substr(a + 1, b - a - 1).c_str());
    type = 'B';
    width = (bits + 7) / 8;
    return width > 0;
  }
  if (c == 'A' || c == 'I' || c == 'R' || c == 'S' || c == 'C') {
    type = c;
    size_t a = s.find('(');
    if (a != std::string::npos) width = std::atoi(s.c_str() + a + 1);
    return true;
  }
  return false;
}

// Split at commas outside parentheses
static std::vector<std::string> split_top(const std::string &s) {
  std::vector<std::string> out;
  int depth = 0;
  std::string cur;
  for (char c : s) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if (c == ',' && depth == 0) {
      out.push_back(cur);
      cur.clear();
    } else if (c != ' ') {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

static bool expand_list(const std::string &s, std::vector<std::pair<char, int>> &out,
                        int depth) {
  if (depth > 8) return false;
  for (const std::string &item : split_top(s)) {
    size_t i = 0;
    while (i < item.size() && std::isdigit((unsigned char)item[i])) i++;
    int count = i > 0 ? std::atoi(item.substr(0, i).c_str()) : 1;
    std::string rest = item.substr(i);
    if (rest.empty() || count < 1 || count > 1000) return false;
    std::vector<std::pair<char, int>> one;
    if (rest[0] == '(') {
      if (rest.back() != ')') return false;
      if (!expand_list(rest.substr(1, rest.size() - 2), one, depth + 1)) return false;
    } else {
      char t;
      int w;
      if (!parse_item(rest, t, w)) return false;
      one.push_back({t, w});
    }
    for (int k = 0; k < count; k++) out.insert(out.end(), one.begin(), one.end());
  }
  return true;
}

bool expand_format(const std::string &fmt, std::vector<std::pair<char, int>> &out) {
  out.clear();
  std::string s = fmt;
  while (!s.empty() && s.back() == ' ') s.pop_back();
  if (s.size() >= 2 && s.front() == '(' && s.back() == ')') s = s.substr(1, s.size() - 2);
  if (s.empty()) return true;
  return expand_list(s, out, 0);
}

bool Module::open(const std::vector<uint8_t> &data, std::string *err) {
  m_data = &data;
  m_pos = 0;
  m_defs.clear();
  auto fail = [&](const char *m) {
    if (err) *err = m;
    return false;
  };
  if (data.size() < 24) return fail("file too short for ISO 8211");
  const uint8_t *L = data.data();
  int recLen = num_at(L, 5);
  if (L[6] != 'L') return fail("not an ISO 8211 file (no DDR)");
  int fcl = num_at(L + 10, 2);
  int fieldArea = num_at(L + 12, 5);
  int sLen = L[20] - '0', sPos = L[21] - '0', sTag = L[23] - '0';
  if (recLen < 24 || (size_t)recLen > data.size() || fieldArea < 24 || fieldArea > recLen ||
      sLen < 1 || sLen > 9 || sPos < 1 || sPos > 9 || sTag < 1 || sTag > 9 || fcl < 0)
    return fail("damaged ISO 8211 leader");
  const int entry = sLen + sPos + sTag;
  for (int d = 24; d + entry <= fieldArea && L[d] != kFT; d += entry) {
    std::string tag((const char *)L + d, sTag);
    int len = num_at(L + d + sTag, sLen);
    int pos = num_at(L + d + sTag + sLen, sPos);
    if (len < 0 || pos < 0 || fieldArea + pos + len > recLen) return fail("damaged DDR directory");
    if (tag == "0000") continue;
    const uint8_t *f = L + fieldArea + pos;
    const uint8_t *e = f + len;
    const uint8_t *p = f + std::min(fcl, len);
    std::vector<std::string> parts;
    std::string cur;
    for (; p < e; p++) {
      if (*p == kUT || *p == kFT) {
        parts.push_back(cur);
        cur.clear();
        if (*p == kFT) break;
      } else {
        cur += (char)*p;
      }
    }
    if (!cur.empty()) parts.push_back(cur);
    FieldDefn fd;
    fd.tag = tag;
    if (!parts.empty()) fd.name = parts[0];
    std::string desc = parts.size() > 1 ? parts[1] : "";
    std::string fmt = parts.size() > 2 ? parts[2] : "";
    if (!desc.empty() && desc[0] == '*') {
      fd.repeating = true;
      desc.erase(0, 1);
    }
    std::vector<std::string> names;
    if (!desc.empty()) {
      size_t a = 0;
      for (;;) {
        size_t b = desc.find('!', a);
        names.push_back(desc.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
      }
    }
    std::vector<std::pair<char, int>> items;
    if (!expand_format(fmt, items)) items.clear();
    if (!names.empty() && items.size() == names.size()) {
      int fixed = 0;
      bool allFixed = true;
      for (size_t i = 0; i < names.size(); i++) {
        fd.subs.push_back({names[i], items[i].first, items[i].second});
        if (items[i].second > 0) fixed += items[i].second;
        else allFixed = false;
      }
      fd.fixedWidth = allFixed ? fixed : 0;
    }
    m_defs[tag] = fd;
  }
  m_pos = recLen;
  return true;
}

const FieldDefn *Module::defn(const std::string &tag) const {
  auto it = m_defs.find(tag);
  return it == m_defs.end() ? nullptr : &it->second;
}

static Value convert(const Subfield &sf, const uint8_t *p, size_t n) {
  Value v;
  if (sf.type == 'u' || sf.type == 's') {
    uint64_t u = 0;
    for (size_t i = 0; i < n && i < 8; i++) u |= (uint64_t)p[i] << (8 * i);
    if (sf.type == 's' && n > 0 && n < 8 && (p[n - 1] & 0x80)) u |= ~0ULL << (8 * n);
    v.isNum = true;
    v.num = sf.type == 's' ? (double)(int64_t)u : (double)u;
    return v;
  }
  v.str.assign((const char *)p, n);
  if (sf.type == 'B') return v;
  if (sf.type == 'I' || sf.type == 'R') {
    std::string t = v.str;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    char *end = nullptr;
    double d = std::strtod(t.c_str(), &end);
    if (!t.empty() && end && *end == 0) {
      v.isNum = true;
      v.num = d;
    }
  }
  return v;
}

static void parse_field(const FieldDefn &fd, const uint8_t *p, size_t len, Field &out) {
  const uint8_t *end = p + len;
  if (end > p && end[-1] == kFT) end--;
  do {
    const uint8_t *start = p;
    std::vector<Value> vals;
    for (const Subfield &sf : fd.subs) {
      if (p >= end) {
        vals.push_back(Value());
        continue;
      }
      if (sf.width > 0) {
        size_t n = std::min<size_t>(sf.width, end - p);
        vals.push_back(convert(sf, p, n));
        p += n;
      } else {
        const uint8_t *d = p;
        while (d < end && *d != kUT && *d != kFT) d++;
        vals.push_back(convert(sf, p, d - p));
        p = d < end ? d + 1 : end;
      }
    }
    out.inst.push_back(std::move(vals));
    if (p == start) break;  // nothing consumed: avoid looping forever
  } while (fd.repeating && p < end);
}

bool Module::next(Record &rec, std::string *err) {
  rec.fields.clear();
  if (!m_data) return false;
  const std::vector<uint8_t> &data = *m_data;
  // trailing padding / end of file
  while (m_pos < data.size() && (data[m_pos] == ' ' || data[m_pos] == 0 || data[m_pos] == '\n'))
    m_pos++;
  if (m_pos + 24 > data.size()) return false;
  const uint8_t *L = data.data() + m_pos;
  int recLen = num_at(L, 5);
  int fieldArea = num_at(L + 12, 5);
  int sLen = L[20] - '0', sPos = L[21] - '0', sTag = L[23] - '0';
  if (L[6] == 'R') {
    if (err) *err = "ISO 8211 leader reuse ('R') is not supported";
    return false;
  }
  if (recLen < 24 || m_pos + recLen > data.size() || fieldArea < 24 || fieldArea > recLen ||
      sLen < 1 || sLen > 9 || sPos < 1 || sPos > 9 || sTag < 1 || sTag > 9) {
    if (err) *err = "damaged ISO 8211 record";
    return false;
  }
  const int entry = sLen + sPos + sTag;
  for (int d = 24; d + entry <= fieldArea && L[d] != kFT; d += entry) {
    std::string tag((const char *)L + d, sTag);
    int len = num_at(L + d + sTag, sLen);
    int pos = num_at(L + d + sTag + sLen, sPos);
    if (len < 0 || pos < 0 || fieldArea + pos + len > recLen) {
      if (err) *err = "damaged ISO 8211 record directory";
      return false;
    }
    if (tag == "0001") continue;
    const FieldDefn *fd = defn(tag);
    if (!fd || fd->subs.empty()) continue;
    Field f;
    f.defn = fd;
    f.tag = tag;
    parse_field(*fd, L + fieldArea + pos, len, f);
    rec.fields.push_back(std::move(f));
  }
  m_pos += recLen;
  return true;
}

}  // namespace iso8211

// ---------------------------------------------------------------------------
// S-57 records
// ---------------------------------------------------------------------------
namespace {

using iso8211::Field;
using iso8211::Record;

enum : int { RCNM_VI = 110, RCNM_VC = 120, RCNM_VE = 130, RCNM_VF = 140, RCNM_FE = 100 };

struct Ptr {
  int rcnm = 0;
  uint32_t rcid = 0;
  int ornt = 255, usag = 255, topi = 255, mask = 255;
};

struct Vec {
  int rcnm = 0;
  uint32_t rcid = 0;
  int rver = 0;
  bool is3d = false;
  std::vector<std::array<int64_t, 3>> c;  // raw y, x, z
  std::vector<Ptr> vrpt;
};

struct Feat {
  uint32_t rcid = 0;
  int prim = 255, objl = 0, rver = 0;
  std::vector<std::pair<int, std::string>> attf;  // ATTL, ATVL
  std::vector<Ptr> fspt;
};

struct Dataset {
  EncHeader hdr;
  std::map<uint64_t, Vec> vec;  // key (rcnm << 32) | rcid
  std::map<uint32_t, Feat> feat;
};

uint64_t vkey(int rcnm, uint32_t rcid) { return ((uint64_t)(uint32_t)rcnm << 32) | rcid; }

int to_int(const std::string &s, int dflt = 0) {
  const char *p = s.c_str();
  while (*p == ' ') p++;
  if (!*p) return dflt;
  return std::atoi(p);
}

void read_name(const iso8211::Value *v, int &rcnm, uint32_t &rcid) {
  rcnm = 0;
  rcid = 0;
  if (!v || v->str.size() < 5) return;
  const unsigned char *b = (const unsigned char *)v->str.data();
  rcnm = b[0];
  rcid = (uint32_t)b[1] | ((uint32_t)b[2] << 8) | ((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 24);
}

std::vector<Ptr> read_ptrs(const Field *f, bool vrpt) {
  std::vector<Ptr> out;
  if (!f) return out;
  for (size_t i = 0; i < f->inst.size(); i++) {
    Ptr p;
    read_name(f->get("NAME", i), p.rcnm, p.rcid);
    p.ornt = (int)f->getInt("ORNT", i, 255);
    p.usag = (int)f->getInt("USAG", i, 255);
    p.mask = (int)f->getInt("MASK", i, 255);
    if (vrpt) p.topi = (int)f->getInt("TOPI", i, 255);
    out.push_back(p);
  }
  return out;
}

std::vector<std::array<int64_t, 3>> read_coords(const Field *f, bool is3d) {
  std::vector<std::array<int64_t, 3>> out;
  if (!f) return out;
  for (size_t i = 0; i < f->inst.size(); i++)
    out.push_back({(int64_t)f->getInt("YCOO", i), (int64_t)f->getInt("XCOO", i),
                   is3d ? (int64_t)f->getInt("VE3D", i) : 0});
  return out;
}

std::vector<std::pair<int, std::string>> read_attf(const Field *f) {
  std::vector<std::pair<int, std::string>> out;
  if (!f) return out;
  for (size_t i = 0; i < f->inst.size(); i++)
    out.push_back({(int)f->getInt("ATTL", i), f->getStr("ATVL", i)});
  return out;
}

Vec make_vec(const Record &r, const Field *vrid) {
  Vec v;
  v.rcnm = (int)vrid->getInt("RCNM");
  v.rcid = (uint32_t)vrid->getInt("RCID");
  v.rver = (int)vrid->getInt("RVER");
  const Field *sg3 = r.find("SG3D");
  v.is3d = sg3 != nullptr;
  v.c = v.is3d ? read_coords(sg3, true) : read_coords(r.find("SG2D"), false);
  v.vrpt = read_ptrs(r.find("VRPT"), true);
  return v;
}

Feat make_feat(const Record &r, const Field *frid) {
  Feat f;
  f.rcid = (uint32_t)frid->getInt("RCID");
  f.prim = (int)frid->getInt("PRIM", 0, 255);
  f.objl = (int)frid->getInt("OBJL");
  f.rver = (int)frid->getInt("RVER");
  f.attf = read_attf(r.find("ATTF"));
  f.fspt = read_ptrs(r.find("FSPT"), false);
  return f;
}

void read_dsid(const Record &r, EncHeader &h) {
  if (const Field *f = r.find("DSID")) {
    h.dsnm = f->getStr("DSNM");
    h.edtn = f->getStr("EDTN");
    h.updn = to_int(f->getStr("UPDN"));
    h.intu = (int)f->getInt("INTU");
  }
  if (const Field *f = r.find("DSPM")) {
    h.cscl = (int)f->getInt("CSCL");
    double comf = f->getNum("COMF", 0, 0), somf = f->getNum("SOMF", 0, 0);
    if (comf > 0) h.comf = comf;
    if (somf > 0) h.somf = somf;
    h.duni = (int)f->getInt("DUNI", 0, 1);
  }
}

// Insert / delete / modify `n` items at 1-based index `ix` (S-57 3.1, 8.4.2)
template <class T>
void apply_list(std::vector<T> &dst, int ui, int ix, int n, const std::vector<T> &src) {
  if (ix < 1) ix = 1;
  size_t at = std::min<size_t>(ix - 1, dst.size());
  if (ui == 1) {
    size_t k = std::min<size_t>(n > 0 ? n : src.size(), src.size());
    dst.insert(dst.begin() + at, src.begin(), src.begin() + k);
  } else if (ui == 2) {
    size_t k = std::min<size_t>(n, dst.size() - at);
    dst.erase(dst.begin() + at, dst.begin() + at + k);
  } else if (ui == 3) {
    for (int i = 0; i < n && i < (int)src.size(); i++) {
      if (at + i < dst.size()) dst[at + i] = src[i];
      else dst.push_back(src[i]);
    }
  }
}

void update_attf(std::vector<std::pair<int, std::string>> &dst,
                 const std::vector<std::pair<int, std::string>> &src) {
  for (auto &a : src) {
    auto it = std::find_if(dst.begin(), dst.end(),
                           [&](const std::pair<int, std::string> &d) { return d.first == a.first; });
    const bool del = a.second.size() == 1 && (unsigned char)a.second[0] == 0x7f;
    if (it != dst.end()) {
      if (del) dst.erase(it);
      else it->second = a.second;
    } else if (!del) {
      dst.push_back(a);
    }
  }
}

// Read every record of a base cell or update into (or onto) the dataset.
bool ingest(const std::vector<uint8_t> &buf, Dataset &ds, bool isUpdate, std::string *err) {
  iso8211::Module m;
  if (!m.open(buf, err)) return false;
  Record r;
  std::string e;
  bool first = true;
  while (m.next(r, &e)) {
    if (const Field *dsid = r.find("DSID")) {
      if (isUpdate) {
        std::string edtn = dsid->getStr("EDTN");
        int updn = to_int(dsid->getStr("UPDN"));
        if (!edtn.empty() && to_int(edtn, -1) == 0) {
          // Cancellation notice. OpenCPN stops applying updates and still
          // shows the cell, so the depths here match the chart on screen.
          ds.hdr.cancelled = true;
          if (err) *err = "the producer has cancelled this cell (EDTN 0)";
          return false;
        }
        if (!edtn.empty() && !ds.hdr.edtn.empty() && to_int(edtn, -1) != to_int(ds.hdr.edtn, -2)) {
          if (err) *err = "update for edition " + edtn + ", cell is edition " + ds.hdr.edtn;
          return false;
        }
        if (updn <= ds.hdr.updn) return true;  // already contained in the base
        if (updn != ds.hdr.updn + 1) {
          if (err) *err = "update " + std::to_string(updn) + " out of sequence";
          return false;
        }
        ds.hdr.updn = updn;
      } else {
        read_dsid(r, ds.hdr);
      }
      first = false;
      continue;
    }
    if (r.find("DSPM")) {
      if (!isUpdate) read_dsid(r, ds.hdr);
      continue;
    }
    if (first && isUpdate) {
      if (err) *err = "update without DSID";
      return false;
    }
    if (const Field *vrid = r.find("VRID")) {
      int rcnm = (int)vrid->getInt("RCNM");
      uint32_t rcid = (uint32_t)vrid->getInt("RCID");
      int ruin = (int)vrid->getInt("RUIN", 0, 1);
      uint64_t k = vkey(rcnm, rcid);
      if (!isUpdate || ruin == 1) {
        ds.vec[k] = make_vec(r, vrid);
      } else if (ruin == 2) {
        ds.vec.erase(k);
      } else if (ruin == 3) {
        auto it = ds.vec.find(k);
        if (it == ds.vec.end()) continue;
        Vec &v = it->second;
        v.rver++;
        if (const Field *c = r.find("VRPC")) {
          apply_list(v.vrpt, (int)c->getInt("VPUI"), (int)c->getInt("VPIX"),
                     (int)c->getInt("NVPT"), read_ptrs(r.find("VRPT"), true));
        }
        if (const Field *c = r.find("SGCC")) {
          const Field *s3 = r.find("SG3D");
          bool is3d = v.is3d || s3;
          auto src = is3d ? read_coords(s3, true) : read_coords(r.find("SG2D"), false);
          if (s3) v.is3d = true;
          apply_list(v.c, (int)c->getInt("CCUI"), (int)c->getInt("CCIX"),
                     (int)c->getInt("CCNC"), src);
        }
      }
      continue;
    }
    if (const Field *frid = r.find("FRID")) {
      uint32_t rcid = (uint32_t)frid->getInt("RCID");
      int ruin = (int)frid->getInt("RUIN", 0, 1);
      if (!isUpdate || ruin == 1) {
        ds.feat[rcid] = make_feat(r, frid);
      } else if (ruin == 2) {
        ds.feat.erase(rcid);
      } else if (ruin == 3) {
        auto it = ds.feat.find(rcid);
        if (it == ds.feat.end()) continue;
        Feat &f = it->second;
        f.rver++;
        if (const Field *a = r.find("ATTF")) update_attf(f.attf, read_attf(a));
        if (const Field *c = r.find("FSPC")) {
          apply_list(f.fspt, (int)c->getInt("FSUI"), (int)c->getInt("FSIX"),
                     (int)c->getInt("NSPT"), read_ptrs(r.find("FSPT"), false));
        }
      }
      continue;
    }
  }
  if (!e.empty()) {
    if (err) *err = e;
    return false;
  }
  return true;
}

// ---- Geometry
struct Geo {
  const Dataset &ds;
  double comf, somf, dscale;

  LatLon ll(const std::array<int64_t, 3> &c) const {
    return {(double)c[0] / comf, (double)c[1] / comf};
  }
  const Vec *find(int rcnm, uint32_t rcid) const {
    auto it = ds.vec.find(vkey(rcnm, rcid));
    return it == ds.vec.end() ? nullptr : &it->second;
  }
  bool node(const Ptr &p, LatLon &out) const {
    const Vec *v = find(p.rcnm ? p.rcnm : RCNM_VC, p.rcid);
    if (!v && p.rcnm != RCNM_VI) v = find(RCNM_VI, p.rcid);
    if (!v || v->c.empty()) return false;
    out = ll(v->c[0]);
    return true;
  }
  // Edge in its stored direction; nodes as begin / end RCIDs
  struct Edge {
    std::vector<LatLon> pts;
    uint64_t s = 0, e = 0;
  };
  bool edge(uint32_t rcid, int ornt, Edge &out) const {
    const Vec *v = find(RCNM_VE, rcid);
    if (!v) return false;
    const Ptr *b = nullptr, *e = nullptr;
    for (const Ptr &p : v->vrpt) {
      if (p.topi == 1 && !b) b = &p;
      else if (p.topi == 2 && !e) e = &p;
    }
    if (!b && !v->vrpt.empty()) b = &v->vrpt[0];
    if (!e && v->vrpt.size() > 1) e = &v->vrpt[1];
    out.pts.clear();
    LatLon q;
    if (b && node(*b, q)) out.pts.push_back(q);
    for (auto &c : v->c) out.pts.push_back(ll(c));
    if (e && node(*e, q)) out.pts.push_back(q);
    out.s = b ? vkey(b->rcnm, b->rcid) : 0;
    out.e = e ? vkey(e->rcnm, e->rcid) : 0;
    if (ornt == 2) {
      std::reverse(out.pts.begin(), out.pts.end());
      std::swap(out.s, out.e);
    }
    return out.pts.size() >= 2;
  }
};

bool same(const LatLon &a, const LatLon &b) {
  return std::fabs(a.lat - b.lat) < 1e-9 && std::fabs(a.lon - b.lon) < 1e-9;
}

void build_lines(const Geo &g, const Feat &f, Feature &out) {
  std::vector<LatLon> cur;
  uint64_t curEnd = 0;
  for (const Ptr &p : f.fspt) {
    Geo::Edge e;
    if (p.rcnm != RCNM_VE || !g.edge(p.rcid, p.ornt, e)) continue;
    bool joins = !cur.empty() && ((curEnd && curEnd == e.s) || same(cur.back(), e.pts.front()));
    if (joins) {
      cur.insert(cur.end(), e.pts.begin() + 1, e.pts.end());
    } else {
      if (cur.size() >= 2) out.parts.push_back(cur);
      cur = e.pts;
    }
    curEnd = e.e;
  }
  if (cur.size() >= 2) out.parts.push_back(cur);
}

void build_rings(const Geo &g, const Feat &f, Feature &out) {
  std::vector<Geo::Edge> edges;
  for (const Ptr &p : f.fspt) {
    Geo::Edge e;
    if (p.rcnm == RCNM_VE && g.edge(p.rcid, p.ornt, e)) edges.push_back(std::move(e));
  }
  std::vector<bool> used(edges.size(), false);
  auto starts = [&](const Geo::Edge &e, uint64_t node, const LatLon &at) {
    return (node && e.s == node) || same(e.pts.front(), at);
  };
  for (size_t i = 0; i < edges.size(); i++) {
    if (used[i]) continue;
    used[i] = true;
    std::vector<LatLon> ring = edges[i].pts;
    const uint64_t startNode = edges[i].s;
    uint64_t endNode = edges[i].e;
    size_t last = i;
    for (;;) {
      if ((startNode && endNode == startNode) || (ring.size() > 2 && same(ring.back(), ring.front())))
        break;  // closed
      // the next edge in sequence, else any unused edge that continues
      size_t j = edges.size();
      if (last + 1 < edges.size() && !used[last + 1] && starts(edges[last + 1], endNode, ring.back()))
        j = last + 1;
      for (size_t k = 0; j == edges.size() && k < edges.size(); k++)
        if (!used[k] && starts(edges[k], endNode, ring.back())) j = k;
      if (j == edges.size()) break;  // open ring: keep what there is
      used[j] = true;
      ring.insert(ring.end(), edges[j].pts.begin() + 1, edges[j].pts.end());
      endNode = edges[j].e;
      last = j;
    }
    if (ring.size() > 1 && same(ring.back(), ring.front())) ring.pop_back();
    if (ring.size() >= 3) out.parts.push_back(std::move(ring));
  }
}

bool is_depth_attr(const std::string &a) {
  return a == "DRVAL1" || a == "DRVAL2" || a == "VALSOU" || a == "VALDCO";
}

double depth_scale(int duni) {
  switch (duni) {
    case 3: return 0.3048;            // feet
    case 2: case 4: return 1.8288;    // fathoms
    default: return 1.0;              // metres
  }
}

bool build_feature(const Geo &g, const Feat &f, const DecodeOptions &opt, Feature &out) {
  const S57Code *oc = s57_object(f.objl);
  std::string cls = oc ? oc->acronym : "OBJL" + std::to_string(f.objl);
  if (!opt.keepClasses.empty() && !opt.keepClasses.count(cls)) return false;
  out = Feature();
  out.cls = cls;
  out.cm93Cls = cls;
  for (auto &a : f.attf) {
    const S57Code *ac = s57_attribute(a.first);
    if (!ac) continue;
    AttrValue v;
    v.type = ac->type;
    v.str = a.second;
    while (!v.str.empty() && (v.str.back() == ' ' || v.str.back() == 0)) v.str.pop_back();
    if (!v.str.empty() && (ac->type == 'E' || ac->type == 'L' || ac->type == 'F' || ac->type == 'I')) {
      char *end = nullptr;
      double d = std::strtod(v.str.c_str(), &end);
      if (end != v.str.c_str()) {
        v.hasNum = true;
        v.num = is_depth_attr(ac->acronym) ? d * g.dscale : d;
      }
    }
    out.attrs[ac->acronym] = v;
  }

  if (f.prim == 1) {  // point(s)
    for (const Ptr &p : f.fspt) {
      const Vec *v = g.find(p.rcnm ? p.rcnm : RCNM_VI, p.rcid);
      if (!v) continue;
      if (v->is3d) {
        out.geom = Geom::MultiPoint3D;
        for (auto &c : v->c) {
          out.points.push_back(g.ll(c));
          out.z.push_back((double)c[2] / g.somf * g.dscale);
        }
      } else if (!v->c.empty()) {
        if (out.geom == Geom::None) out.geom = Geom::Point;
        out.points.push_back(g.ll(v->c[0]));
      }
    }
  } else if (f.prim == 2) {
    out.geom = Geom::Line;
    build_lines(g, f, out);
  } else if (f.prim == 3) {
    out.geom = Geom::Area;
    build_rings(g, f, out);
  } else {
    return false;  // no geometry: of no use here
  }
  if (out.points.empty() && out.parts.empty()) return false;
  for (auto &p : out.points) {
    out.minLat = std::min(out.minLat, p.lat), out.maxLat = std::max(out.maxLat, p.lat);
    out.minLon = std::min(out.minLon, p.lon), out.maxLon = std::max(out.maxLon, p.lon);
  }
  for (auto &part : out.parts)
    for (auto &p : part) {
      out.minLat = std::min(out.minLat, p.lat), out.maxLat = std::max(out.maxLat, p.lat);
      out.minLon = std::min(out.minLon, p.lon), out.maxLon = std::max(out.maxLon, p.lon);
    }
  return true;
}

void extent(Dataset &ds) {
  EncHeader &h = ds.hdr;
  for (auto &kv : ds.vec)
    for (auto &c : kv.second.c) {
      double la = (double)c[0] / h.comf, lo = (double)c[1] / h.comf;
      h.minLat = std::min(h.minLat, la), h.maxLat = std::max(h.maxLat, la);
      h.minLon = std::min(h.minLon, lo), h.maxLon = std::max(h.maxLon, lo);
    }
}

}  // namespace

int enc_band(int intu, int cscl) {
  if (intu >= 1 && intu <= 6) return intu;
  if (cscl <= 0) return 3;
  if (cscl > 1500000) return 1;
  if (cscl > 350000) return 2;
  if (cscl > 90000) return 3;
  if (cscl > 22000) return 4;
  if (cscl > 4000) return 5;
  return 6;
}

bool decode_enc_buffers(const std::vector<uint8_t> &base,
                        const std::vector<std::vector<uint8_t>> &updates,
                        const DecodeOptions &opt, std::vector<Feature> &out,
                        EncHeader *hdr, std::string *err) {
  Dataset ds;
  if (!ingest(base, ds, false, err)) return false;
  for (size_t i = 0; i < updates.size(); i++) {
    std::string e;
    const int before = ds.hdr.updn;
    if (!ingest(updates[i], ds, true, &e)) {
      if (ds.hdr.updateError.empty())
        ds.hdr.updateError = "update " + std::to_string(i + 1) + ": " + e;
      break;  // later updates depend on this one
    }
    if (ds.hdr.updn != before) ds.hdr.updatesApplied++;
  }
  extent(ds);
  if (hdr) *hdr = ds.hdr;
  Geo g{ds, ds.hdr.comf, ds.hdr.somf, depth_scale(ds.hdr.duni)};
  for (auto &kv : ds.feat) {
    Feature f;
    if (build_feature(g, kv.second, opt, f)) out.push_back(std::move(f));
  }
  return true;
}

bool read_enc_header_buffer(const std::vector<uint8_t> &base, EncHeader &hdr,
                            std::string *err) {
  hdr = EncHeader();
  iso8211::Module m;
  if (!m.open(base, err)) return false;
  Record r;
  std::string e;
  bool any = false;
  // raw coordinates (lat/lon x COMF) until COMF is known
  double y0 = 1e300, y1 = -1e300, x0 = 1e300, x1 = -1e300;
  while (m.next(r, &e)) {
    if (r.find("DSID") || r.find("DSPM")) {
      read_dsid(r, hdr);
      any = true;
      continue;
    }
    if (!r.find("VRID")) continue;
    const Field *s = r.find("SG2D");
    if (!s) s = r.find("SG3D");
    if (!s) continue;
    for (size_t i = 0; i < s->inst.size(); i++) {
      double y = (double)s->getInt("YCOO", i), x = (double)s->getInt("XCOO", i);
      y0 = std::min(y0, y), y1 = std::max(y1, y);
      x0 = std::min(x0, x), x1 = std::max(x1, x);
    }
  }
  if (!any) {
    if (err) *err = e.empty() ? "no DSID record: not an S-57 cell" : e;
    return false;
  }
  if (y0 <= y1) {
    hdr.minLat = y0 / hdr.comf, hdr.maxLat = y1 / hdr.comf;
    hdr.minLon = x0 / hdr.comf, hdr.maxLon = x1 / hdr.comf;
  }
  return true;
}

bool read_enc_header(const std::string &path, EncHeader &hdr, std::string *err) {
  std::vector<uint8_t> data;
  if (!read_file(path, data, err)) return false;
  return read_enc_header_buffer(data, hdr, err);
}

static std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

std::vector<std::string> find_enc_updates(const std::string &basePath) {
  std::vector<std::pair<int, std::string>> found;
  std::error_code ec;
  fs::path bp = fs::u8path(basePath);
  const std::string stem = lower(bp.stem().u8string());
  fs::path dir = bp.parent_path();
  if (dir.empty()) dir = ".";
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    fs::path p = it->path();
    if (lower(p.stem().u8string()) != stem) continue;
    std::string ext = p.extension().u8string();
    if (ext.size() != 4 || !std::isdigit((unsigned char)ext[1]) ||
        !std::isdigit((unsigned char)ext[2]) || !std::isdigit((unsigned char)ext[3]))
      continue;
    int n = std::atoi(ext.c_str() + 1);
    if (n > 0) found.push_back({n, p.u8string()});
  }
  std::sort(found.begin(), found.end());
  std::vector<std::string> out;
  for (auto &f : found) out.push_back(f.second);
  return out;
}

bool load_enc(const std::string &basePath, const DecodeOptions &opt, CellData &out,
              EncHeader *hdr, std::string *err) {
  std::vector<uint8_t> base;
  if (!read_file(basePath, base, err)) return false;
  std::vector<std::vector<uint8_t>> ups;
  std::vector<std::string> upPaths = find_enc_updates(basePath);
  for (const std::string &p : upPaths) {
    std::vector<uint8_t> d;
    if (!read_file(p, d)) break;
    ups.push_back(std::move(d));
  }
  EncHeader h;
  std::vector<Feature> feats;
  std::string e;
  if (!decode_enc_buffers(base, ups, opt, feats, &h, &e)) {
    if (err) *err = basePath + ": " + e;
    if (hdr) *hdr = h;
    return false;
  }
  out.features.insert(out.features.end(), std::make_move_iterator(feats.begin()),
                      std::make_move_iterator(feats.end()));
  out.filesLoaded++;
  out.files.push_back(basePath);
  for (int i = 0; i < h.updatesApplied && i < (int)upPaths.size(); i++)
    out.files.push_back(upPaths[i]);
  out.ch = (char)('0' + enc_band(h.intu, h.cscl));
  out.name = fs::u8path(h.dsnm.empty() ? basePath : h.dsnm).stem().u8string();
  if (!h.updateError.empty()) out.readError = basePath + ": " + h.updateError;
  out.swLat = h.minLat;
  out.swLon = h.minLon;
  out.sizeDeg = std::max(h.maxLat - h.minLat, h.maxLon - h.minLon);
  if (hdr) *hdr = h;
  return true;
}

}  // namespace cm93
