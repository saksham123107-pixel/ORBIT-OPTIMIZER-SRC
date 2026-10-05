// PRIMEx Optimizer authentication: Discord OAuth2 (PKCE + localhost) and KeyAuth.
// No HWID is used anywhere in authorization.
#include "auth.h"
#include "crypto.h"
#include "reg.h"
#include "util.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace px { namespace auth { namespace {

std::mutex g_mu;

std::string FieldStr(const nlohmann::json &j, const char *k) {
  if (!j.contains(k) || j[k].is_null()) return "";
  if (j[k].is_string()) return j[k].get<std::string>();
  if (j[k].is_number_unsigned()) return std::to_string(j[k].get<unsigned long long>());
  if (j[k].is_number_integer()) return std::to_string(j[k].get<long long>());
  if (j[k].is_number_float()) return std::to_string(j[k].get<double>());
  if (j[k].is_boolean()) return j[k].get<bool>() ? "true" : "false";
  return "";
}
long long FieldInt(const nlohmann::json &j, const char *k) {
  try {
    if (j.contains(k)) {
      if (j[k].is_number_integer()) return j[k].get<long long>();
      if (j[k].is_number_unsigned()) return (long long)j[k].get<unsigned long long>();
      if (j[k].is_string()) return std::stoll(j[k].get<std::string>());
    }
  } catch (...) {}
  return 0;
}
std::string Lower(std::string s) { for (char &c : s) c=(char)tolower((unsigned char)c); return s; }
std::string Trim(std::string s) { while(!s.empty()&&isspace((unsigned char)s.front()))s.erase(s.begin()); while(!s.empty()&&isspace((unsigned char)s.back()))s.pop_back(); return s; }
long long NowUnix() { return (long long)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string FormatLeft(long long seconds) {
  if(seconds<0) seconds=0; long long d=seconds/86400,h=(seconds%86400)/3600,m=(seconds%3600)/60; char b[64]{};
  if(d) snprintf(b,sizeof(b),"%lldd %02lldh",d,h); else snprintf(b,sizeof(b),"%02lldh %02lldm",h,m); return b;
}
std::string UrlEncode(const std::string &s) { std::string o; char b[4]{}; for(unsigned char c:s){ if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')o.push_back((char)c); else {snprintf(b,sizeof(b),"%%%02X",c);o+=b;} } return o; }
std::string Base64Url(const std::vector<unsigned char>& in) {
  static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o; int val=0,bits=-6;
  for(unsigned char c:in){ val=(val<<8)|c; bits+=8; while(bits>=0){o.push_back(t[(val>>bits)&0x3F]);bits-=6;} }
  if(bits>-6)o.push_back(t[((val<<8)>>(bits+8))&0x3F]); while(o.size()%4)o.push_back('=');
  for(char &c:o){if(c=='+')c='-';else if(c=='/')c='_';} while(!o.empty()&&o.back()=='=')o.pop_back(); return o;
}
std::string RandomString(size_t n) { static const char a[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~"; std::random_device rd; std::mt19937_64 g(rd()); std::string s; s.reserve(n); for(size_t i=0;i<n;i++)s.push_back(a[g()%((sizeof(a)-1))]); return s; }
std::string PkceChallenge(const std::string &v) {
  HCRYPTPROV prov=0; HCRYPTHASH h=0; std::vector<unsigned char> d(32); DWORD len=32;
  if(!CryptAcquireContextW(&prov,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)) return "";
  if(!CryptCreateHash(prov,CALG_SHA_256,0,0,&h)){CryptReleaseContext(prov,0);return "";}
  CryptHashData(h,(const BYTE*)v.data(),(DWORD)v.size(),0); bool ok=CryptGetHashParam(h,HP_HASHVAL,d.data(),&len,0)!=FALSE; CryptDestroyHash(h); CryptReleaseContext(prov,0);
  if(!ok)return ""; d.resize(len); return Base64Url(d);
}

const char *kDiscordClientId = getenv("PRIMEX_DISCORD_CLIENT_ID") ? getenv("PRIMEX_DISCORD_CLIENT_ID") : "<CLIENT_ID>";
const char *kDiscordGuildId = getenv("PRIMEX_DISCORD_GUILD_ID") ? getenv("PRIMEX_DISCORD_GUILD_ID") : "<GUILD_ID>";
const char *kTrialRole = getenv("PRIMEX_TRIAL_ROLE") ? getenv("PRIMEX_TRIAL_ROLE") : "<TRIAL_ROLE_ID>";
const char *kPremiumRole = getenv("PRIMEX_PREMIUM_ROLE") ? getenv("PRIMEX_PREMIUM_ROLE") : "<PREMIUM_ROLE_ID>";
const int kOAuthPort = 48731;
const char *kOAuthRedirect = "http://127.0.0.1:48731/callback";

std::string Env(const char *name) { char b[8192]{}; DWORD n=GetEnvironmentVariableA(name,b,sizeof(b)); return n?std::string(b,n):std::string(); }
std::wstring ExeDir() { wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr,exe,MAX_PATH); wchar_t *p=wcsrchr(exe,L'\\'); if(p)*p=0; return exe; }

// auth.env is hex-embedded at build time (cmake/embed_auth.cmake) so the
// bot token ships inside the EXE — no sidecar auth.env on the end-user PC.
extern "C" const char kAuthEnvEmbeddedHex[];
extern "C" const std::size_t kAuthEnvEmbeddedLen;

static std::string HexDecode(const char *hex, std::size_t len) {
  std::string out;
  out.reserve(len / 2);
  auto nyb = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (std::size_t i = 0; i + 1 < len; i += 2) {
    int h = nyb(hex[i]), l = nyb(hex[i + 1]);
    if (h < 0 || l < 0) break;
    out.push_back((char)((h << 4) | l));
  }
  return out;
}

static std::string ParseAuthEnvText(const std::string &text) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t has = line.find('#');
    if (has != std::string::npos) line.erase(has);
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = Trim(line.substr(0, eq));
    if (k == "PRIMEX_DISCORD_BOT_TOKEN") {
      std::string v = Trim(line.substr(eq + 1));
      if (!v.empty() && (v.front() == '"' || v.front() == '\'')) v.erase(v.begin());
      if (!v.empty() && (v.back() == '"' || v.back() == '\'')) v.pop_back();
      v = Trim(v);
      if (!v.empty()) return v;
    }
  }
  return "";
}

std::string AuthEnvFile() {
  // 1) Embedded auth.env (preferred — no sidecar file required).
  // HexDecode expects the hex *string* length (2 chars per byte), not the
  // decoded byte count stored in kAuthEnvEmbeddedLen. Using the byte count
  // here truncated the token and caused Discord 401 Unauthorized.
  if (kAuthEnvEmbeddedLen > 0) {
    const std::size_t hexLen = std::strlen(kAuthEnvEmbeddedHex);
    std::string text = HexDecode(kAuthEnvEmbeddedHex, hexLen);
    std::string t = ParseAuthEnvText(text);
    if (!t.empty()) return t;
  }
  // 2) Optional override: auth.env beside the EXE (dev builds only).
  std::string path = Narrow(ExeDir()) + "\\auth.env";
  std::ifstream in(path, std::ios::binary);
  if (in.is_open()) {
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    std::string t = ParseAuthEnvText(text);
    if (!t.empty()) return t;
  }
  return "";
}
std::string DiscordBotToken() { std::string t=AuthEnvFile(); if(!t.empty())return t; return Env("PRIMEX_DISCORD_BOT_TOKEN"); }

std::string DiscordAvatarUrl(const nlohmann::json &user) {
  std::string uid=FieldStr(user,"id"), hash=FieldStr(user,"avatar");
  if(uid.empty())return "";
  if(!hash.empty())return "https://cdn.discordapp.com/avatars/"+uid+"/"+hash+".png?size=128";
  int idx=0;
  try{idx=(int)((std::stoull(uid)>>22)%6);}catch(...){}
  return "https://cdn.discordapp.com/embed/avatars/"+std::to_string(idx)+".png";
}

bool HttpRequest(const wchar_t *method,const std::string &host,const std::string &path,const std::string &headers,const std::string &body,std::string &out,DWORD &status,std::string &err) {
  out.clear(); status=0; HINTERNET s=WinHttpOpen(L"PRIMEx Optimizer/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0); if(!s){err="WinHTTP unavailable.";return false;}
  WinHttpSetTimeouts(s,8000,8000,15000,15000); HINTERNET c=WinHttpConnect(s,Widen(host).c_str(),INTERNET_DEFAULT_HTTPS_PORT,0); if(!c){WinHttpCloseHandle(s);err="Network connection failed.";return false;}
  HINTERNET r=WinHttpOpenRequest(c,method,Widen(path).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE); if(!r){WinHttpCloseHandle(c);WinHttpCloseHandle(s);err="HTTP request failed.";return false;}
  BOOL ok=WinHttpSendRequest(r,headers.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:Widen(headers).c_str(),headers.empty()?0:(DWORD)-1L,body.empty()?WINHTTP_NO_REQUEST_DATA:(LPVOID)body.data(),(DWORD)body.size(),(DWORD)body.size(),0);
  if(!ok||!WinHttpReceiveResponse(r,nullptr)){WinHttpCloseHandle(r);WinHttpCloseHandle(c);WinHttpCloseHandle(s);err="Network request failed.";return false;}
  DWORD sz=sizeof(status); WinHttpQueryHeaders(r,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&sz,WINHTTP_NO_HEADER_INDEX);
  DWORD avail=0,rd=0; do{if(!WinHttpQueryDataAvailable(r,&avail)||!avail)break;std::string chunk(avail,'\0');if(!WinHttpReadData(r,chunk.data(),avail,&rd))break;out.append(chunk.data(),rd);}while(avail);
  WinHttpCloseHandle(r);WinHttpCloseHandle(c);WinHttpCloseHandle(s); return true;
}

bool JsonHttp(const char *method,const std::string &path,const std::string &token,const std::string &body,nlohmann::json &j,std::string &err,bool botAuth=false) {
  std::string out; DWORD st=0; std::string hdr=std::string("Authorization: ")+(botAuth?"Bot ":"Bearer ")+token+"\r\n"; if(!body.empty())hdr+="Content-Type: application/json\r\n";
  if(!HttpRequest(Widen(method).c_str(),"discord.com",path,hdr,body,out,st,err))return false;
  try{j=nlohmann::json::parse(out);}catch(...){err="Discord returned an invalid response.";return false;} if(st<200||st>=300){err=FieldStr(j,"message");if(err.empty())err="Discord API error (HTTP "+std::to_string(st)+").";return false;} return true;
}

// ── auth.json store (same layout as shell AuthStore) ───────────────────────
// %APPDATA%\PRIMEx Optimizer\auth.json — each section is
// DPAPI(CurrentUser) + HWID entropy {v,hwid,savedUtc,keyauth,discord}
std::wstring AuthJsonPath() {
  wchar_t buf[MAX_PATH]{};
  DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
  std::wstring dir = (n > 0 && n < MAX_PATH)
                         ? std::wstring(buf) + L"\\PRIMEx Optimizer"
                         : L".\\PRIMEx Optimizer";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\auth.json";
}

std::string LocalHwidAuth() {
  std::string sid;
  HANDLE tok = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
    DWORD sz = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &sz);
    if (sz) {
      std::vector<BYTE> ub(sz);
      DWORD got = 0;
      if (GetTokenInformation(tok, TokenUser, ub.data(), sz, &got)) {
        LPSTR str = nullptr;
        if (ConvertSidToStringSidA(((TOKEN_USER *)ub.data())->User.Sid, &str)) {
          sid = str;
          LocalFree(str);
        }
      }
    }
    CloseHandle(tok);
  }
  std::string machine;
  HKEY k = nullptr;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0,
                    KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
    char mb[128]{};
    DWORD cb = sizeof(mb), t = 0;
    if (RegQueryValueExA(k, "MachineGuid", nullptr, &t, (LPBYTE)mb, &cb) ==
            ERROR_SUCCESS &&
        t == REG_SZ)
      machine = mb;
    RegCloseKey(k);
  }
  if (machine.empty()) {
    char name[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD cn = sizeof(name);
    if (GetComputerNameA(name, &cn)) machine = name;
  }
  if (sid.empty()) sid = machine.empty() ? "PRIMEx" : machine;
  if (machine.empty()) machine = "PRIMEx";
  return sid + "::" + machine;
}

std::vector<uint8_t> HwidEntropyAuth() {
  std::string s = "PRIMEx::" + LocalHwidAuth();
  return std::vector<uint8_t>(s.begin(), s.end());
}

bool ProtectWithEntropy(const std::string &plain, std::vector<uint8_t> &enc) {
  auto ent = HwidEntropyAuth();
  DATA_BLOB in{(DWORD)plain.size(), (BYTE *)plain.data()};
  DATA_BLOB entropy{(DWORD)ent.size(), ent.empty() ? nullptr : ent.data()};
  DATA_BLOB out{};
  if (!CryptProtectData(&in, nullptr, &entropy, nullptr, nullptr, 0, &out))
    return false;
  enc.assign(out.pbData, out.pbData + out.cbData);
  LocalFree(out.pbData);
  return true;
}

bool UnprotectWithEntropy(const std::vector<uint8_t> &enc, std::string &plain) {
  auto ent = HwidEntropyAuth();
  DATA_BLOB in{(DWORD)enc.size(), (BYTE *)const_cast<uint8_t *>(enc.data())};
  DATA_BLOB entropy{(DWORD)ent.size(), ent.empty() ? nullptr : ent.data()};
  DATA_BLOB out{};
  if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, 0, &out))
    return false;
  plain.assign((char *)out.pbData, out.cbData);
  LocalFree(out.pbData);
  return true;
}

