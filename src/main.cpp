// DebloatWand - ADB based Android bloatware remover (no root needed).
// Uses:  pm uninstall -k --user 0 <pkg>   (remove for user 0, reversible)
//        pm install-existing --user 0 <pkg> (restore)
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <set>

enum { ID_DEV = 100, ID_REFDEV, ID_LOAD, ID_REMOVE, ID_RESTORE, ID_EXPORT, ID_FILTER, ID_LIST, ID_LOG,
       ID_SHOWUSER, ID_SHOWSYS, ID_ADB };
enum { WM_LOG = WM_APP + 1, WM_DEVICES, WM_PKGS, WM_BUSY };

struct Pkg { std::wstring name; bool sys = true; bool installed = true; bool risky = false; bool checked = false; };

static HWND gMain, gDev, gFilter, gList, gLog, gShowUser, gShowSys, gBtn[5];
static std::vector<Pkg> gPkgs;
static std::vector<int> gView;           // indices into gPkgs shown in list
static std::wstring gAdb = L"adb.exe";
static std::atomic<bool> gBusy{false};
static bool gFilling = false;

// Packages that can break the phone if removed (warned / blocked unless confirmed).
static const wchar_t* kRiskyExact[] = {
  L"android", L"com.android.systemui", L"com.android.phone", L"com.android.settings",
  L"com.android.providers.settings", L"com.android.providers.telephony", L"com.android.providers.contacts",
  L"com.android.providers.media", L"com.android.providers.downloads", L"com.android.providers.calendar",
  L"com.android.packageinstaller", L"com.google.android.packageinstaller", L"com.android.shell",
  L"com.android.keychain", L"com.android.bluetooth", L"com.android.nfc", L"com.android.inputdevices",
  L"com.android.server.telecom", L"com.android.permissioncontroller", L"com.google.android.permissioncontroller",
  L"com.android.networkstack", L"com.android.networkstack.inprocess", L"com.android.captiveportallogin",
  L"com.google.android.gms", L"com.google.android.gsf", L"com.android.vending", L"com.android.webview",
  L"com.google.android.webview", L"com.android.certinstaller", L"com.android.location.fused",
  L"com.android.wifi.resources", L"com.android.stk", L"com.android.mms.service", L"com.android.emergency",
  L"com.android.cellbroadcastreceiver", L"com.android.se", L"com.android.storagemanager",
  L"com.android.externalstorage", L"com.android.documentsui", L"com.qualcomm.qti.services.systemhelper",
  L"com.qualcomm.qcrilmsgtunnel", L"com.qualcomm.telephony", L"org.codeaurora.ims", L"com.android.carrierconfig",
  L"com.android.modulemetadata", L"com.android.providers.partnerbookmarks" };
static const wchar_t* kRiskyPrefix[] = { L"com.android.inputmethod", L"com.google.android.inputmethod",
  L"com.android.launcher", L"com.google.android.apps.nexuslauncher", L"com.android.theme", L"android.overlay",
  L"com.android.internal", L"com.android.systemui.", L"com.android.overlay", L"com.google.android.ext" };

static bool isRisky(const std::wstring& n) {
  for (auto s : kRiskyExact) if (n == s) return true;
  for (auto p : kRiskyPrefix) if (n.rfind(p, 0) == 0) return true;
  return false;
}

static std::wstring W(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w;
}
static std::string U8(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
static void postLog(const std::wstring& s) { PostMessageW(gMain, WM_LOG, 0, (LPARAM) new std::wstring(s)); }

static std::wstring exeDir() {
  wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH);
  std::wstring p = b; return p.substr(0, p.find_last_of(L"\\/") + 1);
}

// Run "adb <args>", capture stdout+stderr. Returns false if the process could not start.
static bool runAdb(const std::wstring& args, std::string& out) {
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
  char tmp[4096]; DWORD n;
  while (ReadFile(rd, tmp, sizeof tmp, &n, nullptr) && n) out.append(tmp, n);
  WaitForSingleObject(pi.hProcess, 60000);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
  return true;
}

