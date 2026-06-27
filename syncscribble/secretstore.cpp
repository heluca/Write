#include "secretstore.h"

#if defined(__ANDROID__)
// --- Android: AES/GCM key in the AndroidKeyStore, via JNI to MainActivity ---
#include <jni.h>
#include <SDL.h>

namespace {
struct AndroidCall {
  JNIEnv* env = NULL;
  jobject activity = NULL;
  jclass clazz = NULL;
  jmethodID method = NULL;
  AndroidCall(const char* name, const char* sig) {
    env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if(!env) return;
    activity = (jobject)SDL_AndroidGetActivity();
    if(!activity) return;
    clazz = env->GetObjectClass(activity);
    if(clazz) method = env->GetMethodID(clazz, name, sig);
  }
  ~AndroidCall() {
    if(activity) env->DeleteLocalRef(activity);
    if(clazz) env->DeleteLocalRef(clazz);
  }
};
}

bool SecretStore::available()
{
  AndroidCall c("secretAvailable", "()Z");
  return c.method && c.env->CallBooleanMethod(c.activity, c.method);
}

bool SecretStore::store(const std::string& account, const std::string& secret)
{
  AndroidCall c("secretStore", "(Ljava/lang/String;Ljava/lang/String;)Z");
  if(!c.method) return false;
  jstring ja = c.env->NewStringUTF(account.c_str());
  jstring js = c.env->NewStringUTF(secret.c_str());
  jboolean ok = c.env->CallBooleanMethod(c.activity, c.method, ja, js);
  c.env->DeleteLocalRef(ja);  c.env->DeleteLocalRef(js);
  return ok;
}

std::string SecretStore::lookup(const std::string& account)
{
  AndroidCall c("secretLookup", "(Ljava/lang/String;)Ljava/lang/String;");
  if(!c.method) return std::string();
  jstring ja = c.env->NewStringUTF(account.c_str());
  jstring jr = (jstring)c.env->CallObjectMethod(c.activity, c.method, ja);
  c.env->DeleteLocalRef(ja);
  if(!jr) return std::string();
  const char* s = c.env->GetStringUTFChars(jr, NULL);
  std::string result(s ? s : "");
  if(s) c.env->ReleaseStringUTFChars(jr, s);
  c.env->DeleteLocalRef(jr);
  return result;
}

bool SecretStore::clear(const std::string& account)
{
  AndroidCall c("secretClear", "(Ljava/lang/String;)Z");
  if(!c.method) return false;
  jstring ja = c.env->NewStringUTF(account.c_str());
  jboolean ok = c.env->CallBooleanMethod(c.activity, c.method, ja);
  c.env->DeleteLocalRef(ja);
  return ok;
}

#elif !defined(WEBDAV_USE_LIBSECRET)
// Other platforms with no keychain backend wired yet: no-op stubs (iOS/Win/Mac keychains TBD)
bool SecretStore::available() { return false; }
bool SecretStore::store(const std::string&, const std::string&) { return false; }
std::string SecretStore::lookup(const std::string&) { return std::string(); }
bool SecretStore::clear(const std::string&) { return false; }

#else
// --- Linux desktop: freedesktop Secret Service (libsecret) ---
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
