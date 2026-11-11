/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * depth_engine.h : picks the chart under the vessel (CM93 cell or S-57 ENC)
 * and finds the least charted depth in it (or within a radius). No
 * wxWidgets dependency.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#pragma once

#include <list>
#include <map>
#include <memory>
#include <string>

#include "cm93_reader.h"
#include "enc_index.h"

namespace cm93 {

// ---------------------------------------------------------------------------
// Chart bands ("slots"): the CM93 scale bands and the S-57 ENC usage bands
// in one order of detail, least detailed first. Where charts of several
// bands cover a position, the most detailed band wins, whichever kind it is.
//   Z (1:20M)  A (1:3M)  1 Overview  B (1:1M)  2 General  C (1:200k)
//   3 Coastal  D (1:100k)  E (1:50k)  4 Approach  F (1:20k)  5 Harbour
//   G (1:7500)  6 Berthing
// ---------------------------------------------------------------------------
constexpr int kNumSlots = 14;
struct SlotInfo {
  char ch;   // CM93 scale letter, or ENC usage band digit
  int cm93;  // CM93 scale index 0..7, -1 for an ENC band
  int enc;   // ENC usage band 1..6, 0 for a CM93 band
};
extern const SlotInfo kSlots[kNumSlots];
int slot_from_char(char c);      // -1 if not a band character
int slot_of_cm93(int scaleIdx);  // slot of a CM93 scale index

enum class SearchMode : int {
  Cell = 0,           // every depth object in the CM93 cell under the vessel
  Radius = 1,         // only objects within radiusNm of the vessel
  DepthAreaHere = 2,  // DRVAL1 of the depth area containing the vessel
  Interpolated = 3,   // depth estimated from the nearest soundings
};

// One sounding used by the interpolation
struct UsedSounding {
  LatLon pos;
  double depth;   // m
  double distNm;  // from the vessel
  double weight;  // normalised, sums to 1
  char scaleChar = ' ';  // CM93 scale band it came from
};

struct EngineConfig {
  // -1 = auto (most detailed chart covering the position, CM93 or ENC);
  // 0..7 = this CM93 scale band only (ENCs are then not used)
  int scaleIdx = -1;
  bool useCm93 = true;  // CM93 charts (when a CM93 folder is set)
  bool useEnc = true;   // S-57 ENC cells (when any are indexed)
  SearchMode mode = SearchMode::Cell;
  double radiusNm = 1.0;
  bool useSoundings = true;  // SOUNDG
  bool useAreas = true;      // DEPARE (incl. CM93 ITDARE), DRGARE
  bool useHazards = true;    // OBSTRN, WRECKS, UWTROC with VALSOU
  bool useContours = true;   // DEPCNT (VALDCO)
  bool interpQuadrants = false;  // interpolated mode: one sounding per quadrant
  bool openCpnSoundings = true;  // decode soundings as OpenCPN draws them
  bool landCheck = true;  // interpolated mode: skip soundings with land between
  // Depth taken for a danger (rock, wreck, obstruction such as a reef) with
  // no charted depth that the vessel is over: 0 = 0 m; 1 = the least depth
  // of the depth area it lies in (the ECDIS rule, S-52 DEPVAL02).
  int undepthedDangerRule = 0;
  // Rocks, wrecks and obstructions charted only at a smaller scale than the
  // one the depth comes from still count (CM93 small-scale charts carry
  // obstruction areas the detailed charts sometimes lack). false = only
  // dangers at the depth's own scale or more detailed.
  bool coarseDangers = true;
  // A rock / wreck / obstruction AREA is overruled where a more detailed
  // chart has a sounding inside it, within coarseCheckNm of the place in
  // question, at least coarseClearDepthM deep (and deeper than the danger's
  // own depth): the detailed survey shows navigable water there, and the
  // less detailed chart's area is a generalisation (an "obstructions may
  // exist" zone, say). Point dangers are never overruled. 0 = never.
  double coarseCheckNm = 0.25;
  double coarseClearDepthM = 5.0;  // the plugin passes its safety depth
  double offsetE = 0.0;      // metres, as OpenCPN's CM93 offset dialog
  double offsetN = 0.0;
};

struct DepthResult {
  bool valid = false;      // a cell was found and searched
  std::string status;      // human-readable status / error

  bool haveLeast = false;  // a least depth was found
  double leastDepth = 0;   // metres below chart datum (negative = drying)
  LatLon leastPos{0, 0};   // where (nearest point of that object)
  double rangeNm = 0;      // from the vessel
  double bearingDeg = 0;   // true
  std::string leastClass;  // S-57 class that gave the least depth

