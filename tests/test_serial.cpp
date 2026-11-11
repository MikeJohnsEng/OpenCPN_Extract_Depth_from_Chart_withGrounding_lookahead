// POSIX-only check of SerialPort against a pseudo-terminal pair: open at
// 4800 baud, write a sentence, read it back from the master side.
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "serial_port.h"

int main() {
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  if (master < 0 || grantpt(master) || unlockpt(master)) {
    std::cerr << "no pty available\n";
    return 2;
  }
  std::string slave = ptsname(master);

  SerialPort sp;
  std::string err;
  int fails = 0;
  if (!sp.open(slave, 4800, &err)) {
    std::cerr << "open failed: " << err << "\n";
    return 1;
  }
  const std::string msg = "$PCMLD,2.3,,,,,,,,,,,C,,G,V*00\r\n";
  if (!sp.write(msg, &err)) {
    std::cerr << "write failed: " << err << "\n";
    fails++;
  }
  std::string got;
  pollfd p{master, POLLIN, 0};
  while (got.size() < msg.size() && poll(&p, 1, 1000) > 0) {
    char buf[256];
    ssize_t n = read(master, buf, sizeof buf);
    if (n <= 0) break;
    got.append(buf, n);
  }
  if (got != msg) {
    std::cerr << "read back mismatch: [" << got << "]\n";
    fails++;
  }
  SerialPort bad;
  if (bad.open("/dev/does-not-exist", 4800, &err)) fails++;
  if (bad.open(slave, 1234, &err)) fails++;  // unsupported baud
  if (bad.write("x", &err)) fails++;         // not open
  std::cout << "listPorts returned " << SerialPort::listPorts().size()
            << " entries\n";
  std::cout << (fails ? "serial test FAILED\n" : "serial test passed\n");
  close(master);
  return fails ? 1 : 0;
}
