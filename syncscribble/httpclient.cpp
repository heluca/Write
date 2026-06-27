#include "httpclient.h"
#include <curl/curl.h>
#include <strings.h>

// One-time global init; curl_global_init is not thread-safe so do it before any threads start.
static void ensureCurlInit()
{
  static bool inited = (curl_global_init(CURL_GLOBAL_DEFAULT) == 0);
  (void)inited;
}

HttpRequest::HttpRequest(const std::string& url) : m_url(url) { ensureCurlInit(); }

HttpRequest& HttpRequest::auth(const std::string& user, const std::string& pass)
{
  m_user = user;  m_pass = pass;  return *this;
}

HttpRequest& HttpRequest::header(const std::string& h) { m_headers.push_back(h);  return *this; }

static size_t writeToStream(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  size_t n = size*nmemb;
  return static_cast<IOStream*>(userdata)->write(ptr, n);
}

static size_t readFromStream(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  size_t n = size*nmemb;
  return static_cast<IOStream*>(userdata)->read(ptr, n);
}

// lets curl rewind the upload body to resend it (e.g. after a 401 auth challenge)
static int seekStream(void* userdata, curl_off_t offset, int origin)
{
  return static_cast<IOStream*>(userdata)->seek((long)offset, origin) ? CURL_SEEKFUNC_OK : CURL_SEEKFUNC_FAIL;
}

// capture ETag / Last-Modified from response headers
static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata)
{
  size_t n = size*nitems;
  HttpResponse* resp = static_cast<HttpResponse*>(userdata);
  auto match = [&](const char* name, std::string& out) {
    size_t nl = strlen(name);
    if(n > nl && strncasecmp(buffer, name, nl) == 0) {
      const char* p = buffer + nl;
      const char* end = buffer + n;
      while(p < end && (*p == ' ' || *p == '\t')) ++p;
      const char* q = end;
      while(q > p && (q[-1] == '\r' || q[-1] == '\n' || q[-1] == ' ')) --q;
      out.assign(p, q - p);
    }
  };
  match("ETag:", resp->etag);
  match("Last-Modified:", resp->lastModified);
  return n;
}

HttpResponse HttpRequest::perform(const char* method, IOStream* sink, IOStream* source, size_t srclen)
{
  HttpResponse resp;
  CURL* curl = curl_easy_init();
  if(!curl) { resp.error = "curl_easy_init failed";  return resp; }

  curl_easy_setopt(curl, CURLOPT_URL, m_url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // safe for use off the main thread
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Write/WebDAV");

  if(!m_user.empty()) {
    // negotiate whatever auth the server offers (Basic/Digest); on PUT this means a 401 challenge
    // then a body resend, which needs CURLOPT_SEEKFUNCTION (set below) to rewind the upload stream
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_ANY);
    curl_easy_setopt(curl, CURLOPT_USERNAME, m_user.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, m_pass.c_str());
  }

  if(sink) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, sink);
  }

  if(strcmp(method, "GET") == 0) {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  }
  else if(strcmp(method, "PUT") == 0) {
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readFromStream);
    curl_easy_setopt(curl, CURLOPT_READDATA, source);
    curl_easy_setopt(curl, CURLOPT_SEEKFUNCTION, seekStream);
    curl_easy_setopt(curl, CURLOPT_SEEKDATA, source);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)srclen);
  }
  else {  // PROPFIND and other WebDAV verbs
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
  }

  struct curl_slist* hdrs = NULL;
  for(const std::string& h : m_headers)
    hdrs = curl_slist_append(hdrs, h.c_str());
  if(hdrs)
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

  CURLcode res = curl_easy_perform(curl);
  if(res != CURLE_OK)
    resp.error = curl_easy_strerror(res);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);

  if(hdrs) curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);
  return resp;
}

HttpResponse HttpRequest::get(IOStream* sink)
{
  return perform("GET", sink, NULL, 0);
}

HttpResponse HttpRequest::put(IOStream* source, size_t len, const std::string& etag)
{
  if(!etag.empty())
    header("If-Match: " + etag);
  return perform("PUT", NULL, source, len);
}

HttpResponse HttpRequest::propfind(IOStream* sink, int depth)
{
  header(depth == 0 ? "Depth: 0" : "Depth: 1");
  header("Content-Type: application/xml");
  return perform("PROPFIND", sink, NULL, 0);
}
