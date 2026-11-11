/***************************************************************************
 * cm93depth_pi - CM93 / S-57 least-depth plugin for OpenCPN
 *
 * s57_reader.h : standalone reader for unencrypted S-57 ENC cells (*.000
 * base cells and their *.001, *.002 ... updates), producing the same
 * Feature model as the CM93 reader.
 *
 * ISO/IEC 8211 file structure, S-57 edition 3.1 record layout and the
 * update rules (insert / delete / modify, pointer and coordinate update
 * instructions) follow IHO S-57 3.1 and OpenCPN's s57reader.cpp / GDAL's
 * ISO 8211 library. No wxWidgets / OpenCPN dependency, so it can be unit
 * tested on its own. Encrypted S-63 cells cannot be read.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cm93_reader.h"

namespace cm93 {

// ---------------------------------------------------------------------------
// S-57 object and attribute catalogues (s57_codes.cpp)
// ---------------------------------------------------------------------------
struct S57Code {
  int code;
  const char *acronym;
  char type;  // attributes: E enumerated, L list, F float, I integer, A/S text
};
extern const S57Code kS57Objects[];
extern const size_t kS57ObjectCount;
extern const S57Code kS57Attributes[];
extern const size_t kS57AttributeCount;
const S57Code *s57_object(int code);     // nullptr if unknown
const S57Code *s57_attribute(int code);  // nullptr if unknown
int s57_object_code(const std::string &acronym);     // -1 if unknown
int s57_attribute_code(const std::string &acronym);  // -1 if unknown

// ---------------------------------------------------------------------------
// ISO/IEC 8211
// ---------------------------------------------------------------------------
namespace iso8211 {

struct Subfield {
  std::string name;
  // A I R S C : text (I, R also read as numbers)
  // u / s     : binary unsigned / signed little-endian integer (b1w / b2w)
  // B         : bit string (B(n)), kept as raw bytes
  char type = 'A';
  int width = 0;  // bytes (binary) or characters; 0 = variable, delimited
};

struct FieldDefn {
  std::string tag;
  std::string name;
  bool repeating = false;  // array descriptor began with '*'
  std::vector<Subfield> subs;
  int fixedWidth = 0;  // bytes of one instance when every subfield is fixed
  int index(const std::string &sub) const;  // -1 if absent
};

struct Value {
  bool isNum = false;
  double num = 0;
  std::string str;  // text, or raw bytes for B(n)
};

struct Field {
  const FieldDefn *defn = nullptr;
  std::string tag;
  std::vector<std::vector<Value>> inst;  // one entry per repetition
  long long getInt(const char *sub, size_t i = 0, long long dflt = 0) const;
  double getNum(const char *sub, size_t i = 0, double dflt = 0) const;
  std::string getStr(const char *sub, size_t i = 0) const;
  const Value *get(const char *sub, size_t i = 0) const;
};

struct Record {
  std::vector<Field> fields;
  const Field *find(const char *tag) const;
};

class Module {
public:
  // Parses the data descriptive record. `data` must stay alive while the
  // module is used.
  bool open(const std::vector<uint8_t> &data, std::string *err = nullptr);
  // Next data record; false at the end or on a damaged record (err set).
  bool next(Record &rec, std::string *err = nullptr);
  const FieldDefn *defn(const std::string &tag) const;

private:
  const std::vector<uint8_t> *m_data = nullptr;
  size_t m_pos = 0;
  std::map<std::string, FieldDefn> m_defs;
};

// Expand an ISO 8211 format control string such as "(b11,b14,2b11,3A,A(8))"
// into one (type, width) per subfield. Exposed for tests.
bool expand_format(const std::string &fmt, std::vector<std::pair<char, int>> &out);

}  // namespace iso8211

// ---------------------------------------------------------------------------
// S-57 cells
// ---------------------------------------------------------------------------
struct EncHeader {
  std::string dsnm;     // data set name, e.g. "AU5TAS01.000"
  std::string edtn;     // edition
  int updn = 0;         // update number of the base cell (re-issues > 0)
  int intu = 0;         // intended usage 1 overview .. 6 berthing
  int cscl = 0;         // compilation scale 1:n
  double comf = 1e7;    // coordinate multiplication factor
  double somf = 10;     // 3-D (sounding) multiplication factor
  int duni = 1;         // depth units: 1 m, 2 fathoms+feet, 3 feet, 4 fathoms
  bool cancelled = false;  // an update cancels the cell (EDTN 0); still read,
                           // as OpenCPN still shows it
  // Extent of every coordinate in the cell (not just M_COVR)
  double minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9;
  int updatesApplied = 0;
  std::string updateError;  // first update that failed, "" if none
};

// ENC usage band 1..6 from INTU, or from the compilation scale when INTU is
// missing or out of range.
int enc_band(int intu, int cscl);

// Decode a base cell image plus its updates (in order) and append the
// features. Coordinates in degrees, depths in metres.
bool decode_enc_buffers(const std::vector<uint8_t> &base,
                        const std::vector<std::vector<uint8_t>> &updates,
                        const DecodeOptions &opt, std::vector<Feature> &out,
                        EncHeader *hdr = nullptr, std::string *err = nullptr);

// Header and coordinate extent only (no features built, no updates).
bool read_enc_header_buffer(const std::vector<uint8_t> &base, EncHeader &hdr,
                            std::string *err = nullptr);
bool read_enc_header(const std::string &path, EncHeader &hdr,
                     std::string *err = nullptr);

// Update files belonging to a base cell path ("X.000" -> "X.001", ...),
// in order, as found in its directory (any case).
std::vector<std::string> find_enc_updates(const std::string &basePath);

// Load a base cell and every update found beside it.
bool load_enc(const std::string &basePath, const DecodeOptions &opt,
              CellData &out, EncHeader *hdr = nullptr,
              std::string *err = nullptr);

}  // namespace cm93
