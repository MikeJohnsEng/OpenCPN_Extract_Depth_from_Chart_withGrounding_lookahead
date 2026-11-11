/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * Shows the least charted depth in the CM93 cell under the vessel (or within
 * a radius) and sends it as a proprietary NMEA 0183 sentence on a serial
 * port or an OpenCPN output connection.
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
#include "ocpn_plugin.h"
#include "serial_port.h"

#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 5
#define PLUGIN_VERSION_PATCH 1

class DepthPanel;
class AlarmDialog;

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

  // alarm
  cm93::AlarmLogic m_alarm;
  AlarmDialog *m_alarmDlg = nullptr;
  wxString m_alarmText;
  bool m_alrSentActive = false;  // an active $IIALR has gone out
  bool m_alarmOff = false;       // switched off by the watch officer
  int64_t m_alarmOffSinceMs = 0;
};
