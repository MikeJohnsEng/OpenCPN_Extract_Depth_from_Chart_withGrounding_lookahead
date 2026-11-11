/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/

// ===========================================================================
//  USER SETTINGS - defaults
//
//  Every value here is the factory default. All of them can be changed at
//  run time in the plugin's Preferences dialog and are then stored in
//  OpenCPN's config file under [PlugIns/cm93depth_pi]; edit these only to
//  change what a fresh install starts with.
// ===========================================================================

// --- Charts ----------------------------------------------------------------

// Chart sources. The plugin reads CM93 cells and unencrypted S-57 ENC cells
// (*.000 base cells with their *.001, *.002 ... updates). Where charts of
// both kinds cover the vessel, the MOST DETAILED wins, in this order (least
// detailed first):
//   CM93 Z, A, ENC 1 Overview, CM93 B, ENC 2 General, CM93 C,
//   ENC 3 Coastal, CM93 D, CM93 E, ENC 4 Approach, CM93 F,
//   ENC 5 Harbour, CM93 G, ENC 6 Berthing.
// Switching a source off leaves only the other. Both default true.
static const bool DEF_USE_CM93 = true;
static const bool DEF_USE_ENC = true;

// ENC cells are looked for in all of OpenCPN's chart directories (Options >
// Charts > Chart Files) and their sub-folders, plus these folders,
// separated by ';'. Default empty: OpenCPN's chart directories only.
// Encrypted S-63 cells cannot be read and are skipped.
static const char *const DEF_ENC_EXTRA_DIRS = "";

// Time spent per timer tick reading ENC cell headers while the cell index
// is built, milliseconds. Default 150. The index is built once (and again
// for new or changed cells); it is kept in enc_index.txt in OpenCPN's
// plugin data folder, so later starts are quick. Raising it builds the
// index sooner but makes OpenCPN less responsive meanwhile. Range 20..2000.
static const int DEF_ENC_INDEX_MS = 150;

// CM93 root directory (the folder holding the 8-digit cell folders and
// CM93OBJ.DIC). Empty = search OpenCPN's chart directories for it.
static const char *const DEF_CM93_ROOT = "";

// CM93 scale band to read. -1 = auto: use the most detailed chart (CM93 G,
// then F, E ... Z, with the ENC bands interleaved as above) whose coverage
// (M_COVR) contains the vessel. 0..7 = force CM93 Z, A, B, C, D, E, F, G;
// ENC cells are then not used. Forcing a smaller scale (lower number) gives a
// bigger cell and fewer, more generalised depths; a larger one gives a
// smaller, more detailed cell that may not cover your position.
static const int DEF_SCALE_IDX = -1;

// Chart position offset, metres, applied to every cell. Same meaning and
// sign as OpenCPN's "CM93 Offset" dialog. Use only if your CM93 charts are
// visibly shifted against GPS. Default 0.
static const double DEF_OFFSET_E_M = 0.0;
static const double DEF_OFFSET_N_M = 0.0;

// --- Search ----------------------------------------------------------------

// 2 = "Depth area here" (default): the minimum depth (DRVAL1) of the
//     charted depth area the vessel is in, e.g. 15 for a 15-20 m area.
//     Dangers charted inside that area with no depth, or shallower than
//     DRVAL1, are counted and reported alongside it.
// 0 = least depth anywhere in the CM93 cell under the vessel. Near any
//     coast this is nearly always 0 m or drying, so of little use there.
// 1 = least depth within SEARCH_RADIUS_NM of the vessel.
// 3 = depth at the vessel interpolated from the nearest soundings (up to
//     four, one per quadrant where possible, inverse-distance weighted),
//     limited to the charted depth band there. Uses SEARCH_RADIUS_NM as the
//     farthest a sounding may be.
static const int DEF_SEARCH_MODE = 2;

// Radius for search mode 1, nautical miles. Default 1.0. Raising it looks
// further out (earlier warning, but more distant shoals dominate);
// lowering it reports only what is close by. Range 0.05 .. 50.
// In mode 3 it is the farthest a sounding may be from the vessel: raising
// it finds soundings in sparse water, but distant ones say less about the
// depth under the boat.
static const double DEF_SEARCH_RADIUS_NM = 1.0;

// Which chart objects count toward the least depth. All on by default.
static const bool DEF_USE_SOUNDINGS = true;  // SOUNDG spot soundings
static const bool DEF_USE_AREAS = true;      // DEPARE / ITDARE / DRGARE DRVAL1
static const bool DEF_USE_HAZARDS = true;    // OBSTRN / WRECKS / UWTROC VALSOU
static const bool DEF_USE_CONTOURS = true;   // DEPCNT VALDCO

// Interpolated mode (3): how the soundings are chosen.
// false (default) = simply the four nearest soundings.
// true = the nearest sounding in each direction (NE, SE, SW, NW) first, so
//        the boat is surrounded where possible, then the nearest remaining.
//        Can reach for a distant sounding when one direction is empty;
//        best kept with a modest radius.
static const bool DEF_INTERP_QUADRANTS = false;

// Soundings as OpenCPN draws them. OpenCPN's CM93 reader keeps only the
// first sounding of a record that holds several (each further one is
// overwritten by the next record), so the chart shows fewer soundings than
// the cell stores. true (default) = use exactly the soundings on the chart.
// false = every sounding stored in the cell, including ones OpenCPN never
// draws. Config file only: [PlugIns/cm93depth_pi] OpenCpnSoundings=0/1.
static const bool DEF_OPENCPN_SOUNDINGS = true;

// Interpolated mode (3): leave out soundings with charted land between them
// and the vessel (across a headland, behind an island). Default true. Each
// sounding is checked against the coastline of its own chart scale. Off
// = nearest soundings by distance only, as before; with a large radius
// that can reach round a headland into a different depth regime.
static const bool DEF_INTERP_LAND_CHECK = true;

// Depth taken when the vessel is over a rock, wreck or obstruction (a
// submerged reef, say) that has no charted depth, in "depth area here" and
// interpolated modes. Ones charted as drying, covering and uncovering or
// awash are 0 m regardless; foul areas / foul ground / fish havens are not
// treated as dangers.
// 0 (default) = 0 m: assume it may come to the surface. Conservative; a
//     sizeable reef area then reads 0 m over all of it.
// 1 = the least depth (DRVAL1) of the depth area it lies in, the rule ECDIS
//     uses for undepthed dangers (IHO S-52, DEPVAL02). Less alarming, but
//     the reef may well be shallower than its surroundings.
static const int DEF_UNDEPTHED_DANGER_RULE = 0;

// Rocks, wrecks and obstructions count even when charted only on a
// smaller-scale chart than the one the depth comes from (CM93's small-scale
// cells carry obstruction areas that its detailed cells sometimes lack).
// Default true: conservative. false = only dangers on the depth's own chart
// or a more detailed one (fewer alarms from generalised small-scale areas).
static const bool DEF_COARSE_DANGERS = true;

// A rock / wreck / obstruction AREA is overruled where a MORE DETAILED chart
// has a sounding inside it, within this many nautical miles of the vessel
// (or of where the look-ahead zone meets it), at least the safety depth
// deep. Small-scale charts mark whole zones as obstruction areas ("numerous
// structures and submerged obstructions exist in certain areas indicated on
// this chart; see larger scale charts"); where the detailed chart has
// actually sounded navigable depth inside such a zone next to the vessel,
// that sounding is the better evidence. A real reef missing from the
// detailed chart has no deep detailed soundings inside its outline beside
// the vessel, so it still counts. Point dangers (rocks, wrecks charted as
// points) are never overruled.
// Default 0.25 nm (460 m). Raising it lets soundings farther away overrule
// (fewer false 0 m readings, more risk of discounting a real but poorly
// placed danger); lowering it needs a detailed sounding closer by. 0 = never
// overrule (as version 1.5.0 and earlier). Range 0 .. 2.
static const double DEF_COARSE_DANGER_CHECK_NM = 0.25;

// --- Output ----------------------------------------------------------------

// 0 = off, 1 = plugin's own serial port, 2 = an OpenCPN output connection.
// Default 0 so nothing is transmitted until you pick a port.
static const int DEF_OUTPUT_ROUTE = 0;

// Serial port for route 1, e.g. "/dev/ttyUSB0" or "COM5". Default empty.
static const char *const DEF_SERIAL_PORT = "";

// Baud rate for route 1. Default 4800 (NMEA 0183 standard). 38400 for
// high-speed NMEA (0183-HS). Must match the receiver.
static const int DEF_BAUD = 4800;

// Five-character proprietary sentence address. Default "PCMLD". Must be
// 'P' followed by four upper-case letters/digits.
static const char *const DEF_SENTENCE_ADDRESS = "PCMLD";

