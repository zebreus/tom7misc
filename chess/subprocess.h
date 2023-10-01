
#ifndef _SUBPROCESS_H
#define _SUBPROCESS_H

#include <string>

struct Subprocess {
  // The command line starts with the executable to execute. It
  // can optionally include arguments, which are unstructured.
  // TODO: Utilities for creating command lines from vectors, but
  // note that there is no actually standard way of quoting these
  // (except on Windows, the executable can be double-quoted); the
  // process gets a string to parse, not an array.
  //
  // Returns nullptr if creating the process fails.
  static Subprocess *Create(const std::string &command_line);

  // These functions are not thread-safe and should only be called from
  // one thread. However, it is permissible to have one thread (only) writing
  // while another (only) reads.
  virtual bool Write(const std::string &data) = 0;

  // Reads from both stdout and stderr.
  virtual bool ReadLine(std::string *line) = 0;

  // Terminates the process if it is still running.
  virtual ~Subprocess();
};

#endif
