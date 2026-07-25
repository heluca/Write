// Phase 0 standalone proof: GET (and optionally PROPFIND) a WebDAV URL over HTTPS via HttpClient.
// Build (Linux, system libcurl):
//   g++ -std=c++14 -I.. httptest.cpp ../syncscribble/httpclient.cpp \
//       $(pkg-config --cflags --libs libcurl) -o httptest
// Usage:
//   ./httptest <url> [user] [password] [--propfind]

#include <stdio.h>
#include <string.h>
// fileutil.h is single-header; pull in MemStream's implementation for this standalone test
#define FILEUTIL_IMPLEMENTATION
#include "ulib/fileutil.h"
#include "syncscribble/httpclient.h"

// minimal stub so the single-header fileutil impl links in this standalone harness
void platform_assert(bool cond, const char* msg, const char*, const char*, unsigned int)
{ if(!cond) { fprintf(stderr, "assert failed: %s\n", msg); abort(); } }

int main(int argc, char* argv[])
{
  if(argc < 2) {
    fprintf(stderr, "usage: %s <url> [user] [password] [--propfind]\n", argv[0]);
    return 2;
  }
  const char* url = argv[1];
  const char* user = (argc > 2 && strncmp(argv[2], "--", 2) != 0) ? argv[2] : "";
  const char* pass = (argc > 3 && strncmp(argv[3], "--", 2) != 0) ? argv[3] : "";
  bool propfind = false;
  for(int i = 2; i < argc; ++i)
    if(strcmp(argv[i], "--propfind") == 0) propfind = true;

  MemStream body;
  HttpRequest req(url);
  if(user[0]) req.auth(user, pass);

  HttpResponse resp = propfind ? req.propfind(&body, 1) : req.get(&body);

  printf("status: %ld\n", resp.status);
  if(!resp.error.empty()) printf("error: %s\n", resp.error.c_str());
  if(!resp.etag.empty()) printf("etag: %s\n", resp.etag.c_str());
  if(!resp.lastModified.empty()) printf("last-modified: %s\n", resp.lastModified.c_str());
  printf("body bytes: %zu\n", body.size());
  if(propfind || body.size() < 2048)
    printf("--- body ---\n%.*s\n", (int)body.size(), body.data());

  return resp.ok() ? 0 : 1;
}
