// Oriel dialog shim.
//
// Registered per-user over CLSID_FileSaveDialog / CLSID_FileOpenDialog so that
// every app asking the shell for a file dialog reaches us first. This build
// does nothing but forward to the genuine implementation: the interception has
// to be proven harmless before any of our own UI goes near it.
//
// Two rules this file exists to enforce:
//   1. Never hardcode where the real dialog lives. Read the machine
//      registration - which we deliberately do not override - at runtime.
//   2. Any failure on our side must end in the real dialog, never in no dialog.
//      An app that cannot save a file is a far worse outcome than a plain one.

#include <windows.h>
#include <shobjidl.h>      // IFileDialog2 is declared here, not in shobjidl_core.h
#include <shlwapi.h>
#include "protocol.h"
#include <new>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

namespace {

LONG g_objects = 0;
LONG g_locks   = 0;
HINSTANCE g_self = nullptr;

void logf(const char* fmt, ...) {
    char line[512];
    va_list ap; va_start(ap, fmt);
    const int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_self, path, MAX_PATH);
    if (wchar_t* s = wcsrchr(path, L'\\')) wcscpy_s(s + 1, 32, L"oriel-shim.log");
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    // stamp the host process, since this DLL lives inside other people's apps
    char head[64];
    const int hn = sprintf_s(head, "[pid %lu] ", GetCurrentProcessId());
    WriteFile(h, head, hn, &w, nullptr);
    WriteFile(h, line, n, &w, nullptr);
    WriteFile(h, "\r\n", 2, &w, nullptr);
    CloseHandle(h);
}

// ── the genuine implementation ─────────────────────────────────────
// HKLM keeps the original registration; our override only ever lands in HKCU.
// Loading through it means we follow the OS wherever it moves the code.
HRESULT genuineFactory(REFCLSID clsid, IClassFactory** out) {
    wchar_t guid[64];
    if (!StringFromGUID2(clsid, guid, 64)) return E_FAIL;

    wchar_t key[200];
    swprintf_s(key, L"SOFTWARE\\Classes\\CLSID\\%s\\InprocServer32", guid);

    wchar_t dllPath[MAX_PATH]{};
    DWORD cb = sizeof(dllPath);
    LSTATUS st = RegGetValueW(HKEY_LOCAL_MACHINE, key, nullptr,
                              RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                              nullptr, dllPath, &cb);
    if (st != ERROR_SUCCESS) { logf("no machine registration (%ld)", st); return E_FAIL; }

    // Held for the life of the process on purpose: objects we hand out live in
    // this module, so it must never be unloaded underneath them.
    static HMODULE mod = nullptr;
    if (!mod) mod = LoadLibraryExW(dllPath, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!mod) { logf("LoadLibrary %ls failed %lu", dllPath, GetLastError()); return E_FAIL; }

    auto entry = reinterpret_cast<LPFNGETCLASSOBJECT>(
        GetProcAddress(mod, "DllGetClassObject"));
    if (!entry) { logf("no DllGetClassObject in %ls", dllPath); return E_FAIL; }

    return entry(clsid, IID_IClassFactory, reinterpret_cast<void**>(out));
}

// ── talking to the resident Oriel ──────────────────────────────────
// Every failure path here returns false, and false always means "show the
// genuine dialog". Oriel being absent, busy, stuck or crashed must never cost
// the host application its ability to save a file.
struct PickResult { bool handled = false; bool cancelled = false; std::wstring path; };