std::string B64Encode(const std::vector<uint8_t> &data) {
  if (data.empty()) return {};
  DWORD n = 0;
  CryptBinaryToStringA(data.data(), (DWORD)data.size(),
                       CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
  if (!n) return {};
  std::string s(n, '\0');
  if (!CryptBinaryToStringA(data.data(), (DWORD)data.size(),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &s[0],
                            &n))
    return {};
  s.resize(n);
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == '\0'))
    s.pop_back();
  return s;
}

bool B64Decode(const std::string &s, std::vector<uint8_t> &out) {
  if (s.empty()) return false;
  DWORD n = 0;
  if (!CryptStringToBinaryA(s.c_str(), (DWORD)s.size(), CRYPT_STRING_BASE64,
                            nullptr, &n, nullptr, nullptr) ||
      !n)
    return false;
  out.resize(n);
  if (!CryptStringToBinaryA(s.c_str(), (DWORD)s.size(), CRYPT_STRING_BASE64,
                            out.data(), &n, nullptr, nullptr))
    return false;
  out.resize(n);
  return true;
}

bool ReadAuthRoot(nlohmann::json &root) {
  std::ifstream f(AuthJsonPath().c_str(), std::ios::binary);
  if (!f) return false;
  try {
    root = nlohmann::json::parse(std::istreambuf_iterator<char>(f),
                                 std::istreambuf_iterator<char>());
    return root.is_object();
  } catch (...) {
    root = nlohmann::json::object();
    return false;
  }
}

