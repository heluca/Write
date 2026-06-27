#include "secretstore.h"

// Secret Service (libsecret) is a Linux desktop facility; other platforms get no-op stubs
// (Android uses its own keystore - a future phase; iOS/Win/Mac keychains likewise TBD).
#ifndef WEBDAV_USE_LIBSECRET

bool SecretStore::available() { return false; }
bool SecretStore::store(const std::string&, const std::string&) { return false; }
std::string SecretStore::lookup(const std::string&) { return std::string(); }
bool SecretStore::clear(const std::string&) { return false; }

#else
#include <libsecret/secret.h>

// schema for Write's WebDAV passwords; "account" attribute holds the server URL
static const SecretSchema* writeSchema()
{
  static const SecretSchema schema = {
    "com.styluslabs.write.webdav", SECRET_SCHEMA_NONE,
    {
      { "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
      { NULL, (SecretSchemaAttributeType)0 }
    },
    0, 0, 0, 0, 0, 0, 0, 0  // reserved fields
  };
  return &schema;
}

bool SecretStore::available()
{
  GError* error = NULL;
  SecretService* svc = secret_service_get_sync(SECRET_SERVICE_NONE, NULL, &error);
  if(error) { g_error_free(error); return false; }
  if(svc) { g_object_unref(svc); return true; }
  return false;
}

bool SecretStore::store(const std::string& account, const std::string& secret)
{
  GError* error = NULL;
  std::string label = "Write WebDAV (" + account + ")";
  gboolean ok = secret_password_store_sync(writeSchema(), SECRET_COLLECTION_DEFAULT,
      label.c_str(), secret.c_str(), NULL, &error, "account", account.c_str(), NULL);
  if(error) { g_error_free(error); return false; }
  return ok;
}

std::string SecretStore::lookup(const std::string& account)
{
  GError* error = NULL;
  gchar* pw = secret_password_lookup_sync(writeSchema(), NULL, &error,
      "account", account.c_str(), NULL);
  if(error) { g_error_free(error); return std::string(); }
  if(!pw) return std::string();
  std::string result(pw);
  secret_password_free(pw);
  return result;
}

bool SecretStore::clear(const std::string& account)
{
  GError* error = NULL;
  secret_password_clear_sync(writeSchema(), NULL, &error, "account", account.c_str(), NULL);
  if(error) { g_error_free(error); return false; }
  return true;
}

#endif // WEBDAV_USE_LIBSECRET