// Calculation and transmit interval, milliseconds. Default 1000 (1 Hz).
// Lower = fresher data and more serial traffic (one sentence of up to ~75
// bytes per interval, two with the look-ahead on: at 4800 baud that is up
// to ~16% / ~30% of the link at 1 Hz). Range 200..10000.
static const int DEF_INTERVAL_MS = 1000;

// Keep sending (status field = V, empty depth fields) when there is no
// fix, no chart or no depth, so the receiver can tell "no data" from
// "link dead". Default true.
static const bool DEF_SEND_WHEN_INVALID = true;

// A fix not updated for this many seconds is treated as lost. Default 10.
// Raising it tolerates GPS dropouts longer before flagging V; lowering it
// flags lost fixes sooner.
static const int DEF_STALE_FIX_SEC = 10;

// --- Look-ahead ------------------------------------------------------------
//
// Searches a zone ahead of the vessel for the least charted depth and for
// the FIRST SHOAL: the nearest point ahead shallower than the safety depth,
// land, or (option) a rock / wreck / obstruction with no charted depth. Sent
// as a second sentence, $PCMLA, on the same output as $PCMLD.

// Calculate and send the look-ahead. Default true.
static const bool DEF_LA_ENABLED = true;

// Shape of the zone. 0 = corridor (default): a strip along the course, as
// wide as twice LA_HALF_WIDTH_NM; "distance ahead" is measured along the
// course. 1 = sector: a pie slice of +-LA_HALF_ANGLE_DEG about the course;
// "distance ahead" is the range. The corridor matches holding a course; the
// sector also catches shoals you could turn onto, at the cost of more false
// alarms abeam when it is long.
static const int DEF_LA_ZONE = 0;

// How the length is set. 0 = time (default): SOG x LA_MINUTES, never
// shorter than LA_MIN_LENGTH_NM or longer than LA_MAX_LENGTH_NM. 1 = fixed
// distance LA_DISTANCE_NM.
static const int DEF_LA_BASIS = 0;

// Time basis: how far ahead in time, minutes. Default 10. Raising it warns
// earlier but takes in more water you may never reach; lowering it warns
// later. Range 1 .. 120.
static const double DEF_LA_MINUTES = 10.0;

// Distance basis: zone length, nm. Default 2.0. Range 0.05 .. 50.
static const double DEF_LA_DISTANCE_NM = 2.0;

// Time basis: shortest zone, nm (also used when slow or stopped). Default
// 0.5. Raising it keeps a useful look-ahead at low speed.
static const double DEF_LA_MIN_LENGTH_NM = 0.5;

// Longest zone, nm, either basis. Default 5.0. Hard limit 50. Raising it
// lets a fast boat look further; the search takes longer over bigger areas.
static const double DEF_LA_MAX_LENGTH_NM = 5.0;

// Corridor half-width each side of the course, nm. Default 0.1 (185 m).
// Raising it allows for cross-track error and leeway (and finds more);
// lowering it reports only what is close to the track. Range 0.01 .. 5.
static const double DEF_LA_HALF_WIDTH_NM = 0.1;

// Sector half-angle each side of the course, degrees. Default 20. Range
// 1 .. 90. Raising it covers more of the water you could steer into.
static const double DEF_LA_HALF_ANGLE_DEG = 20.0;

// Safety depth, metres below chart datum. Anything charted shallower is a
// shoal. Default 5.0. Set it to draft + under-keel margin + an allowance
// for chart error (CM93 depths are old and unsurveyed by modern standards).
// No tide is applied: chart datum is close to the lowest tide.
static const double DEF_LA_SAFETY_DEPTH_M = 5.0;

// Below this SOG, knots, COG is unreliable: the course is taken from the
// heading (if OpenCPN has one), the zone has its minimum length, and no
// times are given. Default 0.5.
static const double DEF_LA_MIN_SOG_KN = 0.5;

// Rocks, wrecks and obstructions with no charted depth count as shoals.
// Default true (they may be awash). Off: they are only counted.
static const bool DEF_LA_UNDEPTHED_AS_SHOAL = true;

// Address of the look-ahead sentence. Default "PCMLA". 'P' + four capitals
// or digits; must differ from SENTENCE_ADDRESS.
static const char *const DEF_LA_SENTENCE_ADDRESS = "PCMLA";

// Draw the zone (green clear, red with a shoal) and a ring at the first
// shoal on the chart. Default true.
static const bool DEF_LA_SHOW_ZONE = true;

// --- Grounding alarm -------------------------------------------------------
//
// Sounds and shows an always-on-top alarm window on this PC when the
// look-ahead finds a shoal close ahead, or the depth at the vessel is below
// the safety depth. Silence mutes the sound for ALARM_SILENCE_SEC; Acknowledge
// stops it while the condition lasts (it sounds again if the condition gets
// worse: half the time to the shoal, or depth here below the safety depth).
// The alarm clears by itself when the condition clears.

// Alarm on. Default true. Needs the look-ahead for the shoal-ahead alarm.
static const bool DEF_ALARM_ENABLED = true;

// Shoal-ahead alarm when the first shoal is this many minutes away or less,
// at the current SOG. Default 6. Raising it warns earlier (more alarms that
// a course change will make unnecessary); lowering it leaves less time to
// act. Keep it below the look-ahead time (LA_MINUTES), or the look-ahead
// zone limits it. Range 0.5 .. 60.
static const double DEF_ALARM_MINUTES = 6.0;

// ... or when it is this close, nm, whatever the speed (and when SOG is too
// low for a time). Default 0.2 (370 m). Range 0.01 .. 5.
static const double DEF_ALARM_DISTANCE_NM = 0.2;

// Also alarm when the depth AT the vessel is below the safety depth (modes
// "depth area here" and interpolated), or the position is on charted land.
// Default true.
static const bool DEF_ALARM_DEPTH_HERE = true;

// Sound file (WAV). Empty (default) = OpenCPN's own sounds/beep_ssl.wav.
static const char *const DEF_ALARM_SOUND = "";

// While unacknowledged the sound repeats every this many seconds. Default 5.
static const int DEF_ALARM_REPEAT_SEC = 5;

// Silence mutes the sound for this many seconds (IMO bridge alert practice:
// 30). Default 30. Range 5 .. 300.
static const int DEF_ALARM_SILENCE_SEC = 30;

// Also send the alarm as an IEC 61162-1 $IIALR sentence on the plugin's
// NMEA output (alarm 001, condition A/V, acknowledged A/V), each interval
// while raised and once when it clears. Default false.
static const bool DEF_ALARM_SEND_ALR = false;

// The watch officer can switch the alarm off completely (button in the
// display window and in the alarm window), e.g. for sheltered water that is
// navigable but shallower than the safety depth. While off, the display
// window says so in amber. It is never saved: the alarm is on again when
// OpenCPN starts. It also comes back on by itself after this many minutes;
// 0 (default) = stays off until switched on. Range 0 .. 720.
static const int DEF_ALARM_OFF_REARM_MIN = 0;

// --- Display ---------------------------------------------------------------

// Put a temporary (not saved) mark on the chart at the least-depth spot.
static const bool DEF_MARK_ON_CHART = true;
// OpenCPN mark icon name for that mark. Default "triangle".
static const char *const DEF_MARK_ICON = "triangle";

// Display window starts minimised: just the depth, what it is, and a shoal
// warning when the look-ahead has one. The Enlarge / Minimise button in the
// window switches; the choice is remembered. Default true.
static const bool DEF_PANEL_COMPACT = true;

// ===========================================================================
//  End of user settings
// ===========================================================================

#include "cm93depth_pi.h"

#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>

#include <algorithm>
#include <cmath>

#ifndef CM93DEPTH_NO_GL
#ifdef __WXMSW__
#include <windows.h>
#endif
#ifdef __WXOSX__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include "alarm_dialog.h"
#include "depth_panel.h"
#include "icons.h"
#include "nmea_out.h"
#include "prefs_dialog.h"

static const int kChartRetryMs = 30000;  // re-search for charts if missing
static const int kSerialRetryMs = 5000;  // re-open a failed serial port

// The two entry points OpenCPN looks up when it loads the plugin
#ifdef _WIN32
#define CM93_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CM93_PLUGIN_EXPORT DECL_EXP
#endif

extern "C" CM93_PLUGIN_EXPORT opencpn_plugin *create_pi(void *ppimgr) {
  return new cm93depth_pi(ppimgr);
}
extern "C" CM93_PLUGIN_EXPORT void destroy_pi(opencpn_plugin *p) { delete p; }

