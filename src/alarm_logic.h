/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * alarm_logic.h : grounding alarm state machine (no wxWidgets).
 *
 * States, after the bridge alert pattern (IMO BAM):
 *   Clear         no alarm condition
 *   Active        condition present, not acknowledged: sounds on entry and
 *                 then every repeat interval; window flashes
 *   Silenced      Active with the sound muted for the silence period; it
 *                 sounds again when the period ends if still not acknowledged
 *   Acknowledged  the operator has accepted it: no sound, window stays
 *                 (steady) while the condition lasts
 * The alarm clears by itself when the condition goes. An acknowledged alarm
 * becomes Active again if it gets worse: a more serious kind (depth here
 * below the safety depth after a shoal ahead), or the time to the shoal
 * falls to half what it was when acknowledged.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "depth_engine.h"

namespace cm93 {

enum class AlarmKind : int {
  None = 0,
  ShoalAhead = 1,  // look-ahead: first shoal within the alarm time / distance
  DepthHere = 2,   // depth at the vessel below the safety depth (or land)
};

class AlarmLogic {
public:
  enum class State { Clear, Active, Silenced, Acknowledged };

  // Call every update. severity: time to the shoal, minutes (lower is
  // worse; NaN when unknown). Returns true when the sound should play now.
  bool update(int64_t nowMs, AlarmKind kind, double severity, int64_t repeatMs) {
    if (kind == AlarmKind::None) {
      m_state = State::Clear;
      m_kind = AlarmKind::None;
      return false;
    }
    bool sound = false;
    switch (m_state) {
      case State::Clear:
        m_state = State::Active;
        sound = true;
        break;
      case State::Active:
        sound = nowMs - m_lastSoundMs >= repeatMs;
        break;
      case State::Silenced:
        if (nowMs >= m_silenceUntilMs) {
          m_state = State::Active;
          sound = true;
        }
        break;
      case State::Acknowledged: {
        const bool worseKind = (int)kind > (int)m_ackKind;
        const bool closer = std::isfinite(severity) && std::isfinite(m_ackSeverity) &&
                            severity <= 0.5 * m_ackSeverity;
        if (worseKind || closer) {
          m_state = State::Active;
          sound = true;
        }
        break;
      }
    }
    m_kind = kind;
    m_severity = severity;
    if (sound) m_lastSoundMs = nowMs;
    return sound;
  }

  // Mute for silenceMs; still Active (unacknowledged) underneath.
  void silence(int64_t nowMs, int64_t silenceMs) {
    if (m_state != State::Active && m_state != State::Silenced) return;
    m_state = State::Silenced;
    m_silenceUntilMs = nowMs + silenceMs;
  }

  void acknowledge() {
    if (m_state == State::Clear) return;
    m_state = State::Acknowledged;
    m_ackKind = m_kind;
    m_ackSeverity = m_severity;
  }

  State state() const { return m_state; }
  AlarmKind kind() const { return m_kind; }
  bool raised() const { return m_state != State::Clear; }
  bool acknowledged() const { return m_state == State::Acknowledged; }
  int64_t silenceUntilMs() const { return m_silenceUntilMs; }

private:
  State m_state = State::Clear;
  AlarmKind m_kind = AlarmKind::None;
  double m_severity = NAN;
  AlarmKind m_ackKind = AlarmKind::None;
  double m_ackSeverity = NAN;
  int64_t m_lastSoundMs = 0;
  int64_t m_silenceUntilMs = 0;
};

struct AlarmConfig {
  double minutes = 6.0;        // shoal ahead within this time at current SOG
  double distanceNm = 0.2;     // ... or within this distance (any speed)
  bool depthHere = true;       // depth at the vessel below the safety depth
  double safetyDepthM = 5.0;   // the look-ahead safety depth
  bool depthIsHere = true;     // search mode gives the depth AT the vessel
                               // (depth area here / interpolated), not the
                               // least depth around it (cell / radius)
};

// Which alarm, if any, the latest results call for. severity = minutes to
// the shoal (NaN if unknown); text = one line for the operator.
inline AlarmKind evaluate_alarm(const DepthResult &here, const LookAheadResult &ahead,
                                const AlarmConfig &c, bool fixValid, double &severity,
                                std::string &text) {
  severity = NAN;
  text.clear();
  if (!fixValid) return AlarmKind::None;
  char b[160];
  if (c.depthHere && here.valid) {
    if (here.onLand) {
      text = "ON CHARTED LAND";
      severity = 0;
      return AlarmKind::DepthHere;
    }
    if (c.depthIsHere && here.haveLeast && here.leastDepth < c.safetyDepthM) {
      std::snprintf(b, sizeof b, "DEPTH HERE %.1f m, BELOW SAFETY DEPTH %.1f m",
                    here.leastDepth, c.safetyDepthM);
      text = b;
      severity = 0;
      return AlarmKind::DepthHere;
    }
  }
  if (ahead.valid && ahead.haveShoal) {
    const LookHit &h = ahead.shoal;
    const bool soon = std::isfinite(h.timeMin) && h.timeMin <= c.minutes;
    const bool close = h.aheadNm <= c.distanceNm;  // ("near" is a Windows macro)
    if (soon || close) {
      const char *what = h.land ? "LAND" : h.undepthed ? "DANGER, NO CHARTED DEPTH"
                                                       : "SHOAL";
      if (std::isfinite(h.timeMin))
        std::snprintf(b, sizeof b, "%s AHEAD: %.2f nm, %.1f min, %03.0f T", what,
                      h.aheadNm, h.timeMin, h.bearingDeg);
      else
        std::snprintf(b, sizeof b, "%s AHEAD: %.2f nm, %03.0f T", what, h.aheadNm,
                      h.bearingDeg);
      text = b;
      severity = h.timeMin;
      return AlarmKind::ShoalAhead;
    }
  }
  return AlarmKind::None;
}

}  // namespace cm93
