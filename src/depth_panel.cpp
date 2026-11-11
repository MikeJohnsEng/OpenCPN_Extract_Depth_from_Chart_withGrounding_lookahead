/***************************************************************************
 * cm93depth_pi - floating display window
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "depth_panel.h"

#include <wx/clipbrd.h>
#include <wx/statline.h>

#include <cmath>
#include <initializer_list>

#include "cm93depth_pi.h"

static wxString class_label(const std::string &c) {
  if (c == "SOUNDG") return _("sounding");
  if (c == "DEPARE") return _("depth area");
  if (c == "DRGARE") return _("dredged area");
  if (c == "OBSTRN") return _("obstruction");
  if (c == "WRECKS") return _("wreck");
  if (c == "UWTROC") return _("rock");
  if (c == "DEPCNT") return _("depth contour");
  if (c == "INTERP") return _("interpolated");
  if (c == "LNDARE") return _("land");
  if (c == "SBDARE") return _("seabed area, no depth (taken as 0 m)");
  if (c == "HOLE") return _("charted feature with no depth (reef, rock, islet or unsurveyed)");
  return wxString::FromUTF8(c);
}

DepthPanel::DepthPanel(wxWindow *parent, cm93depth_pi *plugin)
    : wxDialog(parent, wxID_ANY, _("CM93 Least Depth"), wxDefaultPosition,
               wxDefaultSize,
               wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER | wxFRAME_FLOAT_ON_PARENT),
      m_pi(plugin) {
  auto *top = new wxBoxSizer(wxVERTICAL);

  m_depth = new wxStaticText(this, wxID_ANY, _T("--"), wxDefaultPosition,
                             wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
  wxFont big = m_depth->GetFont();
  big.SetPointSize(big.GetPointSize() * 2 + 4);
  big.SetWeight(wxFONTWEIGHT_BOLD);
  m_depth->SetFont(big);
  top->Add(m_depth, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
  m_caption = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
  top->Add(m_caption, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
  m_shoalShort = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
  {
    wxFont f = m_shoalShort->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    m_shoalShort->SetFont(f);
  }
  top->Add(m_shoalShort, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
  m_alarmOffText = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
  {
    wxFont f = m_alarmOffText->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    m_alarmOffText->SetFont(f);
  }
  top->Add(m_alarmOffText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
  top->AddSpacer(6);

  auto add = [&](wxStaticText *&t) {
    t = new wxStaticText(this, wxID_ANY, wxEmptyString);
    top->Add(t, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  };
  add(m_where);
  add(m_atPos);
  add(m_hazards);
  add(m_scope);
  m_lookLine = new wxStaticLine(this);
  top->Add(m_lookLine, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  add(m_lookHead);
  wxFont bold = m_lookHead->GetFont();
  bold.SetWeight(wxFONTWEIGHT_BOLD);
  m_lookHead->SetFont(bold);
  add(m_look);
  m_statusLine = new wxStaticLine(this);
  top->Add(m_statusLine, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  add(m_status);
  add(m_output);
  add(m_sentence);
  add(m_lookSentence);
  wxFont mono(wxFontInfo(m_sentence->GetFont().GetPointSize() - 1).Family(wxFONTFAMILY_TELETYPE));
  m_sentence->SetFont(mono);
  m_lookSentence->SetFont(mono);

  auto *btns = new wxBoxSizer(wxHORIZONTAL);
  m_toggle = new wxButton(this, wxID_ANY, _("Minimise"));
  m_toggle->Bind(wxEVT_BUTTON, &DepthPanel::onToggleSize, this);
  m_report = new wxButton(this, wxID_ANY, _("Copy report"));
  m_report->SetToolTip(_("Copy a troubleshooting report for this position to the clipboard"));
  m_report->Bind(wxEVT_BUTTON, &DepthPanel::onCopyReport, this);
  m_settings = new wxButton(this, wxID_ANY, _("Settings..."));
  m_settings->Bind(wxEVT_BUTTON, &DepthPanel::onSettings, this);
  btns->Add(m_toggle, 0, wxRIGHT, 6);
  m_alarmBtn = new wxButton(this, wxID_ANY, _("Alarm: on"));
  m_alarmBtn->Bind(wxEVT_BUTTON, &DepthPanel::onAlarmButton, this);
  btns->Add(m_alarmBtn, 0, wxRIGHT, 6);
  btns->AddStretchSpacer();
  btns->Add(m_report, 0, wxRIGHT, 6);
  btns->Add(m_settings, 0);
  top->Add(btns, 0, wxEXPAND | wxALL, 6);
  Bind(wxEVT_CLOSE_WINDOW, &DepthPanel::onClose, this);

  SetSizer(top);
  m_compact = m_pi->settings().panelCompact;
  applyCompact();
}

void DepthPanel::onAlarmButton(wxCommandEvent &) {
  if (!m_pi->alarmOff()) {
    int r = wxMessageBox(_("Switch the grounding alarm OFF?\n\nThere will be no shoal or "
                           "depth alarm until it is switched on again here (or OpenCPN is "
                           "restarted)."),
                         _("Grounding alarm"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
    if (r != wxYES) return;
    m_pi->setAlarmOff(true);
  } else {
    m_pi->setAlarmOff(false);
  }
  refresh();
}

void DepthPanel::onToggleSize(wxCommandEvent &) {
  m_compact = !m_compact;
  m_pi->setPanelCompact(m_compact);
  applyCompact();
  refresh();
}

// Compact: the depth figure, what it is, a shoal warning when there is one,
// and the Enlarge button. Full: everything, for checking and setting up.
void DepthPanel::applyCompact() {
  const bool full = !m_compact;
  const PluginSettings &s = m_pi->settings();
  for (wxWindow *w : std::initializer_list<wxWindow *>{
           m_where, m_atPos, m_hazards, m_scope, m_statusLine, m_status, m_output,
           m_sentence, m_report, m_settings})
    w->Show(full);
  const bool laFull = full && s.laEnabled;
  m_lookLine->Show(laFull);
  m_lookHead->Show(laFull);
  m_look->Show(laFull);
  m_lookSentence->Show(laFull);
  m_toggle->SetLabel(m_compact ? _("Enlarge") : _("Minimise"));
  m_toggle->SetToolTip(m_compact ? _("Show all details, sentences and settings")
                                 : _("Show only the depth"));
  SetTitle(m_compact ? _("Chart depth") : _("CM93 Least Depth"));
  // re-fit, shrinking too when minimised
  SetMinSize(wxSize(GetCharWidth() * (m_compact ? 22 : 46), -1));
  Layout();
  SetSize(GetSizer()->ComputeFittingWindowSize(this));
}

void DepthPanel::onClose(wxCloseEvent &) { m_pi->onPanelClosed(); }

void DepthPanel::onSettings(wxCommandEvent &) { m_pi->showPreferences(this); }

void DepthPanel::onCopyReport(wxCommandEvent &) {
  wxString report = m_pi->diagnosticsReport();
  if (wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(report));
    wxTheClipboard->Flush();  // keep it after OpenCPN exits
    wxTheClipboard->Close();
    wxMessageBox(_("Report copied to the clipboard."), _("CM93 Least Depth"),
                 wxOK | wxICON_INFORMATION, this);
  } else {
    wxMessageBox(report, _("CM93 Least Depth report"), wxOK, this);
  }
}

void DepthPanel::setColorScheme(PI_ColorScheme cs) {
  wxColour bg, fg;
  switch (cs) {
    case PI_GLOBAL_COLOR_SCHEME_DUSK:
      bg = wxColour(60, 60, 60);
      fg = wxColour(220, 220, 220);
      break;
    case PI_GLOBAL_COLOR_SCHEME_NIGHT:
      bg = wxColour(0, 0, 0);
      fg = wxColour(170, 40, 40);  // dim red, keeps night vision
      break;
    default:
      bg = wxNullColour;
      fg = wxNullColour;
      break;
  }
  SetBackgroundColour(bg);
  for (wxWindow *w : GetChildren()) {
    w->SetBackgroundColour(bg);
    w->SetForegroundColour(fg);
  }
  Refresh();
}

void DepthPanel::refresh() {
  const cm93::DepthResult &r = m_pi->lastResult();
  const PluginSettings &s = m_pi->settings();
  const bool here = s.mode == (int)cm93::SearchMode::DepthAreaHere;
  const bool interp = s.mode == (int)cm93::SearchMode::Interpolated;

  if (r.valid && r.haveLeast) {
    m_depth->SetLabel(wxString::Format(_T("%.1f m"), r.leastDepth));
    wxString src = class_label(r.leastClass);
    if (here)
      m_where->SetLabel(_("Minimum of the charted depth area at the vessel"));
    else if (interp) {
      wxString d;
      for (const auto &sd : r.soundings)
        d += wxString::Format(_T("%s%.1f m @ %.2f nm (%c)"), d.IsEmpty() ? _T("") : _T(", "),
                              sd.depth, sd.distNm, sd.scaleChar);
      wxString lbl = wxString::Format(_("Interpolated from %d sounding(s): %s"),
                                      (int)r.soundings.size(), d);
      lbl += _T("\n") + wxString::Format(_("Shallowest of these %.1f m; %s"), r.soundingMin,
                                         r.enclosed ? _("surrounded on all sides")
                                                    : _("not surrounded on all sides"));
      if (r.landBlocked > 0)
        lbl += _T("\n") + wxString::Format(_("%d nearer sounding(s) left out: land in between"),
                                           r.landBlocked);
      if (r.clamped)
        lbl += _T("\n") + wxString::Format(_("Estimate %.1f m limited to the charted depth band"),
                                           r.rawEstimate);
      m_where->SetLabel(lbl);
    }
    else if (r.rangeNm < 0.005)
      m_where->SetLabel(wxString::Format(_("Here (%s)"), src));
    else
      m_where->SetLabel(wxString::Format(_("%.2f nm at %03.0f%s T  (%s)"),
                                         r.rangeNm, r.bearingDeg,
                                         wxString(wxUniChar(0x00B0)), src));
  } else if (r.valid && r.onLand) {
    m_depth->SetLabel(_("LAND"));
    m_where->SetLabel(_("The position is on charted land"));
  } else {
    m_depth->SetLabel(_T("--"));
    m_where->SetLabel(wxEmptyString);
  }

  if (r.valid && r.haveAtPos) {
    wxString hi = std::isfinite(r.atPosMax)
                      ? wxString::Format(_T("%.1f"), r.atPosMax)
                      : wxString(_T("?"));
    wxString lbl = (r.atPosMin == 0.0 && r.atPosMax == 0.0)
                       ? wxString(_("Depth area here: no depth charted, taken as 0 m (drying)"))
                       : wxString::Format(_("Depth area here: %.1f to %s m"), r.atPosMin, hi);
    if (r.atPosScaleChar != ' ' && r.atPosScaleChar != r.scaleChar)
      lbl += wxString::Format(_("  (band %c)"), r.atPosScaleChar);
    m_atPos->SetLabel(lbl);
  } else {
    m_atPos->SetLabel(r.valid && !r.onLand ? _("Depth area here: none charted") : wxString());
  }

  wxString haz;
  if (r.valid && (here || interp) && (r.shallowHazards > 0 || r.unknownHazards > 0)) {
    const wxString where = here ? _("Inside this area") : _("Among these soundings");
    if (r.shallowHazards > 0)
      haz = wxString::Format(_("%s: %d danger(s) charted shallower, least %.1f m"),
                             where, r.shallowHazards, r.shallowHazardMin);
    if (r.unknownHazards > 0) {
      if (!haz.IsEmpty()) haz += _T("\n");
      haz += wxString::Format(_("%s: %d danger(s) with no charted depth"), where,
                              r.unknownHazards);
    }
  } else if (r.valid && r.unknownHazards > 0) {
    haz = wxString::Format(_("Dangers with no charted depth in scope: %d"),
                           r.unknownHazards);
  }
  if (r.valid && r.dangerHere) {
    wxString d = r.dangerHereAssumed
                     ? wxString::Format(_("Over a %s with no charted depth (band %c chart): taken as %.1f m"),
                                        class_label(r.dangerHereClass), r.dangerHereScale,
                                        r.dangerHereDepth)
                     : wxString::Format(_("Over a %s charted at %.1f m (band %c chart)"),
                                        class_label(r.dangerHereClass), r.dangerHereDepth,
                                        r.dangerHereScale);
    haz = haz.IsEmpty() ? d : d + _T("\n") + haz;
  }
  if (r.valid && r.dangerOverruled) {
    wxString d = wxString::Format(
        _("In a %s area (band %c chart), not counted: a %.1f m sounding on the band %c chart "
          "%.2f nm away lies inside it"),
        class_label(r.overruledClass), r.overruledScale, r.overruledByDepth, r.overruledByScale,
        r.overruledByDistNm);
    haz = haz.IsEmpty() ? d : haz + _T("\n") + d;
  }
  m_hazards->SetLabel(haz);
  // a shallower danger inside the area is worth drawing the eye to
  m_hazards->SetForegroundColour(
      (here || interp) && (r.shallowHazards > 0 || r.dangerHere) ? *wxRED
                                                                 : GetForegroundColour());

  if (r.valid && !r.onLand) {
    wxString scope = here     ? wxString(_("Depth area at vessel"))
                     : interp ? wxString::Format(_("Soundings within %.2f nm"), s.radiusNm)
                     : s.mode == 1
                         ? wxString::Format(_("Within %.2f nm"), s.radiusNm)
                         : wxString(_("Whole chart"));
    wxString chart = r.cellName.empty()
                         ? wxString::Format(_("CM93 %c cell %d"), r.scaleChar, r.cellIndex)
                         : wxString::Format(_("ENC %s (band %c)"),
                                            wxString::FromUTF8(r.cellName), r.scaleChar);
    m_scope->SetLabel(wxString::Format(_("%s of %s  (%d objects)"), scope, chart,
                                       r.objectsSearched) +
                      _T("\n") + _("Bands: ") + wxString::FromUTF8(r.scaleTrace));
  } else if (!r.scaleTrace.empty()) {
    m_scope->SetLabel(_("Bands: ") + wxString::FromUTF8(r.scaleTrace));
  } else {
    m_scope->SetLabel(wxEmptyString);
  }

  wxString st = wxString::FromUTF8(r.status);
  if (!r.valid && !m_pi->chartStatus().IsEmpty() &&
      r.status.find("charts") != std::string::npos)
    st = m_pi->chartStatus();
  if (!r.readError.empty())
    st += _T("\n") + _("Chart read error ") + wxString::FromUTF8(r.readError);
  m_status->SetLabel(_("Status: ") + st);
  m_output->SetLabel(m_pi->outputStatus());
  m_sentence->SetLabel(m_pi->lastSentence());

  // ---- grounding alarm switch (both sizes)
  {
    const bool avail = m_pi->alarmAvailable();
    m_alarmBtn->Show(avail);
    const bool off = m_pi->alarmOff();
    m_alarmBtn->SetLabel(off ? _("Alarm: OFF") : _("Alarm: on"));
    m_alarmBtn->SetToolTip(off ? _("Switch the grounding alarm back on")
                               : _("Switch the grounding alarm off (asks first)"));
    wxString t;
    if (avail && off) {
      int left = m_pi->alarmOffMinutesLeft();
      t = left >= 0 ? wxString::Format(_("GROUNDING ALARM OFF - back on in %d min"), left)
                    : wxString(_("GROUNDING ALARM OFF"));
    }
    m_alarmOffText->SetLabel(t);
    m_alarmOffText->SetForegroundColour(wxColour(200, 120, 0));
    m_alarmOffText->Show(!t.IsEmpty());
  }

  // ---- what the big figure is (both sizes)
  {
    wxString cap;
    if (r.valid && r.onLand) cap = wxEmptyString;
    else if (interp) cap = _("Interpolated chart depth");
    else if (here) cap = _("Minimum of the charted depth area here");
    else if (s.mode == 1) cap = wxString::Format(_("Least charted depth within %.2f nm"), s.radiusNm);
    else cap = _("Least charted depth in the cell");
    if (r.valid && r.haveLeast && r.dangerHere && r.leastDepth >= r.dangerHereDepth - 1e-9)
      cap = r.dangerHereAssumed
                ? wxString::Format(_("Over a %s, no charted depth"), class_label(r.dangerHereClass))
                : wxString::Format(_("Over a %s"), class_label(r.dangerHereClass));
    if (r.valid && r.haveLeast && m_compact && r.status.rfind("Position stale", 0) == 0)
      cap += _T("  ") + _("(position stale)");
    m_caption->SetLabel(cap);
  }

  // ---- look-ahead
  const cm93::LookAheadResult &la = m_pi->lastLookAhead();
  const bool laOn = s.laEnabled;
  const bool laFull = laOn && !m_compact;
  m_lookLine->Show(laFull);
  m_lookHead->Show(laFull);
  m_look->Show(laFull);
  m_lookSentence->Show(laFull);
  {
    wxString sh;
    if (laOn && la.valid && la.haveShoal) {
      sh = wxString::Format(_("Shoal < %.1f m ahead: %.2f nm"), la.safetyDepthM, la.shoal.aheadNm);
      if (std::isfinite(la.shoal.timeMin)) sh += wxString::Format(_(", %.0f min"), la.shoal.timeMin);
    }
    m_shoalShort->SetLabel(sh);
    m_shoalShort->SetForegroundColour(*wxRED);
    m_shoalShort->Show(m_compact && !sh.IsEmpty());
  }
  if (laOn) {
    const wxString deg(wxUniChar(0x00B0));
    auto what = [&](const cm93::LookHit &h) {
      if (h.land) return wxString(_("land"));
      if (h.undepthed) return class_label(h.cls) + _(", no charted depth");
      return wxString::Format(_T("%s %.1f m"), class_label(h.cls), h.depth);
    };
    auto brg = [&](const cm93::LookHit &h) {
      return h.rangeNm < 0.005 ? wxString(_("here"))
                               : wxString::Format(_T("%03.0f%s T"), h.bearingDeg, deg);
    };
    wxString head, body;
    bool alarm = false;
    if (!la.valid) {
      head = _("Look-ahead: ") + wxString::FromUTF8(la.status);
    } else if (la.haveShoal) {
      alarm = true;
      head = wxString::Format(_("Shoal ahead (< %.1f m): %.2f nm"), la.safetyDepthM,
                              la.shoal.aheadNm);
      if (std::isfinite(la.shoal.timeMin))
        head += wxString::Format(_(", %.1f min"), la.shoal.timeMin);
      head += _T(", ") + brg(la.shoal) + _T("\n") + what(la.shoal);
    } else {
      head = wxString::Format(_("Clear ahead of depths under %.1f m"), la.safetyDepthM);
    }
    if (la.valid) {
      if (la.haveLeast)
        body = wxString::Format(_("Least ahead %.1f m at %.2f nm, "), la.least.depth,
                                la.least.rangeNm) +
               brg(la.least) + _T("  (") + class_label(la.least.cls) + _T(")");
      else
        body = _("No charted depths in the zone");
      if (la.undepthed > 0)
        body += _T("\n") + wxString::Format(_("Dangers with no charted depth: %d, nearest %.2f nm ahead"),
                                            la.undepthed, la.nearestUndepthed.aheadNm);
      wxString zone =
          la.zone == cm93::LookZone::Sector
              ? wxString::Format(_("Sector %.2f nm, %s%.0f%s"), la.lengthNm,
                                 wxString(wxUniChar(0x00B1)), la.halfAngleDeg, deg)
              : wxString::Format(_("Corridor %.2f nm x %s%.2f nm"), la.lengthNm,
                                 wxString(wxUniChar(0x00B1)), la.halfWidthNm);
      zone += wxString::Format(_(" on %03.0f%s T"), la.courseDeg, deg);
      zone += la.courseFromHeading
                  ? wxString::Format(_(" (heading; SOG %.1f kn)"), la.sogKn)
                  : wxString::Format(_(" (COG, %.1f kn)"), la.sogKn);
      body += _T("\n") + zone;
      body += _T("\n") + _("Bands along it: ") + wxString::FromUTF8(la.scaleTrace);
    }
    m_lookHead->SetLabel(head);
    m_lookHead->SetForegroundColour(alarm ? *wxRED : GetForegroundColour());
    m_look->SetLabel(body);
    m_lookSentence->SetLabel(m_pi->lastLookSentence());
  }

  // keep text from being truncated as labels grow
  int w = GetCharWidth() * 46;
  m_status->Wrap(w);
  m_output->Wrap(w);
  m_look->Wrap(w);
  Layout();
  // grow (never shrink) so a size the user chose is kept
  wxSize best = GetSizer()->ComputeFittingWindowSize(this);
  wxSize cur = GetSize();
  if (best.x > cur.x || best.y > cur.y)
    SetSize(wxSize(wxMax(best.x, cur.x), wxMax(best.y, cur.y)));
}
