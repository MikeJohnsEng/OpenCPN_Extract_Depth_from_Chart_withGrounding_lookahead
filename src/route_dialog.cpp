/***************************************************************************
 * cm93depth_pi - route check window (see route_dialog.h)
 *
 * Copyright (C) 2026 Mike
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "route_dialog.h"

#include <wx/clipbrd.h>
#include <wx/statbox.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace {
const char *kBandNames[cm93::kNumSlots] = {
    "Z  CM93 1:20,000,000", "A  CM93 1:3,000,000", "1  ENC overview",
    "B  CM93 1:1,000,000",  "2  ENC general",      "C  CM93 1:200,000",
    "3  ENC coastal",       "D  CM93 1:100,000",   "E  CM93 1:50,000",
    "4  ENC approach",      "F  CM93 1:20,000",    "5  ENC harbour",
    "G  CM93 1:7,500",      "6  ENC berthing"};

wxString U(const std::string &s) { return wxString::FromUTF8(s.c_str()); }

wxColour status_text_colour(cm93::CheckStatus s) {
  switch (s) {
    case cm93::CheckStatus::Pass: return wxColour(0, 130, 40);
    case cm93::CheckStatus::Warn: return wxColour(190, 110, 0);
    default: return wxColour(200, 0, 0);
  }
}
}  // namespace

RouteCheckDialog::RouteCheckDialog(wxWindow *parent, cm93depth_pi *plugin)
    : wxDialog(parent, wxID_ANY, _("Route check - charted depths"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_pi(plugin) {
  const PluginSettings &s = m_pi->settings();
  const int B = 4;
  auto *top = new wxBoxSizer(wxVERTICAL);

  // ---- route
  auto *row = new wxBoxSizer(wxHORIZONTAL);
  row->Add(new wxStaticText(this, wxID_ANY, _("Route")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, B);
  m_route = new wxChoice(this, wxID_ANY);
  m_route->SetMinSize(wxSize(GetCharWidth() * 36, -1));
  row->Add(m_route, 1, wxRIGHT, B);
  auto *refresh = new wxButton(this, wxID_ANY, _("Refresh list"));
  refresh->Bind(wxEVT_BUTTON, &RouteCheckDialog::onRefresh, this);
  row->Add(refresh, 0);
  top->Add(row, 0, wxEXPAND | wxALL, B);

  // ---- settings
  auto *box = new wxStaticBoxSizer(wxVERTICAL, this, _("Check"));
  wxWindow *sb = box->GetStaticBox();
  auto *g = new wxFlexGridSizer(4, B, 3 * B);
  g->AddGrowableCol(1);
  g->AddGrowableCol(3);
  auto lbl = [&](const wxString &t) {
    g->Add(new wxStaticText(sb, wxID_ANY, t), 0, wxALIGN_CENTER_VERTICAL);
  };
  lbl(_("Corridor each side (nm)"));
  m_halfW = new wxSpinCtrlDouble(sb, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxSP_ARROW_KEYS, 0.01, 5.0, s.rcHalfWidthNm, 0.01);
  m_halfW->SetDigits(2);
  m_halfW->SetToolTip(_("The cross-track error you allow: everything charted this far either "
                        "side of each leg is checked."));
  g->Add(m_halfW, 0);
  lbl(_("Circle round waypoints (nm)"));
  m_wpR = new wxSpinCtrlDouble(sb, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxSP_ARROW_KEYS, 0.0, 5.0, s.rcWaypointRadiusNm, 0.05);
  m_wpR->SetDigits(2);
  m_wpR->SetToolTip(_("The boat cuts inside the corner and may overshoot: this circle round "
                      "every waypoint is checked too. 0 = legs only."));
  g->Add(m_wpR, 0);
  lbl(_("Height of tide (m)"));
  m_tide = new wxSpinCtrlDouble(sb, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, -5.0, 15.0, s.rcTideM, 0.1);
  m_tide->SetDigits(1);
  m_tide->SetToolTip(_("Added to charted depths (below chart datum). 0 = charted depths only, "
                       "the most cautious."));
  g->Add(m_tide, 0);
  lbl(_("Properly charted: band"));
  m_poor = new wxChoice(sb, wxID_ANY);
  for (const char *n : kBandNames) m_poor->Append(wxString::FromUTF8(n) + _(" or better"));
  int ps = cm93::slot_from_char(s.rcPoorBand);
  m_poor->SetSelection(ps >= 0 ? ps : cm93::slot_from_char('C'));
  m_poor->SetToolTip(_("A leg relying in places on charts less detailed than this gets a "
                       "warning: poorly charted."));
  g->Add(m_poor, 1, wxEXPAND);
  lbl(_("Sparse data: depth bands wider than (m)"));
  m_sparse = new wxSpinCtrlDouble(sb, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                  wxSP_ARROW_KEYS, 5.0, 1000.0, s.rcSparseBandM, 5.0);
  m_sparse->SetDigits(0);
  m_sparse->SetToolTip(_("A depth area this broad (e.g. 0 to 200 m on a small-scale chart) only "
                         "says the depth is somewhere in that band. Where it is the only depth "
                         "charted, its minimum is reported as sparse data (a warning, dashed "
                         "line), not as a shoal."));
  g->Add(m_sparse, 0);
  g->AddSpacer(0);
  g->AddSpacer(0);
  box->Add(g, 0, wxEXPAND | wxALL, B);
  m_safety = new wxStaticText(sb, wxID_ANY, wxEmptyString);
  box->Add(m_safety, 0, wxLEFT | wxRIGHT | wxBOTTOM, B);
  m_undep = new wxCheckBox(sb, wxID_ANY,
                           _("Rocks, wrecks, obstructions with no charted depth and gaps fail the route"));
  m_undep->SetValue(s.rcUndepthedFails);
  box->Add(m_undep, 0, wxLEFT | wxRIGHT | wxBOTTOM, B);
  m_show = new wxCheckBox(sb, wxID_ANY, _("Show the result on the chart"));
  m_show->SetValue(s.rcShowOnChart);
  box->Add(m_show, 0, wxLEFT | wxRIGHT | wxBOTTOM, B);
  m_keep = new wxCheckBox(sb, wxID_ANY, _("... and keep it there when this window is closed"));
  m_keep->SetValue(s.rcKeepOnClose);
  m_keep->SetToolTip(_("Off: closing this window clears the route from the chart. On: it "
                       "stays until 'Clear from chart', or 'Clear route check from chart' in "
                       "the chart's right-click menu."));
  box->Add(m_keep, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4 * B);
  m_onActivate = new wxCheckBox(sb, wxID_ANY, _("Check a route automatically when it is activated"));
  m_onActivate->SetValue(s.rcOnActivate);
  box->Add(m_onActivate, 0, wxLEFT | wxRIGHT | wxBOTTOM, B);
  m_alarm = new wxCheckBox(sb, wxID_ANY,
                           _("... and if it fails, sound the alarm and open this window"));
  m_alarm->SetValue(s.rcAlarmOnFail);
  box->Add(m_alarm, 0, wxLEFT | wxRIGHT | wxBOTTOM, B);
  top->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT, B);
  for (wxWindow *w : std::initializer_list<wxWindow *>{m_undep, m_show, m_keep, m_onActivate, m_alarm})
    w->Bind(wxEVT_CHECKBOX, &RouteCheckDialog::onSettingChanged, this);
  for (wxSpinCtrlDouble *w : {m_halfW, m_wpR, m_tide, m_sparse})
    w->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent &) {
      wxCommandEvent e;
      onSettingChanged(e);
    });
  m_poor->Bind(wxEVT_CHOICE, &RouteCheckDialog::onSettingChanged, this);

  // ---- run
  auto *run = new wxBoxSizer(wxHORIZONTAL);
  m_check = new wxButton(this, wxID_ANY, _("Check route"));
  m_check->Bind(wxEVT_BUTTON, &RouteCheckDialog::onCheck, this);
  run->Add(m_check, 0, wxRIGHT, B);
  m_gauge = new wxGauge(this, wxID_ANY, 100);
  run->Add(m_gauge, 1, wxALIGN_CENTER_VERTICAL);
  top->Add(run, 0, wxEXPAND | wxALL, B);
  m_status = new wxStaticText(this, wxID_ANY, _("Not checked yet"));
  wxFont f = m_status->GetFont();
  f.SetWeight(wxFONTWEIGHT_BOLD);
  m_status->SetFont(f);
  top->Add(m_status, 0, wxLEFT | wxRIGHT, B);

  // ---- results
  m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(GetCharWidth() * 90, 220),
                          wxLC_REPORT | wxLC_SINGLE_SEL);
  m_list->AppendColumn(_("Leg / waypoint"), wxLIST_FORMAT_LEFT, GetCharWidth() * 22);
  m_list->AppendColumn(_("Length"), wxLIST_FORMAT_RIGHT, GetCharWidth() * 9);
  m_list->AppendColumn(_("Least depth"), wxLIST_FORMAT_RIGHT, GetCharWidth() * 11);
  m_list->AppendColumn(_("Bands"), wxLIST_FORMAT_LEFT, GetCharWidth() * 8);
  m_list->AppendColumn(_("Result"), wxLIST_FORMAT_LEFT, GetCharWidth() * 9);
  m_list->AppendColumn(_("First finding"), wxLIST_FORMAT_LEFT, GetCharWidth() * 40);
  m_list->Bind(wxEVT_LIST_ITEM_SELECTED, &RouteCheckDialog::onSelected, this);
  m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &RouteCheckDialog::onActivated, this);
  top->Add(m_list, 1, wxEXPAND | wxALL, B);
  top->Add(new wxStaticText(this, wxID_ANY,
                            _("Select a line for its findings; double-click to show it on the chart.")),
           0, wxLEFT | wxRIGHT, B);
  m_detail = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            wxSize(-1, 90), wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
  top->Add(m_detail, 0, wxEXPAND | wxALL, B);
  top->Add(new wxStaticText(this, wxID_ANY,
                            _("FAIL: a charted hazard (red ring). WARNING: sparse, poor or no "
                              "charting - no hazard identified (dashed line).\nCharted depths "
                              "only. A pass means no charted problem was found, not that the "
                              "route is safe.")),
           0, wxLEFT | wxRIGHT, B);

  auto *btns = new wxBoxSizer(wxHORIZONTAL);
  auto *copy = new wxButton(this, wxID_ANY, _("Copy report"));
  copy->Bind(wxEVT_BUTTON, &RouteCheckDialog::onCopy, this);
  auto *clear = new wxButton(this, wxID_ANY, _("Clear from chart"));
  clear->Bind(wxEVT_BUTTON, &RouteCheckDialog::onClear, this);
  auto *close = new wxButton(this, wxID_ANY, _("Close"));
  close->Bind(wxEVT_BUTTON, &RouteCheckDialog::onCloseBtn, this);
  btns->Add(copy, 0, wxRIGHT, B);
  btns->Add(clear, 0);
  btns->AddStretchSpacer();
  btns->Add(close, 0);
  top->Add(btns, 0, wxEXPAND | wxALL, B);

  Bind(wxEVT_CLOSE_WINDOW, &RouteCheckDialog::onClose, this);
  SetSizerAndFit(top);
  refreshRoutes(wxEmptyString);
  showResult();
}

void RouteCheckDialog::refreshRoutes(const wxString &select) {
  wxString want = select;
  if (want.IsEmpty() && m_route->GetSelection() != wxNOT_FOUND &&
      m_route->GetSelection() < (int)m_guids.size())
    want = m_guids[m_route->GetSelection()];
  if (want.IsEmpty()) want = GetActiveRouteGUID();
  m_route->Clear();
  m_guids.Clear();
  const wxString active = GetActiveRouteGUID();
  wxArrayString guids = GetRouteGUIDArray();
  int sel = 0;
  for (const wxString &g : guids) {
    std::unique_ptr<PlugIn_Route_Ex> r = GetRouteEx_Plugin(g);
    if (!r) continue;
    wxString name = r->m_NameString.IsEmpty() ? _("(unnamed route)") : r->m_NameString;
    int n = r->pWaypointList ? (int)r->pWaypointList->GetCount() : 0;
    wxString label = wxString::Format(_("%s  (%d waypoints)"), name, n);
    if (g == active) label += _("  - ACTIVE");
    if (g == want) sel = (int)m_guids.size();
    m_route->Append(label);
    m_guids.Add(g);
  }
  if (m_guids.IsEmpty()) {
    m_route->Append(_("(no routes in OpenCPN)"));
    m_route->SetSelection(0);
    m_check->Enable(false);
  } else {
    m_route->SetSelection(sel);
    m_check->Enable(!m_pi->routeCheckRunning());
  }
}

void RouteCheckDialog::selectRoute(const wxString &guid) { refreshRoutes(guid); }

PluginSettings RouteCheckDialog::readSettings() const {
  PluginSettings s = m_pi->settings();
  s.rcHalfWidthNm = m_halfW->GetValue();
  s.rcWaypointRadiusNm = m_wpR->GetValue();
  s.rcTideM = m_tide->GetValue();
  s.rcSparseBandM = m_sparse->GetValue();
  int ps = m_poor->GetSelection();
  if (ps >= 0 && ps < cm93::kNumSlots) s.rcPoorBand = cm93::kSlots[ps].ch;
  s.rcUndepthedFails = m_undep->GetValue();
  s.rcShowOnChart = m_show->GetValue();
  s.rcKeepOnClose = m_keep->GetValue();
  s.rcOnActivate = m_onActivate->GetValue();
  s.rcAlarmOnFail = m_alarm->GetValue();
  return s;
}

void RouteCheckDialog::onSettingChanged(wxCommandEvent &) {
  m_pi->applyRouteCheckSettings(readSettings());
  m_alarm->Enable(m_onActivate->GetValue());
  m_keep->Enable(m_show->GetValue());
}

void RouteCheckDialog::onCheck(wxCommandEvent &) {
  int i = m_route->GetSelection();
  if (i == wxNOT_FOUND || i >= (int)m_guids.size()) return;
  m_pi->applyRouteCheckSettings(readSettings());
  m_pi->startRouteCheck(m_guids[i], false);
}

void RouteCheckDialog::onRefresh(wxCommandEvent &) { refreshRoutes(wxEmptyString); }

void RouteCheckDialog::showMessage(const wxString &m) {
  m_status->SetForegroundColour(GetForegroundColour());
  m_status->SetLabel(m);
  Layout();
}

void RouteCheckDialog::onProgress() {
  const bool run = m_pi->routeCheckRunning();
  m_check->Enable(!run && !m_guids.IsEmpty());
  m_gauge->SetValue(run ? (int)std::lround(m_pi->routeCheckProgress() * 100) : 0);
  if (run) showMessage(wxString::Format(_("Checking... %d%%"),
                                        (int)std::lround(m_pi->routeCheckProgress() * 100)));
}

void RouteCheckDialog::showResult() {
  m_safety->SetLabel(wxString::Format(
      _("Safety depth %.1f m (set on the Look-ahead tab of the settings): charted depth + "
        "tide below it is a shoal."),
      m_pi->settings().laSafetyDepthM));
  m_alarm->Enable(m_onActivate->GetValue());
  m_keep->Enable(m_show->GetValue());
  onProgress();
  m_list->DeleteAllItems();
  m_rows.clear();
  m_detail->Clear();
  if (!m_pi->haveRouteCheck()) {
    if (!m_pi->routeCheckRunning()) showMessage(_("Not checked yet"));
    return;
  }
  const cm93::RouteCheckResult &r = m_pi->routeCheckResult();
  m_status->SetForegroundColour(status_text_colour(r.overall));
  m_status->SetLabel(wxString::Format(_("%s  -  %s, %.1f nm"), U(r.status), U(r.routeName),
                                      r.totalNm));
  auto add = [&](const wxString &item, const wxString &len, const wxString &least,
                 const wxString &bands, cm93::CheckStatus st,
                 const std::vector<cm93::RouteIssue> &issues, int leg, int wp) {
    long n = m_list->InsertItem(m_list->GetItemCount(), item);
    m_list->SetItem(n, 1, len);
    m_list->SetItem(n, 2, least);
    m_list->SetItem(n, 3, bands);
    m_list->SetItem(n, 4, wxString::FromUTF8(cm93::check_status_name(st)));
    wxString first;
    for (const auto &i : issues)
      if (!i.text.empty()) {
        first = U(i.text);
        break;
      }
    m_list->SetItem(n, 5, first);
    m_list->SetItemTextColour(n, status_text_colour(st));
    m_rows.push_back({leg, wp});
  };
  for (const auto &l : r.legs)
    add(wxString::Format(_("Leg %d: %s - %s"), l.index + 1, U(l.from), U(l.to)),
        wxString::Format(_T("%.2f nm"), l.lengthNm),
        l.haveLeast ? wxString::Format(_T("%.1f m"), l.leastDepth) : wxString(_T("-")),
        U(l.bands), l.status, l.issues, l.index, -1);
  for (const auto &w : r.waypoints)
    add(wxString::Format(_("Waypoint %s"), U(w.name)), wxEmptyString,
        w.onLand      ? wxString(_("land"))
        : !w.haveDepth ? wxString(_T("-"))
        : w.depthBroad ? (std::isfinite(w.depth2) ? wxString::Format(_T("%g-%g m"), w.depth, w.depth2)
                                                  : wxString::Format(_T("%g+ m"), w.depth))
                       : wxString::Format(_T("%.1f m"), w.depth),
        wxEmptyString, w.status, w.issues, -1, w.index);
  Layout();
}

void RouteCheckDialog::onSelected(wxListEvent &e) {
  long n = e.GetIndex();
  m_detail->Clear();
  if (n < 0 || n >= (long)m_rows.size() || !m_pi->haveRouteCheck()) return;
  const cm93::RouteCheckResult &r = m_pi->routeCheckResult();
  const auto &issues = m_rows[n].leg >= 0 ? r.legs[m_rows[n].leg].issues
                                          : r.waypoints[m_rows[n].wp].issues;
  wxString t;
  for (const auto &i : issues)
    if (!i.text.empty()) t += U(i.text) + _T("\n");
  m_detail->SetValue(t.IsEmpty() ? _("Nothing found.") : t);
}

void RouteCheckDialog::onActivated(wxListEvent &e) {
  long n = e.GetIndex();
  if (n < 0 || n >= (long)m_rows.size() || !m_pi->haveRouteCheck()) return;
  const cm93::RouteCheckResult &r = m_pi->routeCheckResult();
  cm93::LatLon p;
  const std::vector<cm93::RouteIssue> *issues;
  if (m_rows[n].leg >= 0) {
    const auto &l = r.legs[m_rows[n].leg];
    p = cm93::rhumb_destination(r.waypoints[l.index].pos, l.courseDeg, l.lengthNm / 2);
    issues = &l.issues;
  } else {
    p = r.waypoints[m_rows[n].wp].pos;
    issues = &r.waypoints[m_rows[n].wp].issues;
  }
  for (const auto &i : *issues)
    if (i.kind != cm93::IssueKind::PoorChart) {
      p = i.pos;
      break;
    }
  m_pi->jumpTo(p.lat, p.lon);
}

void RouteCheckDialog::onCopy(wxCommandEvent &) {
  if (!m_pi->haveRouteCheck()) return;
  std::string rep = cm93::route_check_report(m_pi->routeCheckResult());
  if (wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(U(rep)));
    wxTheClipboard->Close();
  }
}

void RouteCheckDialog::onClear(wxCommandEvent &) { m_pi->hideRouteDrawing(); }

void RouteCheckDialog::closeWindow() {
  if (!m_keep->GetValue()) m_pi->hideRouteDrawing();
  Hide();
}

void RouteCheckDialog::onCloseBtn(wxCommandEvent &) { closeWindow(); }

void RouteCheckDialog::onClose(wxCloseEvent &e) {
  if (e.CanVeto()) {
    e.Veto();
    closeWindow();  // the window is kept; deleted by the plugin when it is disabled
  } else {
    m_pi->onRouteDialogClosed();  // forgets this window
    Destroy();
  }
}