void WriteAuthRoot(const nlohmann::json &root) {
  std::ofstream f(AuthJsonPath().c_str(), std::ios::binary | std::ios::trunc);
  if (!f) return;
  f << root.dump();
}

bool SaveAuthSection(const char *section, const std::string &plain) {
  if (!section || plain.empty()) return false;
  std::vector<uint8_t> enc;
  if (!ProtectWithEntropy(plain, enc)) return false;
  nlohmann::json root = nlohmann::json::object();
  ReadAuthRoot(root);
  if (!root.is_object()) root = nlohmann::json::object();
  root["v"] = 1;
  root["hwid"] = LocalHwidAuth();
  root["savedUtc"] = (long long)std::chrono::duration_cast<
                         std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  root[section] = {{"enc", B64Encode(enc)}};
  WriteAuthRoot(root);
  return true;
}

bool LoadAuthSection(const char *section, std::string &out) {
  if (!section) return false;
  nlohmann::json root;
  if (!ReadAuthRoot(root)) return false;
  if (!root.contains(section) || !root[section].is_object()) return false;
  if (!root[section].contains("enc") || !root[section]["enc"].is_string())
    return false;
  std::vector<uint8_t> enc;
  if (!B64Decode(root[section]["enc"].get<std::string>(), enc)) return false;
  return UnprotectWithEntropy(enc, out);
}

void DeleteAuthSection(const char *section) {
  if (!section) return;
  nlohmann::json root;
  if (!ReadAuthRoot(root)) return;
  if (root.erase(section)) WriteAuthRoot(root);
}

bool AuthHasSection(const char *section) {
  std::string tmp;
  return LoadAuthSection(section, tmp);
}

void MigrateLegacyBlobs() {
  auto try_one = [&](const std::wstring &path, const char *section) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(h, nullptr);
    std::vector<uint8_t> enc(sz ? sz : 1);
    DWORD r = 0;
    BOOL ok = ReadFile(h, enc.data(), sz, &r, nullptr);
    CloseHandle(h);
    if (ok && r == sz && sz) {
      // legacy blob was DPAPI without entropy
      DATA_BLOB in{(DWORD)enc.size(), (BYTE *)enc.data()}, out{};
      if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        std::string plain((char *)out.pbData, out.cbData);
        LocalFree(out.pbData);
        SaveAuthSection(section, plain);
      }
    }
    DeleteFileW(path.c_str());
  };

  if (!AuthHasSection("keyauth"))
    try_one(AppDataDir() + L"\\session.dat", "keyauth");

  if (!AuthHasSection("discord")) {
    try_one(AppDataDir() + L"\\discord-session.dat", "discord");
    wchar_t app[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
      try_one(std::wstring(app) + L"\\PRIMEx Optimizer\\discord-session.dat",
              "discord");
  }
}

bool ProtectText(const std::string &plain,std::vector<uint8_t>&enc){return ProtectWithEntropy(plain,enc);}
bool UnprotectText(const std::vector<uint8_t>&enc,std::string&plain){return UnprotectWithEntropy(enc,plain);}
bool SaveDiscordSession(const nlohmann::json &j){return SaveAuthSection("discord", j.dump());}
bool LoadDiscordSession(nlohmann::json&j){std::string p;if(!LoadAuthSection("discord",p))return false;try{j=nlohmann::json::parse(p);return true;}catch(...){return false;}}
void WipeDiscordSession(){DeleteAuthSection("discord");}

