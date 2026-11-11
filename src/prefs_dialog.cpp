/***************************************************************************
 * cm93depth_pi - preferences dialog
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "prefs_dialog.h"

#include <wx/notebook.h>
#include <algorithm>
#include <initializer_list>
#include <wx/statbox.h>

#include "nmea_out.h"
#include "serial_port.h"

PrefsDialog::PrefsDialog(wxWindow *parent, cm93depth_pi *plugin,
                         const PluginSettings &s)
    : wxDialog(parent, wxID_ANY, _("CM93 Least Depth - Preferences"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_pi(plugin),
      m_s(s) {
  auto *top = new wxBoxSizer(wxVERTICAL);
  const int B = 4;

  auto grid = [&](wxStaticBoxSizer *box) {
    auto *g = new wxFlexGridSizer(2, B, 2 * B);
    g->AddGrowableCol(1);
    box->Add(g, 0, wxEXPAND | wxALL, B);
    return g;
  };
  auto label = [&](wxWindow *p, const wxString &t) {
    return new wxStaticText(p, wxID_ANY, t);
  };

  // Three tabs keep the dialog short enough for small screens
  auto *nb = new wxNotebook(this, wxID_ANY);
  auto page = [&](const wxString &title, wxBoxSizer *&sz) {
    auto *pg = new wxPanel(nb, wxID_ANY);
    sz = new wxBoxSizer(wxVERTICAL);
    pg->SetSizer(sz);
    nb->AddPage(pg, title);
    return pg;
  };
  wxBoxSizer *s1, *s2, *s3;
  wxPanel *p1 = page(_("Charts and search"), s1);
  wxPanel *p2 = page(_("Look-ahead"), s2);
  wxPanel *p3 = page(_("Output and chart"), s3);
  wxBoxSizer *s4;
  wxPanel *p4 = page(_("Alarm"), s4);

  // ---- Chart sources -----------------------------------------------------
  {
    auto *src = new wxStaticBoxSizer(wxVERTICAL, p1, _("Chart sources (the most detailed chart wins)"));
    wxWindow *sb = src->GetStaticBox();
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    m_useCm93 = new wxCheckBox(sb, wxID_ANY, _("CM93 charts"));
    m_useCm93->SetValue(s.useCm93);
    m_useEnc = new wxCheckBox(sb, wxID_ANY, _("S-57 ENC cells (*.000, unencrypted)"));
    m_useEnc->SetValue(s.useEnc);
    m_useEnc->SetToolTip(_("ENC cells in OpenCPN's chart directories and their sub-folders, "
                           "with their updates (*.001, *.002 ...). Encrypted S-63 cells "
                           "cannot be read."));
    row->Add(m_useCm93, 0, wxRIGHT, 4 * B);
    row->Add(m_useEnc, 0);
    src->Add(row, 0, wxALL, B);
    auto *g0 = grid(src);
    g0->Add(label(sb, _("More ENC folders")), 0, wxALIGN_CENTER_VERTICAL);
    m_encDirs = new wxTextCtrl(sb, wxID_ANY, s.encExtraDirs);
    m_encDirs->SetToolTip(_("Folders to search for ENC cells besides OpenCPN's chart "
                            "directories, separated by ';'. Usually empty."));
    g0->Add(m_encDirs, 1, wxEXPAND);
    auto *row2 = new wxBoxSizer(wxHORIZONTAL);
    m_encStatus = new wxStaticText(sb, wxID_ANY, m_pi->encStatus());
    row2->Add(m_encStatus, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, B);
    auto *rescan = new wxButton(sb, wxID_ANY, _("Rescan ENC cells"));
    rescan->SetToolTip(_("Look for new or updated ENC cells now (done by itself at start-up "
                         "and when OpenCPN's chart directories change)."));
    rescan->Bind(wxEVT_BUTTON, &PrefsDialog::onRescanEnc, this);
    row2->Add(rescan, 0);
    src->Add(row2, 0, wxEXPAND | wxALL, B);
    s1->Add(src, 0, wxEXPAND | wxALL, B);
  }

  // ---- Charts ------------------------------------------------------------
  auto *charts = new wxStaticBoxSizer(wxVERTICAL, p1, _("CM93 charts"));
  wxWindow *cp = charts->GetStaticBox();
  m_autoRoot = new wxCheckBox(cp, wxID_ANY,
                              _("Find CM93 automatically in OpenCPN's chart directories"));
  m_autoRoot->SetValue(s.cm93Root.IsEmpty());
  charts->Add(m_autoRoot, 0, wxALL, B);
  auto *g = grid(charts);
  g->Add(label(cp, _("CM93 folder")), 0, wxALIGN_CENTER_VERTICAL);
  m_root = new wxDirPickerCtrl(cp, wxID_ANY, s.cm93Root.IsEmpty() ? m_pi->activeRoot() : s.cm93Root,
                               _("Select the CM93 folder"), wxDefaultPosition,
                               wxSize(GetCharWidth() * 40, -1),
                               wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
  g->Add(m_root, 1, wxEXPAND);
  g->Add(label(cp, _("Scale band")), 0, wxALIGN_CENTER_VERTICAL);
  // (a fixed CM93 band leaves the ENC cells out)
  m_scale = new wxChoice(cp, wxID_ANY);
  m_scale->Append(_("Auto (most detailed covering chart, CM93 or ENC)"));
  const char *names[] = {"Z  1:20,000,000", "A  1:3,000,000", "B  1:1,000,000",
                         "C  1:200,000",    "D  1:100,000",   "E  1:50,000",
                         "F  1:20,000",     "G  1:7,500"};
  for (const char *n : names) m_scale->Append(wxString::FromUTF8(n));
  m_scale->SetSelection(s.scaleIdx + 1);
  g->Add(m_scale, 1, wxEXPAND);
  g->Add(label(cp, _("Offset east / north (m)")), 0, wxALIGN_CENTER_VERTICAL);
  auto *offs = new wxBoxSizer(wxHORIZONTAL);
  m_offE = new wxSpinCtrlDouble(cp, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                wxDefaultSize, wxSP_ARROW_KEYS, -5000, 5000, s.offsetE, 1);
  m_offN = new wxSpinCtrlDouble(cp, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                wxDefaultSize, wxSP_ARROW_KEYS, -5000, 5000, s.offsetN, 1);
  offs->Add(m_offE, 1, wxRIGHT, B);
  offs->Add(m_offN, 1);
  g->Add(offs, 1, wxEXPAND);
  s1->Add(charts, 0, wxEXPAND | wxALL, B);

  // ---- Search ------------------------------------------------------------
  auto *search = new wxStaticBoxSizer(wxVERTICAL, p1, _("Least depth search"));
  wxWindow *sp = search->GetStaticBox();
  // radio order -> PluginSettings::mode
  wxString modes[] = {_("Depth area at the vessel (minimum of its depth band)"),
                      _("Whole cell under the vessel"),
                      _("Within a radius of the vessel"),
                      _("Interpolated from the nearest soundings (within the radius)")};
  m_mode = new wxRadioBox(sp, wxID_ANY, _("Search area"), wxDefaultPosition,
                          wxDefaultSize, 4, modes, 1, wxRA_SPECIFY_COLS);
  m_mode->SetSelection(s.mode == 2 ? 0 : s.mode == 0 ? 1 : s.mode == 1 ? 2 : 3);
  search->Add(m_mode, 0, wxEXPAND | wxALL, B);
  g = grid(search);
  g->Add(label(sp, _("Radius (nm)")), 0, wxALIGN_CENTER_VERTICAL);
  m_radius = new wxSpinCtrlDouble(sp, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxDefaultSize, wxSP_ARROW_KEYS, 0.05, 50.0,
                                  s.radiusNm, 0.05);
  m_radius->SetDigits(2);
  g->Add(m_radius, 0);
  m_quad = new wxCheckBox(sp, wxID_ANY,
                          _("Interpolation: take one sounding from each direction around the boat"));
  m_quad->SetToolTip(_("Off: the four nearest soundings. On: the nearest to the NE, SE, SW and "
                       "NW first, which can reach for a distant sounding if one direction is empty."));
  m_quad->SetValue(s.interpQuadrants);
  search->Add(m_quad, 0, wxLEFT | wxRIGHT | wxTOP, B);
  m_landChk = new wxCheckBox(sp, wxID_ANY,
                             _("Interpolation: leave out soundings with land between them and the boat"));
  m_landChk->SetToolTip(_("Stops a large radius reaching round a headland or behind an island."));
  m_landChk->SetValue(s.interpLandCheck);
  search->Add(m_landChk, 0, wxLEFT | wxRIGHT | wxTOP, B);
  {
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(label(sp, _("Over a rock / wreck / reef with no charted depth:")), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, B);
    m_undepRule = new wxChoice(sp, wxID_ANY);
    m_undepRule->Append(_("0 m (may reach the surface)"));
    m_undepRule->Append(_("least depth of the surrounding area (ECDIS rule)"));
    m_undepRule->SetSelection(s.undepthedDangerRule);
    m_undepRule->SetToolTip(_("Used in 'depth area at the vessel' and interpolated modes. "
                              "Dangers charted as drying or awash are always 0 m; foul "
                              "ground and fish havens are not counted as dangers."));
    row->Add(m_undepRule, 1);
    search->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, B);
    m_coarse = new wxCheckBox(sp, wxID_ANY,
                              _("Count rocks, wrecks and obstructions charted only at smaller scales"));
    m_coarse->SetToolTip(_("CM93's small-scale charts carry obstruction areas that the detailed "
                           "charts sometimes lack. Off: only those on the chart the depth "
                           "comes from, or a more detailed one."));
    m_coarse->SetValue(s.coarseDangers);
    search->Add(m_coarse, 0, wxLEFT | wxRIGHT | wxTOP, B);
    auto *row2 = new wxBoxSizer(wxHORIZONTAL);
    row2->Add(label(sp, _("Danger areas overruled by a more detailed chart's soundings within (nm):")),
              0, wxALIGN_CENTER_VERTICAL | wxRIGHT, B);
    m_coarseCheck = new wxSpinCtrlDouble(sp, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                         wxDefaultSize, wxSP_ARROW_KEYS, 0.0, 2.0,
                                         s.coarseCheckNm, 0.05);
    m_coarseCheck->SetDigits(2);
    m_coarseCheck->SetToolTip(_("Small-scale charts mark whole zones as obstruction areas. Where a "
                                "more detailed chart has a sounding inside such an area this close "
                                "to the vessel, at least the safety depth deep, the area is not "
                                "counted. Point dangers always count. 0 = never overrule."));
    row2->Add(m_coarseCheck, 0);
    search->Add(row2, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, B);
  }
  search->Add(label(sp, _("Count these chart objects:")), 0, wxLEFT | wxTOP, B);
  auto *objs = new wxGridSizer(2, B, 2 * B);
  m_snd = new wxCheckBox(sp, wxID_ANY, _("Soundings"));
  m_area = new wxCheckBox(sp, wxID_ANY, _("Depth / dredged areas"));
  m_haz = new wxCheckBox(sp, wxID_ANY, _("Rocks, wrecks, obstructions"));
  m_cnt = new wxCheckBox(sp, wxID_ANY, _("Depth contours"));
  m_snd->SetValue(s.useSoundings);
  m_area->SetValue(s.useAreas);
  m_haz->SetValue(s.useHazards);
  m_cnt->SetValue(s.useContours);
  objs->Add(m_snd);
  objs->Add(m_area);
  objs->Add(m_haz);
  objs->Add(m_cnt);
  search->Add(objs, 0, wxALL, B);
  s1->Add(search, 0, wxEXPAND | wxALL, B);

  // ---- Look-ahead ----------------------------------------------------------
  {
    m_laOn = new wxCheckBox(p2, wxID_ANY,
                            _("Look ahead along the course and send the look-ahead sentence"));
    m_laOn->SetValue(s.laEnabled);
    s2->Add(m_laOn, 0, wxALL, 2 * B);
    wxString zones[] = {_("Corridor along the course"), _("Sector centred on the course")};
    m_laZone = new wxRadioBox(p2, wxID_ANY, _("Zone"), wxDefaultPosition, wxDefaultSize,
                              2, zones, 1, wxRA_SPECIFY_COLS);
    m_laZone->SetSelection(s.laZone);
    s2->Add(m_laZone, 0, wxEXPAND | wxALL, B);
    wxString bases[] = {_("Time ahead at the current speed over ground"),
                        _("Fixed distance")};
    m_laBasis = new wxRadioBox(p2, wxID_ANY, _("Length"), wxDefaultPosition,
                               wxDefaultSize, 2, bases, 1, wxRA_SPECIFY_COLS);
    m_laBasis->SetSelection(s.laBasis);
    s2->Add(m_laBasis, 0, wxEXPAND | wxALL, B);

    auto *lg = new wxFlexGridSizer(2, B, 2 * B);
    lg->AddGrowableCol(1);
    auto spin = [&](const wxString &lbl, double lo, double hi, double v, double inc,
                    int digits, const wxString &tip) {
      lg->Add(label(p2, lbl), 0, wxALIGN_CENTER_VERTICAL);
      auto *c = new wxSpinCtrlDouble(p2, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                     wxDefaultSize, wxSP_ARROW_KEYS, lo, hi, v, inc);
      c->SetDigits(digits);
      c->SetToolTip(tip);
      lg->Add(c, 0);
      return c;
    };
    m_laMinutes = spin(_("Time ahead (min)"), 1, 120, s.laMinutes, 1, 0,
                       _("Zone length = SOG x this time"));
    m_laMinLen = spin(_("Shortest zone (nm)"), 0.05, 50, s.laMinLengthNm, 0.05, 2,
                      _("Used at low speed, and as the least length on a time basis"));
    m_laDist = spin(_("Fixed distance (nm)"), 0.05, 50, s.laDistanceNm, 0.1, 2,
                    _("Zone length on a distance basis"));
    m_laMaxLen = spin(_("Longest zone (nm)"), 0.1, 50, s.laMaxLengthNm, 0.5, 1,
                      _("Upper limit for either basis"));
    m_laHalfW = spin(_("Corridor half-width (nm)"), 0.01, 5, s.laHalfWidthNm, 0.01, 2,
                     _("Each side of the course"));
    m_laHalfA = spin(_("Sector half-angle (deg)"), 1, 90, s.laHalfAngleDeg, 1, 0,
                     _("Each side of the course"));
    m_laSafety = spin(_("Safety depth (m)"), -5, 100, s.laSafetyDepthM, 0.1, 1,
                      _("Charted depths shallower than this are shoals. Draft + "
                        "under-keel margin + allowance for chart error."));
    m_laMinSog = spin(_("Minimum SOG (kn)"), 0.1, 5, s.laMinSogKn, 0.1, 1,
                      _("Below this the heading is used as the course and no "
                        "times are given"));
    lg->Add(label(p2, _("Sentence address")), 0, wxALIGN_CENTER_VERTICAL);
    m_laAddr = new wxTextCtrl(p2, wxID_ANY, s.laSentenceAddress);
    m_laAddr->SetMaxLength(5);
    m_laAddr->SetToolTip(_("Proprietary address: 'P' and four capital letters or digits"));
    lg->Add(m_laAddr, 0);
    s2->Add(lg, 0, wxEXPAND | wxALL, 2 * B);
    m_laUndep = new wxCheckBox(p2, wxID_ANY,
                               _("Rocks, wrecks and obstructions with no charted depth count as shoals"));
    m_laUndep->SetValue(s.laUndepthedAsShoal);
    s2->Add(m_laUndep, 0, wxALL, B);
    m_laShow = new wxCheckBox(p2, wxID_ANY, _("Draw the zone and the first shoal on the chart"));
    m_laShow->SetValue(s.laShowZone);
    s2->Add(m_laShow, 0, wxALL, B);
    s2->Add(label(p2, _("Sent on the same output as the least-depth sentence.")), 0,
            wxALL, B);
  }

  // ---- Output ------------------------------------------------------------
  auto *out = new wxStaticBoxSizer(wxVERTICAL, p3, _("NMEA output"));
  wxWindow *op = out->GetStaticBox();
  g = grid(out);
  g->Add(label(op, _("Send via")), 0, wxALIGN_CENTER_VERTICAL);
  m_route = new wxChoice(op, wxID_ANY);
  m_route->Append(_("Off"));
  m_route->Append(_("Serial port (opened by this plugin)"));
  m_route->Append(_("OpenCPN output connection"));
  m_route->SetSelection(s.outputRoute);
  g->Add(m_route, 1, wxEXPAND);

  g->Add(label(op, _("Serial port")), 0, wxALIGN_CENTER_VERTICAL);
  auto *portRow = new wxBoxSizer(wxHORIZONTAL);
  m_port = new wxComboBox(op, wxID_ANY, s.serialPort);
  for (const auto &p : SerialPort::listPorts()) m_port->Append(wxString::FromUTF8(p));
  m_port->SetValue(s.serialPort);
  portRow->Add(m_port, 1, wxRIGHT, B);
  auto *rescan = new wxButton(op, wxID_ANY, _("Rescan"), wxDefaultPosition,
                              wxDefaultSize, wxBU_EXACTFIT);
  rescan->Bind(wxEVT_BUTTON, &PrefsDialog::onRefreshPorts, this);
  portRow->Add(rescan, 0);
  g->Add(portRow, 1, wxEXPAND);

  g->Add(label(op, _("Baud rate")), 0, wxALIGN_CENTER_VERTICAL);
  m_baud = new wxChoice(op, wxID_ANY);
  int bsel = 0, i = 0;
  for (int b : SerialPort::standardBauds()) {
    m_baud->Append(wxString::Format(_T("%d"), b));
    if (b == s.baud) bsel = i;
    i++;
  }
  m_baud->SetSelection(bsel);
  g->Add(m_baud, 0);

  g->Add(label(op, _("OpenCPN connection")), 0, wxALIGN_CENTER_VERTICAL);
  m_conn = new wxChoice(op, wxID_ANY);
  wxArrayString conns = cm93depth_pi::listOpenCPNConnections();
  if (!s.ocpnConnection.IsEmpty() && conns.Index(s.ocpnConnection) == wxNOT_FOUND)
    conns.Add(s.ocpnConnection);
  m_conn->Append(conns);
  m_conn->SetStringSelection(s.ocpnConnection);
  if (m_conn->GetSelection() == wxNOT_FOUND && m_conn->GetCount()) m_conn->SetSelection(0);
  g->Add(m_conn, 1, wxEXPAND);

  g->Add(label(op, _("Sentence address")), 0, wxALIGN_CENTER_VERTICAL);
  m_addr = new wxTextCtrl(op, wxID_ANY, s.sentenceAddress);
  m_addr->SetMaxLength(5);
  m_addr->SetToolTip(_("Proprietary address: 'P' and four capital letters or digits"));
  g->Add(m_addr, 0);

  g->Add(label(op, _("Update interval (s)")), 0, wxALIGN_CENTER_VERTICAL);
  m_interval = new wxSpinCtrlDouble(op, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxDefaultSize, wxSP_ARROW_KEYS, 0.2, 10.0,
                                    s.intervalMs / 1000.0, 0.1);
  m_interval->SetDigits(1);
  g->Add(m_interval, 0);

  g->Add(label(op, _("Fix lost after (s)")), 0, wxALIGN_CENTER_VERTICAL);
  m_stale = new wxSpinCtrl(op, wxID_ANY, wxEmptyString, wxDefaultPosition,
                           wxDefaultSize, wxSP_ARROW_KEYS, 2, 120, s.staleFixSec);
  g->Add(m_stale, 0);

  m_sendInvalid = new wxCheckBox(op, wxID_ANY,
                                 _("Keep sending with status V when there is no valid data"));
  m_sendInvalid->SetValue(s.sendWhenInvalid);
  out->Add(m_sendInvalid, 0, wxALL, B);
  s3->Add(out, 0, wxEXPAND | wxALL, B);

  // ---- Display -----------------------------------------------------------
  auto *disp = new wxStaticBoxSizer(wxVERTICAL, p3, _("Chart"));
  wxWindow *dp = disp->GetStaticBox();
  auto *markRow = new wxBoxSizer(wxHORIZONTAL);
  m_mark = new wxCheckBox(dp, wxID_ANY, _("Mark the least-depth position, icon:"));
  m_mark->SetValue(s.markOnChart);
  m_icon = new wxTextCtrl(dp, wxID_ANY, s.markIcon);
  markRow->Add(m_mark, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, B);
  markRow->Add(m_icon, 1);
  disp->Add(markRow, 0, wxEXPAND | wxALL, B);
  s3->Add(disp, 0, wxEXPAND | wxALL, B);
  top->Add(nb, 1, wxEXPAND | wxALL, B);

  // ---- Alarm ---------------------------------------------------------------
  {
    m_alOn = new wxCheckBox(p4, wxID_ANY, _("Grounding alarm on this PC (sound and alarm window)"));
    m_alOn->SetValue(s.alarmEnabled);
    s4->Add(m_alOn, 0, wxALL, 2 * B);
    auto *ag = new wxFlexGridSizer(2, B, 2 * B);
    ag->AddGrowableCol(1);
    ag->Add(label(p4, _("Shoal ahead within (min)")), 0, wxALIGN_CENTER_VERTICAL);
    m_alMin = new wxSpinCtrlDouble(p4, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                   wxSP_ARROW_KEYS, 0.5, 60, s.alarmMinutes, 0.5);
    m_alMin->SetDigits(1);
    m_alMin->SetToolTip(_("At the current speed over ground. Keep it below the look-ahead time."));
    ag->Add(m_alMin, 0);
    ag->Add(label(p4, _("or within (nm)")), 0, wxALIGN_CENTER_VERTICAL);
    m_alDist = new wxSpinCtrlDouble(p4, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                    wxSP_ARROW_KEYS, 0.01, 5, s.alarmDistanceNm, 0.05);
    m_alDist->SetDigits(2);
    ag->Add(m_alDist, 0);
    ag->Add(label(p4, _("Repeat sound every (s)")), 0, wxALIGN_CENTER_VERTICAL);
    m_alRepeat = new wxSpinCtrl(p4, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, 1, 120, s.alarmRepeatSec);
    ag->Add(m_alRepeat, 0);
    ag->Add(label(p4, _("Silence mutes for (s)")), 0, wxALIGN_CENTER_VERTICAL);
    m_alSilence = new wxSpinCtrl(p4, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxSP_ARROW_KEYS, 5, 300, s.alarmSilenceSec);
    ag->Add(m_alSilence, 0);
    ag->Add(label(p4, _("Switched off: back on after (min, 0 = never)")), 0,
            wxALIGN_CENTER_VERTICAL);
    m_alRearm = new wxSpinCtrl(p4, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxSP_ARROW_KEYS, 0, 720, s.alarmOffRearmMin);
    m_alRearm->SetToolTip(_("When the watch officer switches the alarm off (Alarm button in the "
                            "depth window), it comes back on by itself after this long. It is "
                            "always on again when OpenCPN starts."));
    ag->Add(m_alRearm, 0);
    ag->Add(label(p4, _("Sound file (empty = OpenCPN beep)")), 0, wxALIGN_CENTER_VERTICAL);
    auto *sr = new wxBoxSizer(wxHORIZONTAL);
    m_alSound = new wxTextCtrl(p4, wxID_ANY, s.alarmSound);
    sr->Add(m_alSound, 1, wxRIGHT, B);
    auto *browse = new wxButton(p4, wxID_ANY, _("..."), wxDefaultPosition, wxDefaultSize,
                                wxBU_EXACTFIT);
    browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      wxFileDialog fd(this, _("Alarm sound"), wxEmptyString, wxEmptyString,
                      _("WAV files (*.wav)|*.wav|All files (*.*)|*.*"),
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST);
      if (fd.ShowModal() == wxID_OK) m_alSound->SetValue(fd.GetPath());
    });
    sr->Add(browse, 0, wxRIGHT, B);
    auto *test = new wxButton(p4, wxID_ANY, _("Test"), wxDefaultPosition, wxDefaultSize,
                              wxBU_EXACTFIT);
    test->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      if (!m_pi->playSound(m_alSound->GetValue().Trim().Trim(false)))
        wxMessageBox(_("Could not play that sound; the system bell was used."),
                     _("CM93 Least Depth"), wxOK | wxICON_WARNING, this);
    });
    sr->Add(test, 0);
    ag->Add(sr, 1, wxEXPAND);
    s4->Add(ag, 0, wxEXPAND | wxALL, 2 * B);
    m_alHere = new wxCheckBox(p4, wxID_ANY,
                              _("Also when the depth here is below the safety depth, or on land"));
    m_alHere->SetValue(s.alarmDepthHere);
    s4->Add(m_alHere, 0, wxALL, B);
    m_alAlr = new wxCheckBox(p4, wxID_ANY, _("Also send it as $IIALR on the NMEA output"));
    m_alAlr->SetValue(s.alarmSendAlr);
    s4->Add(m_alAlr, 0, wxALL, B);
    s4->Add(label(p4, _("The safety depth is set on the Look-ahead tab. The shoal-ahead\n"
                        "alarm needs the look-ahead on.")),
            0, wxALL, B);
    m_alOn->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { updateEnable(); });
  }

  // ---- Buttons -----------------------------------------------------------
  top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 2 * B);
  Bind(wxEVT_BUTTON, &PrefsDialog::onOK, this, wxID_OK);
  m_autoRoot->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { updateEnable(); });
  m_mode->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent &) { updateEnable(); });
  m_route->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { updateEnable(); });
  m_mark->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { updateEnable(); });
  m_laOn->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { updateEnable(); });
  m_laZone->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent &) { updateEnable(); });
  m_laBasis->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent &) { updateEnable(); });

  SetSizerAndFit(top);
  updateEnable();
  CentreOnParent();
}

void PrefsDialog::updateEnable() {
  m_root->Enable(!m_autoRoot->GetValue());
  int sel = m_mode->GetSelection();
  m_radius->Enable(sel == 2 || sel == 3);
  m_quad->Enable(sel == 3);
  m_landChk->Enable(sel == 3);
  // area / sounding / contour choices only apply to cell and radius search
  m_snd->Enable(sel == 1 || sel == 2);
  m_area->Enable(sel == 1 || sel == 2);
  m_cnt->Enable(sel == 1 || sel == 2);
  int r = m_route->GetSelection();
  m_port->Enable(r == 1);
  m_baud->Enable(r == 1);
  m_conn->Enable(r == 2);
  m_icon->Enable(m_mark->GetValue());
  const bool la = m_laOn->GetValue();
  const bool sector = m_laZone->GetSelection() == 1;
  const bool timeBasis = m_laBasis->GetSelection() == 0;
  for (wxWindow *w : std::initializer_list<wxWindow *>{
           m_laZone, m_laBasis, m_laMaxLen, m_laSafety, m_laMinSog, m_laAddr, m_laUndep,
           m_laShow})
    w->Enable(la);
  m_laMinutes->Enable(la && timeBasis);
  m_laMinLen->Enable(la && timeBasis);
  m_laDist->Enable(la && !timeBasis);
  m_laHalfW->Enable(la && !sector);
  m_laHalfA->Enable(la && sector);
  const bool al = m_alOn->GetValue();
  for (wxWindow *w : std::initializer_list<wxWindow *>{m_alMin, m_alDist, m_alRepeat, m_alSilence, m_alRearm,
                                                       m_alSound, m_alHere, m_alAlr})
    w->Enable(al);
}

void PrefsDialog::onRescanEnc(wxCommandEvent &) {
  m_pi->rescanEnc();
  m_encStatus->SetLabel(m_pi->encStatus());
  Layout();
}

void PrefsDialog::onRefreshPorts(wxCommandEvent &) {
  wxString cur = m_port->GetValue();
  m_port->Clear();
  for (const auto &p : SerialPort::listPorts()) m_port->Append(wxString::FromUTF8(p));
  m_port->SetValue(cur);
}

void PrefsDialog::onOK(wxCommandEvent &e) {
  wxString addr = m_addr->GetValue().Upper().Trim().Trim(false);
  if (!cm93::valid_proprietary_address(addr.ToStdString())) {
    wxMessageBox(_("The sentence address must be 'P' followed by four capital "
                   "letters or digits, for example PCMLD."),
                 _("CM93 Least Depth"), wxOK | wxICON_WARNING, this);
    return;
  }
  wxString laddr = m_laAddr->GetValue().Upper().Trim().Trim(false);
  if (!cm93::valid_proprietary_address(laddr.ToStdString()) || laddr == addr) {
    wxMessageBox(_("The look-ahead sentence address must be 'P' followed by four "
                   "capital letters or digits, different from the least-depth "
                   "address, for example PCMLA."),
                 _("CM93 Least Depth"), wxOK | wxICON_WARNING, this);
    return;
  }
  if (m_route->GetSelection() == 1 && m_port->GetValue().Trim().IsEmpty()) {
    wxMessageBox(_("Choose or type a serial port."), _("CM93 Least Depth"),
                 wxOK | wxICON_WARNING, this);
    return;
  }

  m_s.useCm93 = m_useCm93->GetValue();
  m_s.useEnc = m_useEnc->GetValue();
  m_s.encExtraDirs = m_encDirs->GetValue().Trim().Trim(false);
  m_s.cm93Root = m_autoRoot->GetValue() ? wxString() : m_root->GetPath();
  m_s.scaleIdx = m_scale->GetSelection() - 1;
  m_s.offsetE = m_offE->GetValue();
  m_s.offsetN = m_offN->GetValue();
  static const int kModeForRadio[] = {2, 0, 1, 3};
  m_s.mode = kModeForRadio[m_mode->GetSelection()];
  m_s.radiusNm = m_radius->GetValue();
  m_s.useSoundings = m_snd->GetValue();
  m_s.useAreas = m_area->GetValue();
  m_s.useHazards = m_haz->GetValue();
  m_s.useContours = m_cnt->GetValue();
  m_s.interpQuadrants = m_quad->GetValue();
  m_s.interpLandCheck = m_landChk->GetValue();
  m_s.undepthedDangerRule = m_undepRule->GetSelection();
  m_s.coarseDangers = m_coarse->GetValue();
  m_s.coarseCheckNm = m_coarseCheck->GetValue();
  m_s.outputRoute = m_route->GetSelection();
  m_s.serialPort = m_port->GetValue().Trim().Trim(false);
  long b = 4800;
  m_baud->GetStringSelection().ToLong(&b);
  m_s.baud = (int)b;
  m_s.ocpnConnection = m_conn->GetStringSelection();
  m_s.sentenceAddress = addr;
  m_s.intervalMs = (int)(m_interval->GetValue() * 1000.0 + 0.5);
  m_s.staleFixSec = m_stale->GetValue();
  m_s.sendWhenInvalid = m_sendInvalid->GetValue();
  m_s.laEnabled = m_laOn->GetValue();
  m_s.laZone = m_laZone->GetSelection();
  m_s.laBasis = m_laBasis->GetSelection();
  m_s.laMinutes = m_laMinutes->GetValue();
  m_s.laDistanceNm = m_laDist->GetValue();
  m_s.laMaxLengthNm = m_laMaxLen->GetValue();
  m_s.laMinLengthNm = std::min(m_laMinLen->GetValue(), m_s.laMaxLengthNm);
  m_s.laHalfWidthNm = m_laHalfW->GetValue();
  m_s.laHalfAngleDeg = m_laHalfA->GetValue();
  m_s.laSafetyDepthM = m_laSafety->GetValue();
  m_s.laMinSogKn = m_laMinSog->GetValue();
  m_s.laUndepthedAsShoal = m_laUndep->GetValue();
  m_s.laShowZone = m_laShow->GetValue();
  m_s.laSentenceAddress = laddr;
  m_s.alarmEnabled = m_alOn->GetValue();
  m_s.alarmMinutes = m_alMin->GetValue();
  m_s.alarmDistanceNm = m_alDist->GetValue();
  m_s.alarmRepeatSec = m_alRepeat->GetValue();
  m_s.alarmSilenceSec = m_alSilence->GetValue();
  m_s.alarmSound = m_alSound->GetValue().Trim().Trim(false);
  m_s.alarmDepthHere = m_alHere->GetValue();
  m_s.alarmSendAlr = m_alAlr->GetValue();
  m_s.alarmOffRearmMin = m_alRearm->GetValue();
  m_s.markOnChart = m_mark->GetValue();
  m_s.markIcon = m_icon->GetValue().Trim().Trim(false);
  if (m_s.markIcon.IsEmpty()) m_s.markIcon = _T("triangle");
  e.Skip();  // let the dialog close with wxID_OK
}
