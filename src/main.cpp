// DebloatWand - MUX HUD style ADB bloatware remover (no root). Win32, owner-drawn dark HUD theme.
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <cwctype>
#include <cstdlib>
#include <cwchar>
#include "adb.h"

// ---------------------------------------------------------------- theme
static const COLORREF cBG = RGB(4, 10, 16), cPanel = RGB(9, 20, 30), cCyan = RGB(0, 229, 255), cAmber = RGB(255, 179, 0),
                      cRed = RGB(255, 61, 87), cDim = RGB(107, 135, 148), cWhite = RGB(226, 238, 244);
static COLORREF mix(COLORREF a, COLORREF b, int pct) {
    return RGB((GetRValue(a) * pct + GetRValue(b) * (100 - pct)) / 100, (GetGValue(a) * pct + GetGValue(b) * (100 - pct)) / 100,
               (GetBValue(a) * pct + GetBValue(b) * (100 - pct)) / 100);
}
static int gDpi = 96;
static int S(int v) { return MulDiv(v, gDpi, 96); }

enum { ID_DEV = 100, ID_REFRESH, ID_INSTALL, ID_OPT_G, ID_OPT_D, ID_TAB_INFO, ID_TAB_APPS, ID_SEARCH,
       ID_F_ALL, ID_F_SAFE, ID_F_RISKY, ID_F_REMOVED, ID_SHOWSYS, ID_SHOWUSER, ID_PRESET, ID_CLEAR,
       ID_REMOVE, ID_RESTORE, ID_EXPORT, ID_LIST, ID_INFO, ID_DETAIL, ID_LOG };
enum { WM_LOG = WM_APP + 1, WM_DEVICES, WM_PKGS, WM_INFO, WM_BUSY };

struct BtnInfo { std::wstring text; COLORREF col = cCyan; bool toggle = false, on = false; };
typedef std::vector<std::pair<std::wstring, std::wstring>> InfoRows;

static HWND gMain, gDev, gSearch, gList, gInfo, gDetail, gLog;
static std::map<int, BtnInfo> gBtns;
static std::map<int, HWND> gBH;
static std::vector<HWND> gAppsCtl;
static HFONT gFont, gFontB, gFontT;
static HBRUSH gBrPanel, gBrBG;
static std::vector<Pkg> gPkgs;
static std::vector<int> gView;
static InfoRows gInfoRows;
static std::atomic<bool> gBusy{false};
static bool gFilling = false;
static int gTab = 1, gFilter = ID_F_ALL;
static RECT gRcHead, gRcContent, gRcLog, gRcSearch, gRcDetail, gStripInfo, gStripApps;
static std::wstring gDevState = L"LINK: --";
static std::wstring gStatus;

static void postLog(const std::wstring& s) { PostMessageW(gMain, WM_LOG, 0, (LPARAM) new std::wstring(s)); }
static void setBusy(bool b) { gBusy = b; PostMessageW(gMain, WM_BUSY, b, 0); }
static std::wstring exeDir() {
    wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH);
    std::wstring p = b; return p.substr(0, p.find_last_of(L"\\/") + 1);
}
static std::wstring curSerial() {
    int i = (int)SendMessageW(gDev, CB_GETCURSEL, 0, 0);
    if (i < 0) return L"";
    wchar_t b[160]; SendMessageW(gDev, CB_GETLBTEXT, i, (LPARAM)b);
    std::wstring s = b; size_t sp = s.find(L' ');
    return sp == std::wstring::npos ? s : s.substr(0, sp);
}
static std::wstring joinDeps(const std::vector<Dep>& v, bool withReason, size_t cap = 10) {
    std::wstring s;
    for (size_t i = 0; i < v.size() && i < cap; i++) {
        if (i) s += L", ";
        s += v[i].pkg; if (withReason) s += L" (" + v[i].reason + L")";
    }
    if (v.size() > cap) s += L", +" + std::to_wstring(v.size() - cap) + L" more";
    return s;
}

