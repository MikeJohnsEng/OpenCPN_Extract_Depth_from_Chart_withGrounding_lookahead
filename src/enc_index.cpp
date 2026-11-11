/***************************************************************************
 * cm93depth_pi - CM93 / S-57 least-depth plugin for OpenCPN
 *
 * enc_index.cpp : see enc_index.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "enc_index.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "s57_reader.h"

namespace cm93 {

namespace fs = std::filesystem;
static const char *kCacheHeader = "cm93depth_pi ENC index 1";

static bool is_base_cell(const fs::path &p) {
  std::string e = p.extension().u8string();
  return e == ".000";
}

static std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

// Path key that ignores case and "a/./b" differences
static std::string path_key(const fs::path &p) {
  return lower(p.lexically_normal().generic_u8string());
}

void EncIndex::readCatalog(const std::string &path) {
  std::vector<uint8_t> data;
  if (!read_file(path, data)) return;
  iso8211::Module m;
  if (!m.open(data)) return;
  const fs::path dir = fs::u8path(path).parent_path();
  iso8211::Record r;
  while (m.next(r)) {
    const iso8211::Field *f = r.find("CATD");
    if (!f) continue;
    std::string file = f->getStr("FILE");
    std::replace(file.begin(), file.end(), '\\', '/');
    fs::path fp = fs::u8path(file);
    if (lower(fp.extension().u8string()) != ".000") continue;
    const iso8211::Value *s = f->get("SLAT"), *w = f->get("WLON"), *n = f->get("NLAT"),
                         *e = f->get("ELON");
    if (!s || !w || !n || !e || !s->isNum || !w->isNum || !n->isNum || !e->isNum) continue;
    std::string stem = fp.stem().u8string();
    if (stem.size() < 3 || stem[2] < '1' || stem[2] > '6') continue;
    EncEntry en;
    en.dsnm = fp.filename().u8string();
    en.intu = en.band = stem[2] - '0';
    en.minLat = s->num, en.maxLat = n->num, en.minLon = w->num, en.maxLon = e->num;
    if (en.minLat > en.maxLat || en.minLon > en.maxLon) continue;  // across 180: read the cell
    m_catalog[path_key(dir / fp)] = en;
  }
}

void EncIndex::configure(const std::vector<std::string> &dirs, const std::string &cachePath) {
  m_dirs = dirs;
  m_cachePath = cachePath;
  m_walk.clear();
  for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) m_walk.push_back(*it);
  m_files.clear();
  m_next = 0;
  m_building.clear();
  m_unusable = 0;
  m_firstError.clear();
  m_walking = true;
  m_complete = dirs.empty();
  m_progressive = m_entries.empty();
  m_catalog.clear();
  loadCache();
  if (m_complete) publish();
}

void EncIndex::loadCache() {
  m_cache.clear();
  if (m_cachePath.empty()) return;
  std::ifstream in(fs::u8path(m_cachePath));
  std::string line;
  if (!std::getline(in, line) || line != kCacheHeader) return;
  while (std::getline(in, line)) {
    std::vector<std::string> t;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, '\t')) t.push_back(tok);
    if (t.size() < 11) continue;
    EncEntry e;
    e.path = t[0];
    e.size = std::strtoull(t[1].c_str(), nullptr, 10);
    e.mtime = std::strtoll(t[2].c_str(), nullptr, 10);
    e.dsnm = t[3];
    e.intu = std::atoi(t[4].c_str());
    e.cscl = std::atoi(t[5].c_str());
    e.band = std::atoi(t[6].c_str());
    e.minLat = std::atof(t[7].c_str());
    e.maxLat = std::atof(t[8].c_str());
    e.minLon = std::atof(t[9].c_str());
    e.maxLon = std::atof(t[10].c_str());
    if (t.size() > 11) e.error = t[11];
    m_cache[e.path] = e;
  }
}

void EncIndex::saveCache() const {
  if (m_cachePath.empty()) return;
  std::error_code ec;
  fs::create_directories(fs::u8path(m_cachePath).parent_path(), ec);
  std::ofstream out(fs::u8path(m_cachePath), std::ios::trunc);
  if (!out) return;
  out << kCacheHeader << "\n";
  char buf[256];
  for (const EncEntry &e : m_building) {
    std::string err = e.error;
    std::replace(err.begin(), err.end(), '\t', ' ');
    std::replace(err.begin(), err.end(), '\n', ' ');
    std::snprintf(buf, sizeof buf, "\t%llu\t%lld\t", (unsigned long long)e.size,
                  (long long)e.mtime);
    out << e.path << buf << e.dsnm;
    std::snprintf(buf, sizeof buf, "\t%d\t%d\t%d\t%.8f\t%.8f\t%.8f\t%.8f\t", e.intu, e.cscl,
                  e.band, e.minLat, e.maxLat, e.minLon, e.maxLon);
    out << buf << err << "\n";
  }
}

void EncIndex::publish() {
  std::vector<EncEntry> use;
  for (const EncEntry &e : m_building)
    if (e.band >= 1 && e.band <= 6) use.push_back(e);
  if (use.size() == m_entries.size()) {
    bool same = true;
    for (size_t i = 0; i < use.size() && same; i++) same = use[i].path == m_entries[i].path;
    if (same) {
      m_entries.swap(use);  // details may have changed; the set has not
      return;
    }
  }
  m_entries.swap(use);
  m_generation++;
}

bool EncIndex::step(double budgetMs) {
  if (m_complete) return true;
  using clk = std::chrono::steady_clock;
  const auto t0 = clk::now();
  auto spent = [&]() {
    return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
  };

  // 1. Walk the folders
  while (m_walking) {
    if (m_walk.empty()) {
      m_walking = false;
      std::sort(m_files.begin(), m_files.end());
      m_files.erase(std::unique(m_files.begin(), m_files.end()), m_files.end());
      break;
    }
    std::string dir = m_walk.back();
    m_walk.pop_back();
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(dir), fs::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
      std::error_code ec2;
      if (it->is_symlink(ec2) && it->is_directory(ec2)) continue;  // no loops
      if (it->is_directory(ec2)) {
        m_walk.push_back(it->path().u8string());
      } else if (is_base_cell(it->path()) && it->is_regular_file(ec2)) {
        m_files.push_back(it->path().u8string());
      } else if (lower(it->path().filename().u8string()) == "catalog.031") {
        readCatalog(it->path().u8string());
      }
    }
    if (spent() > budgetMs) return false;
  }

  // 2. Read each new or changed cell's header and extent
  bool added = false;
  while (m_next < m_files.size()) {
    const std::string &path = m_files[m_next++];
    std::error_code ec;
    fs::path p = fs::u8path(path);
    EncEntry e;
    e.path = path;
    e.size = (uint64_t)fs::file_size(p, ec);
    e.mtime = (int64_t)fs::last_write_time(p, ec).time_since_epoch().count();
    auto c = m_cache.find(path);
    auto cat = m_catalog.find(path_key(p));
    if (c != m_cache.end() && c->second.size == e.size && c->second.mtime == e.mtime) {
      e = c->second;
    } else if (cat != m_catalog.end()) {
      const uint64_t size = e.size;
      const int64_t mtime = e.mtime;
      e = cat->second;
      e.path = path;
      e.size = size;
      e.mtime = mtime;
    } else {
      EncHeader h;
      std::string err;
      if (!read_enc_header(path, h, &err)) {
        e.error = err.find("ISO 8211") != std::string::npos
                      ? "not readable: encrypted (S-63) or not an S-57 cell"
                      : err;
      } else if (h.minLat > h.maxLat) {
        e.error = "no coordinates";
      } else {
        e.dsnm = h.dsnm;
        e.intu = h.intu;
        e.cscl = h.cscl;
        e.band = enc_band(h.intu, h.cscl);
        e.minLat = h.minLat, e.maxLat = h.maxLat;
        e.minLon = h.minLon, e.maxLon = h.maxLon;
      }
    }
    if (e.band < 1) {
      m_unusable++;
      if (m_firstError.empty()) m_firstError = path + ": " + e.error;
    }
    m_building.push_back(e);
    added = true;
    if (spent() > budgetMs) break;
  }
  if (m_next >= m_files.size()) {
    m_complete = true;
    publish();
    saveCache();
    return true;
  }
  if (m_progressive && added) publish();
  return false;
}

std::string EncIndex::summary() const {
  char buf[160];
  if (!m_complete)
    std::snprintf(buf, sizeof buf, "S-57: indexing %d of %d cells", filesDone(), filesFound());
  else if (m_files.empty())
    std::snprintf(buf, sizeof buf, "S-57: no ENC cells (*.000) in the chart folders");
  else
    std::snprintf(buf, sizeof buf, "S-57: %d ENC cells%s", (int)m_entries.size(),
                  m_unusable ? (", " + std::to_string(m_unusable) + " unreadable").c_str() : "");
  return buf;
}

}  // namespace cm93