  bool haveAtPos = false;  // depth area containing the vessel
  double atPosMin = 0;     // DRVAL1
  double atPosMax = 0;     // DRVAL2 (NaN if absent)
  std::string atPosClass;  // DEPARE / DRGARE
  char atPosScaleChar = ' ';  // scale band the depth area came from

  int unknownHazards = 0;  // OBSTRN/WRECKS/UWTROC in scope without VALSOU

  // DepthAreaHere mode only: charted dangers inside the vessel's depth area
  // whose VALSOU is shallower than the area's DRVAL1.
  int shallowHazards = 0;
  double shallowHazardMin = 0;  // shallowest of those, m

  int scaleIdx = -1;     // band ("slot", see kSlots) used, -1 none
  char scaleChar = ' ';  // its letter (CM93) or digit (ENC usage band)
  int cellIndex = 0;     // CM93 cell index (0 for an ENC)
  std::string cellName;  // ENC data set name, e.g. "AU5TAS01" ("" for CM93)
  double cellSwLat = 0, cellSwLon = 0, cellSizeDeg = 0;
  int objectsSearched = 0;

  // What was found in each band, most detailed first, e.g. "G- Fc En D+"
  // (ENC bands appear only where an ENC's extent includes the position):
  //   -  no cell file at this position
  //   x  cell file present but could not be read (see readError)
  //   c  cell present but its coverage (M_COVR) excludes the position
  //   n  cell covers the position but has no depth area there
  //   +  used
  //   .  not examined (a more detailed scale was used)
  std::string scaleTrace;
  std::string readError;  // first cell read error met, "" if none

  // Interpolated mode only
  std::vector<UsedSounding> soundings;  // nearest first
  bool enclosed = false;     // a sounding in each of the four quadrants
  bool clamped = false;      // estimate limited to the depth area's band
  double rawEstimate = 0;    // before limiting
  double soundingMin = 0;    // shallowest sounding used
  int landBlocked = 0;       // nearer soundings skipped: land in between

  // The vessel's position is on charted land (any mode): no depth
  bool onLand = false;

  // The vessel is over a danger (modes H and I): its depth limits the result
  bool dangerHere = false;
  double dangerHereDepth = 0;
  std::string dangerHereClass;
  bool dangerHereAssumed = false;  // no charted depth: depth from the rule
  char dangerHereScale = ' ';      // scale band it is charted at

  // A danger area under the vessel that a more detailed chart overrules
  // (see EngineConfig::coarseCheckNm); the first one met
  bool dangerOverruled = false;
  std::string overruledClass;
  char overruledScale = ' ';     // band of the danger
  double overruledByDepth = 0;   // the sounding that overrules it, m
  double overruledByDistNm = 0;  // its distance from the vessel
  char overruledByScale = ' ';   // its band
};

// ---------------------------------------------------------------------------
// Look-ahead: least depth and first shoal in a zone ahead of the vessel
// (implemented in lookahead.cpp)
// ---------------------------------------------------------------------------
enum class LookZone : int {
  Corridor = 0,  // rectangle along the course: length x (2 x half-width)
  Sector = 1,    // circular sector centred on the course: radius = length
};
enum class LookBasis : int {
  Time = 0,      // length = SOG x minutes, limited to min..max length
  Distance = 1,  // fixed length
};

struct LookAheadConfig {
  LookZone zone = LookZone::Corridor;
  LookBasis basis = LookBasis::Time;
  double minutes = 10.0;       // Time basis
  double distanceNm = 2.0;     // Distance basis
  double minLengthNm = 0.5;    // Time basis: shortest zone (also at low SOG)
  double maxLengthNm = 20.0;   // either basis: longest zone (hard cap 50)
  double halfWidthNm = 0.25;   // Corridor, each side of the track
  double halfAngleDeg = 22.5;  // Sector, each side of the course (max 90)
  double safetyDepthM = 5.0;   // shallower than this counts as a shoal
  double minSogKn = 0.5;       // below this the course comes from heading
  bool undepthedAsShoal = true;  // undepthed dangers count as shoals
};

// One object found in the zone
struct LookHit {
  double depth = 0;     // m below chart datum (NaN for land / undepthed)
  bool land = false;
  bool undepthed = false;
  std::string cls;      // S-57 class, "LNDARE" for land
  LatLon pos{0, 0};     // the point of the object nearest along the zone
  double aheadNm = 0;   // along-track distance (corridor) or range (sector)
  double rangeNm = 0;   // straight-line range from the vessel
  double bearingDeg = 0;  // true
  double timeMin = 0;   // aheadNm at SOG, NaN when SOG is below minSogKn
  char scaleChar = ' ';
  std::string cellName;  // ENC data set name ("" for CM93)
};

struct LookAheadResult {
  bool valid = false;    // zone built and searched
  std::string status;

