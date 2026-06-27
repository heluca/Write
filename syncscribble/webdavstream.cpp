#include "webdavstream.h"
#include "httpclient.h"
#include "scribbleapp.h"
#include "scribbleconfig.h"
#include "ulib/stringutil.h"
#include "ulib/md5.h"
#include "pugixml.hpp"
#include <time.h>

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
  if(url.compare(0, 7, "davs://") == 0) return "https://" + url.substr(7);
  if(url.compare(0, 6, "dav://") == 0) return "http://" + url.substr(6);
  return url;
}

// percent-decode a WebDAV href (server returns encoded paths)
static std::string urlDecode(const std::string& s)
{
  std::string out;
  for(size_t i = 0; i < s.size(); ++i) {
    if(s[i] == '%' && i + 2 < s.size()) {
      out += (char)strtol(s.substr(i+1, 2).c_str(), NULL, 16);
      i += 2;
    }
    else
      out += s[i];
  }
  return out;
}

bool webdavListDir(const char* url, std::vector<WebDavEntry>& entries)
{
  std::string httpurl = toHttpUrl(url);
  if(httpurl.empty() || httpurl.back() != '/')
    httpurl += '/';
  HttpRequest req(httpurl);
  std::string user = ScribbleApp::cfg->String("webdavUser", "");
  if(!user.empty())
    req.auth(user, ScribbleApp::cfg->String("webdavPassword", ""));
  MemStream body;
  HttpResponse resp = req.propfind(&body, 1);
  if(resp.status != 207)
    return false;

  pugi::xml_document doc;
  if(!doc.load_buffer(body.data(), body.size()))
    return false;
  // the collection's own path, to skip its self-entry and to derive child names
  std::string basepath = urlDecode(httpurl.substr(httpurl.find('/', httpurl.find("://") + 3)));

  // namespaces vary (D:, lp1:, etc.), so match by local name
  for(pugi::xml_node resp_node = doc.first_child().first_child(); resp_node; resp_node = resp_node.next_sibling()) {
    if(!StringRef(resp_node.name()).endsWith("response"))
      continue;
    WebDavEntry e;
    for(pugi::xml_node c = resp_node.first_child(); c; c = c.next_sibling()) {
      StringRef cn(c.name());
      if(cn.endsWith("href"))
        e.name = urlDecode(c.text().as_string());
      else if(cn.endsWith("propstat")) {
        pugi::xml_node prop = c.child("D:prop");
        if(!prop) { for(pugi::xml_node p = c.first_child(); p; p = p.next_sibling()) if(StringRef(p.name()).endsWith("prop")) { prop = p; break; } }
        for(pugi::xml_node p = prop.first_child(); p; p = p.next_sibling()) {
          StringRef pn(p.name());
          if(pn.endsWith("resourcetype")) {
            for(pugi::xml_node r = p.first_child(); r; r = r.next_sibling())
              if(StringRef(r.name()).endsWith("collection")) e.isDir = true;
          }
          else if(pn.endsWith("getcontentlength"))
            e.size = atol(p.text().as_string());
          else if(pn.endsWith("getlastmodified")) {
            struct tm tm = {};
            if(strptime(p.text().as_string(), "%a, %d %b %Y %H:%M:%S", &tm))
              e.mtime = timegm(&tm);
          }
        }
      }
    }
    // e.name is the full href path; reduce to the child name relative to the collection
    std::string path = e.name;
    if(!path.empty() && path.back() == '/') path.pop_back();
    std::string leaf = path.substr(path.find_last_of('/') + 1);
    // skip the collection's own entry
    std::string selfpath = basepath;
    if(!selfpath.empty() && selfpath.back() == '/') selfpath.pop_back();
    if(leaf.empty() || path == selfpath || e.name == basepath)
      continue;
    e.name = e.isDir ? leaf + "/" : leaf;
    entries.push_back(e);
  }
  return true;
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

// URL for a "conflict copy" sibling, e.g. .../Note-conflict-20260627T113255.svgz
static std::string conflictUrl(const std::string& url)
{
  char ts[32];
  time_t now = time(NULL);
  strftime(ts, sizeof(ts), "%Y%m%dT%H%M%S", gmtime(&now));
  FSPath p(url);
  return p.parent().childPath(p.baseName() + "-conflict-" + ts + "." + p.extension());
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
  // If-Match guards against overwriting changes made on another device since we loaded/saved
  HttpResponse resp = req.put(this, size(), m_etag);
  seek(savedpos);

  if(resp.status == 412) {
    // the server copy changed under us (edited on another device). Don't overwrite it: save
    // our version as a separate conflict copy so neither side's work is lost.
    std::string curl = conflictUrl(m_url);
    HttpRequest creq(curl);
    if(!m_user.empty())
      creq.auth(m_user, m_pass);
    seek(0);
    HttpResponse cresp = creq.put(this, size());
    seek(savedpos);
    if(cresp.ok()) {
      m_dirty = false;
      m_offline = false;
      remove(shadowPath().c_str());
      ScribbleApp::app->showNotify(fstring(_("This note was changed on another device. Your version was saved as \"%s\" to avoid losing changes."),
          FSPath(curl).fileName().c_str()), 2);
      return true;
    }
    // couldn't even save the conflict copy: fall through to offline handling below
  }

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
