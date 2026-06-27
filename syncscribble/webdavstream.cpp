#include "webdavstream.h"
#include "httpclient.h"
#include "scribbleapp.h"
#include "scribbleconfig.h"
#include "ulib/stringutil.h"

bool WebDavStream::isWebDavUrl(const char* path)
{
  if(!path) return false;
  StringRef p(path);
  // explicit dav schemes, or plain http(s) when it matches the configured remote root
  if(p.startsWith("dav://") || p.startsWith("davs://"))
    return true;
  const char* root = ScribbleApp::cfg->String("webdavUrl", "");
  if(root[0] && p.startsWith(root))
    return true;
  return false;
}

// dav:// -> http://, davs:// -> https://  (leave http/https as-is)
static std::string toHttpUrl(const std::string& url)
{
  if(url.compare(0, 6, "davs://") == 0) return "https://" + url.substr(7);
  if(url.compare(0, 6, "dav://") == 0) return "http://" + url.substr(6);
  return url;
}

WebDavStream::WebDavStream(const char* url, const char* mode) : m_url(toHttpUrl(url))
{
  m_user = ScribbleApp::cfg->String("webdavUser", "");
  m_pass = ScribbleApp::cfg->String("webdavPassword", "");
  m_writeMode = strchr(mode, 'w') != NULL;
  // read modes need the current contents; pure write mode starts empty
  m_open = m_writeMode ? true : doGet();
}

WebDavStream::~WebDavStream()
{
  if(m_dirty)
    flush();
}

bool WebDavStream::doGet()
{
  HttpRequest req(m_url);
  if(!m_user.empty())
    req.auth(m_user, m_pass);
  HttpResponse resp = req.get(this);  // body streamed into our MemStream buffer
  if(!resp.ok()) {
    truncate(0);
    return false;
  }
  seek(0);  // rewind for the reader
  m_etag = !resp.etag.empty() ? resp.etag : resp.lastModified;
  m_dirty = false;
  return true;
}

bool WebDavStream::flush()
{
  if(!m_dirty)
    return true;
  long savedpos = tell();
  seek(0);
  HttpRequest req(m_url);
  if(!m_user.empty())
    req.auth(m_user, m_pass);
  HttpResponse resp = req.put(this, size());  // If-Match handling added in Phase 4
  seek(savedpos);
  if(!resp.ok())
    return false;
  m_etag = !resp.etag.empty() ? resp.etag : resp.lastModified;
  m_dirty = false;
  return true;
}
