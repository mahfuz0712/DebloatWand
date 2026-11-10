#pragma once
#include <string>
#include <vector>

struct Dep { std::wstring pkg, reason; };

struct Pkg {
    std::wstring name;
    bool sys = true, installed = true, risky = false, checked = false;
    std::wstring uidGroup, why;           // why = reasons for the RISKY class / known note
    std::vector<Dep> needs;               // this app depends on ...
    std::vector<Dep> neededBy;            // ... these installed apps depend on this app
    std::vector<std::wstring> uidMates;   // other packages sharing the same sharedUserId
};

std::wstring W(const std::string& s);
std::string U8(const std::wstring& w);
std::vector<std::string> splitLines(const std::string& s);

void setAdbPath(const std::wstring& p);
bool runAdb(const std::wstring& args, std::string& out, unsigned long timeoutMs = 60000);

bool isRisky(const std::wstring& pkg);
bool isBloatPreset(const std::wstring& pkg);
// Fills needs / neededBy / uidGroup / uidMates / risky / why from `dumpsys package` output.
void buildDeps(std::vector<Pkg>& pk, const std::string& dumpsys);