cm93depth_pi::cm93depth_pi(void *ppimgr)
    : wxEvtHandler(), opencpn_plugin_118(ppimgr), m_timer(this) {
  m_s.useCm93 = DEF_USE_CM93;
  m_s.useEnc = DEF_USE_ENC;
  m_s.encExtraDirs = DEF_ENC_EXTRA_DIRS;
  m_s.encIndexMs = DEF_ENC_INDEX_MS;
  m_s.cm93Root = DEF_CM93_ROOT;
  m_s.scaleIdx = DEF_SCALE_IDX;
  m_s.offsetE = DEF_OFFSET_E_M;
  m_s.offsetN = DEF_OFFSET_N_M;
  m_s.mode = DEF_SEARCH_MODE;
  m_s.radiusNm = DEF_SEARCH_RADIUS_NM;
  m_s.useSoundings = DEF_USE_SOUNDINGS;
  m_s.useAreas = DEF_USE_AREAS;
  m_s.useHazards = DEF_USE_HAZARDS;
  m_s.useContours = DEF_USE_CONTOURS;
  m_s.interpQuadrants = DEF_INTERP_QUADRANTS;
  m_s.openCpnSoundings = DEF_OPENCPN_SOUNDINGS;
  m_s.interpLandCheck = DEF_INTERP_LAND_CHECK;
  m_s.undepthedDangerRule = DEF_UNDEPTHED_DANGER_RULE;
  m_s.coarseDangers = DEF_COARSE_DANGERS;
  m_s.coarseCheckNm = DEF_COARSE_DANGER_CHECK_NM;
  m_s.outputRoute = DEF_OUTPUT_ROUTE;
  m_s.serialPort = DEF_SERIAL_PORT;
  m_s.baud = DEF_BAUD;
  m_s.sentenceAddress = DEF_SENTENCE_ADDRESS;
  m_s.intervalMs = DEF_INTERVAL_MS;
  m_s.sendWhenInvalid = DEF_SEND_WHEN_INVALID;
  m_s.staleFixSec = DEF_STALE_FIX_SEC;
  m_s.laEnabled = DEF_LA_ENABLED;
  m_s.laZone = DEF_LA_ZONE;
  m_s.laBasis = DEF_LA_BASIS;
  m_s.laMinutes = DEF_LA_MINUTES;
  m_s.laDistanceNm = DEF_LA_DISTANCE_NM;
  m_s.laMinLengthNm = DEF_LA_MIN_LENGTH_NM;
  m_s.laMaxLengthNm = DEF_LA_MAX_LENGTH_NM;
  m_s.laHalfWidthNm = DEF_LA_HALF_WIDTH_NM;
  m_s.laHalfAngleDeg = DEF_LA_HALF_ANGLE_DEG;
  m_s.laSafetyDepthM = DEF_LA_SAFETY_DEPTH_M;
  m_s.laMinSogKn = DEF_LA_MIN_SOG_KN;
  m_s.laUndepthedAsShoal = DEF_LA_UNDEPTHED_AS_SHOAL;
  m_s.laSentenceAddress = DEF_LA_SENTENCE_ADDRESS;
  m_s.laShowZone = DEF_LA_SHOW_ZONE;
  m_s.alarmEnabled = DEF_ALARM_ENABLED;
  m_s.alarmMinutes = DEF_ALARM_MINUTES;
  m_s.alarmDistanceNm = DEF_ALARM_DISTANCE_NM;
  m_s.alarmDepthHere = DEF_ALARM_DEPTH_HERE;
  m_s.alarmSound = DEF_ALARM_SOUND;
  m_s.alarmRepeatSec = DEF_ALARM_REPEAT_SEC;
  m_s.alarmSilenceSec = DEF_ALARM_SILENCE_SEC;
  m_s.alarmSendAlr = DEF_ALARM_SEND_ALR;
  m_s.alarmOffRearmMin = DEF_ALARM_OFF_REARM_MIN;
  m_s.markOnChart = DEF_MARK_ON_CHART;
  m_s.markIcon = DEF_MARK_ICON;
  m_s.panelVisible = true;
  m_s.panelCompact = DEF_PANEL_COMPACT;
  m_s.panelPos = wxDefaultPosition;
}

cm93depth_pi::~cm93depth_pi() {}

int cm93depth_pi::Init() {
  m_parentWindow = GetOCPNCanvasWindow();
  loadConfig();

  m_toolId = InsertPlugInTool(_("CM93 least depth"), GetPlugInBitmap(),
                              GetPlugInBitmap(), wxITEM_CHECK,
                              _("CM93 least depth"), _T(""), nullptr, -1, 0,
                              this);

  resolveRoot(true);
  startEncIndex();
  Bind(wxEVT_TIMER, &cm93depth_pi::OnTimer, this, m_timer.GetId());
  m_timer.Start(std::clamp(m_s.intervalMs, 200, 10000));

  if (m_s.panelVisible) togglePanel(true);

  return (WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL | WANTS_CONFIG |
          WANTS_NMEA_EVENTS | WANTS_PREFERENCES | WANTS_OVERLAY_CALLBACK |
          WANTS_OPENGL_OVERLAY_CALLBACK);
}

bool cm93depth_pi::DeInit() {
  // Called when the plugin is disabled in Options > Plugins and when
  // OpenCPN closes. Everything this plugin put on screen must be gone when
  // this returns: OpenCPN may unload the DLL straight after, and a window
  // still alive then (wxWidgets deletes top-level windows later, at idle
  // time, when Destroy() is used) would run code that no longer exists.
  m_timer.Stop();
  Unbind(wxEVT_TIMER, &cm93depth_pi::OnTimer, this, m_timer.GetId());
  removeMarker();
  closeAlarmWindow();
  if (m_panel) {
    m_s.panelPos = m_panel->GetPosition();
    m_panel->Hide();
    DepthPanel *p = m_panel;
    m_panel = nullptr;  // nothing may reach it from here on
    delete p;           // now, not at the next idle time
  }
  if (m_toolId >= 0) {
    RemovePlugInTool(m_toolId);
    m_toolId = -1;
  }
  saveConfig();
  m_serial.close();
  m_look = cm93::LookAheadResult();  // no zone left to draw
  if (m_parentWindow) RequestRefresh(m_parentWindow);
  return true;
}

int cm93depth_pi::GetAPIVersionMajor() { return 1; }
int cm93depth_pi::GetAPIVersionMinor() { return 18; }
int cm93depth_pi::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }
int cm93depth_pi::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }
int cm93depth_pi::GetPlugInVersionPatch() { return PLUGIN_VERSION_PATCH; }

wxBitmap *cm93depth_pi::GetPlugInBitmap() {
  if (!m_icon) m_icon = std::make_unique<wxBitmap>(MakeLeastDepthIcon(32));
  return m_icon.get();
}

wxString cm93depth_pi::GetCommonName() { return _T("CM93 Least Depth"); }
wxString cm93depth_pi::GetShortDescription() {
  return _("Least charted depth from CM93 and S-57 charts, output as NMEA");
}
wxString cm93depth_pi::GetLongDescription() {
  return _(
      "Reads the CM93 vector chart cell or unencrypted S-57 ENC cell under the "
      "vessel (the most detailed one) and finds the least charted depth in it, "
      "or within a set radius, from soundings, depth areas, depth contours and "
      "dangers with a charted depth.\n\n"
      "Shows the result in a small window, marks it on the chart, and sends "
      "it as the proprietary NMEA 0183 sentence $PCMLD on a serial port or "
      "an OpenCPN output connection.\n\n"
      "Looks ahead along the course over a set time or distance and reports "
      "the least depth there and the first point shallower than a safety "
      "depth, as the sentence $PCMLA, and draws the zone on the chart.\n\n"
      "Depths are charted values below chart datum. No tide is applied. Not "
      "for use as the sole means of navigation.");
}

int cm93depth_pi::GetToolbarToolCount() { return 1; }

void cm93depth_pi::OnToolbarToolCallback(int id) {
  if (id != m_toolId) return;
  togglePanel(!(m_panel && m_panel->IsShown()));
}

void cm93depth_pi::togglePanel(bool show) {
  if (show) {
    if (!m_panel) {
      m_panel = new DepthPanel(m_parentWindow, this);
      if (m_s.panelPos != wxDefaultPosition) m_panel->Move(m_s.panelPos);
    }
    m_panel->Show();
    m_panel->refresh();
  } else if (m_panel) {
    m_s.panelPos = m_panel->GetPosition();
    m_panel->Hide();
  }
  m_s.panelVisible = show;
  if (m_toolId >= 0) SetToolbarItemState(m_toolId, show);
}

void cm93depth_pi::onPanelClosed() { togglePanel(false); }

