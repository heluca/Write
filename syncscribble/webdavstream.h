#ifndef WEBDAVSTREAM_H
#define WEBDAVSTREAM_H

// An IOStream backed by a WebDAV resource: GET-into-buffer on open, PUT-on-flush.
// Subclasses MemStream so reads/seeks/writes operate on the in-memory copy (this also
// gives the block-gzip random access in Document::loadBgzDoc for free); only the network
// transfer is added. Documents are loaded and saved whole, so whole-file GET/PUT fits.

#include "ulib/fileutil.h"
#include <string>

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
