
/* maximally simple interface to HTTP
   using SDL_net */

#ifndef _ESCAPE_HTTP_H
#define _ESCAPE_HTTP_H

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "httputil.h"

/* Before using the HTTP class, you must initialize SDL_net
   with SDLNet_Init(). */

/* results from GET/POST/etc. */
enum class HTTPResult {
  OK, ERROR_404, ERROR_OTHER,
};

/* interface only */
struct HTTP {
  static HTTP *Create();

  /* set user-agent */
  virtual void SetUA(std::string_view ua) = 0;
  /* ... other stuff ... */

  /* doesn't really connect -- just sets host:port for
     later requests. might fail if can't look up hostname. */
  virtual bool Connect(std::string_view host, int port = 80) = 0;

  /* download the entire thing to a string */
  virtual HTTPResult Get(std::string_view path, std::string &out) = 0;

  /* create a temp file (in the cwd) and download to that.
     return the name of the temp file */
  virtual HTTPResult GetTempFile(std::string_view path, std::string &file) = 0;

  /* use post, allowing to upload files */
  virtual HTTPResult Put(std::string_view path,
                         const std::vector<FormEntry> &items,
                         std::string &out) = 0;

  /* set callback object. This object is never freed. */
  virtual void SetCallback(std::function<void(int, int)> callback) = 0;

  /* this will be called with various debugging
     messages, if non-null */
  void (*log_message)(std::string_view s);

  virtual ~HTTP() {};
};

#endif
