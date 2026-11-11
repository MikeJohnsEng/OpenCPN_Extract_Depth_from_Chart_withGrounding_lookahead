/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * serial_port.h : minimal write-only serial port (POSIX termios / Win32).
 * 8 data bits, no parity, 1 stop bit, no flow control (NMEA 0183 framing).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#pragma once

#include <string>
#include <vector>

class SerialPort {
public:
  SerialPort() = default;
  ~SerialPort() { close(); }
  SerialPort(const SerialPort &) = delete;
  SerialPort &operator=(const SerialPort &) = delete;

  // e.g. "/dev/ttyUSB0", "/dev/cu.usbserial-1410", "COM5"
  bool open(const std::string &port, int baud, std::string *err = nullptr);
  void close();
  bool isOpen() const;
  const std::string &portName() const { return m_port; }

  // Non-blocking; returns false (with error) if the write failed.
  // A full transmit buffer drops the sentence rather than stalling OpenCPN.
  bool write(const std::string &data, std::string *err = nullptr);

  // Best-effort list of candidate ports for the preferences dialog.
  static std::vector<std::string> listPorts();
  static const std::vector<int> &standardBauds();

private:
  std::string m_port;
#ifdef _WIN32
  void *m_handle = nullptr;  // HANDLE
#else
  int m_fd = -1;
#endif
};
