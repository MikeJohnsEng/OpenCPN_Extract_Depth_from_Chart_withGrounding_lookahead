/***************************************************************************
 * cm93depth_pi - floating display window
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

#include "ocpn_plugin.h"

class cm93depth_pi;
class wxStaticLine;

class DepthPanel : public wxDialog {
public:
  DepthPanel(wxWindow *parent, cm93depth_pi *plugin);
  void refresh();
  void setColorScheme(PI_ColorScheme cs);

private:
  void onClose(wxCloseEvent &e);
  void onSettings(wxCommandEvent &e);
  void onCopyReport(wxCommandEvent &e);
  void onToggleSize(wxCommandEvent &e);
  void onAlarmButton(wxCommandEvent &e);
  void applyCompact();  // show/hide for the current size and re-fit

  cm93depth_pi *m_pi;
  wxStaticText *m_depth;
  wxStaticText *m_caption;    // what the big figure is
  wxStaticText *m_shoalShort; // compact view: first shoal ahead, if any
  wxButton *m_toggle;         // Minimise / Enlarge
  wxButton *m_alarmBtn;       // grounding alarm on / off
  wxStaticText *m_alarmOffText;
  wxButton *m_report;
  wxButton *m_settings;
  wxStaticLine *m_statusLine;
  bool m_compact = false;
  wxStaticText *m_where;
  wxStaticText *m_atPos;
  wxStaticText *m_hazards;
  wxStaticText *m_scope;
  wxStaticText *m_status;
  wxStaticText *m_output;
  wxStaticText *m_sentence;
  wxStaticText *m_lookHead;   // first shoal ahead, or clear
  wxStaticText *m_look;       // least depth ahead, dangers, zone, scales
  wxStaticText *m_lookSentence;
  wxStaticLine *m_lookLine;
};