// ---------------------------------------------------------------- worker jobs
static void infoBody(const std::wstring& ser) {
    std::wstring pre = L"-s " + ser + L" shell ";
    std::string o;
    runAdb(pre + L"getprop", o);
    std::map<std::string, std::string> pr;
    for (auto& l : splitLines(o)) {
        size_t p = l.find("]: [");
        if (l.size() < 6 || l[0] != '[' || p == std::string::npos) continue;
        std::string v = l.substr(p + 4); if (!v.empty() && v.back() == ']') v.pop_back();
        pr[l.substr(1, p - 1)] = v;
    }
    auto g = [&](const char* k) { auto it = pr.find(k); return it == pr.end() || it->second.empty() ? std::wstring(L"-") : W(it->second); };
    InfoRows r;
    r.push_back({L"MODEL", g("ro.product.model")});
    r.push_back({L"BRAND / MANUFACTURER", g("ro.product.brand") + L" / " + g("ro.product.manufacturer")});
    r.push_back({L"DEVICE / PRODUCT", g("ro.product.device") + L" / " + g("ro.product.name")});
    r.push_back({L"ANDROID", g("ro.build.version.release") + L"  (SDK " + g("ro.build.version.sdk") + L")"});
    r.push_back({L"SECURITY PATCH", g("ro.build.version.security_patch")});
    r.push_back({L"BUILD", g("ro.build.display.id")});
    r.push_back({L"FINGERPRINT", g("ro.build.fingerprint")});
    r.push_back({L"CPU ABI", g("ro.product.cpu.abi")});
    r.push_back({L"PLATFORM", g("ro.board.platform") + L" / " + g("ro.hardware")});
    r.push_back({L"SERIAL", g("ro.serialno")});
    r.push_back({L"BOOTLOADER", g("ro.boot.flash.locked") == L"0" ? L"UNLOCKED" : (g("ro.boot.flash.locked") == L"1" ? L"LOCKED" : g("ro.boot.vbmeta.device_state"))});
    r.push_back({L"VERIFIED BOOT", g("ro.boot.verifiedbootstate") + L"   verity: " + g("ro.boot.veritymode")});
    r.push_back({L"SYSTEM-AS-ROOT", g("ro.build.system_root_image")});
    r.push_back({L"DYNAMIC PARTITIONS", g("ro.boot.dynamic_partitions")});
    r.push_back({L"A/B SLOT", g("ro.boot.slot_suffix") + L"   ab_update: " + g("ro.build.ab_update")});
    r.push_back({L"TREBLE", g("ro.treble.enabled")});
    r.push_back({L"LOCALE", g("persist.sys.locale") != L"-" ? g("persist.sys.locale") : g("ro.product.locale")});
    runAdb(pre + L"cat /proc/meminfo", o);
    for (auto& l : splitLines(o)) if (l.rfind("MemTotal:", 0) == 0) {
        long long kb = atoll(l.c_str() + 9); r.push_back({L"RAM", std::to_wstring(kb / 1024) + L" MB"}); break; }
    runAdb(pre + L"wm size", o); { auto ls = splitLines(o); if (!ls.empty()) { size_t c = ls[0].find(": "); r.push_back({L"SCREEN", W(c == std::string::npos ? ls[0] : ls[0].substr(c + 2))}); } }
    runAdb(pre + L"wm density", o); { auto ls = splitLines(o); if (!ls.empty()) { size_t c = ls[0].find(": "); r.push_back({L"DENSITY", W(c == std::string::npos ? ls[0] : ls[0].substr(c + 2))}); } }
    runAdb(pre + L"dumpsys battery", o);
    for (auto& l : splitLines(o)) {
        size_t c = l.find(": "); if (c == std::string::npos) continue;
        std::string k = l.substr(0, c); size_t a = k.find_first_not_of(' '); k = a == std::string::npos ? k : k.substr(a);
        if (k == "level") r.push_back({L"BATTERY", W(l.substr(c + 2)) + L" %"});
        if (k == "temperature") r.push_back({L"BATTERY TEMP", std::to_wstring(atoi(l.c_str() + c + 2) / 10) + L" C"});
    }
    runAdb(pre + L"df -h /system /data", o);
    for (auto& l : splitLines(o)) {
        std::vector<std::string> t; size_t i = 0;
        while (i < l.size()) { while (i < l.size() && l[i] == ' ') i++; size_t j = i; while (j < l.size() && l[j] != ' ') j++; if (j > i) t.push_back(l.substr(i, j - i)); i = j; }
        if (t.size() >= 6 && t[0] != "Filesystem") r.push_back({L"STORAGE " + W(t.back()), L"size " + W(t[1]) + L"   used " + W(t[2]) + L"   free " + W(t[3]) + L"   (" + W(t[4]) + L")"});
    }
    runAdb(pre + L"su -c id", o);
    r.push_back({L"ROOT (su)", o.find("uid=0") != std::string::npos ? L"YES" : L"NO / not granted"});
    PostMessageW(gMain, WM_INFO, 0, (LPARAM) new InfoRows(r));
}

static std::set<std::wstring> listPkgs(const std::wstring& ser, const wchar_t* flags) {
    std::string out; std::set<std::wstring> r;
    runAdb(L"-s " + ser + L" shell pm list packages " + flags, out);
    for (auto& l : splitLines(out)) if (l.rfind("package:", 0) == 0) r.insert(W(l.substr(8)));
    return r;
}
static void loadBody(const std::wstring& ser) {
    postLog(L"Reading packages + dependency graph ...");
    auto all = listPkgs(ser, L"-u"), inst = listPkgs(ser, L""), sys = listPkgs(ser, L"-s");
    if (all.empty()) { postLog(L"[ERROR] No packages returned. Authorise USB debugging on the device."); return; }
    auto* v = new std::vector<Pkg>();
    for (auto& n : all) { Pkg p; p.name = n; p.sys = sys.count(n) > 0; p.installed = inst.count(n) > 0; v->push_back(p); }
    std::string dump; runAdb(L"-s " + ser + L" shell dumpsys package", dump, 120000);
    buildDeps(*v, dump);
    PostMessageW(gMain, WM_PKGS, 0, (LPARAM)v);
}
static void jobDevices() {
    std::string out;
    if (!runAdb(L"devices", out)) { postLog(L"[ERROR] adb.exe not found. Put adb.exe next to this program or in PATH."); setBusy(false); return; }
    auto* v = new std::vector<std::wstring>();
    for (auto& l : splitLines(out)) {
        size_t t = l.find('\t'); if (t == std::string::npos) continue;
        std::string st = l.substr(t + 1);
        v->push_back(W(l.substr(0, t)) + L"  (" + W(st) + L")");
        if (st != "device") postLog(L"[WARN] " + W(l.substr(0, t)) + L" is '" + W(st) + L"': enable USB debugging and accept the RSA prompt.");
    }
    setBusy(false);
    PostMessageW(gMain, WM_DEVICES, 0, (LPARAM)v);
}
static void jobAll(std::wstring ser) { infoBody(ser); loadBody(ser); setBusy(false); }

