// Proves the shim without touching the registry.
//
// Loads oriel_dialog.dll directly and asks it for a dialog exactly the way COM
// would after the per-user override is in place. Nothing here changes system
// state, so the interception can be validated before it is ever switched on.

#include <windows.h>
#include <shobjidl.h>
#include <cstdio>
#include <thread>
#include <chrono>

static int failures = 0;

static void check(bool ok, const char* what, HRESULT hr = S_OK) {
    if (ok) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s (hr=0x%08X)\n", what, hr);
        ++failures;
    }
}

// Takes the dialog back down the moment it finishes opening.
//
// An earlier version hunted for the window and posted WM_CLOSE; it missed, and
// left a dialog sitting on the user's screen. Asking the dialog to close itself
// through its own event sink runs on its own thread and cannot miss.
class AutoClose final : public IFileDialogEvents {
public:
    bool fired = false;

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IFileDialogEvents) {
            *ppv = static_cast<IFileDialogEvents*>(this);
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

    IFACEMETHODIMP OnFileOk(IFileDialog*) override { return S_OK; }
    IFACEMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP OnFolderChange(IFileDialog* d) override {
        fired = true;
        d->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        return S_OK;
    }
    IFACEMETHODIMP OnSelectionChange(IFileDialog*) override { return S_OK; }
    IFACEMETHODIMP OnShareViolation(IFileDialog*, IShellItem*,
                                    FDE_SHAREVIOLATION_RESPONSE* r) override {
        if (r) *r = FDESVR_DEFAULT;
        return S_OK;
    }
    IFACEMETHODIMP OnTypeChange(IFileDialog*) override { return S_OK; }
    IFACEMETHODIMP OnOverwrite(IFileDialog*, IShellItem*,
                               FDE_OVERWRITE_RESPONSE* r) override {
        if (r) *r = FDEOR_DEFAULT;
        return S_OK;
    }

private:
    LONG ref_ = 1;
};

// `shim_test.exe pick` behaves like a real application: it asks the shell for a
// save dialog through plain COM and prints whatever comes back. Whether that is
// Oriel's picker or the genuine dialog is exactly what we want to observe.
static int runAsRealApp() {
    IFileSaveDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dlg));
    if (FAILED(hr)) { printf("CoCreateInstance failed 0x%08X\n", hr); return 1; }
    dlg->SetFileName(L"untitled.txt");
    printf("showing...\n"); fflush(stdout);

    hr = dlg->Show(nullptr);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        printf("RESULT: cancelled\n");
        dlg->Release();
        return 0;
    }
    if (FAILED(hr)) { printf("RESULT: Show failed 0x%08X\n", hr); dlg->Release(); return 1; }

    IShellItem* item = nullptr;
    hr = dlg->GetResult(&item);
    if (SUCCEEDED(hr) && item) {
        LPWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            printf("RESULT: %ls\n", path);
            CoTaskMemFree(path);
        }
        item->Release();
    } else {
        printf("RESULT: GetResult failed 0x%08X\n", hr);
    }
    dlg->Release();
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc > 1 && wcscmp(argv[1], L"pick") == 0) {
        const int rc = runAsRealApp();
        CoUninitialize();
        return rc;
    }

    HMODULE dll = LoadLibraryW(L"oriel_dialog.dll");
    if (!dll) { printf("cannot load oriel_dialog.dll (%lu)\n", GetLastError()); return 1; }
    auto entry = reinterpret_cast<LPFNGETCLASSOBJECT>(GetProcAddress(dll, "DllGetClassObject"));
    if (!entry) { printf("no DllGetClassObject export\n"); return 1; }

    for (int pass = 0; pass < 2; ++pass) {
        const bool save = (pass == 0);
        const CLSID clsid = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
        printf("\n== %s ==\n", save ? "FileSaveDialog" : "FileOpenDialog");

        IClassFactory* factory = nullptr;
        HRESULT hr = entry(clsid, IID_PPV_ARGS(&factory));
        check(SUCCEEDED(hr) && factory, "shim hands out a class factory", hr);
        if (FAILED(hr)) continue;

        IFileDialog* dlg = nullptr;
        hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&dlg));
        factory->Release();
        check(SUCCEEDED(hr) && dlg, "factory creates a dialog", hr);
        if (FAILED(hr)) continue;

        // Forwarding: set something, read it back through the shim.
        hr = dlg->SetTitle(L"Oriel shim self-test");
        check(SUCCEEDED(hr), "SetTitle forwards", hr);

        FILEOPENDIALOGOPTIONS opts = 0;
        hr = dlg->GetOptions(&opts);
        check(SUCCEEDED(hr), "GetOptions forwards", hr);
        hr = dlg->SetOptions(opts | FOS_FORCEFILESYSTEM);
        check(SUCCEEDED(hr), "SetOptions forwards", hr);
        FILEOPENDIALOGOPTIONS back = 0;
        dlg->GetOptions(&back);
        check((back & FOS_FORCEFILESYSTEM) != 0, "the option actually stuck");

        hr = dlg->SetFileName(L"untitled.txt");
        check(SUCCEEDED(hr), "SetFileName forwards", hr);
        LPWSTR name = nullptr;
        hr = dlg->GetFileName(&name);
        check(SUCCEEDED(hr) && name && wcscmp(name, L"untitled.txt") == 0,
              "GetFileName returns what was set", hr);
        if (name) CoTaskMemFree(name);

        // Interfaces the shim implements itself.
        IFileDialog2* d2 = nullptr;
        hr = dlg->QueryInterface(IID_PPV_ARGS(&d2));
        check(SUCCEEDED(hr) && d2, "QI IFileDialog2", hr);
        if (d2) { check(SUCCEEDED(d2->SetCancelButtonLabel(L"やめる")), "IFileDialog2 forwards"); d2->Release(); }

        if (save) {
            IFileSaveDialog* fsd = nullptr;
            hr = dlg->QueryInterface(IID_PPV_ARGS(&fsd));
            check(SUCCEEDED(hr) && fsd, "QI IFileSaveDialog", hr);
            if (fsd) fsd->Release();
            IFileOpenDialog* wrong = nullptr;
            check(FAILED(dlg->QueryInterface(IID_PPV_ARGS(&wrong))),
                  "a save dialog refuses IFileOpenDialog");
            if (wrong) wrong->Release();
        } else {
            IFileOpenDialog* fod = nullptr;
            hr = dlg->QueryInterface(IID_PPV_ARGS(&fod));
            check(SUCCEEDED(hr) && fod, "QI IFileOpenDialog", hr);
            if (fod) fod->Release();
        }

        // Interfaces we do not implement must still reach the inner object.
        IOleWindow* ole = nullptr;
        hr = dlg->QueryInterface(IID_PPV_ARGS(&ole));
        check(SUCCEEDED(hr) && ole, "unknown interfaces fall through to the real dialog", hr);
        if (ole) ole->Release();

        // And a dialog must actually appear. With no Oriel listening this is the
        // fallback path, which is the one that must never fail.
        auto* closer = new AutoClose();
        DWORD cookie = 0;
        hr = dlg->Advise(closer, &cookie);
        check(SUCCEEDED(hr), "Advise accepts an event sink", hr);
        hr = dlg->Show(nullptr);
        check(hr == S_OK || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED),
              "Show puts up a working dialog", hr);
        check(closer->fired, "the dialog really opened (its event sink fired)");
        dlg->Unadvise(cookie);
        closer->Release();

        dlg->Release();
    }

    CoUninitialize();
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all checks passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