wxString cm93depth_pi::diagnosticsReport() {
  std::string s = m_engine.diagnostics(m_lat, m_lon, engineConfig());
  s += "\nsoundings decoded " + std::string(m_s.openCpnSoundings ? "as OpenCPN draws them" : "as stored") +
       "\nmode " + std::to_string(m_s.mode) + ", scale setting " +
       std::to_string(m_s.scaleIdx) + "\nlast result: " + m_result.status +
       "\nscales: " + m_result.scaleTrace + "\n";
  {
    const cm93::DepthResult &r = m_result;
    char b[256];
    if (r.haveLeast) {
      std::snprintf(b, sizeof b, "depth %.1f m from %s", r.leastDepth, r.leastClass.c_str());
      s += b;
      if (r.haveAtPos) {
        std::snprintf(b, sizeof b, "; band at vessel %g to %g m (%c)", r.atPosMin, r.atPosMax,
                      r.atPosScaleChar);
        s += b;
      }
      if (r.clamped) {
        std::snprintf(b, sizeof b, "; estimate %.1f m limited", r.rawEstimate);
        s += b;
      }
      s += "\n";
    }
    if (r.dangerHere) {
      std::snprintf(b, sizeof b, "over %s (%c scale), %s %.1f m\n", r.dangerHereClass.c_str(),
                    r.dangerHereScale,
                    r.dangerHereAssumed ? "no charted depth, taken as" : "charted",
                    r.dangerHereDepth);
      s += b;
    }
    for (const auto &sd : r.soundings) {
      std::snprintf(b, sizeof b, "  used %.1f m at %.3f nm (%c), weight %.2f\n", sd.depth,
                    sd.distNm, sd.scaleChar, sd.weight);
      s += b;
    }
    if (r.landBlocked) s += "  " + std::to_string(r.landBlocked) + " sounding(s) left out: land between\n";
    if (r.dangerOverruled) {
      std::snprintf(b, sizeof b,
                    "over %s area (%c chart), not counted: overruled by a %.1f m sounding "
                    "(%c chart) %.2f nm away inside it\n",
                    r.overruledClass.c_str(), r.overruledScale, r.overruledByDepth,
                    r.overruledByScale, r.overruledByDistNm);
      s += b;
    }
  }
  if (m_s.laEnabled) {
    char b[256];
    std::snprintf(b, sizeof b,
                  "look-ahead: %s, course %.1f%s, SOG %.1f kn, length %.2f nm\n"
                  "look-ahead scales: %s\n",
                  m_look.status.c_str(), m_look.courseDeg,
                  m_look.courseFromHeading ? " (heading)" : "", m_look.sogKn,
                  m_look.lengthNm, m_look.scaleTrace.c_str());
    s += b;
    s += "last $PCMLA: " + std::string(m_lastLookSentence.ToUTF8()) + "\n";
  }
  return wxString::FromUTF8(s.c_str());
}

void cm93depth_pi::ShowPreferencesDialog(wxWindow *parent) {
  showPreferences(parent);
}

void cm93depth_pi::showPreferences(wxWindow *parent) {
  PrefsDialog dlg(parent ? parent : m_parentWindow, this, m_s);
  if (dlg.ShowModal() == wxID_OK) applySettings(dlg.result());
}

void cm93depth_pi::applySettings(const PluginSettings &s) {
  bool rootChanged = s.cm93Root != m_s.cm93Root;
  bool serialChanged = s.serialPort != m_s.serialPort || s.baud != m_s.baud ||
                       s.outputRoute != m_s.outputRoute;
  bool markChanged = s.markOnChart != m_s.markOnChart || s.markIcon != m_s.markIcon;
  bool encChanged = s.useEnc != m_s.useEnc || s.encExtraDirs != m_s.encExtraDirs;
  bool cm93Changed = s.useCm93 != m_s.useCm93;
  m_s = s;
  if (rootChanged || cm93Changed) resolveRoot(true);
  if (encChanged) startEncIndex();
  if (serialChanged) {
    m_serial.close();
    m_lastOpenTryMs = 0;
    m_outStatus.clear();
  }
  if (markChanged) removeMarker();
  m_timer.Start(std::clamp(m_s.intervalMs, 200, 10000));
  saveConfig();
  update();  // recompute now so the panel reflects the change
}

void cm93depth_pi::SetColorScheme(PI_ColorScheme cs) {
  m_colorScheme = cs;
  if (m_panel) m_panel->setColorScheme(cs);
}

// ---------------------------------------------------------------------------
// Position
// ---------------------------------------------------------------------------
void cm93depth_pi::SetPositionFixEx(PlugIn_Position_Fix_Ex &pfix) {
  if (!std::isfinite(pfix.Lat) || !std::isfinite(pfix.Lon)) return;
  bool changed = pfix.FixTime != m_lastFixTime || pfix.Lat != m_lat ||
                 pfix.Lon != m_lon;
  m_lat = pfix.Lat;
  m_lon = pfix.Lon;
  m_cog = pfix.Cog;  // NaN when not available
  m_sog = pfix.Sog;
  m_hdt = pfix.Hdt;
  if (changed) {
    m_lastFixTime = pfix.FixTime;
    m_lastFixChangeMs = wxGetUTCTimeMillis();
  }
}

// ---------------------------------------------------------------------------
// Chart root
// ---------------------------------------------------------------------------
void cm93depth_pi::resolveRoot(bool force) {
  wxLongLong now = wxGetUTCTimeMillis();
  if (!force && (now - m_lastRootTryMs) < kChartRetryMs) return;
  m_lastRootTryMs = now;
  if (!m_s.useCm93) {
    m_rootStatus = _("CM93: switched off");
    return;
  }

  std::string err;
  if (!m_s.cm93Root.IsEmpty()) {
    std::string root = cm93::find_cm93_root(m_s.cm93Root.ToStdString(wxConvUTF8), 2);
    if (root.empty()) root = m_s.cm93Root.ToStdString(wxConvUTF8);
    if (m_engine.setRoot(root, &err)) {
      m_rootStatus = _("CM93 root: ") + wxString::FromUTF8(root);
    } else {
      m_rootStatus = _("CM93 charts not usable: ") + wxString::FromUTF8(err);
    }
    return;
  }

  // Auto-detect from OpenCPN's chart directories
  wxArrayString dirs = GetChartDBDirArrayString();
  for (const wxString &d : dirs) {
    std::string root = cm93::find_cm93_root(d.ToStdString(wxConvUTF8), 2);
    if (root.empty()) continue;
    if (m_engine.setRoot(root, &err)) {
      m_rootStatus = _("CM93 root (auto): ") + wxString::FromUTF8(root);
      return;
    }
  }
  m_rootStatus = _("No CM93 charts found in OpenCPN's chart directories "
                   "(set the CM93 folder in Preferences if you have them).");
  if (!err.empty()) m_rootStatus += _T(" (") + wxString::FromUTF8(err) + _T(")");
}

// ---------------------------------------------------------------------------
// S-57 ENC cells: index the chart folders a little each tick
// ---------------------------------------------------------------------------
std::vector<std::string> cm93depth_pi::encDirs() const {
  std::vector<std::string> dirs;
  if (!m_s.useEnc) return dirs;
  wxArrayString chartDirs = GetChartDBDirArrayString();
  for (const wxString &d : chartDirs) dirs.push_back(d.ToStdString(wxConvUTF8));
  wxStringTokenizer tk(m_s.encExtraDirs, _T(";"));
  while (tk.HasMoreTokens()) {
    wxString t = tk.GetNextToken().Trim().Trim(false);
    if (!t.IsEmpty()) dirs.push_back(t.ToStdString(wxConvUTF8));
  }
  return dirs;
}

void cm93depth_pi::startEncIndex() {
  std::vector<std::string> dirs = encDirs();
  std::string cache;
  if (wxString *priv = GetpPrivateApplicationDataLocation()) {
    wxFileName fn(*priv, _T("enc_index.txt"));
    fn.AppendDir(_T("plugins"));
    fn.AppendDir(_T("cm93depth_pi"));
    cache = fn.GetFullPath().ToStdString(wxConvUTF8);
  }
  m_engine.encIndex().configure(dirs, cache);
  m_encDirs = dirs;
  m_lastEncDirCheckMs = wxGetUTCTimeMillis();
}

void cm93depth_pi::rescanEnc() {
  startEncIndex();
  update();
}

void cm93depth_pi::stepEnc() {
  if (m_s.useEnc) {
    // Chart directories added or removed in OpenCPN: index again
    wxLongLong now = wxGetUTCTimeMillis();
    if (now - m_lastEncDirCheckMs > 60000) {
      m_lastEncDirCheckMs = now;
      if (encDirs() != m_encDirs) startEncIndex();
    }
    cm93::EncIndex &ix = m_engine.encIndex();
    if (!ix.complete()) ix.step(std::clamp(m_s.encIndexMs, 20, 2000));
    m_encStatus = wxString::FromUTF8(ix.summary());
  } else {
    m_encStatus = _("S-57: switched off");
  }
}

