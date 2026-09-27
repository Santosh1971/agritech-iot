#include "myWiFiAPs.hpp"

#define DEBUG
#include "debug.hpp"

#include <string>

MyWiFiAPs::MyWiFiAPs(char* area, size_t len, OnChange onChange):
area(area), len(len), onChange(onChange) {
  parse([=](char* ssid, char* pwd) {
        aps.push_back(AP{String(ssid), String(pwd)});
        return true;
      });
}

bool MyWiFiAPs::find(std::function<bool (String& ssid, String& pwd)> f) {
for (auto& ap: aps) {
    if (f(ap.ssid, ap.pwd)) {
      TRACE("APs::find: break at", ap.ssid);
      return true;
  }
}
  return false;
}

#include <algorithm>    // std::find_if
bool MyWiFiAPs::add(String nSsid, String nPwd, Option option) {
auto iter = std::find_if(aps.begin(), aps.end(), [&nSsid](const AP& ap) {
      return ap.ssid == nSsid;
    });

if (iter != aps.end()) { // exists
  TRACE("APs::add:exists", nSsid, (iter - aps.begin()), option);
  if (iter->pwd == nPwd) {
    switch(option) {
    case Top:
      if (iter - aps.begin() == 0)
        return false;
      break;
    case Bottom:
      if (aps.end() - iter == 1)
        return false;
      break;
    case Any:
      return false;
    }
  }
  TRACE("APs::add:erase existing");
  aps.erase(iter);
}

switch(option) {
  case Top:
    aps.insert(aps.begin(), AP {nSsid, nPwd});
    break;
  default:
    aps.push_back({nSsid, nPwd});
    break;
}
TRACE("APs::add:added", nSsid);

saveChanges();

return true;
}

bool MyWiFiAPs::remove(String ssid) {
TRACE("APs::remove:", ssid);

auto iter = std::find_if(aps.begin(), aps.end(), [&ssid](const AP& ap) {
      return ap.ssid == ssid;
    });
if (iter == aps.end()) {
  WARN("remove::not found", ssid);
  return false;
}

aps.erase(iter);
saveChanges();
return true;
}

int MyWiFiAPs::count() {
return aps.size();
}

void MyWiFiAPs::dump() {
  PRINT("{");
  for (auto& ap: aps) {
    PRINT("{", ap.ssid, ap.pwd, "}");
  }
  PRINTLN("}");
}

void MyWiFiAPs::parse(std::function<bool (char* ssid, char* pwd)> cb) {
for (char* cur=area; cur < (area+len); ) {
    int l = ::strlen(cur);
    if (l == 0) // no more ssids
      break;

    char* pwd = cur + l + 1;
    int pl = ::strlen(pwd);

    TRACE("APs::parse::calling...", cur, pwd);
    if (!cb(cur, pwd))
      break;

    cur = pwd + pl + 1;
  }
}

void MyWiFiAPs::saveChanges() {
  buildArea(aps);
  TRACE("APs::saving AP Changes");
  if (onChange)
    onChange();
}

int MyWiFiAPs::buildArea(APs& input) {
  TRACE("APs::buildArea");
  ::memset(area, '\0', len);
  char* cur = area;
  int count = 0;
  for (auto& s: input) {
    if (cur + s.ssid.length() + s.pwd.length() + 2 > area + len) {
      WARN("APs::SSIDs overshot the area; ignored ssids starting with", s.ssid, count, input.size());
      break;
    }
    ::strcpy(cur, s.ssid.c_str());
    cur += s.ssid.length() + 1;

    ::strcpy(cur, s.pwd.c_str());
    cur += s.pwd.length() + 1;
  }
  TRACE("APs::buildArea:", count);
  return count;
}