  // Zone actually used
  LookZone zone = LookZone::Corridor;
  double courseDeg = 0;
  bool courseFromHeading = false;  // SOG below minSogKn: heading used
  double sogKn = 0;
  double lengthNm = 0;
  double halfWidthNm = 0;   // Corridor
  double halfAngleDeg = 0;  // Sector
  double safetyDepthM = 0;
  std::vector<LatLon> outline;  // closed outline, for drawing

  bool haveLeast = false;  // least charted depth in the zone (no land)
  LookHit least;
  bool haveShoal = false;  // first point ahead shallower than safety depth
  LookHit shoal;           // (also land; undepthed if undepthedAsShoal)
  int undepthed = 0;       // undepthed dangers in the zone
  bool haveUndepthed = false;
  LookHit nearestUndepthed;
  bool landInZone = false;

  // Scale used along the zone, e.g. "0.00-1.20 G, 1.20-5.00 E"
  std::string scaleTrace;
  char coarsestScale = ' ';  // least detailed scale band used anywhere
  int objectsSearched = 0;
};

class DepthEngine {
public:
  DepthEngine() = default;

  // Set the CM93 root directory (the one holding the 8-digit folders).
  // Loads the dictionary. Returns false with an error message on failure.
  bool setRoot(const std::string &root, std::string *err = nullptr);
  const std::string &root() const { return m_root; }
  bool cm93Ready() const { return m_dict.ok() && !m_root.empty(); }
  bool encReady() const { return !m_enc.entries().empty(); }
  // Any charts at all
  bool ready() const { return cm93Ready() || encReady(); }

  // The S-57 ENC cells: configure() it with the chart folders and step() it
  // until complete; the engine picks up new entries by itself.
  EncIndex &encIndex() { return m_enc; }

  // Drop cached cells (call after offsets change or charts are updated).
  void clearCache();

  DepthResult compute(double lat, double lon, const EngineConfig &cfg);

  // Look-ahead from (lat, lon) along cogDeg at sogKn. hdgDeg (NaN if
  // unknown) is used as the course when SOG is below cfg.minSogKn.
  // EngineConfig supplies offsets, a fixed scale and the object switches.
  LookAheadResult lookAhead(double lat, double lon, double cogDeg,
                            double sogKn, double hdgDeg,
                            const LookAheadConfig &lc, const EngineConfig &cfg);

  // Plain-text report for troubleshooting: for every scale, the cell files
  // found at (lat, lon), read errors, and the depth areas that contain it.
  std::string diagnostics(double lat, double lon, const EngineConfig &cfg);

  // Exposed for tests: search an already loaded cell.
  static void searchCell(const CellData &cell, double lat, double lon,
                         const EngineConfig &cfg, DepthResult &res);
  static bool cellCovers(const CellData &cell, double lat, double lon);

  // Test hook: inject a dictionary instead of loading one from disk.
  void setDictionaryForTest(const Dictionary &d, const std::string &root) {
    m_dict = d;
    m_root = root;
  }

private:
  DepthResult computeInterpolated(double lat, double lon, const EngineConfig &cfg);
  // Depth area containing the position, trying every scale (G first).
  bool lookupAreaAcrossScales(double lat, double lon, const EngineConfig &cfg,
                              DepthResult &res);
  std::shared_ptr<CellData> getCell(int cellIndex, int scaleIdx,
                                    const EngineConfig &cfg);
  std::shared_ptr<CellData> getEnc(size_t entry);
  void syncEnc();
  bool slotEnabled(int slot, const EngineConfig &cfg) const;
  // Bands in the order they are searched, most detailed first
  std::vector<int> slotsDown(const EngineConfig &cfg) const;
  // Charts of band `slot` whose extent contains the position (CM93: the
  // cell there; ENC: every cell of that usage band whose extent has it)
  std::vector<std::shared_ptr<CellData>> cellsAt(double lat, double lon, int slot,
                                                 const EngineConfig &cfg);
  // ... and those round it as well (CM93: that cell and its 8 neighbours;
  // ENC: cells within `reachNm`, at least a band-dependent neighbourhood)
  std::vector<std::shared_ptr<CellData>> cellsAround(double lat, double lon, int slot,
                                                     const EngineConfig &cfg,
                                                     double reachNm = 0);
  // Charts of band `slot` overlapping a lat/lon box (lon may be unwrapped)
  std::vector<std::shared_ptr<CellData>> cellsInBox(double minLat, double maxLat,
                                                    double minLon, double maxLon, int slot,
                                                    const EngineConfig &cfg);
  void checkOffsets(const EngineConfig &cfg);
  // True if a depth area, dredged area or land area of a band-`slot` chart
  // contains the position (the band "charts" this spot).
  bool chartedAt(double lat, double lon, int slot, const EngineConfig &cfg);
  // On land per the most detailed band that charts the position (water or
  // land) there. Sets slot to that band.
  bool onLandAt(double lat, double lon, const EngineConfig &cfg, int &slot);
  // Dangers under the vessel, at the scale of res's depth area (or the most
  // detailed scale with one); fills res.dangerHere*.
  void findDangerHere(double lat, double lon, const EngineConfig &cfg, DepthResult &res);
  // A danger area `f` of band `slot` overruled at (lat, lon) by a sounding
  // of a more detailed band (see EngineConfig::coarseCheckNm). Fills the
  // sounding's depth, distance and band.
  bool dangerOverruled(const Feature &f, int slot, double dangerDepth, double lat, double lon,
                       const EngineConfig &cfg, double *byDepth = nullptr,
                       double *byDistNm = nullptr, char *byScale = nullptr);
  // A gap in the depth areas: at the most detailed scale band that has
  // depth areas all round the position, none of them (nor land) contains
  // it. CM93 depth areas cover all charted water, so such a gap is occupied
  // by something else: a reef, rock, islet, or unsurveyed area. surroundMin
  // = least DRVAL1 of the depth areas met around it.
  bool holeAt(double lat, double lon, const EngineConfig &cfg, int &slot,
              double &surroundMin);
  // The same test in one band
  bool gapAt(double lat, double lon, int slot, const EngineConfig &cfg, double &surroundMin);

