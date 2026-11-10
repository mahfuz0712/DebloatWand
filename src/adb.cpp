#include <windows.h>
#include "adb.h"
#include <map>
#include <set>
#include <algorithm>

static std::wstring gAdb = L"adb.exe";
void setAdbPath(const std::wstring& p) { gAdb = p; }

std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string U8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> v; std::string cur;
    for (char c : s) { if (c == '\n') { v.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

bool runAdb(const std::wstring& args, std::string& out, unsigned long timeoutMs) {
    out.clear();
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + gAdb + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return false; }
    char tmp[8192]; DWORD n;
    while (ReadFile(rd, tmp, sizeof tmp, &n, nullptr) && n) out.append(tmp, n);
    WaitForSingleObject(pi.hProcess, timeoutMs);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
    return true;
}

// ---------------------------------------------------------------- classification
static const wchar_t* kRiskyExact[] = {
  L"android", L"com.android.systemui", L"com.android.phone", L"com.android.settings",
  L"com.android.providers.settings", L"com.android.providers.telephony", L"com.android.providers.contacts",
  L"com.android.providers.media", L"com.android.providers.media.module", L"com.android.providers.downloads",
  L"com.android.providers.calendar", L"com.android.packageinstaller", L"com.google.android.packageinstaller",
  L"com.android.shell", L"com.android.keychain", L"com.android.bluetooth", L"com.android.nfc",
  L"com.android.inputdevices", L"com.android.server.telecom", L"com.android.permissioncontroller",
  L"com.google.android.permissioncontroller", L"com.android.networkstack", L"com.android.networkstack.inprocess",
  L"com.android.captiveportallogin", L"com.google.android.gms", L"com.google.android.gsf", L"com.android.vending",
  L"com.android.webview", L"com.google.android.webview", L"com.android.certinstaller", L"com.android.location.fused",
  L"com.android.stk", L"com.android.mms.service", L"com.android.emergency", L"com.android.cellbroadcastreceiver",
  L"com.android.se", L"com.android.storagemanager", L"com.android.externalstorage", L"com.android.documentsui",
  L"com.qualcomm.qti.services.systemhelper", L"com.qualcomm.qcrilmsgtunnel", L"com.qualcomm.telephony",
  L"org.codeaurora.ims", L"com.android.carrierconfig", L"com.android.modulemetadata",
  L"com.google.android.ext.services", L"com.google.android.ext.shared", L"com.google.android.configupdater" };
static const wchar_t* kRiskyPrefix[] = { L"com.android.inputmethod", L"com.google.android.inputmethod",
  L"com.android.launcher", L"com.google.android.apps.nexuslauncher", L"com.android.theme", L"android.overlay",
  L"com.android.internal", L"com.android.systemui.", L"com.android.overlay", L"com.android.wifi",
  L"com.android.localtransport" };

bool isRisky(const std::wstring& n) {
    for (auto s : kRiskyExact) if (n == s) return true;
    for (auto p : kRiskyPrefix) if (n.rfind(p, 0) == 0) return true;
    return false;
}

static const wchar_t* kBloat[] = { L"com.facebook.", L"com.netflix.", L"com.amazon.", L"com.linkedin.",
  L"com.spotify.", L"com.tiktok.", L"com.zhiliaoapp.", L"com.booking", L"com.opera.", L"com.ebay.",
  L"com.twitter.", L"com.instagram.", L"com.google.android.videos", L"com.google.android.music",
  L"com.google.android.apps.magazines", L"com.google.android.apps.books", L"com.google.android.apps.podcasts",
  L"com.google.android.apps.tachyon", L"com.google.android.youtube", L"com.google.android.apps.docs.editors",
  L"com.google.android.apps.subscriptions", L"com.google.android.play.games",
  L"com.google.android.apps.youtube.music" };
bool isBloatPreset(const std::wstring& n) {
    if (isRisky(n)) return false;
    for (auto p : kBloat) if (n.rfind(p, 0) == 0) return true;
    return false;
}

struct Note { const wchar_t* pkg; const wchar_t* text; };
static const Note kNotes[] = {
  {L"com.google.android.gms", L"Google Play services: Play Store, Maps, Gmail, push notifications, location and most Google apps stop working without it."},
  {L"com.google.android.gsf", L"Google Services Framework: needed by Play services and Play Store (sync, accounts)."},
  {L"com.android.vending", L"Play Store: needed to install/update apps from Google Play."},
  {L"com.android.systemui", L"Status bar, notification shade, navigation. Removing it = black screen / bootloop."},
  {L"com.android.settings", L"Settings app. Many system dialogs open it."},
  {L"com.android.phone", L"Telephony: calls, SMS, mobile data, SIM."},
  {L"com.android.providers.telephony", L"SMS/MMS/APN database."},
  {L"com.android.providers.contacts", L"Contacts database used by Dialer, Messages and many apps."},
  {L"com.android.providers.media", L"MediaStore: gallery, music, camera files."},
  {L"com.android.providers.downloads", L"Download manager used by browsers and app updaters."},
  {L"com.android.packageinstaller", L"Installs APK files."},
  {L"com.google.android.packageinstaller", L"Installs APK files."},
  {L"com.android.bluetooth", L"Bluetooth stack."},
  {L"com.android.nfc", L"NFC stack."},
  {L"com.android.webview", L"WebView: used by many apps to render web content."},
  {L"com.google.android.webview", L"WebView: used by many apps to render web content."},
  {L"com.android.inputmethod.latin", L"AOSP keyboard. Keep one keyboard installed."},
  {L"com.google.android.inputmethod.latin", L"Gboard. Keep one keyboard installed."},
  {L"com.android.permissioncontroller", L"Runtime permission dialogs."},
  {L"com.google.android.permissioncontroller", L"Runtime permission dialogs."},
  {L"com.android.providers.settings", L"Stores system settings."},
  {L"com.android.shell", L"adb shell identity - removing it breaks adb."},
};

// ---------------------------------------------------------------- dependency parser
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

void buildDeps(std::vector<Pkg>& pk, const std::string& dump) {
    std::map<std::wstring, size_t> idx;
    for (size_t i = 0; i < pk.size(); i++) idx[pk[i].name] = i;

    std::map<std::string, std::string> libApk;                 // library name -> providing package
    std::map<std::string, std::vector<std::string>> uses;      // package -> libraries
    std::map<std::string, std::string> uidOf;                  // package -> sharedUserId name

    std::string section, cur; int hdrIndent = -1;
    for (auto& l : splitLines(dump)) {
        size_t ind = l.find_first_not_of(' ');
        if (ind == std::string::npos) continue;
        std::string t = l.substr(ind);
        if (ind == 0) { section = trim(t); cur.clear(); hdrIndent = -1; continue; }
        if (section == "Libraries:") {
            size_t a = t.find(" -> (apk) ");
            if (a != std::string::npos) libApk[t.substr(0, a)] = trim(t.substr(a + 10));
        } else if (section == "Packages:") {
            if (t.rfind("Package [", 0) == 0) {
                size_t e = t.find(']'); if (e != std::string::npos) cur = t.substr(9, e - 9);
                hdrIndent = -1; continue;
            }
            if (cur.empty()) continue;
            if (hdrIndent >= 0) {
                if ((int)ind > hdrIndent) {
                    std::string lib = trim(t);
                    size_t e = lib.find_first_of(" :/"); if (e != std::string::npos) lib = lib.substr(0, e);
                    if (!lib.empty()) uses[cur].push_back(lib);
                    continue;
                }
                hdrIndent = -1;
            }
            if (t.rfind("sharedUser=", 0) == 0) {
                size_t b = t.find('{'), sp = b == std::string::npos ? b : t.find(' ', b);
                size_t sl = sp == std::string::npos ? sp : t.find('/', sp);
                if (sl != std::string::npos) uidOf[cur] = t.substr(sp + 1, sl - sp - 1);
            } else if (t.rfind("usesLibraries:", 0) == 0 || t.rfind("usesOptionalLibraries:", 0) == 0 ||
                       t.rfind("usesStaticLibraries:", 0) == 0) {
                hdrIndent = (int)ind;
            }
        }
    }

    auto add = [&](size_t from, size_t to, const std::wstring& why) {
        if (from == to) return;
        for (auto& d : pk[from].needs) if (d.pkg == pk[to].name) return;
        pk[from].needs.push_back({pk[to].name, why});
        pk[to].neededBy.push_back({pk[from].name, why});
    };

    // 1) library dependencies
    for (auto& kv : uses) {
        auto it = idx.find(W(kv.first)); if (it == idx.end()) continue;
        for (auto& lib : kv.second) {
            auto la = libApk.find(lib); if (la == libApk.end()) continue;
            auto pt = idx.find(W(la->second)); if (pt == idx.end()) continue;
            if (!pk[pt->second].installed) continue;
            add(it->second, pt->second, L"uses library " + W(lib));
        }
    }
    // 2) well-known runtime dependencies
    auto gms = idx.find(L"com.google.android.gms"), gsf = idx.find(L"com.google.android.gsf"), vend = idx.find(L"com.android.vending");
    for (size_t i = 0; i < pk.size(); i++) {
        const std::wstring& n = pk[i].name;
        if (!pk[i].installed) continue;
        if (gms != idx.end() && n.rfind(L"com.google.android.", 0) == 0 && n != L"com.google.android.gms" &&
            n != L"com.google.android.gsf" && n.find(L"webview") == std::wstring::npos && n.find(L"permissioncontroller") == std::wstring::npos &&
            n.find(L"packageinstaller") == std::wstring::npos && n.find(L"ext.") == std::wstring::npos)
            add(i, gms->second, L"Google app, needs Play services");
        if (n == L"com.android.vending") {
            if (gms != idx.end()) add(i, gms->second, L"Play Store needs Play services");
            if (gsf != idx.end()) add(i, gsf->second, L"Play Store needs Services Framework");
        }
        if (n == L"com.google.android.gms" && gsf != idx.end()) add(i, gsf->second, L"Play services need Services Framework");
    }
    (void)vend;
    // 3) shared user ids
    std::map<std::wstring, std::vector<std::wstring>> groups;
    for (auto& kv : uidOf) {
        auto it = idx.find(W(kv.first)); if (it == idx.end()) continue;
        pk[it->second].uidGroup = W(kv.second);
        groups[W(kv.second)].push_back(W(kv.first));
    }
    for (auto& p : pk) if (!p.uidGroup.empty())
        for (auto& m : groups[p.uidGroup]) if (m != p.name) p.uidMates.push_back(m);

    // 4) classification + notes
    for (auto& p : pk) {
        std::vector<std::wstring> why;
        for (auto& n : kNotes) if (p.name == n.pkg) why.push_back(n.text);
        bool r = isRisky(p.name);
        if (r && why.empty()) why.push_back(L"Core system component (on the built-in risky list).");
        if (p.uidGroup.rfind(L"android.uid.", 0) == 0) { r = true; why.push_back(L"Runs in shared system UID " + p.uidGroup + L"."); }
        size_t live = 0; for (auto& d : p.neededBy) { auto it = idx.find(d.pkg); if (it != idx.end() && pk[it->second].installed) live++; }
        if (live) { r = true; why.push_back(std::to_wstring(live) + L" installed app(s) depend on it."); }
        p.risky = r;
        std::wstring j; for (size_t i = 0; i < why.size(); i++) { if (i) j += L" "; j += why[i]; }
        p.why = j;
    }
}