wxString cm93depth_pi::chartStatus() const {
  return m_rootStatus + _T("\n") + m_encStatus;
}

cm93::EngineConfig cm93depth_pi::engineConfig() const {
  cm93::EngineConfig c;
  c.scaleIdx = m_s.scaleIdx;
  c.useCm93 = m_s.useCm93;
  c.useEnc = m_s.useEnc;
  c.mode = static_cast<cm93::SearchMode>(m_s.mode);
  c.radiusNm = m_s.radiusNm;
  c.useSoundings = m_s.useSoundings;
  c.useAreas = m_s.useAreas;
  c.useHazards = m_s.useHazards;
  c.useContours = m_s.useContours;
  c.interpQuadrants = m_s.interpQuadrants;
  c.openCpnSoundings = m_s.openCpnSoundings;
  c.landCheck = m_s.interpLandCheck;
  c.undepthedDangerRule = m_s.undepthedDangerRule;
  c.coarseDangers = m_s.coarseDangers;
  c.coarseCheckNm = m_s.coarseCheckNm;
  c.coarseClearDepthM = m_s.laSafetyDepthM;
  c.offsetE = m_s.offsetE;
  c.offsetN = m_s.offsetN;
  return c;
}

cm93::LookAheadConfig cm93depth_pi::lookConfig() const {
  cm93::LookAheadConfig c;
  c.zone = static_cast<cm93::LookZone>(m_s.laZone);
  c.basis = static_cast<cm93::LookBasis>(m_s.laBasis);
  c.minutes = m_s.laMinutes;
  c.distanceNm = m_s.laDistanceNm;
  c.minLengthNm = m_s.laMinLengthNm;
  c.maxLengthNm = m_s.laMaxLengthNm;
  c.halfWidthNm = m_s.laHalfWidthNm;
  c.halfAngleDeg = m_s.laHalfAngleDeg;
  c.safetyDepthM = m_s.laSafetyDepthM;
  c.minSogKn = m_s.laMinSogKn;
  c.undepthedAsShoal = m_s.laUndepthedAsShoal;
  return c;
}

// ---------------------------------------------------------------------------
// Periodic work: compute, mark, transmit, refresh panel
// ---------------------------------------------------------------------------
void cm93depth_pi::OnTimer(wxTimerEvent &) { update(); }

void cm93depth_pi::update() {
  wxLongLong now = wxGetUTCTimeMillis();
  m_fixValid = std::isfinite(m_lat) && std::isfinite(m_lon) &&
               m_lastFixChangeMs != 0 &&
               (now - m_lastFixChangeMs) <= (wxLongLong)m_s.staleFixSec * 1000;

  if (m_s.useCm93 && !m_engine.cm93Ready()) resolveRoot(false);
  stepEnc();

  cm93::DepthResult r;
  if (!m_engine.ready()) {
    r.status = m_engine.encIndex().complete() ? "No charts: no CM93 folder and no S-57 cells"
                                              : "No charts yet: indexing S-57 cells";
  } else if (!std::isfinite(m_lat)) {
    r.status = "Waiting for position";
  } else {
    r = m_engine.compute(m_lat, m_lon, engineConfig());
    if (!m_fixValid) r.status = "Position stale: " + r.status;
  }
  m_result = r;

  updateMarker();

  cm93::SearchMode mode =
      static_cast<cm93::SearchMode>(m_s.mode);
  std::string addr = m_s.sentenceAddress.ToStdString();
  if (!cm93::valid_proprietary_address(addr)) addr = DEF_SENTENCE_ADDRESS;
  std::string sentence = cm93::build_least_depth_sentence(
      m_result, addr, mode, m_s.radiusNm, m_fixValid);
  bool valid = sentence.find(",A*") != std::string::npos;

  if (m_s.outputRoute == (int)OutputRoute::Off) {
    m_outStatus = _("Output off");
    m_lastSentence = wxString::FromUTF8(sentence).Trim();
  } else if (valid || m_s.sendWhenInvalid) {
    sendOutput(sentence);
    m_lastSentence = wxString::FromUTF8(sentence).Trim();
  }

  // ---- look-ahead
  if (m_s.laEnabled) {
    cm93::LookAheadResult la;
    la.zone = static_cast<cm93::LookZone>(m_s.laZone);
    la.safetyDepthM = m_s.laSafetyDepthM;
    if (!m_engine.ready()) {
      la.status = "No charts";
    } else if (!std::isfinite(m_lat)) {
      la.status = "Waiting for position";
    } else {
      la = m_engine.lookAhead(m_lat, m_lon, m_cog, m_sog, m_hdt, lookConfig(),
                              engineConfig());
      if (!m_fixValid) la.status = "Position stale: " + la.status;
    }
    m_look = la;
    std::string laddr = m_s.laSentenceAddress.ToStdString();
    if (!cm93::valid_proprietary_address(laddr)) laddr = DEF_LA_SENTENCE_ADDRESS;
    std::string ls = cm93::build_lookahead_sentence(m_look, laddr, m_fixValid);
    bool lvalid = ls.find(",A*") != std::string::npos;
    if (m_s.outputRoute == (int)OutputRoute::Off) {
      m_lastLookSentence = wxString::FromUTF8(ls).Trim();
    } else if (lvalid || m_s.sendWhenInvalid) {
      sendOutput(ls);
      m_lastLookSentence = wxString::FromUTF8(ls).Trim();
    }
  } else {
    m_look = cm93::LookAheadResult();
    m_lastLookSentence.clear();
  }
  updateAlarm();

  // the zone moves with every fix; redraw while it is shown (or to clear it)
  if (overlayWanted() || m_overlayDrawn) RequestRefresh(m_parentWindow);

  if (m_panel && m_panel->IsShown()) m_panel->refresh();
}

// ---------------------------------------------------------------------------
// Grounding alarm
// ---------------------------------------------------------------------------
bool cm93depth_pi::playSound(const wxString &file) {
  wxString f = file;
  if (f.IsEmpty()) {
    wxString *share = GetpSharedDataLocation();
    if (share) {
      wxFileName fn(*share, _T("beep_ssl.wav"));
      fn.AppendDir(_T("sounds"));
      f = fn.GetFullPath();
    }
  }
  if (!f.IsEmpty() && wxFileExists(f) && PlugInPlaySoundEx(f, -1)) return true;
  wxBell();  // no sound file or no sound device: at least the system bell
  return false;
}

void cm93depth_pi::updateAlarm() {
  cm93::AlarmKind kind = cm93::AlarmKind::None;
  double severity = NAN;
  std::string text;
  const int64_t nowOff = (int64_t)wxGetUTCTimeMillis().GetValue();
  if (m_alarmOff && m_s.alarmOffRearmMin > 0 &&
      nowOff - m_alarmOffSinceMs >= (int64_t)m_s.alarmOffRearmMin * 60000)
    m_alarmOff = false;  // back on by itself
  if (m_s.alarmEnabled && !m_alarmOff) {
    cm93::AlarmConfig ac;
    ac.minutes = m_s.alarmMinutes;
    ac.distanceNm = m_s.alarmDistanceNm;
    ac.depthHere = m_s.alarmDepthHere;
    ac.safetyDepthM = m_s.laSafetyDepthM;
    ac.depthIsHere = m_s.mode == (int)cm93::SearchMode::DepthAreaHere ||
                     m_s.mode == (int)cm93::SearchMode::Interpolated;
    cm93::LookAheadResult none;
    kind = cm93::evaluate_alarm(m_result, m_s.laEnabled ? m_look : none, ac, m_fixValid,
                                severity, text);
  }
  const int64_t now = (int64_t)wxGetUTCTimeMillis().GetValue();
  const bool sound = m_alarm.update(now, kind, severity,
                                    (int64_t)std::max(1, m_s.alarmRepeatSec) * 1000);
  m_alarmText = wxString::FromUTF8(text.c_str());
  if (sound) playSound(m_s.alarmSound);

  if (m_alarm.raised()) {
    if (!m_alarmDlg) m_alarmDlg = new AlarmDialog(m_parentWindow, this);
    int silenced = 0;
    if (m_alarm.state() == cm93::AlarmLogic::State::Silenced)
      silenced = (int)std::max<int64_t>(0, (m_alarm.silenceUntilMs() - now + 999) / 1000);
    m_alarmDlg->show(m_alarmText, m_alarm.acknowledged(), silenced);
  } else {
    closeAlarmWindow();
  }

  // $IIALR on the NMEA output
  if (m_s.alarmSendAlr && m_s.outputRoute != (int)OutputRoute::Off) {
    wxDateTime t = wxDateTime::UNow().ToUTC();
    double utc = t.GetHour() * 3600.0 + t.GetMinute() * 60.0 + t.GetSecond() +
                 t.GetMillisecond() / 1000.0;
    if (m_alarm.raised()) {
      sendOutput(cm93::build_alr_sentence(utc, 1, true, m_alarm.acknowledged(), text));
      m_alrSentActive = true;
    } else if (m_alrSentActive) {
      sendOutput(cm93::build_alr_sentence(utc, 1, false, false, "GROUNDING ALARM CLEAR"));
      m_alrSentActive = false;
    }
  }
}

