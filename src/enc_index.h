/***************************************************************************
 * cm93depth_pi - CM93 / S-57 least-depth plugin for OpenCPN
 *
 * enc_index.h : finds the S-57 ENC base cells (*.000) in a set of chart
 * folders and records each one's usage band, compilation scale and extent.
 *
 * The work is done in small steps (step()) so the plugin can spread it over
 * its timer without holding up OpenCPN, and the result is kept in a cache
 * file so later starts only re-read cells that have changed.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cm93 {

struct EncEntry {
  std::string path;   // base cell file (.000)
  uint64_t size = 0;  // file size and time: the cache key
  int64_t mtime = 0;
  std::string dsnm;
  int intu = 0;
  int cscl = 0;
  int band = 0;  // 1..6, 0 = not usable (unreadable, encrypted, empty)
  double minLat = 0, maxLat = 0, minLon = 0, maxLon = 0;
  std::string error;  // why not usable
};

class EncIndex {
public:
  // Folders to search, recursively, and the cache file ("" = no cache).
  // Starts a new scan; the entries of the previous scan stay in use until
  // it is complete.
  void configure(const std::vector<std::string> &dirs, const std::string &cachePath);
  // Start the same scan again (charts added or updated).
  void rescan() { configure(m_dirs, m_cachePath); }
  // Up to `budgetMs` of work. True when the scan is complete.
  bool step(double budgetMs);
  void runToCompletion() {
    while (!step(1000.0)) {
    }
  }

  bool complete() const { return m_complete; }
  const std::vector<std::string> &dirs() const { return m_dirs; }
  // Usable cells (band 1..6)
  const std::vector<EncEntry> &entries() const { return m_entries; }
  // Changes whenever entries() changes
  unsigned generation() const { return m_generation; }

  // Progress / summary
  int filesFound() const { return (int)m_files.size(); }
  int filesDone() const { return (int)m_next; }
  int unusable() const { return m_unusable; }
  const std::string &firstError() const { return m_firstError; }
  std::string summary() const;

private:
  void publish();
  void loadCache();
  void saveCache() const;
  // An exchange set's CATALOG.031 lists each cell's extent: with it, and the
  // usage band in the cell's name (3rd character), a cell is indexed
  // without reading it
  void readCatalog(const std::string &path);

  std::vector<std::string> m_dirs;
  std::string m_cachePath;
  std::vector<std::string> m_walk;   // folders still to list
  std::vector<std::string> m_files;  // .000 files found
  size_t m_next = 0;                 // next file to index
  bool m_walking = false;
  bool m_complete = true;
  bool m_progressive = false;  // nothing published yet: publish as we go
  std::vector<EncEntry> m_building;  // every file, usable or not
  std::vector<EncEntry> m_entries;
  std::map<std::string, EncEntry> m_cache;
  std::map<std::string, EncEntry> m_catalog;  // by lower-case path
  unsigned m_generation = 0;
  int m_unusable = 0;
  std::string m_firstError;
};

}  // namespace cm93
