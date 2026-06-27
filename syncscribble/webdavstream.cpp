#include "webdavstream.h"
#include "httpclient.h"
#include "scribbleapp.h"
#include "scribbleconfig.h"
#include "ulib/stringutil.h"
#include "ulib/md5.h"

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
  bool gotremote = resp.ok();
  if(gotremote) {
    seek(0);  // rewind for the reader
    m_etag = !resp.etag.empty() ? resp.etag : resp.lastModified;
    m_dirty = false;
  }
  else
    truncate(0);

  // recover unsaved changes from a prior offline session: a shadow copy means the last PUT
  // never completed, so prefer it over the (stale) server contents and keep retrying upload
  FileStream shadow(shadowPath().c_str(), "rb");
  if(shadow.is_open() && shadow.size() > 0) {
    truncate(0);
    char buf[16384];  size_t n;
    while((n = shadow.read(buf, sizeof(buf))) > 0)
      MemStream::write(buf, n);
    seek(0);
    m_dirty = true;
    m_offline = true;
    return true;
  }
  return gotremote;
}

// deterministic local cache path for a remote doc, so an offline copy survives restarts.
// Uses savedPath (persistent) not tempPath, which the app wipes on startup.
std::string WebDavStream::shadowPath() const
{
  uint8_t digest[MD5_DIGEST_SIZE];
  MD5(m_url.data(), (int)m_url.size(), digest);
  std::string hex;
  for(int i = 0; i < MD5_DIGEST_SIZE; ++i)
    hex += fstring("%02x", digest[i]);
  // include the basename for human recognizability
  FSPath urlpath(m_url);
  return FSPath(ScribbleApp::app->savedPath, hex + "-" + urlpath.fileName()).c_str();
}

// write the current buffer to the local shadow so unsaved changes survive an offline PUT failure
void WebDavStream::writeShadow()
{
  std::string path = shadowPath();
  createPath(FSPath(path).parent());  // savedPath dir may not exist yet
  FileStream fs(path.c_str(), "wb");
  if(fs.is_open())
    fs.write(data(), size());
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
  if(!resp.ok()) {
    // offline / server error: keep a local copy and stay dirty so the next save retries.
    // Note: Document::save ignores flush()'s return, so failure must be surfaced here.
    writeShadow();
    if(!m_offline) {
      m_offline = true;
      ScribbleApp::app->showNotify(_("Working offline - changes saved locally and will be uploaded when the server is reachable."), 2);
    }
    return false;
  }
  m_etag = !resp.etag.empty() ? resp.etag : resp.lastModified;
  m_dirty = false;
  if(m_offline) {
    m_offline = false;
    ScribbleApp::app->showNotify(_("Reconnected - changes uploaded."), 1);
    remove(shadowPath().c_str());  // upload succeeded; drop the local copy
  }
  return true;
}