// Called from the alarm window's own buttons: only refresh the window here,
// never close it (that would delete it inside its own event handler); the
// next update closes it if the condition has cleared.
void cm93depth_pi::alarmSilence() {
  const int64_t now = (int64_t)wxGetUTCTimeMillis().GetValue();
  m_alarm.silence(now, (int64_t)std::max(1, m_s.alarmSilenceSec) * 1000);
  if (m_alarmDlg && m_alarm.raised())
    m_alarmDlg->show(m_alarmText, m_alarm.acknowledged(), std::max(1, m_s.alarmSilenceSec));
}

void cm93depth_pi::alarmAcknowledge() {
  m_alarm.acknowledge();
  if (m_alarmDlg && m_alarm.raised()) m_alarmDlg->show(m_alarmText, true, 0);
}

void cm93depth_pi::setAlarmOff(bool off) {
  m_alarmOff = off;
  m_alarmOffSinceMs = (int64_t)wxGetUTCTimeMillis().GetValue();
  wxLogMessage(off ? _T("cm93depth_pi: grounding alarm switched OFF by the operator")
                   : _T("cm93depth_pi: grounding alarm switched on"));
  // The alarm window (if this came from its button) hides itself; the next
  // update clears the alarm and deletes it.
  if (m_panel && m_panel->IsShown()) m_panel->refresh();
}

int cm93depth_pi::alarmOffMinutesLeft() const {
  if (!m_alarmOff || m_s.alarmOffRearmMin <= 0) return -1;
  int64_t left = (int64_t)m_s.alarmOffRearmMin * 60000 -
                 ((int64_t)wxGetUTCTimeMillis().GetValue() - m_alarmOffSinceMs);
  return (int)std::max<int64_t>(0, (left + 59999) / 60000);
}

void cm93depth_pi::closeAlarmWindow() {
  if (m_alarmDlg && m_alarmDlg->busy()) {  // its question box is open:
    m_alarmDlg->Hide();                    // delete it at a later update
    return;
  }
  if (m_alarmDlg) {
    AlarmDialog *d = m_alarmDlg;
    m_alarmDlg = nullptr;
    d->Hide();
    delete d;  // at once, not at idle time (see DeInit)
  }
}

// ---------------------------------------------------------------------------
// Look-ahead zone on the chart
// ---------------------------------------------------------------------------
bool cm93depth_pi::overlayWanted() const {
  return m_s.laEnabled && m_s.laShowZone && m_look.valid && m_look.outline.size() >= 3;
}

void cm93depth_pi::overlayColours(wxColour &zone, wxColour &shoal) const {
  const bool night = m_colorScheme == PI_GLOBAL_COLOR_SCHEME_NIGHT;
  const bool dusk = m_colorScheme == PI_GLOBAL_COLOR_SCHEME_DUSK;
  const int k = night ? 45 : dusk ? 75 : 100;  // percent brightness
  auto c = [&](int r, int g, int b) { return wxColour(r * k / 100, g * k / 100, b * k / 100); };
  zone = m_look.haveShoal ? c(220, 0, 0) : c(0, 160, 60);
  shoal = c(255, 0, 0);
}

bool cm93depth_pi::RenderOverlayMultiCanvas(wxDC &dc, PlugIn_ViewPort *vp,
                                            int /*canvasIndex*/, int priority) {
  if (priority != OVERLAY_LEGACY && priority != -1) return false;
  m_overlayDrawn = overlayWanted();
  if (!m_overlayDrawn) return false;
  wxColour zc, sc;
  overlayColours(zc, sc);
  std::vector<wxPoint> pts;
  for (const auto &p : m_look.outline) {
    wxPoint q;
    GetCanvasPixLL(vp, &q, p.lat, p.lon);
    pts.push_back(q);
  }
  dc.SetPen(wxPen(zc, 2, wxPENSTYLE_SHORT_DASH));
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.DrawPolygon((int)pts.size(), pts.data());
  if (m_look.haveShoal) {
    wxPoint q;
    GetCanvasPixLL(vp, &q, m_look.shoal.pos.lat, m_look.shoal.pos.lon);
    dc.SetPen(wxPen(sc, 3));
    dc.DrawCircle(q, 9);
  }
  return true;
}

bool cm93depth_pi::RenderGLOverlayMultiCanvas(wxGLContext * /*ctx*/, PlugIn_ViewPort *vp,
                                              int /*canvasIndex*/, int priority) {
#ifdef CM93DEPTH_NO_GL
  (void)vp;
  (void)priority;
  return false;
#else
  if (priority != OVERLAY_LEGACY && priority != -1) return false;
  m_overlayDrawn = overlayWanted();
  if (!m_overlayDrawn) return false;
  wxColour zc, sc;
  overlayColours(zc, sc);
  glPushAttrib(GL_LINE_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT |
               GL_HINT_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_LINE_STIPPLE);
  glLineStipple(3, 0xF0F0);  // dashed, like the DC drawing
  glLineWidth(2.0f);
  glColor4ub(zc.Red(), zc.Green(), zc.Blue(), 230);
  glBegin(GL_LINE_LOOP);
  for (const auto &p : m_look.outline) {
    wxPoint q;
    GetCanvasPixLL(vp, &q, p.lat, p.lon);
    glVertex2i(q.x, q.y);
  }
  glEnd();
  glDisable(GL_LINE_STIPPLE);
  if (m_look.haveShoal) {
    wxPoint q;
    GetCanvasPixLL(vp, &q, m_look.shoal.pos.lat, m_look.shoal.pos.lon);
    glLineWidth(3.0f);
    glColor4ub(sc.Red(), sc.Green(), sc.Blue(), 255);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 20; i++) {
      double a = 6.283185307179586 * i / 20;
      glVertex2d(q.x + 9 * std::cos(a), q.y + 9 * std::sin(a));
    }
    glEnd();
  }
  glPopAttrib();
  return true;
#endif
}

wxArrayString cm93depth_pi::listOpenCPNConnections() {
  wxArrayString out;
  for (const DriverHandle &h : GetActiveDrivers()) {
    auto a = GetAttributes(h);
    auto get = [&](const char *k) {
      auto it = a.find(k);
      return it == a.end() ? std::string() : it->second;
    };
    if (get("protocol") != "nmea0183") continue;
    std::string dir = get("ioDirection");
    if (dir.find("OUT") == std::string::npos) continue;
    std::string label;
    if (!get("commPort").empty())
      label = "Serial " + get("commPort");
    else if (!get("netAddress").empty())
      label = "Network " + get("netAddress") + ":" + get("netPort");
    else
      continue;
    out.Add(wxString::FromUTF8(label));
  }
  return out;
}

void cm93depth_pi::sendOutput(const std::string &sentence) {
  wxLongLong now = wxGetUTCTimeMillis();

  if (m_s.outputRoute == (int)OutputRoute::Serial) {
    if (m_s.serialPort.IsEmpty()) {
      m_outStatus = _("No serial port selected");
      return;
    }
    if (!m_serial.isOpen()) {
      if (m_lastOpenTryMs != 0 && (now - m_lastOpenTryMs) < kSerialRetryMs)
        return;
      m_lastOpenTryMs = now;
      std::string err;
      if (!m_serial.open(m_s.serialPort.ToStdString(wxConvUTF8), m_s.baud, &err)) {
        m_outStatus = _("Serial error: ") + wxString::FromUTF8(err);
        return;
      }
    }
    std::string err;
    if (m_serial.write(sentence, &err)) {
      m_outStatus = wxString::Format(_("Sending on %s at %d baud"),
                                     m_s.serialPort, m_s.baud);
    } else {
      m_outStatus = _("Serial write failed: ") + wxString::FromUTF8(err);
      m_serial.close();  // retried after kSerialRetryMs
      m_lastOpenTryMs = now;
    }
    return;
  }

  if (m_s.outputRoute == (int)OutputRoute::OpenCPNConnection) {
    for (const DriverHandle &h : GetActiveDrivers()) {
      auto a = GetAttributes(h);
      auto get = [&](const char *k) {
        auto it = a.find(k);
        return it == a.end() ? std::string() : it->second;
      };
      if (get("protocol") != "nmea0183") continue;
      std::string label = !get("commPort").empty()
                              ? "Serial " + get("commPort")
                              : "Network " + get("netAddress") + ":" + get("netPort");
      if (wxString::FromUTF8(label) != m_s.ocpnConnection) continue;
      auto payload = std::make_shared<std::vector<uint8_t>>(sentence.begin(),
                                                            sentence.end());
      CommDriverResult rc = WriteCommDriver(h, payload);
      m_outStatus = rc == RESULT_COMM_NO_ERROR
                        ? _("Sending via OpenCPN connection ") + m_s.ocpnConnection
                        : wxString::Format(_("OpenCPN connection error %d"), (int)rc);
      return;
    }
    m_outStatus = _("OpenCPN connection not found or not enabled: ") +
                  m_s.ocpnConnection;
  }
}

