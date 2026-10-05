// crypto.h — small crypto helpers for the PRIMEx engine.
// SHA-256 via CryptoAPI (CALG_SHA_256), string obfuscation via XOR.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

#include <string>

namespace px {

// Lowercase hex SHA-256 of a byte string. "" on failure (no CSP).
inline std::string Sha256Hex(const std::string &data) {
  HCRYPTPROV prov = 0;
  HCRYPTHASH hash = 0;
  std::string out(64, '\0');
  if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES,
                            CRYPT_VERIFYCONTEXT))
    return "";
  if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
    CryptReleaseContext(prov, 0);
    return "";
  }
  CryptHashData(hash, (const BYTE *)data.data(), (DWORD)data.size(), 0);
  BYTE dig[32]{};
  DWORD len = sizeof(dig);
  BOOL ok = CryptGetHashParam(hash, HP_HASHVAL, dig, &len, 0);
  if (ok) {
    static const char *d = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
      out[i * 2] = d[dig[i] >> 4];
      out[i * 2 + 1] = d[dig[i] & 15];
    }
  }
  CryptDestroyHash(hash);
  CryptReleaseContext(prov, 0);
  return ok ? out : std::string();
}

// Decode a compile-time XOR'd byte buffer (56+ bit-ish entropy against
// casual string scans; the real integrity comes from AUTH.json being
// server-authoritative, never from obscurity).
inline std::string XorDecode(const unsigned char *x, size_t n,
                             unsigned char key) {
  std::string s;
  s.reserve(n);
  for (size_t i = 0; i < n; ++i)
    s.push_back((char)(x[i] ^ key));
  return s;
}

} // namespace px