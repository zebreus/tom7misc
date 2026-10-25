
// XXX: Merge this with cc-lib's Util class, by calling
// the Util methods where possible until we only have
// stuff of local interest in here.

#ifndef _ESCAPE_UTIL_H
#define _ESCAPE_UTIL_H

#include <cstdlib>
#include <cstdio>
#include <string>
#include <string_view>

#define ATTIC_DIR "attic"

#ifdef WIN32
#   define DIRSEP  "\\"
#   define DIRSEPC '\\'
#else
#   define DIRSEP  "/"
#   define DIRSEPC '/'
#endif

template <class T>
struct vallist {
  T head;
  vallist<T> *next;
  vallist(T h, vallist<T> *n) : head(h), next(n) {}
  static void push(vallist<T> *&sl, T h) {
    sl = new vallist<T>(h, sl);
  }

  static T pop(vallist<T> *&sl, T u) {
    if (sl) {
      vallist<T> *tmp = sl;
      T t = tmp->head;
      sl = sl->next;
      delete tmp;
      return t;
    } else return u;
  }

  /* ie, destroy */
  static void diminish(vallist<T> *&sl) {
    T dummy = {};
    while (sl != nullptr) pop(sl, dummy);
  }

  void destroy() {
    diminish(this);
  }

  int length() {
    int res = 0;
    vallist<T> *tmp = this;
    while (tmp) {
      tmp = tmp->next;
      res++;
    }
    return res;
  }

  static vallist<T> *copy(vallist<T> *sl) {
    if (sl) {
      return new vallist<T>(sl->head,
                            copy(sl->next));
    } else return 0;
  }
};

struct EscapeUtil {
  using string = std::string;

  static int stoi(string s);

  static string ptos(void *);
  static unsigned int hash(string s);
  /* give /home/tom/ of /home/tom/.bashrc */
  static string pathof(string s);
  static string fileof(string s);

  static string ensureext(string f, string ext);
  static string lcase(string in);
  static string ucase(string in);

  /* DirPlus("/usr/local", "core") and
     DirPlus("/usr/local/", core")  both give  "/usr/local/core"
     DirPlus("/usr/local", "/etc/passwd")  gives "/etc/passwd"  */
  static string DirPlus(std::string_view dir, std::string_view file);

  /* spec is a character spec like "A-Z0-9`,."
     XXX document */
  static bool matchspec(string spec, char c);

  /* An ordering on strings that gives a more "natural" sort:
     Tutorial 1, ..., Tutorial 9, Tutorial 10, Tutorial 11, ...
     rather than
     Tutorial 1, Tutorial 10, Tutorial 11, ..., Tutorial 2, Tutorial 20, ...
  */
  static int natural_compare(const string &l, const string &r);

  /* Same as above, but ignore 'the' at the beginning */
  static int library_compare(const string &l, const string &r);

  /* Is string s alphabetized under char k? */
  static bool library_matches(char k, const string &s);

  /* open a new file. if it exists, return 0 */
  static FILE *OpenNew(std::string_view s);
  static int random();
  /* random in 0.0 .. 1.0 */
  static float randfrac();
  static int getpid();
  /* anything ending with \n. ignores \r.
     modifies str. */
  static string getline(string &str);
  /* same, for open file. */
  static string fgetline(FILE *f);

  /* number of entries (not . or ..) in dir d */
  static int DirSize(std::string_view d);

  /* mylevels/good_tricky   to
     mylevels               to
     . */
  static string cdup(const string &dir);

  /* true iff big ends with small */
  static bool endswith(string big_, string small_);
  /* starts */
  static bool startswith(string big_, string small_);

  /* erase any whitespace up to the first
     non-whitespace char. */
  static string losewhitel(const string &s);

  /* try to remove the file. If it
     doesn't exist or is successfully
     removed, then return true. */
  static bool remove(string f);

  /* move a file from src to dst. Return
     true on success. */
  static bool move(string src, string dst);

  /* make a copy by reading/writing */
  static bool copy(string src, string dst);

  // Gives you a filename that doesn't exist, based on the
  // suffix. Since it doesn't create the file, there's an
  // inherent (but low probability) risk that it gets
  // created by someone else before you use the name, though.
  static string TempFile(std::string_view suffix);

  /* same as isdir */
  static bool existsdir(std::string_view);

  static bool makedir(string);

  /* try to launch the url with the default browser;
     doesn't work on all platforms. true on success */
  static bool launchurl(const string &);

  /* creates directories for f */
  static void createpathfor(string f);

  /* open, creating directories if necessary */
  static FILE *fopenp(string f, string mode);

  /* move a file into an 'attic' backup dir */
  static void toattic(string f);

  /* returns false if failed/unsupported */
  static bool setclipboard(string);
};

#endif
