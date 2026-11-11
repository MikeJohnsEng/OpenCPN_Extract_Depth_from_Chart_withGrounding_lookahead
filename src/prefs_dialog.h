/***************************************************************************
 * cm93depth_pi - preferences dialog
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
#include <wx/filepicker.h>
#include <wx/spinctrl.h>

#include "cm93depth_pi.h"

class PrefsDialog : public wxDialog {
public:
  PrefsDialog(wxWindow *parent, cm93depth_pi *plugin, const PluginSettings &s);
  const PluginSettings &result() const { return m_s; }

private:
  void onOK(wxCommandEvent &e);
  void onRefreshPorts(wxCommandEvent &e);
  void onRescanEnc(wxCommandEvent &e);
  void updateEnable();

  cm93depth_pi *m_pi;
  PluginSettings m_s;

  wxCheckBox *m_useCm93, *m_useEnc;
  wxTextCtrl *m_encDirs;
  wxStaticText *m_encStatus;
  wxCheckBox *m_autoRoot;
  wxDirPickerCtrl *m_root;
  wxChoice *m_scale;
  wxSpinCtrlDouble *m_offE, *m_offN;

  wxRadioBox *m_mode;
  wxSpinCtrlDouble *m_radius;
  wxCheckBox *m_snd, *m_area, *m_haz, *m_cnt;
  wxCheckBox *m_quad;
  wxCheckBox *m_landChk;
  wxChoice *m_undepRule;
  wxCheckBox *m_coarse;
  wxSpinCtrlDouble *m_coarseCheck;

  wxChoice *m_route;
  wxComboBox *m_port;
  wxChoice *m_baud;
  wxChoice *m_conn;
  wxTextCtrl *m_addr;
  wxSpinCtrlDouble *m_interval;
  wxSpinCtrl *m_stale;
  wxCheckBox *m_sendInvalid;

  wxCheckBox *m_laOn;
  wxRadioBox *m_laZone, *m_laBasis;
  wxSpinCtrlDouble *m_laMinutes, *m_laMinLen, *m_laDist, *m_laMaxLen;
  wxSpinCtrlDouble *m_laHalfW, *m_laHalfA, *m_laSafety, *m_laMinSog;
  wxTextCtrl *m_laAddr;
  wxCheckBox *m_laUndep, *m_laShow;

  wxCheckBox *m_alOn, *m_alHere, *m_alAlr;
  wxSpinCtrlDouble *m_alMin, *m_alDist;
  wxSpinCtrl *m_alRepeat, *m_alSilence, *m_alRearm;
  wxTextCtrl *m_alSound;

  wxCheckBox *m_mark;
  wxTextCtrl *m_icon;
};
