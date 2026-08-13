#include "dialog_server.h"
#include "log.h"
#include "../shim/protocol.h"

#include <vector>

namespace oriel {

using namespace oriel::proto;

bool DialogServer::start(HWND ui) {
    if (worker_.joinable()) return true;
    ui_ = ui;
    wake_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!wake_) return false;
    worker_ = std::thread(&DialogServer::serve, this);
    return true;
}

void DialogServer::stop() {
    if (!worker_.joinable()) return;
    quit_ = true;
    SetEvent(wake_);
    // Poke the pipe so a blocking connect returns.
    HANDLE h = CreateFileW(pipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    worker_.join();
    CloseHandle(wake_);
    wake_ = nullptr;
}

void DialogServer::serve() {
    const std::wstring name = pipeName();
    logf("dialog server listening on %ls", name.c_str());

    while (!quit_) {
        // Default security: the pipe is per-session and only this user's
        // processes need to reach it.
        HANDLE pipe = CreateNamedPipeW(
            name.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            logf("CreateNamedPipe failed %lu", GetLastError());
            Sleep(500);
            continue;
        }

        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        DWORD err = GetLastError();
        if (!connected && err == ERROR_IO_PENDING) {
            HANDLE h[2]{ ov.hEvent, wake_ };
            const DWORD w = WaitForMultipleObjects(2, h, FALSE, INFINITE);
            if (w != WAIT_OBJECT_0) { CloseHandle(ov.hEvent); CloseHandle(pipe); break; }
            DWORD moved = 0;
            connected = GetOverlappedResult(pipe, &ov, &moved, FALSE);
        } else if (!connected && err == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        }
        CloseHandle(ov.hEvent);
        if (!connected || quit_) { CloseHandle(pipe); if (quit_) break; continue; }

        std::vector<uint8_t> buf(64 * 1024);
        DWORD read = 0;
        if (!ReadFile(pipe, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr) || read == 0) {
            DisconnectNamedPipe(pipe); CloseHandle(pipe); continue;
        }

        const uint8_t* p = buf.data();
        const uint8_t* end = p + read;
        Header h{};
        if (static_cast<size_t>(end - p) < sizeof(h)) { DisconnectNamedPipe(pipe); CloseHandle(pipe); continue; }
        memcpy(&h, p, sizeof(h));
        p += sizeof(h);

        PickJob job;
        bool sane = (h.magic == kMagic && h.version == kVersion);
        if (sane) sane = getString(p, end, &job.folder);
        if (sane) sane = getString(p, end, &job.fileName);
        // Filters are optional: an older shim simply stops here, and a picker
        // with no filter shows everything, which is a working outcome.
        if (sane) {
            uint32_t n = 0;
            if (getU32(p, end, &n) && n <= 256) {
                for (uint32_t i = 0; i < n; ++i) {
                    std::wstring label, spec;
                    if (!getString(p, end, &label) || !getString(p, end, &spec)) break;
                    job.types.push_back({ std::move(label), std::move(spec) });
                }
                uint32_t idx = 0;
                if (getU32(p, end, &idx)) job.typeIndex = static_cast<int>(idx);
            }
        }

        Status status = Status::Decline;
        std::wstring path;

        if (sane) {
            job.save  = (h.mode == static_cast<uint32_t>(Mode::Save));
            job.owner = reinterpret_cast<HWND>(static_cast<uintptr_t>(h.owner));
            job.done  = CreateEventW(nullptr, TRUE, FALSE, nullptr);

            // The picker is a window, so it has to be built on the UI thread.
            if (job.done && PostMessageW(ui_, WM_ORIEL_PICK, 0,
                                         reinterpret_cast<LPARAM>(&job))) {
                WaitForSingleObject(job.done, INFINITE);
                if (job.served) {
                    status = job.cancelled ? Status::Cancelled : Status::Picked;
                    path = job.path;
                }
            }
            if (job.done) CloseHandle(job.done);
        } else {
            logf("malformed request; declining so the caller falls back");
        }

        std::vector<uint8_t> reply;
        const uint32_t magic = kMagic, version = kVersion, st = static_cast<uint32_t>(status);
        auto put32 = [&](uint32_t v) {
            const auto* q = reinterpret_cast<const uint8_t*>(&v);
            reply.insert(reply.end(), q, q + 4);
        };
        put32(magic); put32(version); put32(st);
        putString(reply, path);

        DWORD wrote = 0;
        WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &wrote, nullptr);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    logf("dialog server stopped");
}

} // namespace oriel
