#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

// Minimal HTTP(S) client over libcurl, exposing just what WebDAV needs (GET/PUT/PROPFIND).
// Request/response bodies are IOStream* so this plugs directly into WebDavStream and the
// existing MemStream machinery (curl read/write callbacks -> IOStream::readfn/writefn).

#include <string>
#include <vector>
#include "ulib/fileutil.h"

struct HttpResponse
{
  long status = 0;        // HTTP status code; 0 if the request never completed (network error)
  std::string error;      // libcurl error string if the transfer failed at the transport level
  std::string etag;       // value of the ETag response header, if present
  std::string lastModified;  // value of Last-Modified, fallback conflict token

  bool ok() const { return status >= 200 && status < 300; }
};

class HttpRequest
{
public:
  HttpRequest(const std::string& url);

  // credentials for HTTP Basic auth; empty user disables auth
  HttpRequest& auth(const std::string& user, const std::string& pass);
  // add an arbitrary request header, e.g. "If-Match: \"<etag>\"" or "Depth: 1"
  HttpRequest& header(const std::string& h);

  // body is written into `sink`; PROPFIND body (request) taken from `propfindBody`
  HttpResponse get(IOStream* sink);
  HttpResponse put(IOStream* source, size_t len, const std::string& etag = "");
  HttpResponse propfind(IOStream* sink, int depth = 1);

private:
  std::string m_url;
  std::string m_user;
  std::string m_pass;
  std::vector<std::string> m_headers;

  HttpResponse perform(const char* method, IOStream* sink, IOStream* source, size_t srclen);
};

#endif // HTTPCLIENT_H
