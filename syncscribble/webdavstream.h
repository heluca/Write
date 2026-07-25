#ifndef WEBDAVSTREAM_H
#define WEBDAVSTREAM_H

// An IOStream backed by a WebDAV resource: GET-into-buffer on open, PUT-on-flush.
// Subclasses MemStream so reads/seeks/writes operate on the in-memory copy (this also
// gives the block-gzip random access in Document::loadBgzDoc for free); only the network
// transfer is added. Documents are loaded and saved whole, so whole-file GET/PUT fits.

#include "ulib/fileutil.h"
#include <string>
#include <vector>

struct WebDavEntry
{
  std::string name;     // file or directory name (no path), with trailing '/' for collections
  bool isDir = false;
  Timestamp mtime = 0;
  long size = 0;
};

// failure details for webdavListDir: status is the HTTP status code (0 if the request never
// completed, in which case message is the libcurl transport error, e.g. DNS/TLS/timeout)
struct WebDavError
{
  long status = 0;
  std::string message;
};

// list a WebDAV collection via PROPFIND Depth:1; returns false on error (e.g. offline).
// `url` is a dav://, davs://, http:// or https:// collection URL; credentials come from config.
bool webdavListDir(const char* url, std::vector<WebDavEntry>& entries, WebDavError* err = NULL);

class WebDavStream : public MemStream
{
public:
  // url is the full WebDAV resource URL; credentials are resolved from config.
  // For "rb"/"rb+" modes the resource is fetched immediately (so size()/read() work);
  // for "wb"/"wb+" the buffer starts empty and is PUT on flush()/close.
  WebDavStream(const char* url, const char* mode = "rb+");
  ~WebDavStream() override;

  bool is_open() const override { return m_open; }
  const char* name() const override { return m_url.c_str(); }
  bool flush() override;   // PUT the buffer if dirty
  size_t write(const void* src, size_t len) override { m_dirty = true; return MemStream::write(src, len); }
  bool truncate(size_t len) override { m_dirty = true; return MemStream::truncate(len); }

  // conflict token from the last GET/PUT (ETag preferred, Last-Modified fallback)
  const std::string& etag() const { return m_etag; }

  // true if `path` should be handled as a WebDAV resource rather than a local file
  static bool isWebDavUrl(const char* path);

  // resolve the password for a server/doc URL (keychain -> plaintext config -> session cache);
  // empty result means the caller should prompt and then setSessionPassword()
  static std::string password(const std::string& url);
  static std::string username(const std::string& url);
  static void setSessionPassword(const std::string& serverUrl, const std::string& pw);

  // --- WebDAV server registry (multiple servers, each with its own credentials) ---
  static std::vector<std::string> servers();          // registered server base URLs (end with '/')
  static std::string serverUser(const std::string& serverUrl);
  static std::string serverForUrl(const std::string& path);  // server whose base prefixes path
  static void addServer(const std::string& url, const std::string& user, const std::string& pw, bool savePw);
  static void removeServer(const std::string& serverUrl);

private:
  std::string m_url;
  std::string m_user;
  std::string m_pass;
  std::string m_etag;
  bool m_open = false;
  bool m_dirty = false;
  bool m_writeMode = false;
  bool m_offline = false;  // last PUT failed; changes held in local shadow copy

  bool doGet();
  std::string shadowPath() const;
  void writeShadow();
};

#endif // WEBDAVSTREAM_H
