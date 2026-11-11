/***************************************************************************
 * cm93depth_pi - CM93 least-depth plugin for OpenCPN
 *
 * serial_port.cpp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "serial_port.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

const std::vector<int> &SerialPort::standardBauds() {
  static const std::vector<int> b = {4800,  9600,   19200,  38400,
                                     57600, 115200, 230400};
  return b;
}

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------
static std::string last_error_text() {
  DWORD e = GetLastError();
  char buf[256] = {0};
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, e, 0, buf, sizeof buf - 1, nullptr);
  std::string s(buf);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s.empty() ? ("error " + std::to_string(e)) : s;
}

bool SerialPort::open(const std::string &port, int baud, std::string *err) {
  close();
  std::string path = port;
  if (path.rfind("\\\\.\\", 0) != 0) path = "\\\\.\\" + port;  // COM10+
  HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    if (err) *err = port + ": " + last_error_text();
    return false;
  }
  DCB dcb;
  std::memset(&dcb, 0, sizeof dcb);
  dcb.DCBlength = sizeof dcb;
  GetCommState(h, &dcb);
  dcb.BaudRate = (DWORD)baud;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  if (!SetCommState(h, &dcb)) {
    if (err) *err = port + ": cannot set " + std::to_string(baud) + " baud: " +
                    last_error_text();
    CloseHandle(h);
    return false;
  }
  COMMTIMEOUTS to;
  std::memset(&to, 0, sizeof to);
  to.WriteTotalTimeoutConstant = 50;  // never stall the GUI thread for long
  to.WriteTotalTimeoutMultiplier = 0;
  SetCommTimeouts(h, &to);
  m_handle = h;
  m_port = port;
  return true;
}

void SerialPort::close() {
  if (m_handle) CloseHandle((HANDLE)m_handle);
  m_handle = nullptr;
}

bool SerialPort::isOpen() const { return m_handle != nullptr; }

bool SerialPort::write(const std::string &data, std::string *err) {
  if (!m_handle) {
    if (err) *err = "port not open";
    return false;
  }
  DWORD written = 0;
  if (!WriteFile((HANDLE)m_handle, data.data(), (DWORD)data.size(), &written,
                 nullptr)) {
    if (err) *err = last_error_text();
    return false;
  }
  return true;  // a short write on timeout just drops the rest
}

std::vector<std::string> SerialPort::listPorts() {
  std::vector<std::string> out;
  char target[512];
  for (int i = 1; i <= 64; i++) {
    std::string name = "COM" + std::to_string(i);
    if (QueryDosDeviceA(name.c_str(), target, sizeof target)) out.push_back(name);
  }
  return out;
}

#else
// ---------------------------------------------------------------------------
// Linux / macOS
// ---------------------------------------------------------------------------
static speed_t to_speed(int baud) {
  switch (baud) {
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
#ifdef B230400
    case 230400: return B230400;
#endif
    default: return 0;
  }
}

bool SerialPort::open(const std::string &port, int baud, std::string *err) {
  close();
  speed_t sp = to_speed(baud);
  if (!sp) {
    if (err) *err = "unsupported baud rate " + std::to_string(baud);
    return false;
  }
  int fd = ::open(port.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    if (err) *err = port + ": " + std::strerror(errno);
    return false;
  }
  struct termios tio;
  if (tcgetattr(fd, &tio) != 0) {
    if (err) *err = port + ": not a serial device (" + std::strerror(errno) + ")";
    ::close(fd);
    return false;
  }
  cfmakeraw(&tio);
  tio.c_cflag &= ~(CSTOPB | PARENB | CSIZE);
  tio.c_cflag |= CS8 | CLOCAL;
#ifdef CRTSCTS
  tio.c_cflag &= ~CRTSCTS;
#endif
  tio.c_iflag &= ~(IXON | IXOFF | IXANY);
  cfsetispeed(&tio, sp);
  cfsetospeed(&tio, sp);
  if (tcsetattr(fd, TCSANOW, &tio) != 0) {
    if (err) *err = port + ": cannot configure (" + std::strerror(errno) + ")";
    ::close(fd);
    return false;
  }
  m_fd = fd;
  m_port = port;
  return true;
}

void SerialPort::close() {
  if (m_fd >= 0) ::close(m_fd);
  m_fd = -1;
}

bool SerialPort::isOpen() const { return m_fd >= 0; }

bool SerialPort::write(const std::string &data, std::string *err) {
  if (m_fd < 0) {
    if (err) *err = "port not open";
    return false;
  }
  ssize_t n = ::write(m_fd, data.data(), data.size());
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return true;  // dropped
    if (err) *err = std::strerror(errno);
    return false;
  }
  return true;
}

std::vector<std::string> SerialPort::listPorts() {
  std::vector<std::string> out;
  auto scan = [&](const char *dir, const std::vector<std::string> &prefixes) {
    DIR *d = opendir(dir);
    if (!d) return;
    while (dirent *e = readdir(d)) {
      std::string n = e->d_name;
      if (n.empty() || n[0] == '.') continue;
      for (const auto &p : prefixes)
        if (n.rfind(p, 0) == 0) {
          out.push_back(std::string(dir) + "/" + n);
          break;
        }
    }
    closedir(d);
  };
#ifdef __APPLE__
  scan("/dev", {"cu."});
#else
  scan("/dev/serial/by-id", {""});
  scan("/dev", {"ttyUSB", "ttyACM", "ttyAMA", "ttyS0", "ttyS1", "rfcomm"});
#endif
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}
#endif