// ---------------------------------------------------------------------------
// Chart mark at the least-depth position
// ---------------------------------------------------------------------------
void cm93depth_pi::updateMarker() {
  // In "depth area here" mode the value belongs to the vessel's own
  // position, so there is no separate spot to mark.
  if (!m_s.markOnChart || !m_result.valid || !m_result.haveLeast ||
      m_s.mode == (int)cm93::SearchMode::DepthAreaHere ||
      m_s.mode == (int)cm93::SearchMode::Interpolated) {
    removeMarker();
    return;
  }
  wxString name = wxString::Format(_T("Least %.1f m"), m_result.leastDepth);
  bool moved = !m_markAdded ||
               std::fabs(m_result.leastPos.lat - m_markLat) > 1e-7 ||
               std::fabs(m_result.leastPos.lon - m_markLon) > 1e-7 ||
               name != m_markName;
  if (!moved) return;

  if (m_markGuid.IsEmpty()) m_markGuid = GetNewGUID();
  PlugIn_Waypoint wp(m_result.leastPos.lat, m_result.leastPos.lon,
                     m_s.markIcon, name, m_markGuid);
  wp.m_MarkDescription =
      m_result.cellName.empty()
          ? wxString::Format(_("CM93 least charted depth (%s), %s scale cell"),
                             wxString::FromUTF8(m_result.leastClass),
                             wxString(m_result.scaleChar))
          : wxString::Format(_("Least charted depth (%s), ENC %s"),
                             wxString::FromUTF8(m_result.leastClass),
                             wxString::FromUTF8(m_result.cellName));
  bool ok = m_markAdded ? UpdateSingleWaypoint(&wp)
                        : AddSingleWaypoint(&wp, false /* not saved */);
  if (!ok && m_markAdded) {  // mark deleted by the user: add it again
    ok = AddSingleWaypoint(&wp, false);
  }
  m_markAdded = ok;
  m_markLat = m_result.leastPos.lat;
  m_markLon = m_result.leastPos.lon;
  m_markName = name;
  RequestRefresh(m_parentWindow);
}

