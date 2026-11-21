/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * Shows the least charted depth in the CM93 cell under the vessel (or within
 * a radius) and sends it as a proprietary NMEA 0183 sentence on a serial
 * port or an OpenCPN output connection.
 *
 * Copyright (C) 2026 Mike
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include <wx/timer.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "alarm_logic.h"
#include "depth_engine.h"
#include "route_check.h"
#include "ocpn_plugin.h"
#include "serial_port.h"

#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 6
#define PLUGIN_VERSION_PATCH 3

class DepthPanel;
class AlarmDialog;
class RouteCheckDialog;

enum class OutputRoute : int {
  Off = 0,
  Serial = 1,           // plugin opens the serial port itself
  OpenCPNConnection = 2 // send through an existing OpenCPN output connection
};

// Every user-adjustable setting. Defaults and explanations live in the
// "USER SETTINGS" block at the top of cm93depth_pi.cpp.
struct PluginSettings {
  // Charts
  bool useCm93;           // read CM93 cells
  bool useEnc;            // read S-57 ENC cells
  wxString encExtraDirs;  // more folders to search for ENC cells, ';'-separated
  int encIndexMs;         // ms per tick spent indexing ENC cells
  wxString cm93Root;      // empty = auto-detect from OpenCPN chart dirs
  int scaleIdx;           // -1 auto, 0..7 = Z A B C D E F G
  double offsetE;         // m
  double offsetN;         // m
  // Search
  int mode;  // 0 = whole cell, 1 = radius, 2 = depth area here, 3 = interpolated
  double radiusNm;
  bool useSoundings;
  bool useAreas;
  bool useHazards;
  bool useContours;
  bool interpQuadrants;   // interpolated mode: one sounding per direction
  bool openCpnSoundings;  // decode soundings as OpenCPN draws them
  bool interpLandCheck;   // interpolated mode: skip soundings behind land
  int undepthedDangerRule;  // 0 = 0 m, 1 = least depth of the surrounding area
  bool coarseDangers;     // dangers charted only at smaller scales count too
  double coarseCheckNm;   // detailed soundings this close overrule a danger area
  // Output
  int outputRoute;        // OutputRoute
  wxString serialPort;
  int baud;
  wxString ocpnConnection;  // label of the OpenCPN connection
  wxString sentenceAddress; // e.g. "PCMLD"
  int intervalMs;
  bool sendWhenInvalid;
  int staleFixSec;
  // Look-ahead
  bool laEnabled;
  int laZone;             // cm93::LookZone
  int laBasis;            // cm93::LookBasis
  double laMinutes;
  double laDistanceNm;
  double laMinLengthNm;
  double laMaxLengthNm;
  double laHalfWidthNm;
  double laHalfAngleDeg;
  double laSafetyDepthM;
  double laMinSogKn;
  bool laUndepthedAsShoal;
  wxString laSentenceAddress;  // e.g. "PCMLA"
  bool laShowZone;        // draw the zone and first shoal on the chart
  // Grounding alarm
  bool alarmEnabled;
  double alarmMinutes;    // shoal ahead within this time
  double alarmDistanceNm; // ... or this distance
  bool alarmDepthHere;    // depth at the vessel below the safety depth
  wxString alarmSound;    // sound file; empty = OpenCPN's beep_ssl.wav
  int alarmRepeatSec;     // sound repeat while unacknowledged
  int alarmSilenceSec;    // Silence button mutes for this long
  bool alarmSendAlr;      // send $IIALR on the NMEA output
  int alarmOffRearmMin;   // switched-off alarm comes back on after this; 0 = never
  // Route check
  double rcHalfWidthNm;   // corridor each side of every leg
  double rcWaypointRadiusNm;  // circle checked round every waypoint
  double rcTideM;         // height of tide added to charted depths
  char rcPoorBand;        // least detailed band taken as properly charted
  bool rcUndepthedFails;  // undepthed dangers / gaps fail (else warn)
  double rcSparseBandM;   // depth areas with a wider band are sparse data
  bool rcShowOnChart;     // draw the checked route and its findings
  bool rcKeepOnClose;     // keep the drawing when the window is closed
  bool rcOnActivate;      // check a route automatically when activated
  bool rcAlarmOnFail;     // ... and sound / show the result if it fails
  // Display
  bool markOnChart;
  wxString markIcon;
  bool panelVisible;
  bool panelCompact;      // display window minimised to the depth only
  wxPoint panelPos;
};