struct OAuthResult { bool ok=false; std::string code,state,error; };
OAuthResult WaitForOAuth(const std::string &expectedState) {
  OAuthResult res; SOCKET ls=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); if(ls==INVALID_SOCKET){res.error="Could not start localhost authorization server.";return res;}
  sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(kOAuthPort); if(bind(ls,(sockaddr*)&a,sizeof(a))==SOCKET_ERROR||listen(ls,1)==SOCKET_ERROR){closesocket(ls);res.error="Local authorization port 48731 is unavailable. Add the PRIMEx redirect URI and close any app using this port.";return res;}
  SOCKET c=accept(ls,nullptr,nullptr); if(c==INVALID_SOCKET){closesocket(ls);res.error="Authorization callback failed.";return res;}
  char b[16384]{};int n=recv(c,b,sizeof(b)-1,0);if(n>0){b[n]=0;std::string req(b);auto q=req.find("GET ");auto sp=req.find(' ',q+4);std::string target=q!=std::string::npos&&sp!=std::string::npos?req.substr(q+4,sp-q-4):"";auto qm=target.find('?');std::string qs=qm==std::string::npos?"":target.substr(qm+1);
    auto get=[&](const std::string&k){auto p=qs.find(k+"=");if(p==std::string::npos)return std::string();p+=k.size()+1;auto e=qs.find('&',p);std::string v=qs.substr(p,e==std::string::npos?std::string::npos:e-p);std::string o;for(size_t i=0;i<v.size();){if(v[i]=='%'&&i+2<v.size()){char x[3]={v[i+1],v[i+2],0};o.push_back((char)strtol(x,nullptr,16));i+=3;}else if(v[i]=='+'){o.push_back(' ');i++;}else{o.push_back(v[i++]);}}return o;};
    res.code=get("code");res.state=get("state");if(res.state!=expectedState)res.error="Invalid OAuth state.";else if(res.code.empty())res.error=get("error");else res.ok=true;
  }
  const char *html="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><body style='font-family:Segoe UI;background:#111;color:#fff;text-align:center;padding:80px'><h2>PRIMEx authorization complete</h2><p>You can close this window and return to PRIMEx.</p></body></html>";send(c,html,(int)strlen(html),0);closesocket(c);closesocket(ls);return res;
}

bool DiscordTokenRequest(const std::string &grant,const std::string &value,const std::string &verifier,nlohmann::json &j,std::string &err) {
  std::string form="client_id="+UrlEncode(kDiscordClientId)+"&grant_type="+UrlEncode(grant); if(grant=="authorization_code")form+="&code="+UrlEncode(value)+"&redirect_uri="+UrlEncode(kOAuthRedirect)+"&code_verifier="+UrlEncode(verifier);else form+="&refresh_token="+UrlEncode(value);
  std::string out;DWORD st=0; if(!HttpRequest(L"POST","discord.com","/api/oauth2/token","Content-Type: application/x-www-form-urlencoded\r\n",form,out,st,err))return false;try{j=nlohmann::json::parse(out);}catch(...){err="Discord token response was invalid.";return false;}if(st<200||st>=300){err=FieldStr(j,"error_description");if(err.empty())err=FieldStr(j,"message");if(err.empty())err="Discord authorization failed.";return false;}return true;
}

bool FetchDiscordMember(const std::string &userToken,const std::string &userId,nlohmann::json&member,std::string&err) {
  // Try bot token first (for JoinGuild-style checks), then user token with guilds.members.read.
  std::string bot=DiscordBotToken();
  if(!bot.empty()){
    if(JsonHttp("GET",std::string("/api/v10/guilds/")+kDiscordGuildId+"/members/"+userId,bot,"",member,err,true))
      return true;
  }
  if(!userToken.empty()){
    if(JsonHttp("GET",std::string("/api/v10/users/@me/guilds/")+kDiscordGuildId+"/member",userToken,"",member,err))
      return true;
  }
  if(bot.empty()){err="PRIMEX_DISCORD_BOT_TOKEN is not configured. Set the bot token on the trusted auth host.";return false;}
  return false;
}
bool JoinGuild(const std::string &userToken,const std::string &userId,std::string&err) {
  std::string bot=DiscordBotToken(); if(bot.empty()){err="PRIMEX_DISCORD_BOT_TOKEN is not configured. The server cannot add new members until the trusted auth host is configured.";return false;}
  nlohmann::json body={{"access_token",userToken}}; nlohmann::json dummy; return JsonHttp("PUT",std::string("/api/v10/guilds/")+kDiscordGuildId+"/members/"+userId,bot,body.dump(),dummy,err,true);
}

bool ExchangeAndAuthorize(const std::string &code,const std::string &verifier,Session &out,std::string&err,const nlohmann::json* oldSession=nullptr) {
  nlohmann::json tok; if(!DiscordTokenRequest("authorization_code",code,verifier,tok,err))return false; std::string access=FieldStr(tok,"access_token"),refresh=FieldStr(tok,"refresh_token"); if(access.empty()||refresh.empty()){err="Discord did not return a usable session.";return false;}
  nlohmann::json user; if(!JsonHttp("GET","/api/users/@me",access,"",user,err))return false; std::string uid=FieldStr(user,"id"); if(uid.empty()){err="Discord user identity is unavailable.";return false;}
  nlohmann::json member; std::string merr; bool memberOk=FetchDiscordMember(access,uid,member,merr); if(!memberOk){ if(merr.find("10007")!=std::string::npos || merr.find("Unknown Member")!=std::string::npos){if(!JoinGuild(access,uid,err))return false; memberOk=FetchDiscordMember(access,uid,member,err);} else {err=merr;return false;} }
  if(!memberOk)return false;
  bool premium=false,trial=false; if(member.contains("roles")&&member["roles"].is_array())for(auto&r:member["roles"]){std::string id=FieldStr(r,"id"); if(r.is_string())id=r.get<std::string>(); if(id==kPremiumRole)premium=true;if(id==kTrialRole)trial=true;}
  long long now=NowUnix(), expiry=0; std::string tier,label,left;
  nlohmann::json saved=oldSession?*oldSession:nlohmann::json::object();
  if(premium){tier="Lifetime";label="LIFETIME PREMIUM";left="Lifetime";expiry=0;}
  else if(trial){tier="Freemium";label="FREEMIUM LIFETIME";left="Lifetime";expiry=0;saved["trialExpiry"]=0;}
  else {err="ROLE_REQUIRED::Your Discord account is connected, but you do not have a PRIMEx access role.";return false;}
  saved["userId"]=uid;saved["username"]=FieldStr(user,"global_name").empty()?FieldStr(user,"username"):FieldStr(user,"global_name");saved["avatar"]=DiscordAvatarUrl(user);saved["refreshToken"]=refresh;saved["savedUtc"]=now;saved["tier"]=tier;saved["expiryUnix"]=expiry;
  if(!SaveDiscordSession(saved)){err="Could not save the encrypted Discord session.";return false;}
  out=Session{};out.valid=true;out.source="discord";out.username=saved.value("username",uid);out.avatar=saved.value("avatar","");out.tier=tier;out.tierLabel=label;out.timeLeft=left;out.expiryUnix=expiry;out.grace=false;return true;
}