struct ActJob { std::wstring ser; std::vector<std::pair<std::wstring, bool>> pk; bool remove; };
static void jobAct(ActJob j) {
    int ok = 0, fail = 0;
    for (auto& pr : j.pk) {
        const std::wstring& p = pr.first;
        std::wstring a = !j.remove ? L"-s " + j.ser + L" shell pm install-existing --user 0 " + p
                       : pr.second ? L"-s " + j.ser + L" shell pm uninstall -k --user 0 " + p
                                   : L"-s " + j.ser + L" shell pm uninstall " + p;
        std::string out; runAdb(a, out);
        std::wstring o = W(out); while (!o.empty() && (o.back() == L'\n' || o.back() == L'\r')) o.pop_back();
        bool good = out.find("Success") != std::string::npos || out.find("installed for user") != std::string::npos;
        (good ? ok : fail)++;
        postLog(std::wstring(good ? L"[OK] " : L"[FAIL] ") + p + L"  " + o);
    }
    postLog(L"Done: " + std::to_wstring(ok) + L" ok, " + std::to_wstring(fail) + L" failed.");
    loadBody(j.ser);
    setBusy(false);
}
static void jobInstall(std::wstring ser, std::vector<std::wstring> files, bool grant, bool down) {
    int ok = 0;
    for (auto& f : files) {
        postLog(L"Installing " + f + L" ...");
        std::string out;
        runAdb(L"-s " + ser + L" install -r" + (grant ? L" -g" : L"") + (down ? L" -d" : L"") + L" \"" + f + L"\"", out, 300000);
        std::wstring o = W(out); while (!o.empty() && (o.back() == L'\n' || o.back() == L'\r')) o.pop_back();
        size_t nl = o.rfind(L'\n'); if (nl != std::wstring::npos) o = o.substr(nl + 1);
        bool good = out.find("Success") != std::string::npos; if (good) ok++;
        postLog(std::wstring(good ? L"[OK] " : L"[FAIL] ") + o);
    }
    postLog(L"Install finished: " + std::to_wstring(ok) + L" / " + std::to_wstring(files.size()));
    loadBody(ser);
    setBusy(false);
}

// ---------------------------------------------------------------- UI helpers
static HWND mkBtn(int id, const wchar_t* t, COLORREF c, bool toggle = false, bool on = false) {
    HWND h = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 10, 10, gMain, (HMENU)(INT_PTR)id, nullptr, nullptr);
    gBtns[id] = {t, c, toggle, on}; gBH[id] = h; return h;
}
static HWND mkEdit(int id, DWORD style) {
    HWND h = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, gMain, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)gFont, TRUE); return h;
}
static HWND mkList(int id, bool checks) {
    HWND h = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | LVS_NOCOLUMNHEADER,
                             0, 0, 10, 10, gMain, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)gFont, TRUE);
    ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | (checks ? LVS_EX_CHECKBOXES : 0));
    ListView_SetBkColor(h, cPanel); ListView_SetTextBkColor(h, cPanel); ListView_SetTextColor(h, cCyan);
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
    return h;
}
static void addCol(HWND h, int i, int w) { LVCOLUMNW c{}; c.mask = LVCF_WIDTH; c.cx = w; ListView_InsertColumn(h, i, &c); }

static void frameBox(HDC dc, RECT r, COLORREF c, bool brackets) {
    HBRUSH f = CreateSolidBrush(cPanel); FillRect(dc, &r, f); DeleteObject(f);
    HBRUSH b = CreateSolidBrush(mix(c, cPanel, 28)); FrameRect(dc, &r, b); DeleteObject(b);
    if (!brackets) return;
    HPEN pen = CreatePen(PS_SOLID, 2, c); HGDIOBJ old = SelectObject(dc, pen);
    int L = S(14);
    auto ln = [&](int x1, int y1, int x2, int y2) { MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2); };
    ln(r.left, r.top, r.left + L, r.top); ln(r.left, r.top, r.left, r.top + L);
    ln(r.right - 1, r.top, r.right - 1 - L, r.top); ln(r.right - 1, r.top, r.right - 1, r.top + L);
    ln(r.left, r.bottom - 1, r.left + L, r.bottom - 1); ln(r.left, r.bottom - 1, r.left, r.bottom - 1 - L);
    ln(r.right - 1, r.bottom - 1, r.right - 1 - L, r.bottom - 1); ln(r.right - 1, r.bottom - 1, r.right - 1, r.bottom - 1 - L);
    SelectObject(dc, old); DeleteObject(pen);
}
static void txt(HDC dc, const std::wstring& s, int x, int y, COLORREF c, HFONT f) {
    SelectObject(dc, f); SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT); TextOutW(dc, x, y, s.c_str(), (int)s.size());
}
static void drawStrip(HDC dc, const RECT& r, HWND list, const std::vector<std::wstring>& labels, int firstOff) {
    HBRUSH b = CreateSolidBrush(mix(cCyan, cPanel, 10)); FillRect(dc, &r, b); DeleteObject(b);
    int x = r.left;
    for (size_t i = 0; i < labels.size(); i++) {
        txt(dc, labels[i], x + (i == 0 ? firstOff : S(6)), r.top + S(3), cAmber, gFontB);
        x += ListView_GetColumnWidth(list, (int)i);
    }
}