class cm93depth_pi : public wxEvtHandler, public opencpn_plugin_118 {
public:
  explicit cm93depth_pi(void *ppimgr);
  ~cm93depth_pi() override;

  // opencpn_plugin
  int Init() override;
  bool DeInit() override;
  int GetAPIVersionMajor() override;
  int GetAPIVersionMinor() override;
  int GetPlugInVersionMajor() override;
  int GetPlugInVersionMinor() override;
  int GetPlugInVersionPatch() override;
  wxBitmap *GetPlugInBitmap() override;
  wxString GetCommonName() override;
  wxString GetShortDescription() override;
  wxString GetLongDescription() override;
  int GetToolbarToolCount() override;
  void OnToolbarToolCallback(int id) override;
  void OnContextMenuItemCallback(int id) override;
  void PrepareContextMenu(int canvasIndex) override;
  void ShowPreferencesDialog(wxWindow *parent) override;
  void SetPositionFixEx(PlugIn_Position_Fix_Ex &pfix) override;
  void SetColorScheme(PI_ColorScheme cs) override;
  bool RenderOverlayMultiCanvas(wxDC &dc, PlugIn_ViewPort *vp, int canvasIndex,
                                int priority) override;
  bool RenderGLOverlayMultiCanvas(wxGLContext *pcontext, PlugIn_ViewPort *vp,
                                  int canvasIndex, int priority) override;

  // Used by the panel / preferences dialog
  const PluginSettings &settings() const { return m_s; }
  void applySettings(const PluginSettings &s);
  const cm93::DepthResult &lastResult() const { return m_result; }
  const cm93::LookAheadResult &lastLookAhead() const { return m_look; }
  wxString lastLookSentence() const { return m_lastLookSentence; }
  bool fixValid() const { return m_fixValid; }
  wxString chartStatus() const;  // CM93 and S-57 lines
  wxString encStatus() const { return m_encStatus; }
  void rescanEnc();               // index the ENC cells again now
  wxString outputStatus() const { return m_outStatus; }
  wxString lastSentence() const { return m_lastSentence; }
  wxString activeRoot() const { return wxString::FromUTF8(m_engine.root()); }
  void showPreferences(wxWindow *parent);
  wxString diagnosticsReport();
  void onPanelClosed();
  void setPanelCompact(bool c) { m_s.panelCompact = c; saveConfig(); }
  // Alarm window buttons
  void alarmSilence();
  void alarmAcknowledge();
  // Watch officer's on/off switch (window button, alarm window). Not saved:
  // the alarm is back on whenever OpenCPN starts.
  void setAlarmOff(bool off);
  bool alarmOff() const { return m_alarmOff; }
  bool alarmAvailable() const { return m_s.alarmEnabled; }
  int alarmOffMinutesLeft() const;
  // Play a sound file (empty = the default alarm sound); false if it failed
  bool playSound(const wxString &file);
  const cm93::AlarmLogic &alarm() const { return m_alarm; }
  wxString alarmText() const { return m_alarmText; }

  // Route check (route_dialog.cpp uses these)
  void showRouteCheck(const wxString &guid);  // open the window; "" = pick one
  void startRouteCheck(const wxString &guid, bool automatic);
  void applyRouteCheckSettings(const PluginSettings &s);  // the rc* fields
  bool routeCheckRunning() const { return m_rcChecker.running(); }
  double routeCheckProgress() const { return m_rcChecker.progress(); }
  bool haveRouteCheck() const { return m_rcShown.valid; }
  const cm93::RouteCheckResult &routeCheckResult() const { return m_rcShown; }
  wxString routeCheckGuid() const { return m_rcShownGuid; }
  void clearRouteCheck();     // forget the last check (route gone or changed)
  void hideRouteDrawing();    // take it off the chart, keep the findings
  void jumpTo(double lat, double lon);
  void onRouteDialogClosed();
  // Active route checked automatically: status and one line for the window
  bool activeRouteChecked() const { return m_rcActiveValid; }
  cm93::CheckStatus activeRouteStatus() const { return m_rcActiveStatus; }
  wxString activeRouteText() const { return m_rcActiveText; }

