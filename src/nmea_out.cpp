/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * nmea_out.cpp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "nmea_out.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace cm93 {

std::string nmea_checksum(const std::string &body) {
  unsigned char cs = 0;
  for (unsigned char c : body) cs ^= c;
  char buf[3];
  std::snprintf(buf, sizeof buf, "%02X", cs);
  return buf;
}

char source_code(const std::string &c) {
  if (c == "SOUNDG") return 'S';
  if (c == "DEPARE") return 'A';
  if (c == "DRGARE") return 'D';
  if (c == "SBDARE") return 'A';  // seabed area taken as drying
  if (c == "OBSTRN") return 'O';
  if (c == "WRECKS") return 'W';
  if (c == "UWTROC") return 'R';
  if (c == "DEPCNT") return 'C';
  if (c == "INTERP") return 'I';
  if (c == "LNDARE") return 'L';
  if (c == "HOLE") return 'U';  // gap in the depth areas: reef / islet / unsurveyed
  return 'U';
}

bool valid_proprietary_address(const std::string &a) {
  if (a.size() != 5 || a[0] != 'P') return false;
  for (size_t i = 1; i < a.size(); i++) {
    unsigned char c = (unsigned char)a[i];
    if (!(std::isupper(c) || std::isdigit(c))) return false;
  }
  return true;
}

// Depths: 0.1 m resolution below 100 m, whole metres beyond (keeps the
// sentence inside NMEA 0183's 82-character limit in every case).
static std::string fmt_depth(double d) {
  if (!std::isfinite(d)) return "";
  char b[24];
  if (std::fabs(d) >= 99.95)
    std::snprintf(b, sizeof b, "%.0f", d);
  else
    std::snprintf(b, sizeof b, "%.1f", d);
  if (std::string(b) == "-0.0" || std::string(b) == "-0") return "0.0";
  return b;
}

// Distances: 0.01 nm below 10 nm, 0.1 nm below 100 nm, whole nm beyond.
static std::string fmt_nm(double v) {
  char b[24];
  if (v >= 99.95)
    std::snprintf(b, sizeof b, "%.0f", v);
  else if (v >= 9.995)
    std::snprintf(b, sizeof b, "%.1f", v);
  else
    std::snprintf(b, sizeof b, "%.2f", v);
  return b;
}

static void fmt_lat(double lat, std::string &val, std::string &hemi) {
  hemi = lat < 0 ? "S" : "N";
  double a = std::fabs(lat);
  int deg = (int)a;
  double min = (a - deg) * 60.0;
  if (min >= 59.9995) {  // avoid "60.000" after rounding
    deg += 1;
    min = 0.0;
  }
  char b[24];
  std::snprintf(b, sizeof b, "%02d%06.3f", deg, min);
  val = b;
}

static void fmt_lon(double lon, std::string &val, std::string &hemi) {
  hemi = lon < 0 ? "W" : "E";
  double a = std::fabs(lon);
  int deg = (int)a;
  double min = (a - deg) * 60.0;
  if (min >= 59.9995) {
    deg += 1;
    min = 0.0;
  }
  char b[24];
  std::snprintf(b, sizeof b, "%03d%06.3f", deg, min);
  val = b;
}

std::string build_least_depth_sentence(const DepthResult &r,
                                       const std::string &address,
                                       SearchMode mode, double radiusNm,
                                       bool fixValid) {
  const bool ok = fixValid && r.valid && r.haveLeast;
  std::string f[15];

  if (r.valid && r.haveLeast) {
    f[0] = fmt_depth(r.leastDepth);
    fmt_lat(r.leastPos.lat, f[1], f[2]);
    fmt_lon(r.leastPos.lon, f[3], f[4]);
    char b[24];
    f[5] = fmt_nm(r.rangeNm);
    if (r.rangeNm >= 0.005 && mode != SearchMode::Interpolated) {
      int brg = (int)std::lround(r.bearingDeg) % 360;
      std::snprintf(b, sizeof b, "%03d", brg);
      f[6] = b;
    }
    f[9] = std::string(1, source_code(r.leastClass));
  }
  if (r.valid && r.haveAtPos) {
    f[7] = fmt_depth(r.atPosMin);
    f[8] = fmt_depth(r.atPosMax);
  }
  if (r.valid)
    f[10] = std::to_string(r.unknownHazards +
                           (mode == SearchMode::DepthAreaHere ||
                                    mode == SearchMode::Interpolated
                                ? r.shallowHazards
                                : 0));
  f[11] = mode == SearchMode::Radius          ? "R"
          : mode == SearchMode::DepthAreaHere ? "H"
          : mode == SearchMode::Interpolated  ? "I"
                                              : "C";
  if (mode == SearchMode::Radius) f[12] = fmt_nm(radiusNm);
  if (mode == SearchMode::Interpolated && r.valid && !r.soundings.empty()) {
    double reach = 0;
    for (const auto &s : r.soundings) reach = std::max(reach, s.distNm);
    f[12] = fmt_nm(reach);  // farthest sounding used
  }
  if (r.valid) f[13] = std::string(1, r.scaleChar);
  f[14] = ok ? "A" : "V";

  std::string body = address;
  for (const auto &s : f) {
    body += ',';
    body += s;
  }
  return "$" + body + "*" + nmea_checksum(body) + "\r\n";
}

