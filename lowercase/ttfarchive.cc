
#include "ttfarchive.h"

#include <string>
#include <vector>
#include <unistd.h>

#include "util.h"
#include "base/logging.h"

using namespace std;

void Ttfarchive::AddAllFilesRec(const string &dir, vector<string> *all_files) {
  for (const string &f : Util::ListFiles(dir)) {
    const string filename = Util::DirPlus(dir, f);
    // printf("%s + %s = %s\n", dir.c_str(), f.c_str(), filename.c_str());
    if (Util::isdir(filename)) {
      // printf("Dir: [%s]\n", filename.c_str());
      AddAllFilesRec(filename, all_files);
    } else {
      if (!filename.empty() &&
          // Should probably delete emacs backups..?
          filename[filename.size() - 1] != '#' &&
          filename[filename.size() - 1] != '~') {
        all_files->push_back(Backslash(filename));
      }
    }
  }
}

string Ttfarchive::Frontslash(const string &s) {
  string ret;
  for (const char c : s)
    ret += (c == '\\' ? '/' : c);

  if (ret.find("d:/") == 0) {
    ret[0] = '/';
    ret[1] = 'd';
  } else if (ret.find("c:/") == 0) {
    ret[0] = '/';
    ret[1] = 'c';
  }

  return ret;
}

string Ttfarchive::Backslash(const string &s) {
  string ret;
  for (const char c : s)
    ret += (c == '/' ? '\\' : c);
  return ret;
}