static void updateStatus() {
    int t = 0; for (auto& p : gPkgs) if (p.checked) t++;
    int risky = 0; for (auto& p : gPkgs) if (p.risky && p.installed) risky++;
    gStatus = std::to_wstring(t) + L" ticked  |  " + std::to_wstring(gView.size()) + L" shown / " + std::to_wstring(gPkgs.size()) +
              L"  |  " + std::to_wstring(risky) + L" risky";
    RECT r = {S(10) + S(10) + S(400), 0, 10000, 10000}; (void)r;
    InvalidateRect(gMain, nullptr, FALSE);
}

static void showDetail(int idx) {
    if (idx < 0 || idx >= (int)gPkgs.size()) { SetWindowTextW(gDetail, L""); return; }
    const Pkg& p = gPkgs[idx];
    std::wstring s = p.name + L"\r\n";
    s += L"CLASS: " + std::wstring(p.risky ? L"RISKY" : L"SAFE") + (p.sys ? L"  |  SYSTEM" : L"  |  USER") + (p.installed ? L"  |  INSTALLED" : L"  |  REMOVED") + L"\r\n";
    if (!p.why.empty()) s += L"NOTE: " + p.why + L"\r\n";
    if (!p.uidGroup.empty()) s += L"UID GROUP: " + p.uidGroup + (p.uidMates.empty() ? L"" : L"  shared with " + std::to_wstring(p.uidMates.size()) + L" app(s)") + L"\r\n";
    s += L"NEEDS (" + std::to_wstring(p.needs.size()) + L"): " + (p.needs.empty() ? L"-" : joinDeps(p.needs, true)) + L"\r\n";
    s += L"NEEDED BY (" + std::to_wstring(p.neededBy.size()) + L"): " + (p.neededBy.empty() ? L"-" : joinDeps(p.neededBy, false)) + L"\r\n";
    SetWindowTextW(gDetail, s.c_str());
}