bool RefreshDiscord(Session&out,std::string&err){
  nlohmann::json saved;if(!LoadDiscordSession(saved))return false;std::string refresh=FieldStr(saved,"refreshToken");if(refresh.empty())return false; nlohmann::json tok;if(!DiscordTokenRequest("refresh_token",refresh,"",tok,err))return false;std::string access=FieldStr(tok,"access_token"),newRefresh=FieldStr(tok,"refresh_token");if(access.empty())return false;if(newRefresh.empty())newRefresh=refresh;
  if(newRefresh!=refresh){saved["refreshToken"]=newRefresh;SaveDiscordSession(saved);}
  std::string uid=FieldStr(saved,"userId");if(uid.empty())return false;
  nlohmann::json member; if(!FetchDiscordMember(access,uid,member,err))return false;bool premium=false,trial=false;if(member.contains("roles")&&member["roles"].is_array())for(auto&r:member["roles"]){std::string id=r.is_string()?r.get<std::string>():FieldStr(r,"id");if(id==kPremiumRole)premium=true;if(id==kTrialRole)trial=true;}
  long long now=NowUnix(); if(premium){saved["tier"]="Lifetime";saved["tierLabel"]="LIFETIME PREMIUM";saved["trialExpiry"]=0;saved["expiryUnix"]=0;out.tier="Lifetime";out.tierLabel="LIFETIME PREMIUM";out.timeLeft="Lifetime";out.expiryUnix=0;}else if(trial){saved["tier"]="Freemium";saved["tierLabel"]="FREEMIUM LIFETIME";saved["trialExpiry"]=0;saved["expiryUnix"]=0;out.tier="Freemium";out.tierLabel="FREEMIUM LIFETIME";out.timeLeft="Lifetime";out.expiryUnix=0;}else{err="ROLE_REQUIRED::Your PRIMEx Discord access role is missing.";return false;}
  saved["refreshToken"]=newRefresh;saved["savedUtc"]=now;SaveDiscordSession(saved);out.valid=true;out.source="discord";out.username=saved.value("username",uid);out.avatar=saved.value("avatar","");out.grace=false;return true;
}

// Restore last Discord session without hitting Discord (offline/blip). Keeps
// the user out of the login screen when the network or API hiccups.
bool RestoreDiscordOffline(Session& out, std::string& err) {
  nlohmann::json saved;
  if (!LoadDiscordSession(saved)) return false;
  if (FieldStr(saved, "refreshToken").empty()) return false;
  std::string tier = saved.value("tier", "");
  if (tier == "Lifetime") {
    out.tier = "Lifetime";
    out.tierLabel = "LIFETIME PREMIUM";
    out.timeLeft = "Lifetime";
    out.expiryUnix = 0;
  } else {
    // Freemium (formerly "Trial"): free tweaks only, no expiry.
    out.tier = "Freemium";
    out.tierLabel = "FREEMIUM LIFETIME";
    out.timeLeft = "Lifetime";
    out.expiryUnix = 0;
  }
  out.valid = true;
  out.source = "discord";
  out.username = saved.value("username", "");
  out.avatar = saved.value("avatar", "");
  out.grace = true;
  return true;
}

Session g_current; bool g_initDone=false,g_initOk=false,g_networkOk=false,g_versionMismatch=false;std::string g_initError;std::string g_oauthVerifier;

bool StartDiscordLogin(Session&out,std::string&err){WSADATA wd{};if(WSAStartup(MAKEWORD(2,2),&wd)!=0){err="Could not start Windows networking.";return false;}std::string state=RandomString(32);g_oauthVerifier=RandomString(64);std::string challenge=PkceChallenge(g_oauthVerifier);if(challenge.empty()){WSACleanup();err="Could not initialize OAuth security.";return false;}  std::string url="https://discord.com/oauth2/authorize?client_id="+UrlEncode(kDiscordClientId)+"&response_type=code&redirect_uri="+UrlEncode(kOAuthRedirect)+"&scope=identify%20guilds.join%20guilds.members.read&state="+UrlEncode(state)+"&code_challenge_method=S256&code_challenge="+UrlEncode(challenge);ShellExecuteW(nullptr,L"open",Widen(url).c_str(),nullptr,nullptr,SW_SHOWNORMAL);OAuthResult r=WaitForOAuth(state);WSACleanup();if(!r.ok){err=r.error.empty()?"Discord authorization was cancelled.":r.error;return false;}return ExchangeAndAuthorize(r.code,g_oauthVerifier,out,err,nullptr);}

// ────────────────────────────────────────────────────────────────────────────
// B) KeyAuth 1.3 backend (call under g_mu)
// ────────────────────────────────────────────────────────────────────────────
const char *kAppName = "PRIMEx";
const char *kOwnerId = getenv("PRIMEX_KEYAUTH_OWNER_ID") ? getenv("PRIMEX_KEYAUTH_OWNER_ID") : "<OWNER_ID>";
const char *kVersion = "1.0";
const wchar_t *kKaHost = L"keyauth.win";
const wchar_t *kKaPath = L"/api/1.3/";