bool askOriel(oriel::proto::Mode mode, HWND owner,
              const std::wstring& folder, const std::wstring& file,
              const std::vector<std::pair<std::wstring, std::wstring>>& types,
              uint32_t typeIndex, PickResult* out) {
    using namespace oriel::proto;
    const std::wstring name = pipeName();

    // A quick existence probe: if Oriel is not listening we want to be in the
    // genuine dialog within a few milliseconds, not after a timeout the user
    // can feel.
    if (!WaitNamedPipeW(name.c_str(), 120)) return false;

    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;

    DWORD pipeMode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &pipeMode, nullptr, nullptr);

    std::vector<uint8_t> req;
    Header h{ kMagic, kVersion, static_cast<uint32_t>(mode), 0,
              reinterpret_cast<uint64_t>(owner) };
    const auto* hp = reinterpret_cast<const uint8_t*>(&h);
    req.assign(hp, hp + sizeof(h));
    putString(req, folder);
    putString(req, file);
    // Filters, as a count followed by label/spec pairs. An old Oriel simply
    // stops reading after the file name, which is why they go last.
    putU32(req, static_cast<uint32_t>(types.size()));
    for (const auto& t : types) { putString(req, t.first); putString(req, t.second); }
    putU32(req, typeIndex);

    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ev) { CloseHandle(pipe); return false; }

    auto wait = [&](OVERLAPPED& ov, DWORD budgetMs, DWORD* transferred) -> bool {
        const ULONGLONG deadline = GetTickCount64() + budgetMs;
        for (;;) {
            const ULONGLONG now = GetTickCount64();
            const DWORD left = (now >= deadline) ? 0
                             : static_cast<DWORD>(deadline - now);
            // Pump while we wait, exactly as a real modal dialog does; otherwise
            // the host app stops repainting and looks hung.
            const DWORD r = MsgWaitForMultipleObjects(1, &ev, FALSE, left, QS_ALLINPUT);
            if (r == WAIT_OBJECT_0) return GetOverlappedResult(pipe, &ov, transferred, FALSE) != 0;
            if (r == WAIT_OBJECT_0 + 1) {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); return false; }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                continue;
            }
            return false;   // timed out or failed
        }
    };

    OVERLAPPED ov{};
    ov.hEvent = ev;
    DWORD moved = 0;
    if (!WriteFile(pipe, req.data(), static_cast<DWORD>(req.size()), nullptr, &ov) &&
        GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(ev); CloseHandle(pipe); return false;
    }
    if (!wait(ov, 1000, &moved)) { CancelIo(pipe); CloseHandle(ev); CloseHandle(pipe); return false; }

    // The user now drives the picker, so the read gets a generous budget. If
    // Oriel dies the pipe breaks and the read fails immediately - no hang.
    std::vector<uint8_t> buf(64 * 1024);
    ResetEvent(ev);
    OVERLAPPED ov2{};
    ov2.hEvent = ev;
    if (!ReadFile(pipe, buf.data(), static_cast<DWORD>(buf.size()), nullptr, &ov2) &&
        GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(ev); CloseHandle(pipe); return false;
    }

    // Modality: keep the caller's window inert while the picker is up.
    const bool disable = owner && IsWindow(owner) && IsWindowEnabled(owner);
    if (disable) EnableWindow(owner, FALSE);
    AllowSetForegroundWindow(ASFW_ANY);

    const bool ok = wait(ov2, 10 * 60 * 1000, &moved);

    if (disable) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
    if (!ok) { CancelIo(pipe); CloseHandle(ev); CloseHandle(pipe); return false; }

    CloseHandle(ev);
    CloseHandle(pipe);

    const uint8_t* p = buf.data();
    const uint8_t* end = p + moved;
    if (static_cast<size_t>(end - p) < sizeof(uint32_t) * 3) return false;
    uint32_t magic = 0, version = 0, status = 0;
    memcpy(&magic, p, 4);   p += 4;
    memcpy(&version, p, 4); p += 4;
    memcpy(&status, p, 4);  p += 4;
    if (magic != kMagic || version != kVersion) return false;

    std::wstring path;
    if (!getString(p, end, &path)) return false;

    if (status == static_cast<uint32_t>(Status::Decline)) return false;
    out->handled = true;
    out->cancelled = (status == static_cast<uint32_t>(Status::Cancelled));
    out->path = std::move(path);
    return true;
}

// ── the shim object ────────────────────────────────────────────────
// Implements the dialog interfaces and forwards each call. Interfaces we do not
// name are answered by the inner object directly, which is exactly right while
// this is a pass-through.
class Shim final : public IFileSaveDialog, public IFileOpenDialog, public IFileDialog2 {
public:
    Shim(REFCLSID clsid, IFileDialog* inner, bool isSave)
        : clsid_(clsid), inner_(inner), isSave_(isSave) {
        inner_->AddRef();
        InterlockedIncrement(&g_objects);
    }
    ~Shim() {
        if (inner_) inner_->Release();
        InterlockedDecrement(&g_objects);
    }

