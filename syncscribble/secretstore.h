#ifndef SECRETSTORE_H
#define SECRETSTORE_H

// Stores secrets (WebDAV passwords) in the OS keychain via the freedesktop Secret Service
// (libsecret) - covers gnome-keyring (MATE) and KWallet's Secret Service backend (KDE).
// All functions are best-effort: available() reports whether a keychain is usable.

#include <string>

namespace SecretStore
{
  // is a Secret Service provider reachable (keychain unlocked/available)?
  bool available();

  // store/replace the secret for the given account key (we use the WebDAV URL); returns success
  bool store(const std::string& account, const std::string& secret);

  // look up the secret for an account; returns "" if absent or keychain unavailable
  std::string lookup(const std::string& account);

  // remove a stored secret; returns success (or true if nothing to remove)
  bool clear(const std::string& account);
}

#endif // SECRETSTORE_H