void cm93depth_pi::removeMarker() {
  if (m_markAdded && !m_markGuid.IsEmpty()) {
    DeleteSingleWaypoint(m_markGuid);
    RequestRefresh(m_parentWindow);
  }
  m_markAdded = false;
  m_markLat = m_markLon = NAN;
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
void cm93depth_pi::loadConfig() {
  wxFileConfig *c = GetOCPNConfigObject();
  if (!c) return;
  c->SetPath(_T("/PlugIns/cm93depth_pi"));
  c->Read(_T("UseCM93"), &m_s.useCm93, m_s.useCm93);
  c->Read(_T("UseENC"), &m_s.useEnc, m_s.useEnc);
  c->Read(_T("ENCExtraDirs"), &m_s.encExtraDirs, m_s.encExtraDirs);
  c->Read(_T("ENCIndexMs"), &m_s.encIndexMs, m_s.encIndexMs);
  c->Read(_T("CM93Root"), &m_s.cm93Root, m_s.cm93Root);
  c->Read(_T("ScaleIdx"), &m_s.scaleIdx, m_s.scaleIdx);
  c->Read(_T("OffsetE"), &m_s.offsetE, m_s.offsetE);
  c->Read(_T("OffsetN"), &m_s.offsetN, m_s.offsetN);
  c->Read(_T("Mode"), &m_s.mode, m_s.mode);
  // Version 1 settings defaulted to whole-cell mode; move them to the new
  // default ("depth area here") once.
  long cfgVer = 1;
  c->Read(_T("ConfigVersion"), &cfgVer, c->HasEntry(_T("Mode")) ? 1L : 2L);
  if (cfgVer < 2) m_s.mode = DEF_SEARCH_MODE;
  c->Read(_T("RadiusNm"), &m_s.radiusNm, m_s.radiusNm);
  c->Read(_T("UseSoundings"), &m_s.useSoundings, m_s.useSoundings);
  c->Read(_T("UseAreas"), &m_s.useAreas, m_s.useAreas);
  c->Read(_T("UseHazards"), &m_s.useHazards, m_s.useHazards);
  c->Read(_T("UseContours"), &m_s.useContours, m_s.useContours);
  c->Read(_T("InterpQuadrants"), &m_s.interpQuadrants, m_s.interpQuadrants);
  c->Read(_T("OpenCpnSoundings"), &m_s.openCpnSoundings, m_s.openCpnSoundings);
  c->Read(_T("InterpLandCheck"), &m_s.interpLandCheck, m_s.interpLandCheck);
  c->Read(_T("UndepthedDangerRule"), &m_s.undepthedDangerRule, m_s.undepthedDangerRule);
  c->Read(_T("CoarseDangers"), &m_s.coarseDangers, m_s.coarseDangers);
  c->Read(_T("CoarseCheckNm"), &m_s.coarseCheckNm, m_s.coarseCheckNm);
  if (!(m_s.coarseCheckNm >= 0 && m_s.coarseCheckNm <= 2))
    m_s.coarseCheckNm = DEF_COARSE_DANGER_CHECK_NM;
  if (m_s.undepthedDangerRule < 0 || m_s.undepthedDangerRule > 1)
    m_s.undepthedDangerRule = DEF_UNDEPTHED_DANGER_RULE;
  c->Read(_T("OutputRoute"), &m_s.outputRoute, m_s.outputRoute);
  c->Read(_T("SerialPort"), &m_s.serialPort, m_s.serialPort);
  c->Read(_T("Baud"), &m_s.baud, m_s.baud);
  c->Read(_T("OpenCPNConnection"), &m_s.ocpnConnection, m_s.ocpnConnection);
  c->Read(_T("SentenceAddress"), &m_s.sentenceAddress, m_s.sentenceAddress);
  c->Read(_T("IntervalMs"), &m_s.intervalMs, m_s.intervalMs);
  c->Read(_T("SendWhenInvalid"), &m_s.sendWhenInvalid, m_s.sendWhenInvalid);
  c->Read(_T("StaleFixSec"), &m_s.staleFixSec, m_s.staleFixSec);
  c->Read(_T("LaEnabled"), &m_s.laEnabled, m_s.laEnabled);
  c->Read(_T("LaZone"), &m_s.laZone, m_s.laZone);
  c->Read(_T("LaBasis"), &m_s.laBasis, m_s.laBasis);
  c->Read(_T("LaMinutes"), &m_s.laMinutes, m_s.laMinutes);
  c->Read(_T("LaDistanceNm"), &m_s.laDistanceNm, m_s.laDistanceNm);
  c->Read(_T("LaMinLengthNm"), &m_s.laMinLengthNm, m_s.laMinLengthNm);
  c->Read(_T("LaMaxLengthNm"), &m_s.laMaxLengthNm, m_s.laMaxLengthNm);
  c->Read(_T("LaHalfWidthNm"), &m_s.laHalfWidthNm, m_s.laHalfWidthNm);
  c->Read(_T("LaHalfAngleDeg"), &m_s.laHalfAngleDeg, m_s.laHalfAngleDeg);
  c->Read(_T("LaSafetyDepthM"), &m_s.laSafetyDepthM, m_s.laSafetyDepthM);
  c->Read(_T("LaMinSogKn"), &m_s.laMinSogKn, m_s.laMinSogKn);
  c->Read(_T("LaUndepthedAsShoal"), &m_s.laUndepthedAsShoal, m_s.laUndepthedAsShoal);
  c->Read(_T("LaSentenceAddress"), &m_s.laSentenceAddress, m_s.laSentenceAddress);
  c->Read(_T("LaShowZone"), &m_s.laShowZone, m_s.laShowZone);
  c->Read(_T("AlarmEnabled"), &m_s.alarmEnabled, m_s.alarmEnabled);
  c->Read(_T("AlarmMinutes"), &m_s.alarmMinutes, m_s.alarmMinutes);
  c->Read(_T("AlarmDistanceNm"), &m_s.alarmDistanceNm, m_s.alarmDistanceNm);
  c->Read(_T("AlarmDepthHere"), &m_s.alarmDepthHere, m_s.alarmDepthHere);
  c->Read(_T("AlarmSound"), &m_s.alarmSound, m_s.alarmSound);
  c->Read(_T("AlarmRepeatSec"), &m_s.alarmRepeatSec, m_s.alarmRepeatSec);
  c->Read(_T("AlarmSilenceSec"), &m_s.alarmSilenceSec, m_s.alarmSilenceSec);
  c->Read(_T("AlarmSendAlr"), &m_s.alarmSendAlr, m_s.alarmSendAlr);
  c->Read(_T("AlarmOffRearmMin"), &m_s.alarmOffRearmMin, m_s.alarmOffRearmMin);
  m_s.alarmOffRearmMin = std::clamp(m_s.alarmOffRearmMin, 0, 720);
  m_s.alarmMinutes = std::clamp(m_s.alarmMinutes, 0.5, 60.0);
  m_s.alarmDistanceNm = std::clamp(m_s.alarmDistanceNm, 0.01, 5.0);
  m_s.alarmRepeatSec = std::clamp(m_s.alarmRepeatSec, 1, 120);
  m_s.alarmSilenceSec = std::clamp(m_s.alarmSilenceSec, 5, 300);
  c->Read(_T("MarkOnChart"), &m_s.markOnChart, m_s.markOnChart);
  c->Read(_T("MarkIcon"), &m_s.markIcon, m_s.markIcon);
  c->Read(_T("PanelVisible"), &m_s.panelVisible, m_s.panelVisible);
  c->Read(_T("PanelCompact"), &m_s.panelCompact, m_s.panelCompact);
  int x = -1, y = -1;
  c->Read(_T("PanelX"), &x, -1);
  c->Read(_T("PanelY"), &y, -1);
  if (x >= 0 && y >= 0) m_s.panelPos = wxPoint(x, y);

  // sanity limits
  if (m_s.scaleIdx < -1 || m_s.scaleIdx > 7) m_s.scaleIdx = DEF_SCALE_IDX;
  if (m_s.mode < 0 || m_s.mode > 3) m_s.mode = DEF_SEARCH_MODE;
  m_s.radiusNm = std::clamp(m_s.radiusNm, 0.05, 50.0);
  m_s.intervalMs = std::clamp(m_s.intervalMs, 200, 10000);
  m_s.staleFixSec = std::clamp(m_s.staleFixSec, 2, 120);
  if (m_s.outputRoute < 0 || m_s.outputRoute > 2) m_s.outputRoute = DEF_OUTPUT_ROUTE;
  if (!cm93::valid_proprietary_address(m_s.sentenceAddress.ToStdString()))
    m_s.sentenceAddress = DEF_SENTENCE_ADDRESS;
  if (m_s.laZone < 0 || m_s.laZone > 1) m_s.laZone = DEF_LA_ZONE;
  if (m_s.laBasis < 0 || m_s.laBasis > 1) m_s.laBasis = DEF_LA_BASIS;
  m_s.laMinutes = std::clamp(m_s.laMinutes, 1.0, 120.0);
  m_s.laDistanceNm = std::clamp(m_s.laDistanceNm, 0.05, 50.0);
  m_s.laMaxLengthNm = std::clamp(m_s.laMaxLengthNm, 0.1, 50.0);
  m_s.laMinLengthNm = std::clamp(m_s.laMinLengthNm, 0.05, m_s.laMaxLengthNm);
  m_s.laHalfWidthNm = std::clamp(m_s.laHalfWidthNm, 0.01, 5.0);
  m_s.laHalfAngleDeg = std::clamp(m_s.laHalfAngleDeg, 1.0, 90.0);
  m_s.laSafetyDepthM = std::clamp(m_s.laSafetyDepthM, -5.0, 100.0);
  m_s.laMinSogKn = std::clamp(m_s.laMinSogKn, 0.1, 5.0);
  if (!cm93::valid_proprietary_address(m_s.laSentenceAddress.ToStdString()) ||
      m_s.laSentenceAddress == m_s.sentenceAddress)
    m_s.laSentenceAddress = DEF_LA_SENTENCE_ADDRESS;
}

void cm93depth_pi::saveConfig() {
  wxFileConfig *c = GetOCPNConfigObject();
  if (!c) return;
  c->SetPath(_T("/PlugIns/cm93depth_pi"));
  c->Write(_T("UseCM93"), m_s.useCm93);
  c->Write(_T("UseENC"), m_s.useEnc);
  c->Write(_T("ENCExtraDirs"), m_s.encExtraDirs);
  c->Write(_T("ENCIndexMs"), m_s.encIndexMs);
  c->Write(_T("CM93Root"), m_s.cm93Root);
  c->Write(_T("ScaleIdx"), m_s.scaleIdx);
  c->Write(_T("OffsetE"), m_s.offsetE);
  c->Write(_T("OffsetN"), m_s.offsetN);
  c->Write(_T("ConfigVersion"), 2L);
  c->Write(_T("Mode"), m_s.mode);
  c->Write(_T("RadiusNm"), m_s.radiusNm);
  c->Write(_T("UseSoundings"), m_s.useSoundings);
  c->Write(_T("UseAreas"), m_s.useAreas);
  c->Write(_T("UseHazards"), m_s.useHazards);
  c->Write(_T("UseContours"), m_s.useContours);
  c->Write(_T("InterpQuadrants"), m_s.interpQuadrants);
  c->Write(_T("OpenCpnSoundings"), m_s.openCpnSoundings);
  c->Write(_T("InterpLandCheck"), m_s.interpLandCheck);
  c->Write(_T("UndepthedDangerRule"), m_s.undepthedDangerRule);
  c->Write(_T("CoarseDangers"), m_s.coarseDangers);
  c->Write(_T("CoarseCheckNm"), m_s.coarseCheckNm);
  c->Write(_T("OutputRoute"), m_s.outputRoute);
  c->Write(_T("SerialPort"), m_s.serialPort);
  c->Write(_T("Baud"), m_s.baud);
  c->Write(_T("OpenCPNConnection"), m_s.ocpnConnection);
  c->Write(_T("SentenceAddress"), m_s.sentenceAddress);
  c->Write(_T("IntervalMs"), m_s.intervalMs);
  c->Write(_T("SendWhenInvalid"), m_s.sendWhenInvalid);
  c->Write(_T("StaleFixSec"), m_s.staleFixSec);
  c->Write(_T("LaEnabled"), m_s.laEnabled);
  c->Write(_T("LaZone"), m_s.laZone);
  c->Write(_T("LaBasis"), m_s.laBasis);
  c->Write(_T("LaMinutes"), m_s.laMinutes);
  c->Write(_T("LaDistanceNm"), m_s.laDistanceNm);
  c->Write(_T("LaMinLengthNm"), m_s.laMinLengthNm);
  c->Write(_T("LaMaxLengthNm"), m_s.laMaxLengthNm);
  c->Write(_T("LaHalfWidthNm"), m_s.laHalfWidthNm);
  c->Write(_T("LaHalfAngleDeg"), m_s.laHalfAngleDeg);
  c->Write(_T("LaSafetyDepthM"), m_s.laSafetyDepthM);
  c->Write(_T("LaMinSogKn"), m_s.laMinSogKn);
  c->Write(_T("LaUndepthedAsShoal"), m_s.laUndepthedAsShoal);
  c->Write(_T("LaSentenceAddress"), m_s.laSentenceAddress);
  c->Write(_T("LaShowZone"), m_s.laShowZone);
  c->Write(_T("AlarmEnabled"), m_s.alarmEnabled);
  c->Write(_T("AlarmMinutes"), m_s.alarmMinutes);
  c->Write(_T("AlarmDistanceNm"), m_s.alarmDistanceNm);
  c->Write(_T("AlarmDepthHere"), m_s.alarmDepthHere);
  c->Write(_T("AlarmSound"), m_s.alarmSound);
  c->Write(_T("AlarmRepeatSec"), m_s.alarmRepeatSec);
  c->Write(_T("AlarmSilenceSec"), m_s.alarmSilenceSec);
  c->Write(_T("AlarmSendAlr"), m_s.alarmSendAlr);
  c->Write(_T("AlarmOffRearmMin"), m_s.alarmOffRearmMin);
  c->Write(_T("MarkOnChart"), m_s.markOnChart);
  c->Write(_T("MarkIcon"), m_s.markIcon);
  c->Write(_T("PanelVisible"), m_s.panelVisible);
  c->Write(_T("PanelCompact"), m_s.panelCompact);
  if (m_panel) m_s.panelPos = m_panel->GetPosition();
  c->Write(_T("PanelX"), m_s.panelPos.x);
  c->Write(_T("PanelY"), m_s.panelPos.y);
  c->Flush();
}