    // ── IUnknown ──
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IModalWindow || riid == IID_IFileDialog)
            *ppv = static_cast<IFileDialog*>(static_cast<IFileSaveDialog*>(this));
        else if (riid == IID_IFileDialog2)
            *ppv = static_cast<IFileDialog2*>(this);
        else if (isSave_ && riid == IID_IFileSaveDialog)
            *ppv = static_cast<IFileSaveDialog*>(this);
        else if (!isSave_ && riid == IID_IFileOpenDialog)
            *ppv = static_cast<IFileOpenDialog*>(this);
        else
            return inner_->QueryInterface(riid, ppv);   // customise, events, sites…
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override  { return InterlockedIncrement(&ref_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return n;
    }

    // ── IModalWindow ──
    // The one call we will eventually take over. For now it is timed and passed
    // straight through, so the shim's cost is on the record.
    IFACEMETHODIMP Show(HWND owner) override {
        // Hand the picker whatever the caller already configured, so it opens
        // where the application expects rather than somewhere generic.
        std::wstring folder, file;
        IShellItem* start = nullptr;
        if (SUCCEEDED(inner_->GetFolder(&start)) && start) {
            LPWSTR s = nullptr;
            if (SUCCEEDED(start->GetDisplayName(SIGDN_FILESYSPATH, &s)) && s) {
                folder = s;
                CoTaskMemFree(s);
            }
            start->Release();
        }
        LPWSTR n = nullptr;
        if (SUCCEEDED(inner_->GetFileName(&n)) && n) { file = n; CoTaskMemFree(n); }

        PickResult pick;
        const bool answered = askOriel(
            isSave_ ? oriel::proto::Mode::Save : oriel::proto::Mode::Open,
            owner, folder, file, types_,
            typeIndex_ ? typeIndex_ - 1 : 0,   // the wire is 0-based
            &pick);

        if (answered && pick.handled) {
            picked_.clear();
            if (pick.cancelled) {
                logf("Show -> Oriel picker, cancelled");
                return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            }
            picked_ = pick.path;
            logf("Show -> Oriel picker, picked %ls", picked_.c_str());
            return S_OK;
        }

        // Everything else lands here, and here is always safe.
        LARGE_INTEGER f{}, a{}, b{};
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        const HRESULT hr = inner_->Show(owner);
        QueryPerformanceCounter(&b);
        logf("Show -> genuine dialog (no picker), hr=0x%08X, %.1f ms", hr,
             f.QuadPart ? double(b.QuadPart - a.QuadPart) * 1000.0 / double(f.QuadPart) : 0.0);
        return hr;
    }

    // ── IFileDialog ──
    // Remembered on the way through: the picker needs the filter list, and
    // IFileDialog offers no way to read back what was set.
    IFACEMETHODIMP SetFileTypes(UINT n, const COMDLG_FILTERSPEC* p) override {
        types_.clear();
        for (UINT i = 0; i < n && p; ++i)
            types_.push_back({ p[i].pszName ? p[i].pszName : L"", p[i].pszSpec ? p[i].pszSpec : L"" });
        return inner_->SetFileTypes(n, p);
    }
    IFACEMETHODIMP SetFileTypeIndex(UINT i) override                        { typeIndex_ = i; return inner_->SetFileTypeIndex(i); }
    IFACEMETHODIMP GetFileTypeIndex(UINT* i) override                       { return inner_->GetFileTypeIndex(i); }
    IFACEMETHODIMP Advise(IFileDialogEvents* e, DWORD* c) override          { return inner_->Advise(e, c); }
    IFACEMETHODIMP Unadvise(DWORD c) override                               { return inner_->Unadvise(c); }
    IFACEMETHODIMP SetOptions(FILEOPENDIALOGOPTIONS o) override             { return inner_->SetOptions(o); }
    IFACEMETHODIMP GetOptions(FILEOPENDIALOGOPTIONS* o) override            { return inner_->GetOptions(o); }
    IFACEMETHODIMP SetDefaultFolder(IShellItem* i) override                 { return inner_->SetDefaultFolder(i); }
    IFACEMETHODIMP SetFolder(IShellItem* i) override                        { return inner_->SetFolder(i); }
    IFACEMETHODIMP GetFolder(IShellItem** i) override                       { return inner_->GetFolder(i); }
    IFACEMETHODIMP GetCurrentSelection(IShellItem** i) override             { return inner_->GetCurrentSelection(i); }
    IFACEMETHODIMP SetFileName(LPCWSTR s) override                          { return inner_->SetFileName(s); }
    IFACEMETHODIMP GetFileName(LPWSTR* s) override {
        if (picked_.empty()) return inner_->GetFileName(s);
        if (!s) return E_POINTER;
        const size_t slash = picked_.find_last_of(L'\\');
        const std::wstring leaf = (slash == std::wstring::npos) ? picked_
                                                                : picked_.substr(slash + 1);
        *s = static_cast<LPWSTR>(CoTaskMemAlloc((leaf.size() + 1) * sizeof(wchar_t)));
        if (!*s) return E_OUTOFMEMORY;
        wcscpy_s(*s, leaf.size() + 1, leaf.c_str());
        return S_OK;
    }
    IFACEMETHODIMP SetTitle(LPCWSTR s) override                             { return inner_->SetTitle(s); }
    IFACEMETHODIMP SetOkButtonLabel(LPCWSTR s) override                     { return inner_->SetOkButtonLabel(s); }
    IFACEMETHODIMP SetFileNameLabel(LPCWSTR s) override                     { return inner_->SetFileNameLabel(s); }
    IFACEMETHODIMP GetResult(IShellItem** i) override {
        if (picked_.empty()) return inner_->GetResult(i);
        return itemForPicked(i);
    }
    IFACEMETHODIMP AddPlace(IShellItem* i, FDAP a) override                 { return inner_->AddPlace(i, a); }
    IFACEMETHODIMP SetDefaultExtension(LPCWSTR s) override                  { return inner_->SetDefaultExtension(s); }
    IFACEMETHODIMP Close(HRESULT hr) override                               { return inner_->Close(hr); }
    IFACEMETHODIMP SetClientGuid(REFGUID g) override                        { return inner_->SetClientGuid(g); }
    IFACEMETHODIMP ClearClientData() override                               { return inner_->ClearClientData(); }
    IFACEMETHODIMP SetFilter(IShellItemFilter* f) override                  { return inner_->SetFilter(f); }

    // ── IFileDialog2 ──
    IFACEMETHODIMP SetCancelButtonLabel(LPCWSTR s) override {
        return with2([&](IFileDialog2* d) { return d->SetCancelButtonLabel(s); });
    }
    IFACEMETHODIMP SetNavigationRoot(IShellItem* i) override {
        return with2([&](IFileDialog2* d) { return d->SetNavigationRoot(i); });
    }

    // ── IFileSaveDialog ──
    IFACEMETHODIMP SetSaveAsItem(IShellItem* i) override {
        return withSave([&](IFileSaveDialog* d) { return d->SetSaveAsItem(i); });
    }
    IFACEMETHODIMP SetProperties(IPropertyStore* p) override {
        return withSave([&](IFileSaveDialog* d) { return d->SetProperties(p); });
    }
    IFACEMETHODIMP SetCollectedProperties(IPropertyDescriptionList* l, BOOL b) override {
        return withSave([&](IFileSaveDialog* d) { return d->SetCollectedProperties(l, b); });
    }
    IFACEMETHODIMP GetProperties(IPropertyStore** p) override {
        return withSave([&](IFileSaveDialog* d) { return d->GetProperties(p); });
    }
    IFACEMETHODIMP ApplyProperties(IShellItem* i, IPropertyStore* p, HWND h,
                                   IFileOperationProgressSink* s) override {
        return withSave([&](IFileSaveDialog* d) { return d->ApplyProperties(i, p, h, s); });
    }

    // ── IFileOpenDialog ──
    IFACEMETHODIMP GetResults(IShellItemArray** a) override {
        if (picked_.empty()) return withOpen([&](IFileOpenDialog* d) { return d->GetResults(a); });
        return arrayForPicked(a);
    }
    IFACEMETHODIMP GetSelectedItems(IShellItemArray** a) override {
        if (picked_.empty()) return withOpen([&](IFileOpenDialog* d) { return d->GetSelectedItems(a); });
        return arrayForPicked(a);
    }

private:
    // A save target usually does not exist yet, and SHCreateItemFromParsingName
    // refuses names it cannot resolve. STGM_CREATE in the bind options is what
    // makes the shell parse a path for something about to be created.
    HRESULT itemForPicked(IShellItem** out) {
        if (!out) return E_POINTER;
        *out = nullptr;
        IBindCtx* bc = nullptr;
        if (SUCCEEDED(CreateBindCtx(0, &bc)) && bc) {
            BIND_OPTS bo{ sizeof(BIND_OPTS) };
            bo.grfMode = STGM_CREATE;
            bc->SetBindOptions(&bo);
        }
        const HRESULT hr = SHCreateItemFromParsingName(picked_.c_str(), bc,
                                                       IID_PPV_ARGS(out));
        if (bc) bc->Release();
        return hr;
    }
    HRESULT arrayForPicked(IShellItemArray** out) {
        if (!out) return E_POINTER;
        *out = nullptr;
        IShellItem* item = nullptr;
        const HRESULT hr = itemForPicked(&item);
        if (FAILED(hr) || !item) return FAILED(hr) ? hr : E_FAIL;
        const HRESULT h2 = SHCreateShellItemArrayFromShellItem(item, IID_PPV_ARGS(out));
        item->Release();
        return h2;
    }

    template <class F> HRESULT with2(F&& f) {
        IFileDialog2* d = nullptr;
        HRESULT hr = inner_->QueryInterface(IID_PPV_ARGS(&d));
        if (FAILED(hr)) return hr;
        hr = f(d); d->Release(); return hr;
    }
    template <class F> HRESULT withSave(F&& f) {
        IFileSaveDialog* d = nullptr;
        HRESULT hr = inner_->QueryInterface(IID_PPV_ARGS(&d));
        if (FAILED(hr)) return hr;
        hr = f(d); d->Release(); return hr;
    }
    template <class F> HRESULT withOpen(F&& f) {
        IFileOpenDialog* d = nullptr;
        HRESULT hr = inner_->QueryInterface(IID_PPV_ARGS(&d));
        if (FAILED(hr)) return hr;
        hr = f(d); d->Release(); return hr;
    }

    LONG ref_ = 1;
    CLSID clsid_{};
    IFileDialog* inner_ = nullptr;
    bool isSave_ = true;
    std::wstring picked_;   // non-empty only when our picker produced the answer
    // The filter the host configured. IFileDialog has no getter for it, so the
    // only chance to see it is on the way through.
    std::vector<std::pair<std::wstring, std::wstring>> types_;
    UINT typeIndex_ = 1;    // IFileDialog numbers these from 1
};

