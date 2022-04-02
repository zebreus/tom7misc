#ifndef _PINGU_NBD_UTIL_H
#define _PINGU_NBD_UTIL_H

#include <sstream>
#include <string>

#define CHECK(e) \
  if (e) {} else NbdFatal(__FILE__, __LINE__).Stream() \
				   << "*************** Check failed: " #e "\n"

class NbdOstreamBuf : public std::stringbuf {
public:
  int sync() override {
	nbdkit_debug("%s", str().c_str());
	return 0;
  }
};
struct NbdFatal {
  NbdFatal(const char* file, int line) : os(&buf) {
    Stream() << file << ":" << line << ": ";
  }
  [[noreturn]]
  ~NbdFatal() {
    Stream() << "\n" << std::flush;
    abort();
  }
  std::ostream& Stream() { return os; }
private:
  NbdOstreamBuf buf;
  std::ostream os;
};


#endif