static std::vector<std::string> lines(const std::string& s) {
  std::vector<std::string> v; std::string cur;
  for (char c : s) { if (c == '\n') { v.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; }
  if (!cur.empty()) v.push_back(cur);
  return v;
}

static std::wstring curSerial() {
  int i = (int)SendMessageW(gDev, CB_GETCURSEL, 0, 0);
  if (i < 0) return L"";
  wchar_t b[128]; SendMessageW(gDev, CB_GETLBTEXT, i, (LPARAM)b);
  std::wstring s = b; size_t sp = s.find(L' ');
  return sp == std::wstring::npos ? s : s.substr(0, sp);
}

static void setBusy(bool b) { gBusy = b; PostMessageW(gMain, WM_BUSY, b, 0); }

// ---- worker jobs ----
static void jobDevices() {
  std::string out;
  if (!runAdb(L"devices", out)) { postLog(L"[ERROR] adb.exe not found. Put adb.exe (platform-tools) next to this program or in PATH."); setBusy(false); return; }
  auto* v = new std::vector<std::wstring>();
  for (auto& l : lines(out)) {
    size_t t = l.find('\t'); if (t == std::string::npos) continue;
    std::string ser = l.substr(0, t), st = l.substr(t + 1);
    v->push_back(W(ser) + L"  (" + W(st) + L")");
    if (st != "device") postLog(L"[WARN] " + W(ser) + L" is '" + W(st) + L"' - enable USB debugging and accept the RSA prompt on the device.");
  }
  PostMessageW(gMain, WM_DEVICES, 0, (LPARAM)v);
  setBusy(false);
}

static std::set<std::wstring> listPkgs(const std::wstring& ser, const wchar_t* flags) {
  std::string out; std::set<std::wstring> r;
  runAdb(L"-s " + ser + L" shell pm list packages " + flags, out);
  for (auto& l : lines(out)) if (l.rfind("package:", 0) == 0) r.insert(W(l.substr(8)));
  return r;
}

static void jobLoad(std::wstring ser) {
  postLog(L"Reading package list from " + ser + L" ...");
  auto all = listPkgs(ser, L"-u"), inst = listPkgs(ser, L""), sys = listPkgs(ser, L"-s");
  if (all.empty()) { postLog(L"[ERROR] No packages returned. Is the device authorised (USB debugging)?"); setBusy(false); return; }
  auto* v = new std::vector<Pkg>();
  for (auto& n : all) {
    Pkg p; p.name = n; p.sys = sys.count(n) > 0; p.installed = inst.count(n) > 0; p.risky = isRisky(n);
    v->push_back(p);
  }
  PostMessageW(gMain, WM_PKGS, 0, (LPARAM)v);
  setBusy(false);
}

struct ActJob { std::wstring ser; std::vector<std::wstring> pk; bool remove; };
static void jobAct(ActJob j) {
  int ok = 0, fail = 0;
  for (auto& p : j.pk) {
    std::wstring a = j.remove ? L"-s " + j.ser + L" shell pm uninstall -k --user 0 " + p
                              : L"-s " + j.ser + L" shell pm install-existing --user 0 " + p;
    std::string out; runAdb(a, out);
    std::wstring o = W(out); while (!o.empty() && (o.back() == L'\n' || o.back() == L'\r')) o.pop_back();
    bool good = out.find("Success") != std::string::npos || out.find("installed for user") != std::string::npos;
    (good ? ok : fail)++;
    postLog(std::wstring(good ? L"[OK] " : L"[FAIL] ") + p + L"  " + o);
  }
  postLog(L"Done: " + std::to_wstring(ok) + L" ok, " + std::to_wstring(fail) + L" failed. Press 'Load packages' to refresh.");
  setBusy(false);
}

// ---- UI ----
static void fillList() {
  gFilling = true;
  SendMessageW(gList, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(gList);
  wchar_t fb[256]; GetWindowTextW(gFilter, fb, 256);
  std::wstring f = fb; std::transform(f.begin(), f.end(), f.begin(), ::towlower);
  bool su = SendMessageW(gShowUser, BM_GETCHECK, 0, 0) == BST_CHECKED;
  bool ss = SendMessageW(gShowSys, BM_GETCHECK, 0, 0) == BST_CHECKED;
  gView.clear();
  for (size_t i = 0; i < gPkgs.size(); i++) {
    auto& p = gPkgs[i];
    if (p.sys ? !ss : !su) continue;
    if (!f.empty()) { std::wstring l = p.name; std::transform(l.begin(), l.end(), l.begin(), ::towlower); if (l.find(f) == std::wstring::npos) continue; }
    gView.push_back((int)i);
  }
  int row = 0;
  for (int idx : gView) {
    auto& p = gPkgs[idx];
    LVITEMW it{}; it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = row; it.pszText = const_cast<wchar_t*>(p.name.c_str()); it.lParam = idx;
    ListView_InsertItem(gList, &it);
    ListView_SetItemText(gList, row, 1, const_cast<wchar_t*>(p.sys ? L"System" : L"User"));
    ListView_SetItemText(gList, row, 2, const_cast<wchar_t*>(p.installed ? L"Installed" : L"Removed (user 0)"));
    ListView_SetItemText(gList, row, 3, const_cast<wchar_t*>(p.risky ? L"RISKY" : L""));
    ListView_SetCheckState(gList, row, p.checked);
    row++;
  }
  SendMessageW(gList, WM_SETREDRAW, TRUE, 0);
  gFilling = false;
}

static void layout() {
  RECT rc; GetClientRect(gMain, &rc); int W_ = rc.right, H = rc.bottom, m = 8, logH = 150;
  int y = m;
  MoveWindow(gDev, m, y, 280, 200, TRUE);
  MoveWindow(gBtn[0], m + 288, y, 110, 24, TRUE);
  MoveWindow(gBtn[1], m + 404, y, 130, 24, TRUE);
  y += 32;
  MoveWindow(GetDlgItem(gMain, 900), m, y + 3, 40, 18, TRUE);
  MoveWindow(gFilter, m + 44, y, 240, 24, TRUE);
  MoveWindow(gShowSys, m + 296, y, 100, 24, TRUE);
  MoveWindow(gShowUser, m + 400, y, 120, 24, TRUE);
  MoveWindow(gBtn[2], m + 530, y, 130, 24, TRUE);
  MoveWindow(gBtn[3], m + 666, y, 130, 24, TRUE);
  MoveWindow(gBtn[4], m + 802, y, 130, 24, TRUE);
  y += 32;
  MoveWindow(gList, m, y, W_ - 2 * m, H - y - logH - 2 * m, TRUE);
  MoveWindow(gLog, m, H - logH - m, W_ - 2 * m, logH, TRUE);
}


static void doDevices() { if (gBusy) return; setBusy(true); std::thread(jobDevices).detach(); }
static void doLoad() {
  if (gBusy) return; auto s = curSerial();
  if (s.empty()) { MessageBoxW(gMain, L"Select a device first (Refresh devices).", L"DebloatWand", MB_ICONINFORMATION); return; }
  setBusy(true); std::thread(jobLoad, s).detach();
}
static void doAct(bool remove) {
  if (gBusy) return; auto s = curSerial(); if (s.empty()) return;
  ActJob j; j.ser = s; j.remove = remove;
  std::wstring risky; int nr = 0;
  for (auto& p : gPkgs) if (p.checked && (remove ? p.installed : !p.installed)) {
    j.pk.push_back(p.name); if (p.risky && remove) { nr++; if (nr <= 12) risky += L"\n  " + p.name; }
  }
  if (j.pk.empty()) { MessageBoxW(gMain, remove ? L"Tick some installed packages first." : L"Tick some removed packages first.", L"DebloatWand", MB_ICONINFORMATION); return; }
  if (nr) {
    std::wstring m = L"These " + std::to_wstring(nr) + L" package(s) are marked RISKY and can cause a bootloop or break calling / Wi-Fi / the keyboard:" + risky + L"\n\nRemove them anyway?";
    if (MessageBoxW(gMain, m.c_str(), L"Risky packages", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
  }
  std::wstring q = (remove ? L"Remove " : L"Restore ") + std::to_wstring(j.pk.size()) + L" package(s) for user 0?\n\n"
    + (remove ? L"Note: without root this only disables/hides them. The APK stays in /system, so free storage will not increase.\nRestore any time with the 'Restore' button, or a factory reset brings everything back." : L"");
  if (MessageBoxW(gMain, q.c_str(), L"DebloatWand", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  setBusy(true); std::thread(jobAct, j).detach();
}
static void doExport() {
  wchar_t f[MAX_PATH] = L"removed_packages.txt";
  OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.hwndOwner = gMain; o.lpstrFile = f; o.nMaxFile = MAX_PATH;
  o.lpstrFilter = L"Text\0*.txt\0"; o.Flags = OFN_OVERWRITEPROMPT;
  if (!GetSaveFileNameW(&o)) return;
  std::ofstream os(f); int n = 0;
  for (auto& p : gPkgs) if (!p.installed) { os << U8(p.name) << "\n"; n++; }
  postLog(L"Saved " + std::to_wstring(n) + L" removed package name(s) to " + f);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
  case WM_CREATE: {
    gMain = h; HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD st, int id, DWORD ex = 0) {
      HWND c = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | st, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
      SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE); return c; };
    gDev = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, ID_DEV);
    gBtn[0] = mk(L"BUTTON", L"Refresh devices", BS_PUSHBUTTON, ID_REFDEV);
    gBtn[1] = mk(L"BUTTON", L"Load packages", BS_PUSHBUTTON, ID_LOAD);
    mk(L"STATIC", L"Filter", 0, 900);
    gFilter = mk(L"EDIT", L"", ES_AUTOHSCROLL, ID_FILTER, WS_EX_CLIENTEDGE);
    gShowSys = mk(L"BUTTON", L"System apps", BS_AUTOCHECKBOX, ID_SHOWSYS);
    gShowUser = mk(L"BUTTON", L"User apps", BS_AUTOCHECKBOX, ID_SHOWUSER);
    SendMessageW(gShowSys, BM_SETCHECK, BST_CHECKED, 0); SendMessageW(gShowUser, BM_SETCHECK, BST_CHECKED, 0);
    gBtn[2] = mk(L"BUTTON", L"Remove ticked", BS_PUSHBUTTON, ID_REMOVE);
    gBtn[3] = mk(L"BUTTON", L"Restore ticked", BS_PUSHBUTTON, ID_RESTORE);
    gBtn[4] = mk(L"BUTTON", L"Export removed list", BS_PUSHBUTTON, ID_EXPORT);
    gList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, 0, 10, 10, h, (HMENU)(INT_PTR)ID_LIST, nullptr, nullptr);
    SendMessageW(gList, WM_SETFONT, (WPARAM)font, TRUE);
    ListView_SetExtendedListViewStyle(gList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const wchar_t* cn[] = { L"Package", L"Type", L"State", L"Risk" }; int cw[] = { 420, 70, 130, 70 };
    for (int i = 0; i < 4; i++) { LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = const_cast<wchar_t*>(cn[i]); c.cx = cw[i]; ListView_InsertColumn(gList, i, &c); }
    gLog = mk(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, ID_LOG, WS_EX_CLIENTEDGE);
    // adb.exe next to the program wins, else PATH
    std::wstring local = exeDir() + L"adb.exe";
    if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) gAdb = local;
    layout();
    postLog(L"Enable Developer options > USB debugging on the Android device, connect USB, accept the prompt, then press 'Refresh devices'.");
    postLog(L"Without root, removal only hides/disables the app for user 0 (reversible, storage NOT freed).");
    doDevices();
    return 0; }
  case WM_SIZE: layout(); return 0;
  case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize = { 960, 520 }; return 0;
  case WM_COMMAND:
    switch (LOWORD(w)) {
    case ID_REFDEV: doDevices(); break;
    case ID_LOAD: doLoad(); break;
    case ID_REMOVE: doAct(true); break;
    case ID_RESTORE: doAct(false); break;
    case ID_EXPORT: doExport(); break;
    case ID_SHOWSYS: case ID_SHOWUSER: fillList(); break;
    case ID_FILTER: if (HIWORD(w) == EN_CHANGE) fillList(); break;
    }
    return 0;
  case WM_NOTIFY: {
    auto* nm = (NMHDR*)l;
    if (nm->idFrom == ID_LIST && nm->code == LVN_ITEMCHANGED && !gFilling) {
      auto* v = (NMLISTVIEW*)l;
      if (v->uChanged & LVIF_STATE) {
        int idx = (v->iItem >= 0 && v->iItem < (int)gView.size()) ? gView[v->iItem] : -1;
        if (idx >= 0) gPkgs[idx].checked = ListView_GetCheckState(gList, v->iItem) != 0;
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
    if (!v->empty()) { SendMessageW(gDev, CB_SETCURSEL, 0, 0); postLog(L"Found " + std::to_wstring(v->size()) + L" device(s)."); }
    else postLog(L"No device found.");
    delete v; return 0; }
  case WM_PKGS: {
    auto* v = (std::vector<Pkg>*)l; gPkgs = *v; delete v; fillList();
    int rm = 0; for (auto& p : gPkgs) if (!p.installed) rm++;
    postLog(L"Loaded " + std::to_wstring(gPkgs.size()) + L" packages (" + std::to_wstring(rm) + L" removed for user 0)."); return 0; }
  case WM_BUSY:
    for (int i = 0; i < 5; i++) EnableWindow(gBtn[i], !w);
    SetCursor(LoadCursor(nullptr, w ? IDC_WAIT : IDC_ARROW)); return 0;
  case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int sh) {
  INITCOMMONCONTROLSEX ic{sizeof ic, ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
  WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"DebloatWandWnd";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  RegisterClassW(&wc);
  HWND h = CreateWindowExW(0, wc.lpszClassName, L"DebloatWand - ADB bloatware remover (no root)", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700, nullptr, nullptr, hi, nullptr);
  ShowWindow(h, sh);
  MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  return (int)msg.wParam;
}
