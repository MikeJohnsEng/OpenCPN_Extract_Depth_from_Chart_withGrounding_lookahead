/***************************************************************************
 * cm93depth_pi - route check
 *
 * Checks a planned route against the charts before it is followed: every
 * leg is searched as a corridor (the look-ahead engine, leg by leg in short
 * pieces) and a circle round every waypoint, for charted depths shallower
 * than the safety depth, land, dangers with no charted depth and gaps in
 * the depth areas, and notes legs that only small-scale charts cover.
 * No wxWidgets dependency. The work is done in small steps so the plugin
 * can spread it over a timer.
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

#include <string>
#include <vector>

#include "depth_engine.h"

namespace cm93 {

struct RouteWaypoint {
  double lat = 0, lon = 0;
  std::string name;
};

struct RouteCheckConfig {
  double halfWidthNm = 0.1;    // corridor each side of every leg
  double safetyDepthM = 5.0;   // charted depth + tide below this = shoal
  double tideM = 0.0;          // height of tide added to charted depths
  double waypointRadiusNm = 0.2;  // circle checked round every waypoint
  bool undepthedFails = true;  // undepthed dangers and gaps fail the route
  // Least detailed band (slot index, see kSlots) still taken as properly
  // charted; a leg relying on a less detailed band gets a warning.
  int poorBandSlot = 5;        // CM93 C (1:200,000)
  double chunkNm = 5.0;        // leg pieces searched at a time
};

enum class CheckStatus : int { Pass = 0, Warn = 1, Fail = 2 };

enum class IssueKind : int {
  Shoal = 0,      // charted depth (+ tide) below the safety depth
  Land = 1,       // charted land
  Undepthed = 2,  // rock / wreck / obstruction with no depth, or a gap
  PoorChart = 3,  // only charts less detailed than the limit
  NoChart = 4,    // no chart at all
};

struct RouteIssue {
  IssueKind kind = IssueKind::Shoal;
  int leg = -1;          // leg index, -1 for a waypoint circle
  int waypoint = -1;     // waypoint index for a waypoint circle
  double alongNm = 0;    // from the start of the leg
  LatLon pos{0, 0};
  double depth = 0;      // charted, m (shoals)
  std::string cls;       // S-57 class, "LNDARE", "HOLE"
  char band = ' ';       // chart band it comes from
  std::string text;      // one line for the report
};

struct LegCheck {
  int index = 0;           // leg i runs from waypoint i to i+1
  std::string from, to;
  double lengthNm = 0;
  double courseDeg = 0;    // initial rhumb-line course, true
  bool haveLeast = false;  // least charted depth in the corridor
  double leastDepth = 0;
  LatLon leastPos{0, 0};
  std::string leastCls;
  char coarsestBand = ' '; // least detailed band used along the leg
  std::string bands;       // bands used, e.g. "G E C"
  CheckStatus status = CheckStatus::Pass;
  std::vector<RouteIssue> issues;
};

struct WaypointCheck {
  int index = 0;
  std::string name;
  LatLon pos{0, 0};
  bool haveDepth = false;  // depth area minimum at the waypoint
  double depth = 0;
  std::string depthCls;
  bool onLand = false;
  CheckStatus status = CheckStatus::Pass;
  std::vector<RouteIssue> issues;  // within the waypoint circle
};

struct RouteCheckResult {
  bool valid = false;
  std::string status;
  std::string routeName;
  double totalNm = 0;
  std::vector<LegCheck> legs;
  std::vector<WaypointCheck> waypoints;
  CheckStatus overall = CheckStatus::Pass;
  int fails = 0, warnings = 0;
  RouteCheckConfig config;
};

class RouteChecker {
public:
  // Begin checking a route (at least two waypoints).
  void start(const std::vector<RouteWaypoint> &route, const std::string &name,
             const RouteCheckConfig &rc, const EngineConfig &ec);
  // Up to budgetMs of work. True when the check is complete.
  bool step(DepthEngine &engine, double budgetMs);
  void runToCompletion(DepthEngine &engine) {
    while (!step(engine, 1000.0)) {
    }
  }
  void cancel() { m_tasks.clear(); m_next = 0; m_running = false; }
  bool running() const { return m_running; }
  double progress() const {
    return m_tasks.empty() ? 1.0 : (double)m_next / (double)m_tasks.size();
  }
  const RouteCheckResult &result() const { return m_res; }

private:
  struct Task {
    bool waypoint = false;
    int index = 0;       // leg or waypoint
    LatLon a{0, 0};      // piece start (legs)
    double courseDeg = 0, lengthNm = 0, offsetNm = 0;
  };
  void runTask(DepthEngine &engine, const Task &t);
  void finish();

  std::vector<Task> m_tasks;
  size_t m_next = 0;
  bool m_running = false;
  RouteCheckConfig m_rc;
  EngineConfig m_ec;
  RouteCheckResult m_res;
};

// Rhumb-line course (true, degrees) and distance (nm) from a to b
void rhumb_course_distance(const LatLon &a, const LatLon &b, double &courseDeg,
                           double &distNm);
// Point at distNm along the rhumb line from a on courseDeg
LatLon rhumb_destination(const LatLon &a, double courseDeg, double distNm);

// Plain-text report of a completed check
std::string route_check_report(const RouteCheckResult &r);
const char *check_status_name(CheckStatus s);

}  // namespace cm93
