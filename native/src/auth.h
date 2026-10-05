// PRIMEx Optimizer authentication: Discord OAuth2 + KeyAuth. No HWID.
#pragma once
#include <string>
#include "json.hpp"

namespace px { namespace auth {
struct Session {
  bool valid=false;
  std::string source; // discord | keyauth
  std::string username;
  std::string avatar; // Discord CDN avatar URL (empty = letter fallback)
  std::string tier="None"; // None | Trial | Lifetime
  std::string tierLabel;
  std::string timeLeft;
  long long expiryUnix=0;
  std::string hwid; // always empty; retained only for ABI compatibility
  bool grace=false;
};
nlohmann::json SessionJson(const Session &s);
bool Init(std::string &error);
bool InitDone(); bool InitOk(); bool NetworkOk(); bool VersionMismatch(); std::string InitError();
void Login(const std::string&,const std::string&,bool&,std::string&,Session&);
void KeyAuthLogin(const std::string&,const std::string&,bool&,std::string&,Session&);
void Register(const std::string&,const std::string&,bool&,std::string&,Session&);
void Activate(const std::string&,bool&,std::string&,Session&);
void DiscordLogin(bool&,std::string&,Session&);
void Logout(); void Revalidate(Session&); void AutoLogin(bool&,Session&,std::string&);
Session Current();
} }