bool g_kaInitDone = false;
bool g_kaInitOk = false;
std::string g_kaSessionId;
nlohmann::json g_kaUserInfo = nlohmann::json::object();

std::string Md5Exe() {
  wchar_t exe[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  HANDLE h = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return "00";
  HCRYPTPROV prov = 0;
  HCRYPTHASH hash = 0;
  std::string out = "00";
  if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL,
                           CRYPT_VERIFYCONTEXT)) {
    if (CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
      uint8_t buf[8192]{};
      DWORD n = 0;
      BOOL ok = TRUE;
      while (ok && ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0)
        ok = CryptHashData(hash, buf, n, 0);
      uint8_t dig[16]{};
      DWORD len = sizeof(dig);
      if (CryptGetHashParam(hash, HP_HASHVAL, dig, &len, 0)) {
        char b[33]{};
        for (int i = 0; i < 16; ++i)
          snprintf(b + i * 2, 3, "%02x", dig[i]);
        out = b;
      }
      CryptDestroyHash(hash);
    }
    CryptReleaseContext(prov, 0);
  }
  CloseHandle(h);
  return out;
}

bool HttpPost(const std::vector<std::pair<std::string, std::string>> &fields,
              std::string &body, std::string &tsHeader, std::string &error) {
  body.clear();
  tsHeader.clear();
  std::string form;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i)
      form += "&";
    form += UrlEncode(fields[i].first) + "=" + UrlEncode(fields[i].second);
  }
  HINTERNET ses = WinHttpOpen(L"PRIMEx Optimizer/2.3",
                              WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) {
    error = "Could not reach auth servers (WinHTTP open failed).";
    return false;
  }
  WinHttpSetTimeouts(ses, 8000, 8000, 15000, 15000);
  HINTERNET con = WinHttpConnect(ses, kKaHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!con) {
    error = "Could not reach auth servers (connect failed).";
    WinHttpCloseHandle(ses);
    return false;
  }
  HINTERNET req = WinHttpOpenRequest(con, L"POST", kKaPath, nullptr,
                                     WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE);
  if (!req) {
    error = "Could not reach auth servers (request failed).";
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }
  std::wstring hdrs = L"Content-Type: application/x-www-form-urlencoded";
  BOOL sent = WinHttpSendRequest(req, hdrs.c_str(), (DWORD)hdrs.size(),
                                 (LPVOID)form.data(), (DWORD)form.size(),
                                 (DWORD)form.size(), 0);
  if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
    error = "Connection failure. Please try again (network unreachable).";
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }
  DWORD status = 0, sz = sizeof(status);
  WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                      WINHTTP_NO_HEADER_INDEX);
  if (status == 429) {
    error = "You're connecting too fast to loader, slow down.";
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return false;
  }
  wchar_t ts[64]{};
  DWORD tslen = sizeof(ts);
  if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"x-signature-timestamp",
                          ts, &tslen, WINHTTP_NO_HEADER_INDEX))
    tsHeader = Narrow(ts);
  std::string out;
  DWORD avail = 0, rd = 0;
  do {
    avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail))
      break;
    if (avail == 0)
      break;
    std::string chunk(avail, '\0');
    if (!WinHttpReadData(req, chunk.data(), avail, &rd))
      break;
    out.append(chunk.data(), rd);
  } while (avail > 0);
  body = out;
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  if (status < 200 || status >= 300) {
    error = "Connection failure. Please try again (http " +
            std::to_string(status) + ").";
    return false;
  }
  return true;
}

bool FreshEnough(const std::string &tsHeader) {
  if (tsHeader.empty())
    return true;
  long long ts = 0;
  try {
    ts = std::stoll(tsHeader);
  } catch (...) {
    return false;
  }
  long long now = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
  long long d = now - ts;
  if (d < 0)
    d = -d;
  return d <= 120;
}

struct Resp {
  bool success = false;
  std::string message;
  std::string sessionid;
  std::string download;
  nlohmann::json info = nlohmann::json::object();
};

bool CallKa(const std::vector<std::pair<std::string, std::string>> &fields,
            Resp &r, std::string &error) {
  std::string body, ts;
  if (!HttpPost(fields, body, ts, error))
    return false;
  if (!FreshEnough(ts)) {
    error = "Date/Time settings aren't synced on your device.";
    return false;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(body);
  } catch (...) {
    error = "Connection failure. Please try again (bad response).";
    return false;
  }
  if (FieldStr(j, "ownerid") != kOwnerId) {
    error = "Connection failure. Please try again (owner mismatch).";
    return false;
  }
  r.success = j.contains("success") && j["success"].is_boolean()
                  ? j["success"].get<bool>()
                  : false;
  r.message = FieldStr(j, "message");
  r.sessionid = FieldStr(j, "sessionid");
  r.download = FieldStr(j, "download");
  if (j.contains("info"))
    r.info = j["info"];
  return true;
}

int TierRank(const std::string &t) {
  std::string l;
  for (char c : t) l.push_back((char)tolower((unsigned char)c));
  if (l.find("lifetime") != std::string::npos ||
      l.find("life") != std::string::npos)
    return 3;
  if (l.find("month") != std::string::npos || l.find("30") != std::string::npos ||
      l.find("week") != std::string::npos)
    return 2;
  if (l.find("trial") != std::string::npos || l.find("free") != std::string::npos ||
      l.find("default") != std::string::npos || !l.empty())
    return 1;
  return 0;
}

