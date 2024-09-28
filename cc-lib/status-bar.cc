
#include "status-bar.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/port.h"

#include "util.h"
#include "ansi.h"

using namespace std;

StatusBar::StatusBar(int num_lines) : num_lines(num_lines) {
  CHECK(num_lines > 0);
}

// Print to the screen. Adds trailing newline if not present.
void StatusBar::Printf(const char* format, ...) PRINTF_ATTRIBUTE(1, 2) {
  va_list ap;
  va_start(ap, format);
  string result;
  StringAppendV(&result, format, ap);
  va_end(ap);
  Emit(result);
}

void StatusBar::Emit(const std::string &s) {
  std::vector<std::string> lines = Util::SplitToLines(s);
  std::unique_lock<std::mutex> ml(m);
  MoveUp();
  for (const string &line : lines) {
    printf("%s\n", line.c_str());
  }
  // Maintain space for status.
  if (prev_status_lines.empty()) {
    for (int i = 0; i < num_lines; i++) {
      printf("\n");
    }
  } else {
    EmitStatusLinesWithLock(prev_status_lines);
  }
}

// Update the status bar. This should be done in one call that
// contains num_lines lines. Trailing newline not necessary.
void StatusBar::Statusf(const char* format, ...) PRINTF_ATTRIBUTE(1, 2) {
  va_list ap;
  va_start(ap, format);
  string result;
  StringAppendV(&result, format, ap);
  va_end(ap);
  EmitStatus(result);
}

void StatusBar::EmitStatus(const std::string &s) {
  std::vector<std::string> lines = Util::SplitToLines(s);
  std::unique_lock<std::mutex> ml(m);
  prev_status_lines = lines;
  MoveUp();
  EmitStatusLinesWithLock(lines);
}

  // The idea is that we keep the screen in a state where there are
  // num_lines of status at the bottom, right before the cursor. This
  // is always throw-away space. When we print something other than
  // status, we just pad with the number of blank lines so that the
  // next call will not overwrite what we wrote.
void StatusBar::MoveUp() {
  if (!first) {
    for (int i = 0; i < num_lines; i++) {
      printf(
          // Cursor to beginning of previous line
          ANSI_PREVLINE
          // Clear line
          ANSI_CLEARLINE);
    }
  }
  first = false;
}

void StatusBar::EmitStatusLinesWithLock(const std::vector<std::string> &lines) {
  if ((int)lines.size() != num_lines) {
    printf(ARED("...wrong number of lines...") "\n");
  }
  for (const string &line : lines) {
    printf("%s\n", line.c_str());
  }
}