  // Labels for OpenCPN NMEA 0183 output connections ("Serial /dev/ttyUSB0")
  static wxArrayString listOpenCPNConnections();

private:
  void OnTimer(wxTimerEvent &event);
  void update();
  void loadConfig();
  void saveConfig();
  void resolveRoot(bool force);
  std::vector<std::string> encDirs() const;
  void startEncIndex();
  void stepEnc();
  cm93::EngineConfig engineConfig() const;
  cm93::LookAheadConfig lookConfig() const;
  bool overlayWanted() const;
  void overlayColours(wxColour &zone, wxColour &shoal) const;
  void sendOutput(const std::string &sentence);
  void updateMarker();
  void removeMarker();
  void togglePanel(bool show);
  void updateAlarm();
  void closeAlarmWindow();
  void OnRouteTimer(wxTimerEvent &event);
  void routeCheckDone();
  void pollActiveRoute();
  void dropStaleRouteCheck();
  bool readRoute(const wxString &guid, std::vector<cm93::RouteWaypoint> &pts,
                 wxString &name, uint64_t &signature) const;
  void drawRouteDC(wxDC &dc, PlugIn_ViewPort *vp);
  void drawRouteGL(PlugIn_ViewPort *vp);
  bool routeOverlayWanted() const;
  cm93::RouteCheckConfig routeCheckConfig() const;

  PluginSettings m_s;
  cm93::DepthEngine m_engine;
  cm93::DepthResult m_result;
  cm93::LookAheadResult m_look;

  // own-ship fix
  double m_lat = NAN, m_lon = NAN;
  double m_cog = NAN, m_sog = NAN, m_hdt = NAN;
  time_t m_lastFixTime = 0;
  wxLongLong m_lastFixChangeMs = 0;
  bool m_fixValid = false;

  // charts
  wxString m_rootStatus;
  wxLongLong m_lastRootTryMs = 0;
  wxString m_encStatus;
  std::vector<std::string> m_encDirs;  // folders the ENC index was built from
  wxLongLong m_lastEncDirCheckMs = 0;

  // output
  SerialPort m_serial;
  wxLongLong m_lastOpenTryMs = 0;
  wxString m_outStatus;
  wxString m_lastSentence;
  wxString m_lastLookSentence;
  bool m_overlayDrawn = false;  // zone was on screen at the last refresh

  // chart marker
  wxString m_markGuid;
  bool m_markAdded = false;
  double m_markLat = NAN, m_markLon = NAN;
  wxString m_markName;

  // UI
  wxTimer m_timer;
  int m_toolId = -1;
  DepthPanel *m_panel = nullptr;
  wxWindow *m_parentWindow = nullptr;
  std::unique_ptr<wxBitmap> m_icon;
  PI_ColorScheme m_colorScheme = PI_GLOBAL_COLOR_SCHEME_DAY;

  // route check
  cm93::RouteChecker m_rcChecker;
  wxTimer m_rcTimer;
  bool m_rcAuto = false;            // the running check is the automatic one
  wxString m_rcRunningGuid;
  uint64_t m_rcRunningSig = 0;
  cm93::RouteCheckResult m_rcShown;  // last completed check
  wxString m_rcShownGuid;
  uint64_t m_rcShownSig = 0;        // its waypoints when it was checked
  bool m_rcDrawn = false;           // ... and it is drawn on the chart
  RouteCheckDialog *m_rcDlg = nullptr;
  int m_ctxMenuId = -1;
  int m_ctxClearId = -1;            // "Clear route check from chart"
  wxString m_rcActiveGuid;          // active route last seen
  uint64_t m_rcActiveSig = 0;       // its waypoints when it was checked
  bool m_rcActiveValid = false;
  cm93::CheckStatus m_rcActiveStatus = cm93::CheckStatus::Pass;
  wxString m_rcActiveText;
  int m_rcPollCount = 0;
  double m_lastViewScalePpm = 0;    // chart scale, for jumping to a finding

  // alarm
  cm93::AlarmLogic m_alarm;
  AlarmDialog *m_alarmDlg = nullptr;
  wxString m_alarmText;
  bool m_alrSentActive = false;  // an active $IIALR has gone out
  bool m_alarmOff = false;       // switched off by the watch officer
  int64_t m_alarmOffSinceMs = 0;
};
