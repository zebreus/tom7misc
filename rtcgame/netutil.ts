/*
  XMLHttpRequest but as a promise.
  Resolve is called with the string containing the response.

  obj argument:
  headers: additional headers as map ("header: value")
  method: string; "GET", "POST", etc.
  url: string
  body: string
*/
const request = (obj : {headers?: Record<string, string>,
                        method?: string,
                        url: string,
                        body: string}) : Promise<string> => {
  return new Promise((resolve, reject) => {
    let xhr = new XMLHttpRequest();
    xhr.open(obj.method || "GET", obj.url);
    if (obj.headers) {
      for (let key in obj.headers) {
        xhr.setRequestHeader(key, obj.headers[key]);
      }
    }
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(xhr.response);
      } else {
        reject(xhr.statusText);
      }
    };
    xhr.onerror = () => reject(xhr.statusText);
    xhr.send(obj.body);
  });
};

/* Like above, but wrapped to use a standard protocol:
   - Always uses POST
   - params given as {key: value} object. This function url-encodes them.
   Expects response to have fixed XSSI header. Parses the json. */
const requestJSON = (url : string, params: Record<string, string>) => {
  // Encode a post body suitable for application/x-www-form-urlencoded.
  let kvs = [];
  for (let o in params) {
    kvs.push(encodeURIComponent(o) + '=' + encodeURIComponent(params[o]));
  }

  let obj = {url: url,
             body: kvs.join('&'),
             method: 'POST',
             headers: {'Content-Type': 'application/x-www-form-urlencoded'}};

  return request(obj).
    then((res : string) => {
      if (res.indexOf(XSSI_HEADER) == 0) {
        let r = res.substr(XSSI_HEADER.length);
        return JSON.parse(r);
      } else {
        throw 'no XSSI header in response';
      }
    });
};

// XX maybe belongs in its own file ? not really a network thing.

// Wrapper around a timestamp and period for implementing
// functionality like window.setTimeout.
class Periodically {
  private nextRun : number;
  private paused : boolean;

  constructor(private readonly periodMs : number) {
    if (periodMs <= 0) throw 'precondition';
    this.nextRun = window.performance.now();
    this.paused = false;
  }

  // Return true if periodMs has elapsed since the last run.
  // If this function returns true, we assume the caller does
  // the associated action now (and so move the next run time
  // forward).
  shouldRun() : boolean {
    if (this.paused) return false;
    let n = window.performance.now();
    if (n >= this.nextRun) {
      this.nextRun = n + this.periodMs;
      return true;
    }
    return false;
  }

  pause() : void {
    this.paused = true;
  }

  reset() : void {
    this.paused = false;
    this.nextRun = window.performance.now() + this.periodMs;
  }
};