class Factory final : public IClassFactory {
public:
    explicit Factory(REFCLSID c) : clsid_(c) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override  { return InterlockedIncrement(&ref_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return n;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;

        IClassFactory* real = nullptr;
        HRESULT hr = genuineFactory(clsid_, &real);
        if (FAILED(hr) || !real) { logf("genuine factory failed 0x%08X", hr); return hr; }

        IFileDialog* inner = nullptr;
        hr = real->CreateInstance(nullptr, IID_PPV_ARGS(&inner));
        real->Release();
        if (FAILED(hr) || !inner) { logf("genuine CreateInstance failed 0x%08X", hr); return hr; }

        const bool isSave = IsEqualCLSID(clsid_, CLSID_FileSaveDialog);

        // Which applications actually come through here is a design input, not
        // just a debugging aid - it decides what the replacement UI must support.
        wchar_t host[MAX_PATH]{};
        GetModuleFileNameW(nullptr, host, MAX_PATH);
        const wchar_t* leaf = wcsrchr(host, L'\\');
        logf("create %s for %ls", isSave ? "FileSaveDialog" : "FileOpenDialog",
             leaf ? leaf + 1 : host);

        auto* shim = new (std::nothrow) Shim(clsid_, inner, isSave);
        inner->Release();
        if (!shim) return E_OUTOFMEMORY;

        hr = shim->QueryInterface(riid, ppv);
        shim->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_locks); else InterlockedDecrement(&g_locks);
        return S_OK;
    }

private:
    LONG ref_ = 1;
    CLSID clsid_{};
};

} // namespace

// ── exports ────────────────────────────────────────────────────────
namespace {
// Escape hatch. If our own object turns out to be the problem, dropping a file
// next to the DLL takes us out of the chain entirely on the next dialog - no
// registry edit, no elevation, no restart of the affected apps beyond reopening
// the dialog. Returning CLASS_E_CLASSNOTAVAILABLE here would break the caller,
// so we hand back the genuine factory instead.
bool bypassed() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_self, path, MAX_PATH);
    if (wchar_t* s = wcsrchr(path, L'\\')) wcscpy_s(s + 1, 32, L"oriel-shim.off");
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}
} // namespace

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(clsid, CLSID_FileSaveDialog) &&
        !IsEqualCLSID(clsid, CLSID_FileOpenDialog))
        return CLASS_E_CLASSNOTAVAILABLE;

    if (bypassed()) {
        IClassFactory* real = nullptr;
        const HRESULT hr = genuineFactory(clsid, &real);
        if (FAILED(hr) || !real) return hr;
        const HRESULT q = real->QueryInterface(riid, ppv);
        real->Release();
        return q;
    }

    auto* f = new (std::nothrow) Factory(clsid);
    if (!f) return E_OUTOFMEMORY;
    const HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return (g_objects == 0 && g_locks == 0) ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
