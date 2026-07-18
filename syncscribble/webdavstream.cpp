#include "webdavstream.h"
#include "httpclient.h"
#include "scribbleapp.h"
#include "scribbleconfig.h"
#include "secretstore.h"
#include "ulib/stringutil.h"
#include "ulib/md5.h"
#include "pugixml.hpp"
#include <time.h>
#include <map>

// passwords entered for "prompt every session" mode (keychain off, plaintext off), keyed by URL
static std::map<std::string, std::string>& sessionPasswords()
{
  static std::map<std::string, std::string> s;
  return s;
}

void WebDavStream::setSessionPassword(const std::string& url, const std::string& pw)
{
  sessionPasswords()[url] = pw;
}

// --- WebDAV server registry: multiple servers, each with its own username + password ---
// Storage: two aligned ":::" config lists (webdavServers = base URLs, webdavServerUsers =
// usernames); passwords in the OS keychain keyed by the server URL (or plaintext/session
// per policy). The base URL always ends with '/'.

std::vector<std::string> WebDavStream::servers()
{
  std::vector<std::string> out;
  std::string s = ScribbleApp::cfg->String("webdavServers", "");
  if(!s.empty())
    for(StringRef part : splitStringRef(s, ":::"))
      if(!part.isEmpty())
        out.push_back(part.toString());
  return out;
}

std::string WebDavStream::serverUser(const std::string& serverUrl)
{
  std::vector<std::string> urls = servers();
  std::string usersStr = ScribbleApp::cfg->String("webdavServerUsers", "");
  std::vector<StringRef> users = splitStringRef(usersStr, ":::");
  for(size_t i = 0; i < urls.size() && i < users.size(); ++i)
    if(urls[i] == serverUrl)
      return users[i].toString();
  return std::string();
}

// the registered server whose base URL is a prefix of `path` (so a doc/folder URL maps to
// its server for credential lookup); empty if none match
std::string WebDavStream::serverForUrl(const std::string& path)
{
  std::string best;
  for(const std::string& s : servers()) {
    if(path.compare(0, s.size(), s) == 0 && s.size() > best.size())
      best = s;
  }
  return best;
}

void WebDavStream::addServer(const std::string& url, const std::string& user, const std::string& pw, bool savePw)
{
  std::string serverUrl = url;
  if(serverUrl.empty() || serverUrl.back() != '/')
    serverUrl += '/';
  std::vector<std::string> urls = servers();
  std::vector<StringRef> usersRef = splitStringRef(ScribbleApp::cfg->String("webdavServerUsers", ""), ":::");
  std::vector<std::string> users;
  for(auto& u : usersRef) users.push_back(u.toString());
  users.resize(urls.size());  // keep aligned

  auto it = std::find(urls.begin(), urls.end(), serverUrl);
  if(it == urls.end()) {
    urls.push_back(serverUrl);
    users.push_back(user);
  }
  else
    users[it - urls.begin()] = user;
  ScribbleApp::cfg->set("webdavServers", joinStr(urls, ":::").c_str());
  ScribbleApp::cfg->set("webdavServerUsers", joinStr(users, ":::").c_str());

  // password per policy
  if(!pw.empty()) {
    if(savePw && SecretStore::available())
      SecretStore::store(serverUrl, pw);
    else if(savePw && ScribbleApp::cfg->Int("webdavSavePlaintext", 0))
      ScribbleApp::cfg->set("webdavPassword", pw.c_str());  // single-slot plaintext fallback (dev)
    else
      setSessionPassword(serverUrl, pw);
  }
}

void WebDavStream::removeServer(const std::string& serverUrl)
{
  std::vector<std::string> urls = servers();
  std::vector<StringRef> usersRef = splitStringRef(ScribbleApp::cfg->String("webdavServerUsers", ""), ":::");
  std::vector<std::string> users;
  for(auto& u : usersRef) users.push_back(u.toString());
  users.resize(urls.size());
  auto it = std::find(urls.begin(), urls.end(), serverUrl);
  if(it != urls.end()) {
    users.erase(users.begin() + (it - urls.begin()));
    urls.erase(it);
    ScribbleApp::cfg->set("webdavServers", joinStr(urls, ":::").c_str());
    ScribbleApp::cfg->set("webdavServerUsers", joinStr(users, ":::").c_str());
    SecretStore::clear(serverUrl);
  }
}

// resolve the password for a server URL per the configured policy:
//  1. OS keychain (if available),  2. plaintext config (only if the user opted in),
//  3. a password entered earlier this session.  Empty result => caller should prompt.
std::string WebDavStream::password(const std::string& url)
{
  std::string server = serverForUrl(url);
  if(server.empty()) server = url;  // url may already be the server base
  if(SecretStore::available()) {
    std::string pw = SecretStore::lookup(server);
    if(!pw.empty())
      return pw;
  }
  if(ScribbleApp::cfg->Int("webdavSavePlaintext", 0)) {
    std::string pw = ScribbleApp::cfg->String("webdavPassword", "");
    if(!pw.empty())
      return pw;
  }
  auto it = sessionPasswords().find(server);
  return it != sessionPasswords().end() ? it->second : std::string();
}

// resolve username for a doc/folder/server URL: registered server's user, else legacy webdavUser
std::string WebDavStream::username(const std::string& url)
{
  std::string server = serverForUrl(url);
  if(!server.empty()) {
    std::string u = serverUser(server);
    if(!u.empty())
      return u;
  }
  return ScribbleApp::cfg->String("webdavUser", "");
}

bool WebDavStream::isWebDavUrl(const char* path)
{
  if(!path) return false;
  StringRef p(path);
  // explicit dav schemes, or any path under a registered server (or the legacy single webdavUrl)
  if(p.startsWith("dav://") || p.startsWith("davs://"))
    return true;
  if(!serverForUrl(path).empty())
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

bool webdavListDir(const char* url, std::vector<WebDavEntry>& entries, WebDavError* err)
{
  std::string httpurl = toHttpUrl(url);
  if(httpurl.empty() || httpurl.back() != '/')
    httpurl += '/';
  HttpRequest req(httpurl);
  std::string user = WebDavStream::username(httpurl);
  if(!user.empty())
    req.auth(user, WebDavStream::password(httpurl));
  MemStream body;
  HttpResponse resp = req.propfind(&body, 1);
  if(resp.status != 207) {
    if(err) { err->status = resp.status;  err->message = resp.error; }
    return false;
  }

  pugi::xml_document doc;
  if(!doc.load_buffer(body.data(), body.size())) {
    if(err) { err->status = resp.status;  err->message = "invalid PROPFIND response"; }
    return false;
  }
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
  m_user = username(m_url);
  m_pass = password(m_url);
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
