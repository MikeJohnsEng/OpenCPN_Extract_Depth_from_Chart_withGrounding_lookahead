/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * cm93_reader.h : standalone reader for CM93 v2 vector chart cells.
 *
 * No wxWidgets / OpenCPN dependency, so it can be unit tested on its own.
 * The binary format handling follows OpenCPN's gui/src/cm93.cpp
 * (decode table, header layout, feature table, ring assembly, coordinate
 * transform). OpenCPN is GPLv2+, and so is this file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm93 {

struct LatLon {
  double lat;
  double lon;
};

// Primary geometry of a feature, as stored in the cell (geotype & 0x0f).
enum class Geom : int {
  None = 0,
  Point = 1,         // single 2D point
  Line = 2,          // chain of edges
  Area = 4,          // closed rings built from edges
  MultiPoint3D = 8,  // soundings (x, y, depth)
};

struct AttrValue {
  char type = '?';  // CM93 dictionary type letter: B W G R S C L I
  bool hasNum = false;
  double num = 0.0;
  std::string str;
};

struct Feature {
  std::string cls;      // S-57 style class name, after CM93 substitutions
  std::string cm93Cls;  // class name as it appears in CM93OBJ.DIC
  Geom geom = Geom::None;
  std::vector<std::vector<LatLon>> parts;  // area rings / line pieces
  std::vector<LatLon> points;              // point or multipoint positions
  std::vector<double> z;                   // depth (m) per multipoint position
  std::unordered_map<std::string, AttrValue> attrs;
  double minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9;

  bool getNum(const char *name, double &v) const;
};

// --------------------------------------------------------------------------
// Dictionary (CM93OBJ.DIC + CM93ATTR.DIC or ATTRLUT.DIC)
// --------------------------------------------------------------------------
class Dictionary {
public:
  // Load from a directory holding the .DIC files (case-insensitive names).
  bool load(const std::string &dir, std::string *err = nullptr);
  // Look in `root`, then up to `maxDepth` directory levels below it.
  bool find_and_load(const std::string &root, int maxDepth = 3,
                     std::string *err = nullptr);

  bool ok() const { return m_ok; }
  const std::string &dir() const { return m_dir; }
  std::string className(int iclass) const;
  std::string attrName(int iattr) const;
  char attrType(int iattr) const;

  // Test helper: build a dictionary in memory.
  void setForTest(const std::vector<std::string> &classes,
                  const std::vector<std::pair<std::string, char>> &attrs);

private:
  std::vector<std::string> m_class;
  std::vector<std::string> m_attr;
  std::vector<char> m_attrType;
  std::string m_dir;
  bool m_ok = false;
};

// --------------------------------------------------------------------------
// Cell indexing
// --------------------------------------------------------------------------
constexpr int kNumScales = 8;
struct ScaleInfo {
  char ch;        // directory / file-extension letter
  int dval;       // cell size in units of 20 arc minutes
  int nominal;    // nominal chart scale 1:n
};
extern const ScaleInfo kScales[kNumScales];  // index 0 = Z (smallest) .. 7 = G

int scale_index_from_char(char c);  // -1 if not a CM93 scale letter

// OpenCPN's Get_CM93_CellIndex()
int cell_index(double lat, double lon, int scaleIdx);
// South-west corner of a cell. Longitude is returned in [0, 360).
void cell_origin(int cellIndex, double &lat, double &lon);
// Cell size in degrees (square in lat/lon).
double cell_size_deg(int scaleIdx);

// Relative path components of a cell file:  <root8>/<S>/<c><jlat3><jlon4>.<S>
struct CellPathParts {
  std::string rootDir;   // e.g. "03000420"
  std::string baseName;  // without leading subcell char, e.g. "4800450" ->
                         // full name is subcell + baseName + "." + scale
};
CellPathParts cell_path_parts(int cellIndex, int scaleIdx);

// --------------------------------------------------------------------------
// Decoding
// --------------------------------------------------------------------------
struct DecodeOptions {
  // Only build geometry/attributes for these classes (after substitution).
  // Empty set = keep everything.
  std::set<std::string> keepClasses;
  // Offsets in metres, same meaning as OpenCPN's CM93 offset dialog
  // (subtracted from the cell easting / northing before conversion).
  double offsetE = 0.0;
  double offsetN = 0.0;
  // Soundings exactly as OpenCPN reads them (default). OpenCPN's 3-D point
  // loader advances its write pointer by one point per record, not by the
  // record's point count, so in a record holding several soundings each
  // further point is overwritten by the next record's first point. What
  // OpenCPN draws is then each record's first sounding (plus the tail of
  // the last record). true = reproduce that, so the plugin uses the
  // soundings you see on the chart; false = every point as stored.
  bool openCpnSoundings = true;
};

struct CellHeader {
  double lonMin = 0, latMin = 0, lonMax = 0, latMax = 0;
  int nFeatures = 0;
};

// Byte-wise CM93 encoding (used by the decoder, and by tests to build cells)
uint8_t decode_byte(uint8_t b);
uint8_t encode_byte(uint8_t b);

// Decode one raw (still encoded) cell file image and append its features.
bool decode_cell_buffer(const std::vector<uint8_t> &raw, const Dictionary &dict,
                        const DecodeOptions &opt, std::vector<Feature> &out,
                        CellHeader *hdr = nullptr, std::string *err = nullptr);

// Read a file into memory; transparently decompresses "*.xz" when the
// plugin was built with liblzma (HAVE_LZMA).
bool read_file(const std::string &path, std::vector<uint8_t> &data,
               std::string *err = nullptr);

// --------------------------------------------------------------------------
// Whole cell (base cell '0' plus subcells 'A', 'B', ...)
// --------------------------------------------------------------------------
struct CellData {
  int cellIndex = 0;
  int scaleIdx = 0;
  char ch = ' ';      // band letter shown to the user: CM93 Z..G, ENC 1..6
  std::string name;   // ENC: data set name; CM93: empty
  double swLat = 0, swLon = 0, sizeDeg = 0;
  int filesLoaded = 0;
  std::vector<std::string> files;
  std::vector<Feature> features;
  std::string readError;  // a subcell file that failed to read, if any
};

// Locate one cell file on disk (handles upper/lower case and ".xz").
// Returns empty string if not present.
std::string find_cell_file(const std::string &root, int cellIndex,
                           int scaleIdx, char subcell);

// Load base cell and all consecutive subcells. Returns false if the base
// cell file does not exist or none of the files decode.
bool load_cell(const std::string &root, const Dictionary &dict, int cellIndex,
               int scaleIdx, const DecodeOptions &opt, CellData &out,
               std::string *err = nullptr);

// Find a file in `dir` whose name matches `name` ignoring case.
std::string find_file_ci(const std::string &dir, const std::string &name);
// Same for a sub-directory.
std::string find_dir_ci(const std::string &dir, const std::string &name);

// True if `dir` looks like a CM93 root (8-digit sub-directories and/or
// the dictionary files).
bool looks_like_cm93_root(const std::string &dir);
// Search `dir` and up to `maxDepth` levels below for a CM93 root.
std::string find_cm93_root(const std::string &dir, int maxDepth = 2);

}  // namespace cm93
