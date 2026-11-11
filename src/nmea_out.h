/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * nmea_out.h : builds the proprietary least-depth and look-ahead sentences.
 *
 *  $PCMLD,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>,<11>,<12>,<13>,<14>,<15>*hh
 *
 *   1  least charted depth, metres below chart datum (negative = drying).
 *      In mode H: DRVAL1 (minimum) of the depth area the vessel is in.
 *      In mode I: depth at the vessel interpolated from nearby soundings.
 *   2  latitude of least depth, ddmm.mmm (modes H, I: the vessel)
 *   3  N / S
 *   4  longitude of least depth, dddmm.mmm (modes H, I: the vessel)
 *   5  E / W
 *   6  range from vessel to least depth, nautical miles (mode H: 0.00;
 *      mode I: distance to the nearest sounding used)
 *   7  true bearing from vessel to least depth, degrees (empty at range 0)
 *   8  DRVAL1 of the depth area the vessel is in, metres
 *   9  DRVAL2 of the depth area the vessel is in, metres
 *  10  source of least depth: S sounding, A depth area, D dredged area,
 *      O obstruction, W wreck, R rock, C depth contour, I interpolated
 *  11  number of obstructions/wrecks/rocks in scope with no charted depth.
 *      In mode H: dangers inside the depth area that have no charted depth
 *      or are charted shallower than its DRVAL1. In mode I: dangers within
 *      reach of the soundings used, undepthed or shallower than the estimate.
 *  12  search mode: H = depth area here, C = whole cell, R = radius,
 *      I = interpolated from soundings
 *  13  search radius, nm (empty in cell mode; mode I: farthest sounding used)
 *  14  chart band used: CM93 scale letter Z A B C D E F G, or the S-57 ENC
 *      usage band digit 1 (overview) .. 6 (berthing)
 *  15  status: A = valid, V = invalid (no fix / no chart / no depths)
 *
 * Empty fields mean "not available". Depths are charted values referenced to
 * chart datum; no tide is applied.
 *
 * Resolution: depths 0.1 m (whole metres at 100 m and deeper); distances
 * 0.01 nm below 10 nm, 0.1 nm below 100 nm, whole nm beyond. This keeps the
 * sentence within NMEA 0183's 82-character limit in every case.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#pragma once

#include <string>

#include "depth_engine.h"

namespace cm93 {

// XOR of every character between '$' and '*' (exclusive), two hex digits.
std::string nmea_checksum(const std::string &body);

// Complete sentence including leading '$', '*hh' and CR LF.
// `address` is the five-character proprietary address, e.g. "PCMLD".
std::string build_least_depth_sentence(const DepthResult &r,
                                       const std::string &address,
                                       SearchMode mode, double radiusNm,
                                       bool fixValid);

// Look-ahead sentence (default address PCMLA):
//
//  $PCMLA,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>,<11>,<12>,<13>,<14>,<15>,<16>*hh
//
//   1  least charted depth in the zone, m below chart datum (land excluded)
//   2  range from the vessel to it, nm
//   3  true bearing to it, degrees (empty at range 0)
//   4  its source: S A D O W R C as in $PCMLD
//   5  safety depth, m (the setting)
//   6  distance ahead to the first shoal: the nearest point ahead shallower
//      than the safety depth, land, or (option) an undepthed danger. Along
//      the course for a corridor, range for a sector. Empty = none in zone.
//   7  time to reach it at the current SOG, minutes (empty below the
//      minimum SOG, when the course comes from the heading)
//   8  true bearing to it, degrees
//   9  its source: S A D O W R C, or L land
//  10  number of undepthed dangers (obstruction, wreck, rock) in the zone
//  11  distance ahead to the nearest of them, nm
//  12  zone: C corridor, S sector
//  13  zone length (sector radius), nm
//  14  corridor half-width, nm, or sector half-angle, degrees
//  15  least detailed chart band used along the zone (as $PCMLD field 14)
//  16  status: A valid, V invalid (no fix / no chart / no course)
//
// Worst case 72 characters including CR LF.
std::string build_lookahead_sentence(const LookAheadResult &r,
                                     const std::string &address,
                                     bool fixValid);

// IEC 61162-1 alarm sentence, talker II:
//   $IIALR,hhmmss.ss,<id 3 digits>,<A threshold exceeded / V not>,
//          <A acknowledged / V unacknowledged>,<text>*hh
// utcSec = seconds since midnight UTC; text is cut to fit 82 characters
// and has ',', '*' and '$' replaced.
std::string build_alr_sentence(double utcSec, int id, bool active, bool acknowledged,
                               const std::string &text);

// One-letter code used in field 10.
char source_code(const std::string &s57class);

// True if `a` is a usable proprietary address: 'P' + 4 upper-case letters
// or digits.
bool valid_proprietary_address(const std::string &a);

}  // namespace cm93