Session SnapshotKa() {
  Session s;
  s.source = "keyauth";
  auto subs =
      g_kaUserInfo.contains("subscriptions") ? g_kaUserInfo["subscriptions"]
                                             : nlohmann::json::array();
  int best = 0;
  for (auto &sub : subs) {
    std::string name = FieldStr(sub, "subscription");
    int r = TierRank(name);
    if (r > best) {
      best = r;
      std::string l;
      for (char c : name) l.push_back((char)tolower((unsigned char)c));
      if (r == 3)
        s.tierLabel = "Lifetime";
      else if (r == 2)
        s.tierLabel = (l.find("week") != std::string::npos) ? "Weekly"
                                                            : "Monthly";
      else
        s.tierLabel = (l.find("trial") != std::string::npos ||
                       l.find("free") != std::string::npos ||
                       l.find("default") != std::string::npos)
                          ? "Free Trial"
                          : name;
      s.tier = (r == 3) ? "Lifetime" : (r == 2) ? "Monthly" : "Trial";
      s.timeLeft = FieldStr(sub, "timeleft");
      s.expiryUnix = FieldInt(sub, "expiry");
    }
  }
  s.username = FieldStr(g_kaUserInfo, "username");
  s.valid = (best != 0);
  if (!s.valid) {
    s.tier = "None";
    s.tierLabel = "No license";
  }
  return s;
}

bool IsBanned(const std::string &m) {
  std::string l;
  for (char c : m) l.push_back((char)tolower((unsigned char)c));
  return l.find("banned") != std::string::npos ||
         l.find("blacklisted") != std::string::npos ||
         l.find("blocked") != std::string::npos;
}
bool IsNetworkError(const std::string &m) {
  std::string l;
  for (char c : m) l.push_back((char)tolower((unsigned char)c));
  const char *k[] = {"network", "connection", "timed out", "timeout",
                     "unreachable", "socket", "dns", "host",
                     "could not reach", "failed to connect"};
  for (auto w : k)
    if (l.find(w) != std::string::npos)
      return true;
  return false;
}
std::string MapKeyError(const std::string &m) {
  std::string l;
  for (char c : m) l.push_back((char)tolower((unsigned char)c));
  if (l.find("banned") != std::string::npos)
    return "BANNED::" + m;
  if (l.find("used") != std::string::npos || l.find("redeemed") != std::string::npos)
    return "This key was already used.";
  if (l.find("invalid") != std::string::npos || l.find("not found") != std::string::npos ||
      l.find("exist") != std::string::npos)
    return "Invalid key. Check it and try again.";
  return m.empty() ? "Key activation failed." : m;
}

// ── KeyAuth session persistence (auth.json; DPAPI + HWID entropy) ──────────
void SaveKaSessionLocked(const std::string &user, const std::string &pass,
                         const Session &s) {
  nlohmann::json j = {{"username", user},
                      {"password", pass},
                      {"tier", s.tier},
                      {"tierLabel", s.tierLabel},
                      {"timeLeft", s.timeLeft},
                      {"expiryUnix", s.expiryUnix},
                      {"savedUtc",
                       (long long)std::chrono::duration_cast<
                           std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count()}};
  SaveAuthSection("keyauth", j.dump());
}
bool LoadKaSession(nlohmann::json &out) {
  std::string plain;
  if (!LoadAuthSection("keyauth", plain)) return false;
  try {
    out = nlohmann::json::parse(plain);
    return true;
  } catch (...) {
    return false;
  }
}
void WipeKaSession() { DeleteAuthSection("keyauth"); }

// ── unified init + auth state ──────────────────────────────────────────────
bool g_kaActive = false;

bool InitKeyAuthLocked() {
  if (g_kaInitDone)
    return g_kaInitOk;
  static bool migrated = false;
  if (!migrated) {
    migrated = true;
    MigrateLegacyBlobs();
  }
  Resp r;
  std::vector<std::pair<std::string, std::string>> f = {
      {"type", "init"},
      {"ver", kVersion},
      {"hash", Md5Exe()},
      {"name", kAppName},
      {"ownerid", kOwnerId}};
  std::string e;
  if (!CallKa(f, r, e)) {
    g_kaInitDone = true;
    g_kaInitOk = false;
    g_kaActive = false;
    return false;
  }
  if (!r.success) {
    std::string m = Lower(r.message);
    if (m.find("version") != std::string::npos ||
        m.find("update") != std::string::npos ||
        m.find("download") != std::string::npos)
      g_versionMismatch = true;
    g_kaInitDone = true;
    g_kaInitOk = false;
    g_kaActive = false;
    return false;
  }
  g_kaSessionId = r.sessionid;
  g_kaInitDone = true;
  g_kaInitOk = true;
  g_kaActive = true;
  return true;
}

bool KaLoginLocked(const std::string &user, const std::string &pass,
                   Session &out, std::string &error) {
  Resp r;
  if (!CallKa({{"type", "login"},
               {"username", user},
               {"pass", pass},
                              {"sessionid", g_kaSessionId},
               {"name", kAppName},
               {"ownerid", kOwnerId},
               {"code", ""}},
              r, error))
    return false;
  if (!r.success) {
    if (IsBanned(r.message))
      error = "BANNED::" + r.message;
    else
      error = r.message.empty() ? "Login failed." : r.message;
    return false;
  }
  g_kaUserInfo = r.info;
  Session s = SnapshotKa();
  s.grace = false;
  if (!s.valid) {
    error = "No active subscription on this account.";
    return false;
  }
  SaveKaSessionLocked(user, pass, s);
  out = s;
  return true;
}

bool KaRegisterLocked(const std::string &user, const std::string &pass,
                      Session &out, std::string &error) {
  Resp r;
  if (!CallKa({{"type", "register"},
               {"username", user},
               {"pass", pass},
               {"key", ""},
               {"email", ""},
                              {"sessionid", g_kaSessionId},
               {"name", kAppName},
               {"ownerid", kOwnerId}},
              r, error))
    return false;
  if (!r.success) {
    error = r.message.empty() ? "Registration failed." : r.message;
    return false;
  }
  g_kaUserInfo = r.info;
  Session s = SnapshotKa();
  s.grace = false;
  if (!s.valid) {
    error = "Registered, but no trial subscription was granted. Contact support.";
    return false;
  }
  SaveKaSessionLocked(user, pass, s);
  out = s;
  return true;
}