static std::string fmt_brg(double brg, double rangeNm) {
  if (!std::isfinite(brg) || rangeNm < 0.005) return "";
  char b[8];
  std::snprintf(b, sizeof b, "%03d", (int)std::lround(brg) % 360);
  return b;
}

static std::string fmt_min(double t) {
  if (!std::isfinite(t)) return "";
  char b[24];
  std::snprintf(b, sizeof b, t >= 99.95 ? "%.0f" : "%.1f", std::min(t, 9999.0));
  return b;
}

std::string build_alr_sentence(double utcSec, int id, bool active, bool acknowledged,
                               const std::string &text) {
  char t[16], idb[8];
  int h = (int)(utcSec / 3600) % 24, m = (int)(utcSec / 60) % 60;
  double sec = std::fmod(utcSec, 60.0);
  std::snprintf(t, sizeof t, "%02d%02d%05.2f", h, m, sec);
  std::snprintf(idb, sizeof idb, "%03d", id % 1000);
  std::string clean;
  for (char c : text) clean += (c == ',' || c == '*' || c == '$' || c == '!') ? ' ' : c;
  std::string body = std::string("IIALR,") + t + "," + idb + "," + (active ? "A" : "V") + "," +
                     (acknowledged ? "A" : "V") + ",";
  const size_t room = 82 - 1 - 5 - body.size();  // '$', '*hh', CR LF
  if (clean.size() > room) clean.resize(room);
  body += clean;
  return "$" + body + "*" + nmea_checksum(body) + "\r\n";
}

std::string build_lookahead_sentence(const LookAheadResult &r,
                                     const std::string &address,
                                     bool fixValid) {
  std::string f[16];
  if (r.valid && r.haveLeast) {
    f[0] = fmt_depth(r.least.depth);
    f[1] = fmt_nm(r.least.rangeNm);
    f[2] = fmt_brg(r.least.bearingDeg, r.least.rangeNm);
    f[3] = std::string(1, source_code(r.least.cls));
  }
  f[4] = fmt_depth(r.safetyDepthM);
  if (r.valid && r.haveShoal) {
    f[5] = fmt_nm(r.shoal.aheadNm);
    f[6] = fmt_min(r.shoal.timeMin);
    f[7] = fmt_brg(r.shoal.bearingDeg, r.shoal.rangeNm);
    f[8] = std::string(1, source_code(r.shoal.cls));
  }
  if (r.valid) {
    f[9] = std::to_string(std::min(r.undepthed, 99));
    if (r.haveUndepthed) f[10] = fmt_nm(r.nearestUndepthed.aheadNm);
  }
  f[11] = r.zone == LookZone::Sector ? "S" : "C";
  if (r.lengthNm > 0) {
    f[12] = fmt_nm(r.lengthNm);
    char b[24];
    if (r.zone == LookZone::Sector)
      std::snprintf(b, sizeof b, "%.1f", r.halfAngleDeg);
    else
      std::snprintf(b, sizeof b, "%.2f", r.halfWidthNm);
    f[13] = b;
  }
  if (r.valid) f[14] = std::string(1, r.coarsestScale);
  f[15] = (fixValid && r.valid) ? "A" : "V";

  std::string body = address;
  for (const auto &s : f) {
    body += ',';
    body += s;
  }
  return "$" + body + "*" + nmea_checksum(body) + "\r\n";
}

}  // namespace cm93