static void fillList() {
    gFilling = true;
    SendMessageW(gList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(gList);
    wchar_t fb[256]; GetWindowTextW(gSearch, fb, 256);
    std::wstring f = fb; std::transform(f.begin(), f.end(), f.begin(), ::towlower);
    bool ss = gBtns[ID_SHOWSYS].on, su = gBtns[ID_SHOWUSER].on;
    gView.clear();
    for (size_t i = 0; i < gPkgs.size(); i++) {
        auto& p = gPkgs[i];
        if (p.sys ? !ss : !su) continue;
        if (gFilter == ID_F_SAFE && (p.risky || !p.installed)) continue;
        if (gFilter == ID_F_RISKY && !p.risky) continue;
        if (gFilter == ID_F_REMOVED && p.installed) continue;
        if (!f.empty()) { std::wstring l = p.name; std::transform(l.begin(), l.end(), l.begin(), ::towlower); if (l.find(f) == std::wstring::npos) continue; }
        gView.push_back((int)i);
    }
    int row = 0;
    for (int idx : gView) {
        auto& p = gPkgs[idx];
        LVITEMW it{}; it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = row; it.pszText = const_cast<wchar_t*>(p.name.c_str()); it.lParam = idx;
        ListView_InsertItem(gList, &it);
        ListView_SetItemText(gList, row, 1, const_cast<wchar_t*>(p.sys ? L"SYSTEM" : L"USER"));
        ListView_SetItemText(gList, row, 2, const_cast<wchar_t*>(p.installed ? L"INSTALLED" : L"REMOVED"));
        ListView_SetItemText(gList, row, 3, const_cast<wchar_t*>(p.risky ? L"RISKY" : L"SAFE"));
        std::wstring d = std::to_wstring(p.needs.size()) + L" / " + std::to_wstring(p.neededBy.size());
        ListView_SetItemText(gList, row, 4, const_cast<wchar_t*>(d.c_str()));
        ListView_SetCheckState(gList, row, p.checked);
        row++;
    }
    SendMessageW(gList, WM_SETREDRAW, TRUE, 0);
    gFilling = false;
    showDetail(-1);
    updateStatus();
}

static void fillInfo() {
    ListView_DeleteAllItems(gInfo);
    int row = 0;
    for (auto& kv : gInfoRows) {
        LVITEMW it{}; it.mask = LVIF_TEXT; it.iItem = row; it.pszText = const_cast<wchar_t*>(kv.first.c_str());
        ListView_InsertItem(gInfo, &it);
        ListView_SetItemText(gInfo, row, 1, const_cast<wchar_t*>(kv.second.c_str()));
        row++;
    }
}

static void showTab(int t) {
    gTab = t;
    gBtns[ID_TAB_INFO].on = (t == 0); gBtns[ID_TAB_APPS].on = (t == 1);
    ShowWindow(gInfo, t == 0 ? SW_SHOW : SW_HIDE);
    for (HWND h : gAppsCtl) ShowWindow(h, t == 1 ? SW_SHOW : SW_HIDE);
    InvalidateRect(gMain, nullptr, TRUE);
    for (auto& kv : gBH) InvalidateRect(kv.second, nullptr, TRUE);
}

static void layout() {
    RECT rc; GetClientRect(gMain, &rc); int W_ = rc.right, H = rc.bottom, m = S(10);
    gRcHead = {m, m, W_ - m, m + S(66)};
    int cy = m + S(20), bh = S(26), x = W_ - m - S(14);
    auto place = [&](HWND h, int w, int hh) { x -= w; MoveWindow(h, x, cy, w, hh, TRUE); x -= S(6); };
    place(gBH[ID_OPT_D], S(44), bh); place(gBH[ID_OPT_G], S(44), bh); place(gBH[ID_INSTALL], S(112), bh);
    place(gBH[ID_REFRESH], S(92), bh); place(gDev, S(240), bh + S(200));
    int ty = gRcHead.bottom + S(8);
    MoveWindow(gBH[ID_TAB_INFO], m, ty, S(150), S(28), TRUE);
    MoveWindow(gBH[ID_TAB_APPS], m + S(156), ty, S(150), S(28), TRUE);
    int top = ty + S(36), logH = S(110), bottom = H - m - logH - S(8);
    gRcContent = {m, top, W_ - m, bottom};
    gRcLog = {m, bottom + S(8), W_ - m, H - m};
    MoveWindow(gLog, gRcLog.left + S(8), gRcLog.top + S(8), gRcLog.right - gRcLog.left - S(16), logH - S(16), TRUE);
    int p = S(10), L = m + p, R = W_ - m - p, T = top + p, B = bottom - p;

    // device info tab
    gStripInfo = {L, T, R, T + S(20)};
    MoveWindow(gInfo, L, T + S(22), R - L, B - T - S(22), TRUE);
    ListView_SetColumnWidth(gInfo, 0, S(230)); ListView_SetColumnWidth(gInfo, 1, std::max(S(200), R - L - S(250)));

    // apps tab
    gRcSearch = {L, T, L + S(220), T + S(26)};
    MoveWindow(gSearch, L + S(8), T + S(5), S(204), S(18), TRUE);
    int bx = L + S(230);
    for (int id : {ID_F_ALL, ID_F_SAFE, ID_F_RISKY, ID_F_REMOVED}) { MoveWindow(gBH[id], bx, T, S(70), S(26), TRUE); bx += S(74); }
    bx += S(8);
    for (int id : {ID_SHOWSYS, ID_SHOWUSER}) { MoveWindow(gBH[id], bx, T, S(62), S(26), TRUE); bx += S(66); }
    MoveWindow(gBH[ID_CLEAR], R - S(66), T, S(66), S(26), TRUE);
    MoveWindow(gBH[ID_PRESET], R - S(66) - S(6) - S(80), T, S(80), S(26), TRUE);
    int actY = B - S(30);
    MoveWindow(gBH[ID_REMOVE], L, actY, S(140), S(30), TRUE);
    MoveWindow(gBH[ID_RESTORE], L + S(146), actY, S(140), S(30), TRUE);
    MoveWindow(gBH[ID_EXPORT], L + S(292), actY, S(150), S(30), TRUE);
    int detH = S(118), detY = actY - S(8) - detH;
    gRcDetail = {L, detY, R, detY + detH};
    MoveWindow(gDetail, L + S(8), detY + S(6), R - L - S(16), detH - S(12), TRUE);
    gStripApps = {L, T + S(34), R, T + S(54)};
    MoveWindow(gList, L, T + S(56), R - L, detY - S(8) - (T + S(56)), TRUE);
    int w1 = S(70), w2 = S(110), w3 = S(70), w4 = S(90);
    ListView_SetColumnWidth(gList, 1, w1); ListView_SetColumnWidth(gList, 2, w2); ListView_SetColumnWidth(gList, 3, w3); ListView_SetColumnWidth(gList, 4, w4);
    ListView_SetColumnWidth(gList, 0, std::max(S(150), R - L - (w1 + w2 + w3 + w4) - S(24)));
    InvalidateRect(gMain, nullptr, TRUE);
}

// ---------------------------------------------------------------- actions
static void doDevices() { if (gBusy) return; setBusy(true); std::thread(jobDevices).detach(); }
static void doAll() {
    if (gBusy) return; auto s = curSerial();
    if (s.empty()) { postLog(L"Select a device first."); return; }
    setBusy(true); std::thread(jobAll, s).detach();
}
static void setFilter(int id) {
    gFilter = id;
    for (int f : {ID_F_ALL, ID_F_SAFE, ID_F_RISKY, ID_F_REMOVED}) { gBtns[f].on = (f == id); InvalidateRect(gBH[f], nullptr, TRUE); }
    fillList();
}
static void doAct(bool remove) {
    if (gBusy) return; auto s = curSerial(); if (s.empty()) return;
    ActJob j; j.ser = s; j.remove = remove;
    std::set<std::wstring> ticked;
    for (auto& p : gPkgs) if (p.checked && (remove ? p.installed : !p.installed)) { ticked.insert(p.name); j.pk.push_back({p.name, p.sys}); }
    if (j.pk.empty()) { MessageBoxW(gMain, remove ? L"Tick some installed packages first." : L"Tick some removed packages first.", L"DebloatWand", MB_ICONINFORMATION); return; }
    if (remove) {
        std::wstring risky, deps; int nr = 0, nd = 0;
        for (auto& p : gPkgs) if (ticked.count(p.name)) {
            if (p.risky) { nr++; if (nr <= 8) risky += L"\n  " + p.name; }
            std::vector<std::wstring> blockers;
            for (auto& d : p.neededBy) if (!ticked.count(d.pkg)) {
                for (auto& q : gPkgs) if (q.name == d.pkg && q.installed) { blockers.push_back(d.pkg); break; }
            }
            if (!blockers.empty()) { nd++; if (nd <= 6) { deps += L"\n  " + p.name + L"  <-  needed by "; for (size_t i = 0; i < blockers.size() && i < 3; i++) { if (i) deps += L", "; deps += blockers[i]; } if (blockers.size() > 3) deps += L" ..."; } }
        }
        if (nr || nd) {
            std::wstring m;
            if (nr) m += std::to_wstring(nr) + L" RISKY package(s) (can bootloop or break calls / keyboard / Play services):" + risky + L"\n\n";
            if (nd) m += std::to_wstring(nd) + L" package(s) are still needed by other installed apps:" + deps + L"\n\n";
            m += L"Remove anyway?";
            if (MessageBoxW(gMain, m.c_str(), L"Dependency / risk warning", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        }
    }
    std::wstring q = (remove ? L"Remove " : L"Restore ") + std::to_wstring(j.pk.size()) + L" package(s)?\n\n"
        + (remove ? L"System apps are hidden for user 0 only (no root): the APK stays in /system, storage is NOT freed. Restore any time, or factory reset.\nUser apps are fully uninstalled." : L"");
    if (MessageBoxW(gMain, q.c_str(), L"DebloatWand", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    setBusy(true); std::thread(jobAct, j).detach();
}
static void doInstall(std::vector<std::wstring> files) {
    if (gBusy) return; auto s = curSerial();
    if (s.empty()) { postLog(L"Select a device first."); return; }
    if (files.empty()) return;
    setBusy(true); std::thread(jobInstall, s, files, gBtns[ID_OPT_G].on, gBtns[ID_OPT_D].on).detach();
}
static void doInstallDlg() {
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.hwndOwner = gMain; o.lpstrFile = buf.data(); o.nMaxFile = (DWORD)buf.size();
    o.lpstrFilter = L"APK files\0*.apk\0All files\0*.*\0"; o.lpstrTitle = L"Select APK file(s) to install";
    o.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&o)) return;
    std::vector<std::wstring> files; wchar_t* p = buf.data(); std::wstring first = p; p += first.size() + 1;
    if (!*p) files.push_back(first); else while (*p) { files.push_back(first + L"\\" + p); p += wcslen(p) + 1; }
    doInstall(files);
}
static void doExport() {
    wchar_t f[MAX_PATH] = L"removed_packages.txt";
    OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.hwndOwner = gMain; o.lpstrFile = f; o.nMaxFile = MAX_PATH;
    o.lpstrFilter = L"Text\0*.txt\0"; o.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&o)) return;
    std::ofstream os(f); int n = 0;
    for (auto& p : gPkgs) if (!p.installed) { os << U8(p.name) << "\n"; n++; }
    postLog(L"Saved " + std::to_wstring(n) + L" removed package name(s).");
}

// ---------------------------------------------------------------- window
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        gMain = h;
        gBrPanel = CreateSolidBrush(cPanel); gBrBG = CreateSolidBrush(cBG);
        gFont = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
        gFontB = CreateFontW(-S(13), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
        gFontT = CreateFontW(-S(26), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
        BOOL dark = TRUE;
        if (HMODULE dm = LoadLibraryW(L"dwmapi.dll")) {
            typedef HRESULT(WINAPI * F)(HWND, DWORD, LPCVOID, DWORD);
            if (auto f = (F)GetProcAddress(dm, "DwmSetWindowAttribute")) f(h, 20, &dark, sizeof dark);
        }
        gDev = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10, 10, h, (HMENU)(INT_PTR)ID_DEV, nullptr, nullptr);
        SendMessageW(gDev, WM_SETFONT, (WPARAM)gFont, TRUE); SetWindowTheme(gDev, L"DarkMode_CFD", nullptr);
        mkBtn(ID_REFRESH, L"REFRESH", cCyan); mkBtn(ID_INSTALL, L"INSTALL APK", cAmber);
        mkBtn(ID_OPT_G, L"-g", cDim, true, true); mkBtn(ID_OPT_D, L"-d", cDim, true, false);
        mkBtn(ID_TAB_INFO, L"DEVICE INFO", cCyan, true, false); mkBtn(ID_TAB_APPS, L"APPS LIST", cCyan, true, true);
        gInfo = mkList(ID_INFO, false); addCol(gInfo, 0, 100); addCol(gInfo, 1, 100);
        gList = mkList(ID_LIST, true); for (int i = 0; i < 5; i++) addCol(gList, i, 100);
        gSearch = mkEdit(ID_SEARCH, ES_AUTOHSCROLL);
        mkBtn(ID_F_ALL, L"ALL", cCyan, true, true); mkBtn(ID_F_SAFE, L"SAFE", cCyan, true); mkBtn(ID_F_RISKY, L"RISKY", cRed, true); mkBtn(ID_F_REMOVED, L"REMOVED", cAmber, true);
        mkBtn(ID_SHOWSYS, L"SYSTEM", cDim, true, true); mkBtn(ID_SHOWUSER, L"USER", cDim, true, true);
        mkBtn(ID_PRESET, L"PRESET", cAmber); mkBtn(ID_CLEAR, L"CLEAR", cDim);
        mkBtn(ID_REMOVE, L"REMOVE TICKED", cRed); mkBtn(ID_RESTORE, L"RESTORE TICKED", cAmber); mkBtn(ID_EXPORT, L"EXPORT REMOVED", cDim);
        gDetail = mkEdit(ID_DETAIL, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
        gLog = mkEdit(ID_LOG, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
        for (int id : {ID_SEARCH, ID_F_ALL, ID_F_SAFE, ID_F_RISKY, ID_F_REMOVED, ID_SHOWSYS, ID_SHOWUSER, ID_PRESET, ID_CLEAR, ID_REMOVE, ID_RESTORE, ID_EXPORT})
            gAppsCtl.push_back(id == ID_SEARCH ? gSearch : gBH[id]);
        gAppsCtl.push_back(gList); gAppsCtl.push_back(gDetail);
        std::wstring local = exeDir() + L"adb.exe";
        if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) setAdbPath(local);
        DragAcceptFiles(h, TRUE);
        layout(); showTab(1);
        postLog(L"USB debugging ON + accept the RSA prompt, then REFRESH. Drop .apk files on this window to install.");
        postLog(L"No root: removing a SYSTEM app hides it for user 0 (reversible, storage NOT freed).");
        doDevices();
        return 0; }
    case WM_SIZE: if (gBH.size()) layout(); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize = { S(1000), S(660) }; return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc); FillRect(dc, &rc, gBrBG);
        frameBox(dc, gRcHead, cCyan, true);
        txt(dc, L"MUX // DEBLOAT WAND", gRcHead.left + S(16), gRcHead.top + S(8), cCyan, gFontT);
        txt(dc, L"ADB CONTROL  .  NO ROOT NEEDED  .  " + gDevState, gRcHead.left + S(18), gRcHead.top + S(42), cDim, gFont);
        frameBox(dc, gRcContent, gTab == 0 ? cCyan : cAmber, true);
        frameBox(dc, gRcLog, cDim, false);
        if (gTab == 0) drawStrip(dc, gStripInfo, gInfo, {L"PROPERTY", L"VALUE"}, S(6));
        else {
            frameBox(dc, gRcSearch, cCyan, false);
            frameBox(dc, gRcDetail, cCyan, false);
            drawStrip(dc, gStripApps, gList, {L"PACKAGE", L"TYPE", L"STATE", L"CLASS", L"NEEDS/BY"}, S(24));
            txt(dc, gStatus, gRcContent.left + S(10) + S(452), gRcContent.bottom - S(10) - S(22), cCyan, gFont);
        }
        EndPaint(h, &ps); return 0; }
    case WM_DRAWITEM: {
        auto* d = (DRAWITEMSTRUCT*)l; if (d->CtlType != ODT_BUTTON) break;
        auto it = gBtns.find((int)d->CtlID); if (it == gBtns.end()) break;
        bool dis = (d->itemState & ODS_DISABLED) != 0, pr = (d->itemState & ODS_SELECTED) != 0;
        COLORREF c = dis ? cDim : it->second.col;
        HBRUSH b = CreateSolidBrush((it->second.on || pr) ? mix(c, cBG, 26) : cBG); FillRect(d->hDC, &d->rcItem, b); DeleteObject(b);
        HBRUSH bd = CreateSolidBrush(c); FrameRect(d->hDC, &d->rcItem, bd); DeleteObject(bd);
        std::wstring t = it->second.text;
        if (it->second.toggle && (d->CtlID == ID_OPT_G || d->CtlID == ID_OPT_D || d->CtlID == ID_SHOWSYS || d->CtlID == ID_SHOWUSER)) t = std::wstring(it->second.on ? L"[x] " : L"[ ] ") + t;
        if (it->second.toggle && !it->second.on && (d->CtlID == ID_OPT_G || d->CtlID == ID_OPT_D)) t = it->second.text;
        SelectObject(d->hDC, gFontB); SetTextColor(d->hDC, it->second.on || !it->second.toggle ? c : mix(c, cBG, 70)); SetBkMode(d->hDC, TRANSPARENT);
        RECT r = d->rcItem; DrawTextW(d->hDC, t.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE; }
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w; SetBkColor(dc, cPanel);
        SetTextColor(dc, (HWND)l == gLog ? cDim : ((HWND)l == gDetail ? cWhite : cCyan));
        return (LRESULT)gBrPanel; }
    case WM_COMMAND: {
        int id = LOWORD(w), ev = HIWORD(w);
        if (id == ID_DEV && ev == CBN_SELCHANGE) { doAll(); return 0; }
        if (id == ID_SEARCH && ev == EN_CHANGE) { fillList(); return 0; }
        if (ev != BN_CLICKED) return 0;
        auto it = gBtns.find(id);
        switch (id) {
        case ID_REFRESH: doDevices(); break;
        case ID_INSTALL: doInstallDlg(); break;
        case ID_OPT_G: case ID_OPT_D: it->second.on = !it->second.on; InvalidateRect(gBH[id], nullptr, TRUE); break;
        case ID_TAB_INFO: showTab(0); break;
        case ID_TAB_APPS: showTab(1); break;
        case ID_F_ALL: case ID_F_SAFE: case ID_F_RISKY: case ID_F_REMOVED: setFilter(id); break;
        case ID_SHOWSYS: case ID_SHOWUSER: it->second.on = !it->second.on; InvalidateRect(gBH[id], nullptr, TRUE); fillList(); break;
        case ID_PRESET: for (auto& p : gPkgs) if (p.installed && isBloatPreset(p.name)) p.checked = true; fillList(); break;
        case ID_CLEAR: for (auto& p : gPkgs) p.checked = false; fillList(); break;
        case ID_REMOVE: doAct(true); break;
        case ID_RESTORE: doAct(false); break;
        case ID_EXPORT: doExport(); break;
        }
        return 0; }
    case WM_DROPFILES: {
        HDROP hd = (HDROP)w; UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0); std::vector<std::wstring> files;
        for (UINT i = 0; i < n; i++) {
            UINT len = DragQueryFileW(hd, i, nullptr, 0); std::wstring f(len + 1, 0); DragQueryFileW(hd, i, &f[0], len + 1); f.resize(len);
            std::wstring lf = f; std::transform(lf.begin(), lf.end(), lf.begin(), ::towlower);
            if (lf.size() > 4 && lf.substr(lf.size() - 4) == L".apk") files.push_back(f);
        }
        DragFinish(hd);
        if (files.empty()) postLog(L"Only .apk files can be installed."); else doInstall(files);
        return 0; }
    case WM_NOTIFY: {
        auto* nm = (NMHDR*)l;
        if (nm->code == NM_CUSTOMDRAW && (nm->hwndFrom == gList || nm->hwndFrom == gInfo)) {
            auto* cd = (NMLVCUSTOMDRAW*)l;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) { cd->clrTextBk = cPanel; return CDRF_NOTIFYSUBITEMDRAW; }
            if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                cd->clrTextBk = cPanel; cd->clrText = cCyan;
                if (nm->hwndFrom == gInfo) cd->clrText = cd->iSubItem == 0 ? cDim : cWhite;
                else {
                    int idx = (int)cd->nmcd.lItemlParam;
                    if (idx >= 0 && idx < (int)gPkgs.size()) {
                        const Pkg& p = gPkgs[idx];
                        if (cd->iSubItem == 0) cd->clrText = p.installed ? cWhite : cDim;
                        else if (cd->iSubItem == 3) cd->clrText = p.risky ? cRed : cCyan;
                        else if (cd->iSubItem == 2) cd->clrText = p.installed ? cCyan : cAmber;
                        else cd->clrText = cDim;
                    }
                }
                return CDRF_NEWFONT;
            }
            return CDRF_DODEFAULT;
        }
        if (nm->hwndFrom == gList && nm->code == LVN_ITEMCHANGED && !gFilling) {
            auto* v = (NMLISTVIEW*)l;
            if (v->iItem < 0 || v->iItem >= (int)gView.size()) return 0;
            int idx = gView[v->iItem];
            if (v->uChanged & LVIF_STATE) {
                if ((v->uOldState ^ v->uNewState) & LVIS_STATEIMAGEMASK) { gPkgs[idx].checked = ListView_GetCheckState(gList, v->iItem) != 0; updateStatus(); }
                if ((v->uNewState & LVIS_SELECTED) && !(v->uOldState & LVIS_SELECTED)) showDetail(idx);
            }
        }
        return 0; }
    case WM_LOG: {
        auto* s = (std::wstring*)l; int n = GetWindowTextLengthW(gLog);
        SendMessageW(gLog, EM_SETSEL, n, n); std::wstring t = *s + L"\r\n"; SendMessageW(gLog, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
        delete s; return 0; }
    case WM_DEVICES: {
        auto* v = (std::vector<std::wstring>*)l; SendMessageW(gDev, CB_RESETCONTENT, 0, 0);
        for (auto& s : *v) SendMessageW(gDev, CB_ADDSTRING, 0, (LPARAM)s.c_str());
        gDevState = v->empty() ? L"LINK: NO DEVICE" : L"LINK: " + std::to_wstring(v->size()) + L" DEVICE(S)";
        InvalidateRect(h, &gRcHead, FALSE);
        bool any = !v->empty(); delete v;
        if (any) { SendMessageW(gDev, CB_SETCURSEL, 0, 0); doAll(); } else postLog(L"No device found.");
        return 0; }
    case WM_INFO: {
        auto* v = (InfoRows*)l; gInfoRows = *v; delete v; fillInfo(); return 0; }
    case WM_PKGS: {
        auto* v = (std::vector<Pkg>*)l;
        std::set<std::wstring> keep; for (auto& p : gPkgs) if (p.checked) keep.insert(p.name);
        gPkgs = *v; delete v;
        for (auto& p : gPkgs) if (keep.count(p.name)) p.checked = true;
        fillList();
        int rm = 0; for (auto& p : gPkgs) if (!p.installed) rm++;
        postLog(L"Loaded " + std::to_wstring(gPkgs.size()) + L" packages (" + std::to_wstring(rm) + L" removed for user 0).");
        return 0; }
    case WM_BUSY:
        for (int id : {ID_REFRESH, ID_INSTALL, ID_REMOVE, ID_RESTORE}) EnableWindow(gBH[id], !w);
        EnableWindow(gDev, !w);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int sh) {
    HDC sdc = GetDC(nullptr); gDpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc);
    INITCOMMONCONTROLSEX ic{sizeof ic, ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"DebloatWandHUD";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = nullptr;
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"MUX // DEBLOAT WAND", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, S(1120), S(760), nullptr, nullptr, hi, nullptr);
    ShowWindow(h, sh);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