  std::string m_root;
  Dictionary m_dict;
  double m_cachedOffE = 0, m_cachedOffN = 0;
  bool m_cachedOcpnSnd = true;

  // LRU cache of loaded cells; nullptr entries mean "not on disk".
  std::map<long, std::shared_ptr<CellData>> m_cache;
  // Cells whose files exist but could not be read: key -> "file: reason"
  std::map<long, std::string> m_loadErrors;
  std::string loadError(int cellIndex, int scaleIdx) const;
  std::list<long> m_lru;
  static constexpr size_t kMaxLoaded = 64;

  // S-57 ENC cells
  EncIndex m_enc;
  unsigned m_encGen = ~0u;
  std::vector<size_t> m_band[7];  // entry indices per usage band, detailed first
  std::map<std::string, std::shared_ptr<CellData>> m_encCache;  // by path
  std::map<std::string, std::string> m_encErrors;               // path -> error
  std::list<std::string> m_encLru;
  static constexpr size_t kMaxEncLoaded = 48;
};

// Depth band of an area object, or false if it carries no depth.
//  DEPARE / DRGARE: DRVAL1 .. DRVAL2. With no DRVAL1 (charted as an area but
//    no depth given, e.g. a sand or mud flat): 0 m. CM93 intertidal areas
//    (ITDARE) without DRVAL1: DRVAL2, else 0 m.
//  SBDARE (seabed area, e.g. sand) that dries, covers and uncovers or is
//    awash (WATLEV 1, 2, 4, 5): 0 m.
// d2 is NaN when there is no upper limit.
bool area_depth_band(const Feature &f, double &d1, double &d2);
// A seabed area (SBDARE) with no depth and no water level: counts as 0 m
// only where no depth area contains the position ("depth area here").
bool bare_seabed_area(const Feature &f);

// What a rock / wreck / obstruction says about depth:
//  Charted  VALSOU given (v)
//  Drying   no VALSOU, WATLEV dries, covers and uncovers, or awash: v = 0
//  Unknown  no VALSOU, always under water or not stated (e.g. a reef)
//  NotDanger no VALSOU and CATOBS foul area / foul ground / fish haven:
//           safe to navigate over, a hazard only to anchoring or fishing
enum class DangerKind { Charted, Drying, Unknown, NotDanger };
DangerKind danger_depth(const Feature &f, double &v);
bool is_danger_class(const std::string &cls);

// Geometry helpers (exposed for tests)
namespace geo {
// Local flat projection around (lat0, lon0) in nautical miles (x east, y north)
void to_local(double lat0, double lon0, double lat, double lon, double &x,
              double &y);
void from_local(double lat0, double lon0, double x, double y, double &lat,
                double &lon);
// Even-odd point-in-polygon over all rings (holes handled).
bool point_in_rings(const std::vector<std::vector<LatLon>> &rings, double lat,
                    double lon);
// Nearest point on a polyline set to (lat, lon); returns distance in nm.
double nearest_on_parts(const std::vector<std::vector<LatLon>> &parts,
                        double lat, double lon, LatLon &nearest);
}  // namespace geo

}  // namespace cm93
