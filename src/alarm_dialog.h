/***************************************************************************
 * cm93depth_pi - grounding alarm window
 *
 * A small always-on-top window: flashing red while the alarm is
 * unacknowledged, steady amber once acknowledged. Buttons: Silence (mutes
 * the sound for the silence period) and Acknowledge. It closes by itself
 * when the alarm condition clears.
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

class cm93depth_pi;

class AlarmDialog : public wxDialog {
public:
  AlarmDialog(wxWindow *parent, cm93depth_pi *plugin);
  ~AlarmDialog() override;
  // text: the alarm line; acknowledged / silencedSec: current state
  void show(const wxString &text, bool acknowledged, int silencedSec);
  // A question box of this window is open: it must not be deleted now
  bool busy() const { return m_busy; }

private:
  void onFlash(wxTimerEvent &e);
  void onSilence(wxCommandEvent &e);
  void onAck(wxCommandEvent &e);
  void onOff(wxCommandEvent &e);
  void onClose(wxCloseEvent &e);
  void paint(bool on);

  cm93depth_pi *m_pi;
  wxPanel *m_body;
  wxStaticText *m_title;
  wxStaticText *m_text;
  wxStaticText *m_state;
  wxButton *m_silence;
  wxButton *m_ack;
  wxButton *m_off;
  wxTimer m_flash;
  bool m_flashOn = false;
  bool m_acked = false;
  bool m_busy = false;
};