bool KaActivateLocked(const std::string &key, Session &out,
                      std::string &error) {
  Resp r;
  if (!CallKa({{"type", "license"},
               {"key", key},
                              {"sessionid", g_kaSessionId},
               {"name", kAppName},
               {"ownerid", kOwnerId},
               {"code", ""}},
              r, error))
    return false;
  if (!r.success) {
    error = MapKeyError(r.message);
    return false;
  }
  g_kaUserInfo = r.info;
  Session s = SnapshotKa();
  s.grace = false;
  if (!s.valid) {
    error = "Key accepted, but no active subscription found.";
    return false;
  }
  nlohmann::json stored;
  if (LoadKaSession(stored)) {
    stored["tier"] = s.tier;
    stored["tierLabel"] = s.tierLabel;
    stored["timeLeft"] = s.timeLeft;
    stored["expiryUnix"] = s.expiryUnix;
    stored["savedUtc"] = (long long)std::chrono::duration_cast<
                             std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    SaveAuthSection("keyauth", stored.dump());
  }
  out = s;
  return true;
}

bool KaRevalidateLocked(Session &out) {
  Resp r;
  std::string e;
  if (!CallKa({{"type", "check"},
               {"sessionid", g_kaSessionId},
               {"name", kAppName},
               {"ownerid", kOwnerId}},
              r, e)) {
    g_networkOk = false;
    out = g_current; // offline blip — UI retries next tick
    return true;
  }
  g_networkOk = true;
  if (!r.success) {
    g_current = Session{};
    out = g_current;
    return false;
  }
  bool g = g_current.grace;
  g_kaUserInfo = r.info;
  g_current = SnapshotKa();
  g_current.grace = g;
  out = g_current;
  return true;
}

} // namespace (anonymous)

nlohmann::json SessionJson(const Session &s) {
  return {{"valid",s.valid},{"source",s.source},{"username",s.username},{"avatar",s.avatar},{"tier",s.tier},{"tierLabel",s.tierLabel},{"timeLeft",s.timeLeft},{"expiry",s.expiryUnix},{"grace",s.grace}};
}

bool Init(std::string &error) {
  std::lock_guard<std::mutex> lk(g_mu); if(g_initDone){error=g_initError;return g_initOk;}
  bool ka=InitKeyAuthLocked(); g_networkOk=ka; g_initDone=true; g_initOk=ka || !DiscordBotToken().empty();
  if(!g_initOk) g_initError="Authentication is not configured. Set PRIMEX_DISCORD_BOT_TOKEN or configure KeyAuth."; else g_initError.clear(); error=g_initError; return g_initOk;
}
bool InitDone(){return g_initDone;} bool InitOk(){return g_initOk;} bool NetworkOk(){return g_networkOk;} bool VersionMismatch(){return g_versionMismatch;} std::string InitError(){return g_initError;}
Session Current(){std::lock_guard<std::mutex> lk(g_mu);return g_current;}

void Login(const std::string&,const std::string&,bool&ok,std::string&error,Session&out){ok=false;error="Password login has been removed. Use Discord OAuth2.";out=g_current;}
void KeyAuthLogin(const std::string&,const std::string&,bool&ok,std::string&error,Session&out){ok=false;error="Username/password login has been removed. Use KeyAuth license activation.";out=g_current;}
void Register(const std::string&,const std::string&,bool&ok,std::string&error,Session&out){ok=false;error="Registration is disabled. Activate a KeyAuth license or use Discord.";out=g_current;}

void Activate(const std::string &key,bool&ok,std::string&error,Session&out){std::lock_guard<std::mutex>lk(g_mu);ok=false;if(!g_kaActive)InitKeyAuthLocked();if(!g_kaActive){error="KeyAuth servers are unavailable.";out=g_current;return;}if(!KaActivateLocked(key,out,error)){out=g_current;return;}out.tier="Lifetime";out.tierLabel="LIFETIME PREMIUM";out.timeLeft="Lifetime";out.expiryUnix=0;out.valid=true;g_current=out;ok=true;}

void Logout(){std::lock_guard<std::mutex>lk(g_mu);if(g_current.source=="keyauth"&&g_kaActive){Resp r;std::string e;CallKa({{"type","logout"},{"sessionid",g_kaSessionId},{"name",kAppName},{"ownerid",kOwnerId}},r,e);g_kaUserInfo=nlohmann::json::object();}g_current=Session{};WipeDiscordSession();WipeKaSession();}

void Revalidate(Session&out){std::lock_guard<std::mutex>lk(g_mu);if(g_current.source=="discord"){std::string e;if(RefreshDiscord(out,e)){g_current=out;g_networkOk=true;return;}bool fatal=!e.empty()&&(e.find("ROLE_REQUIRED")!=std::string::npos||e.find("EXPIRED")!=std::string::npos||e.find("invalid_grant")!=std::string::npos||e.find("BANNED")!=std::string::npos);if(fatal){g_current=Session{};out=g_current;return;}if(!g_current.valid){Session s;if(RestoreDiscordOffline(s,e)){g_current=s;out=g_current;return;}}out=g_current;return;}if(g_current.source=="keyauth"){KaRevalidateLocked(out);g_current=out;return;}out=g_current;}

void AutoLogin(bool&ok,Session&out,std::string&note){std::lock_guard<std::mutex>lk(g_mu);ok=false;out=Session{};
  std::string e;if(RefreshDiscord(out,e)){g_current=out;ok=true;note="Discord session restored";return;}
  bool fatal=!e.empty()&&(e.find("ROLE_REQUIRED")!=std::string::npos||e.find("EXPIRED")!=std::string::npos||e.find("invalid_grant")!=std::string::npos||e.find("BANNED")!=std::string::npos);
  if(!fatal){Session s;if(RestoreDiscordOffline(s,e)){g_current=s;out=s;ok=true;note="Discord session restored (offline)";return;}}
  nlohmann::json stored;if(LoadKaSession(stored)&&!stored.value("username","").empty()){if(!g_kaActive)InitKeyAuthLocked();Session s;std::string ke;if(g_kaActive&&KaLoginLocked(stored.value("username",""),stored.value("password",""),s,ke)){s.tier="Lifetime";s.tierLabel="LIFETIME PREMIUM";s.timeLeft="Lifetime";s.expiryUnix=0;g_current=s;out=s;ok=true;note="KeyAuth session restored";return;}}
note="no saved session";}

void DiscordLogin(bool&ok,std::string&error,Session&out){std::lock_guard<std::mutex>lk(g_mu);ok=false;out=g_current; if(!StartDiscordLogin(out,error))return;g_current=out;ok=true;g_networkOk=true;}

} } // namespace px::auth
