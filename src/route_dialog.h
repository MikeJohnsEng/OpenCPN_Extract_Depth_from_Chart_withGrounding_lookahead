/***************************************************************************
 * cm93depth_pi - route check window
 *
 * Pick a route, set the corridor, waypoint circle, height of tide and what
 * counts as properly charted, and check it. Lists every leg and waypoint
 * with its result; double-click one to centre the chart on its first
 * finding. Copy report puts the full findings on the clipboard.
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
#include <wx/listctrl.h>
#include <wx/spinctrl.h>

#include <vector>

#include "cm93depth_pi.h"

class RouteCheckDialog : public wxDialog {
public:
  RouteCheckDialog(wxWindow *parent, cm93depth_pi *plugin);
  // Fill the route list and choose this route ("" = the active, else first)
  void selectRoute(const wxString &guid);
  void onProgress();   // a check is running
  void showResult();   // a check has finished
  void showMessage(const wxString &m);

private:
  void refreshRoutes(const wxString &select);
  PluginSettings readSettings() const;
  void onCheck(wxCommandEvent &e);
  void onRefresh(wxCommandEvent &e);
  void onCopy(wxCommandEvent &e);
  void onClear(wxCommandEvent &e);
  void onCloseBtn(wxCommandEvent &e);
  void onClose(wxCloseEvent &e);
  void onSelected(wxListEvent &e);
  void onActivated(wxListEvent &e);
  void onSettingChanged(wxCommandEvent &e);

  cm93depth_pi *m_pi;
  wxChoice *m_route;
  wxArrayString m_guids;
  wxSpinCtrlDouble *m_halfW, *m_wpR, *m_tide, *m_sparse;
  wxChoice *m_poor;
  wxCheckBox *m_undep, *m_show, *m_keep, *m_onActivate, *m_alarm;
  void closeWindow();  // hide, clearing the chart drawing unless kept
  wxStaticText *m_safety;
  wxButton *m_check;
  wxGauge *m_gauge;
  wxStaticText *m_status;
  wxListCtrl *m_list;
  wxTextCtrl *m_detail;
  struct Row {
    int leg = -1, wp = -1;
  };
  std::vector<Row> m_rows;
};
