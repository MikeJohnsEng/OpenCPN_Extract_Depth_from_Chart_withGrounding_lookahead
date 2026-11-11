/***************************************************************************
 * cm93depth_pi - grounding alarm window
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "alarm_dialog.h"

#include "cm93depth_pi.h"

AlarmDialog::AlarmDialog(wxWindow *parent, cm93depth_pi *plugin)
    : wxDialog(parent, wxID_ANY, _("Grounding alarm"), wxDefaultPosition, wxDefaultSize,
               wxCAPTION | wxSTAY_ON_TOP | wxFRAME_FLOAT_ON_PARENT),
      m_pi(plugin),
      m_flash(this) {
  auto *outer = new wxBoxSizer(wxVERTICAL);
  m_body = new wxPanel(this);
  auto *top = new wxBoxSizer(wxVERTICAL);

  m_title = new wxStaticText(m_body, wxID_ANY, _("GROUNDING ALARM"), wxDefaultPosition,
                             wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
  wxFont big = m_title->GetFont();
  big.SetPointSize(big.GetPointSize() * 2);
  big.SetWeight(wxFONTWEIGHT_BOLD);
  m_title->SetFont(big);
  top->Add(m_title, 0, wxEXPAND | wxALL, 10);

  m_text = new wxStaticText(m_body, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
  wxFont mid = m_text->GetFont();
  mid.SetPointSize(mid.GetPointSize() + 3);
  mid.SetWeight(wxFONTWEIGHT_BOLD);
  m_text->SetFont(mid);
  top->Add(m_text, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

  m_state = new wxStaticText(m_body, wxID_ANY, wxEmptyString, wxDefaultPosition,
                             wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
  top->Add(m_state, 0, wxEXPAND | wxALL, 6);

  auto *btns = new wxBoxSizer(wxHORIZONTAL);
  m_silence = new wxButton(m_body, wxID_ANY, _("Silence"));
  m_ack = new wxButton(m_body, wxID_ANY, _("Acknowledge"));
  m_silence->SetMinSize(wxSize(GetCharWidth() * 16, GetCharHeight() * 3));
  m_ack->SetMinSize(wxSize(GetCharWidth() * 16, GetCharHeight() * 3));
  btns->Add(m_silence, 1, wxRIGHT, 10);
  btns->Add(m_ack, 1);
  top->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
  m_off = new wxButton(m_body, wxID_ANY, _("Switch alarm off..."));
  m_off->SetToolTip(_("Switch the grounding alarm off completely, e.g. in navigable water "
                      "shallower than the safety depth. Switch it on again in the CM93 "
                      "depth window."));
  top->Add(m_off, 0, wxALIGN_RIGHT | wxALL, 10);

  m_body->SetSizer(top);
  outer->Add(m_body, 1, wxEXPAND);
  SetSizer(outer);

  m_silence->Bind(wxEVT_BUTTON, &AlarmDialog::onSilence, this);
  m_ack->Bind(wxEVT_BUTTON, &AlarmDialog::onAck, this);
  m_off->Bind(wxEVT_BUTTON, &AlarmDialog::onOff, this);
  Bind(wxEVT_TIMER, &AlarmDialog::onFlash, this, m_flash.GetId());
  Bind(wxEVT_CLOSE_WINDOW, &AlarmDialog::onClose, this);
  SetMinSize(wxSize(GetCharWidth() * 48, -1));
  Fit();
  CentreOnParent();
}

AlarmDialog::~AlarmDialog() { m_flash.Stop(); }

void AlarmDialog::show(const wxString &text, bool acknowledged, int silencedSec) {
  m_text->SetLabel(text);
  m_acked = acknowledged;
  if (acknowledged) {
    m_state->SetLabel(_("Acknowledged - clears when the condition clears"));
    m_flash.Stop();
    paint(false);
  } else {
    m_state->SetLabel(silencedSec > 0
                          ? wxString::Format(_("Silenced for %d s - acknowledge to stop"),
                                             silencedSec)
                          : wxString(_("Sounding - Silence mutes it, Acknowledge accepts it")));
    if (!m_flash.IsRunning()) m_flash.Start(500);
  }
  m_silence->Enable(!acknowledged);
  m_ack->Enable(!acknowledged);
  m_text->Wrap(GetCharWidth() * 46);
  m_body->Layout();
  Fit();
  if (!IsShown()) Show();
}

void AlarmDialog::onFlash(wxTimerEvent &) {
  m_flashOn = !m_flashOn;
  paint(m_flashOn);
}

void AlarmDialog::paint(bool on) {
  wxColour bg, fg;
  if (m_acked) {
    bg = wxColour(230, 160, 0);  // amber: acknowledged
    fg = *wxBLACK;
  } else if (on) {
    bg = wxColour(210, 0, 0);    // red
    fg = *wxWHITE;
  } else {
    bg = wxColour(60, 0, 0);
    fg = wxColour(255, 200, 200);
  }
  m_body->SetBackgroundColour(bg);
  for (wxStaticText *t : {m_title, m_text, m_state}) {
    t->SetBackgroundColour(bg);
    t->SetForegroundColour(fg);
  }
  m_body->Refresh();
}

void AlarmDialog::onSilence(wxCommandEvent &) { m_pi->alarmSilence(); }
void AlarmDialog::onAck(wxCommandEvent &) { m_pi->alarmAcknowledge(); }

void AlarmDialog::onOff(wxCommandEvent &) {
  m_busy = true;
  int r = wxMessageBox(_("Switch the grounding alarm OFF?\n\nThere will be no shoal or "
                         "depth alarm until it is switched on again with the alarm button "
                         "in the CM93 depth window (or OpenCPN is restarted)."),
                       _("Grounding alarm"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
  m_busy = false;
  if (r != wxYES) return;
  m_pi->setAlarmOff(true);
  m_flash.Stop();
  Hide();  // deleted by the plugin's next update, not inside this handler
}
// The title-bar close is not offered; Alt+F4 acts as Acknowledge.
void AlarmDialog::onClose(wxCloseEvent &e) {
  if (e.CanVeto()) e.Veto();
  m_pi->alarmAcknowledge();
}
