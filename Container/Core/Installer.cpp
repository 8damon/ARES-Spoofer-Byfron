#include "../Header/Installer.h"
#include "../Header/Watchdog.h"
#include "../Services/Services.hpp"

#include <filesystem>
#include <thread>
#include <windows.h>
#include <winioctl.h>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>
#include <string_view>
#include <iostream>
#include <chrono>

#pragma comment(lib, "Kernel32.lib")

namespace fs = std::filesystem;

namespace {

    enum class Target {
        None = 0,
        Bloxstrap,
        Fishstrap,
        RobloxInstaller
    };

    struct Cache {
        bool         valid{ false };
        std::wstring path;
        Target       which{ Target::None };
        bool         fromUsn{ false };
    };

    static std::mutex gCacheMtx;
    static Cache      gCache;

    static std::wstring FormatWinErr(DWORD err) {
        wchar_t* buf = nullptr;
        DWORD n = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
        std::wstring s = (n && buf) ? std::wstring(buf, n) : L"(unknown)";
        if (buf) LocalFree(buf);
        while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n')) s.pop_back();
        return s;
    }

    static inline bool ieq(std::wstring_view a, const wchar_t* b) {
        return _wcsicmp(std::wstring(a).c_str(), b) == 0;
    }

    static inline bool IsNtfsVolume(wchar_t letter) {
        wchar_t root[] = { letter, L':', L'\\', 0 };
        wchar_t fsname[MAX_PATH] = { 0 };
        if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fsname, MAX_PATH))
            return false;
        return _wcsicmp(fsname, L"NTFS") == 0;
    }

    static inline std::vector<wchar_t> GetNtfsDrives() {
        std::vector<wchar_t> out;
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (mask & (1u << i)) {
                wchar_t letter = static_cast<wchar_t>(L'A' + i);
                wchar_t root[] = { letter, L':', L'\\', 0 };
                UINT type = GetDriveTypeW(root);
                if (type == DRIVE_FIXED && IsNtfsVolume(letter)) {
                    out.push_back(letter);
                }
            }
        }
        return out;
    }

    static inline HANDLE OpenVolumeHandle(wchar_t letter) {
        wchar_t vol[] = { L'\\', L'\\', L'.', L'\\', letter, L':', 0 };
        HANDLE h = CreateFileW(
            vol, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        return (h == INVALID_HANDLE_VALUE) ? nullptr : h;
    }

    struct DirNode { ULONGLONG parent{}; std::wstring name; };
    struct Candidate { wchar_t drive{}; ULONGLONG parent{}; std::wstring filename; Target which{ Target::None }; };

    struct ParsedUSN {
        ULONGLONG         frn{};
        ULONGLONG         parent{};
        std::wstring_view name{};
        bool              isDir{};
        DWORD             recordLen{};
    };

    static inline bool ParseOneUSNRecord(BYTE* base, DWORD bytes, DWORD& pos, ParsedUSN& out) {
        if (pos + sizeof(DWORD) > bytes) return false;
        DWORD recLen = *reinterpret_cast<DWORD*>(base + pos);
        if (recLen == 0 || pos + recLen > bytes) return false;

        WORD major = *reinterpret_cast<WORD*>(base + pos + 4);
        if (major < 2) { pos += recLen; return false; }

        ULONGLONG frn = *reinterpret_cast<ULONGLONG*>(base + pos + 8);
        ULONGLONG parent = *reinterpret_cast<ULONGLONG*>(base + pos + 16);
        DWORD attrs = *reinterpret_cast<DWORD*>(base + pos + 52);
        USHORT nameLen = *reinterpret_cast<USHORT*>(base + pos + 56);
        USHORT nameOff = *reinterpret_cast<USHORT*>(base + pos + 58);

        const WCHAR* namePtr = reinterpret_cast<const WCHAR*>(base + pos + nameOff);
        std::wstring_view name{ namePtr, nameLen / sizeof(WCHAR) };

        out.frn = frn;
        out.parent = parent;
        out.name = name;
        out.isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.recordLen = recLen;

        pos += recLen;
        return true;
    }

    static inline std::optional<std::wstring>
        ReconstructPath(wchar_t drive, ULONGLONG parentFrn,
            const std::unordered_map<ULONGLONG, DirNode>& dirMap,
            const std::wstring& filename)
    {
        std::vector<std::wstring> comps;
        comps.push_back(filename);

        ULONGLONG cur = parentFrn;
        size_t guard = 0;
        while (cur != 0 && guard++ < 32768) {
            auto it = dirMap.find(cur);
            if (it == dirMap.end()) break;
            comps.push_back(it->second.name);
            cur = it->second.parent;
        }

        if (comps.empty()) return std::nullopt;

        std::wstring full;
        full.reserve(260);
        full.push_back(drive);
        full.append(L":\\");
        for (size_t i = comps.size(); i-- > 0;) {
            full.append(comps[i]);
            if (i != 0) full.push_back(L'\\');
        }
        return full;
    }

    static inline bool CollectWithUSN(
        wchar_t drive,
        std::unordered_map<ULONGLONG, DirNode>& dirMap,
        std::vector<Candidate>& matches,
        std::wstring& errOut)
    {
        HANDLE hVol = OpenVolumeHandle(drive);
        if (!hVol) {
            errOut = L"CreateFile on volume failed for drive " + std::wstring(1, drive) + L": " + FormatWinErr(GetLastError());
            return false;
        }

        USN_JOURNAL_DATA_V0 journalData{};
        DWORD bytesRet = 0;
        if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
            &journalData, sizeof(journalData), &bytesRet, nullptr))
        {
            DWORD err = GetLastError();
            errOut = L"USN Journal not present on " + std::wstring(1, drive) + L": " + FormatWinErr(err);
            CloseHandle(hVol);
            return false;
        }

        MFT_ENUM_DATA_V1 med{};
        med.StartFileReferenceNumber = 0;
        med.LowUsn = 0;
        med.HighUsn = MAXLONGLONG;

        std::vector<BYTE> buf(1 << 20); // 1MB
        DWORD bytes = 0;
        bool any = false;

        while (DeviceIoControl(hVol, FSCTL_ENUM_USN_DATA,
            &med, sizeof(med),
            buf.data(), static_cast<DWORD>(buf.size()),
            &bytes, nullptr))
        {
            any = true;
            if (bytes < sizeof(USN)) break;

            DWORD pos = sizeof(USN);
            ParsedUSN rec{};
            while (ParseOneUSNRecord(buf.data(), bytes, pos, rec)) {
                if (rec.isDir) {
                    dirMap.emplace(rec.frn, DirNode{ rec.parent, std::wstring(rec.name) });
                    continue;
                }

                if (ieq(rec.name, L"Bloxstrap.exe")) {
                    matches.push_back({ drive, rec.parent, std::wstring(rec.name), Target::Bloxstrap });
                }
                else if (ieq(rec.name, L"Fishstrap.exe")) {
                    matches.push_back({ drive, rec.parent, std::wstring(rec.name), Target::Fishstrap });
                }
                else if (ieq(rec.name, L"RobloxPlayerInstaller.exe")) {
                    matches.push_back({ drive, rec.parent, std::wstring(rec.name), Target::RobloxInstaller });
                }
            }

            med.StartFileReferenceNumber = *reinterpret_cast<USN*>(buf.data());
        }

        DWORD lastErr = GetLastError();
        if (!any) {
            if (lastErr == ERROR_INVALID_PARAMETER) {
                errOut = L"FSCTL_ENUM_USN_DATA failed on drive " + std::wstring(1, drive) + L": USN Journal not properly configured.";
            }
            else {
                errOut = L"FSCTL_ENUM_USN_DATA failed on drive " + std::wstring(1, drive) + L": " + FormatWinErr(lastErr);
            }
        }

        CloseHandle(hVol);
        return any;
    }

    struct Pick { Target which; std::wstring path; };

    static inline std::optional<Pick> ScanOnceFast(std::wstring& diag) {
        auto drives = GetNtfsDrives();
        if (drives.empty()) {
            diag += L"No fixed NTFS volumes detected.\n";
            return std::nullopt;
        }

        bool anyUsnWorked = false;

        for (wchar_t d : drives) {
            std::unordered_map<ULONGLONG, DirNode> dirMap;
            std::vector<Candidate> matches;
            std::wstring err;

            bool ok = CollectWithUSN(d, dirMap, matches, err);
            if (!ok) {
                if (!err.empty()) diag += L"" + err + L"\n";
                continue;
            }
            anyUsnWorked = true;

            if (matches.empty()) {
                diag += L"USN scan on " + std::wstring(1, d) + L": found 0 candidates.\n";
                continue;
            }

            std::optional<Pick> blox, fish, rbx;

            for (const auto& m : matches) {
                auto full = ReconstructPath(m.drive, m.parent, dirMap, m.filename);
                if (!full) continue;

                std::error_code ec;
                if (!fs::exists(*full, ec) || ec) {
                    diag += L"candidate does not exist: " + *full + L"\n";
                    continue;
                }

                switch (m.which) {
                case Target::Bloxstrap:       if (!blox) blox = Pick{ m.which, *full }; break;
                case Target::Fishstrap:       if (!fish) fish = Pick{ m.which, *full }; break;
                case Target::RobloxInstaller: if (!rbx)  rbx = Pick{ m.which, *full }; break;
                default: break;
                }
            }

            if (blox) return blox;
            if (fish) return fish;
            if (rbx)  return rbx;
        }

        if (!anyUsnWorked) {
            diag += L"USN scan did not succeed on any NTFS drive (need admin or USN not available).\n";
        }
        return std::nullopt;
    }

    static std::optional<Pick> FallbackCommon(std::wstring& diag) {
        auto user = TsService::GetUser();
        auto sys = TsService::GetSysDrive();

        std::vector<std::pair<Target, std::wstring>> candidates = {
            { Target::Bloxstrap,       user + L"\\AppData\\Local\\Bloxstrap\\Bloxstrap.exe" },
            { Target::Fishstrap,       user + L"\\AppData\\Local\\Fishstrap\\Fishstrap.exe" },
            { Target::RobloxInstaller, sys + L"\\Program Files (x86)\\Roblox\\Versions\\RobloxPlayerInstaller.exe" }
        };

        for (auto& c : candidates) {
            std::error_code ec;
            if (fs::exists(c.second, ec) && !ec) {
                return Pick{ c.first, c.second };
            }
            else {
                diag += L"fallback not found: " + c.second + L"\n";
            }
        }
        return std::nullopt;
    }

    static std::optional<Pick> FallbackShortcuts(std::wstring& diag) {
        auto user = TsService::GetUser();
        auto sys = TsService::GetSysDrive();

        struct Lnk { Target which; std::wstring path; };
        std::vector<Lnk> lnks = {
            { Target::Bloxstrap, sys + L"\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Bloxstrap.lnk" },
            { Target::Fishstrap, sys + L"\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Fishstrap.lnk" },
            { Target::Bloxstrap, user + L"\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Bloxstrap.lnk" },
            { Target::Fishstrap, user + L"\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Fishstrap.lnk" }
        };

        for (auto& l : lnks) {
            std::error_code ec;
            if (!fs::exists(l.path, ec) || ec) {
                diag += L"shortcut missing: " + l.path + L"\n";
                continue;
            }
            auto target = TsService::ResolveTarget(l.path);
            if (!target.empty()) {
                std::error_code ec2;
                if (fs::exists(target, ec2) && !ec2) {
                    return Pick{ l.which, fs::path(target).wstring() };
                }
            }
            diag += L"shortcut target invalid: " + l.path + L"\n";
        }
        return std::nullopt;
    }

    static std::optional<Pick> FallbackBoundedProbe(std::wstring& diag) {
        auto user = TsService::GetUser();
        auto sys = TsService::GetSysDrive();

        std::vector<fs::path> roots = {
            fs::path(sys) / L"Program Files",
            fs::path(sys) / L"Program Files (x86)",
            fs::path(user) / L"AppData\\Local"
        };

        const size_t kMaxEntries = 200000;
        const int    kMaxDepth = 8;

        size_t seen = 0;
        auto probe = [&](const wchar_t* name) -> std::optional<std::wstring> {
            for (auto& r : roots) {
                std::error_code ec;
                if (!fs::exists(r, ec) || ec) continue;

                std::vector<std::pair<fs::path, int>> stack;
                stack.emplace_back(r, 0);

                while (!stack.empty()) {
                    auto [cur, depth] = stack.back();
                    stack.pop_back();

                    if (seen++ > kMaxEntries) {
                        diag += L"probe aborted: hit entry cap.\n";
                        return std::nullopt;
                    }
                    if (depth > kMaxDepth) continue;

                    ec.clear();
                    for (auto it = fs::directory_iterator(cur,
                        fs::directory_options::skip_permission_denied, ec);
                        it != fs::directory_iterator(); ++it)
                    {
                        if (ec) { ec.clear(); continue; }

                        const fs::path& p = it->path();
                        if (p.has_filename() && _wcsicmp(p.filename().c_str(), name) == 0) {
                            return p.wstring();
                        }

                        if (it->is_directory(ec) && !ec) {
                            stack.emplace_back(p, depth + 1);
                        }
                    }
                }
            }
            return std::nullopt;
            };

        if (auto b = probe(L"Bloxstrap.exe"))       return Pick{ Target::Bloxstrap, *b };
        if (auto f = probe(L"Fishstrap.exe"))       return Pick{ Target::Fishstrap, *f };
        if (auto r = probe(L"RobloxPlayerInstaller.exe")) return Pick{ Target::RobloxInstaller, *r };

        diag += L"bounded probe found no targets.\n";
        return std::nullopt;
    }

    static const Cache& GetPreferredCached(bool forceRescan, std::wstring& diag) {
        std::scoped_lock lk(gCacheMtx);

        if (gCache.valid && !forceRescan) {
            diag += L"using cached path: " + gCache.path + L"\n";
            return gCache;
        }

        if (auto pick = ScanOnceFast(diag)) {
            gCache.valid = true;
            gCache.which = pick->which;
            gCache.path = std::move(pick->path);
            gCache.fromUsn = true;
            diag += L"USN pick: " + gCache.path + L"\n";
            return gCache;
        }

        if (auto s = FallbackShortcuts(diag)) {
            gCache.valid = true;
            gCache.which = s->which;
            gCache.path = std::move(s->path);
            gCache.fromUsn = false;
            diag += L"shortcut/common pick: " + gCache.path + L"\n";
            return gCache;
        }
        if (auto c = FallbackCommon(diag)) {
            gCache.valid = true;
            gCache.which = c->which;
            gCache.path = std::move(c->path);
            gCache.fromUsn = false;
            diag += L"common-path pick: " + gCache.path + L"\n";
            return gCache;
        }

        if (auto p = FallbackBoundedProbe(diag)) {
            gCache.valid = true;
            gCache.which = p->which;
            gCache.path = std::move(p->path);
            gCache.fromUsn = false;
            diag += L"bounded-probe pick: " + gCache.path + L"\n";
            return gCache;
        }

        gCache = Cache{};
        diag += L"no candidates found by any method.\n";
        return gCache;
    }

    template <typename OnPid>
    void LaunchSelected(const std::wstring& path, bool passPlayerArg, OnPid onPid) {
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpFile = path.c_str();
        sei.lpParameters = passPlayerArg ? L"-player" : nullptr;
        sei.nShow = SW_HIDE;

        if (ShellExecuteExW(&sei)) {
            if (sei.hProcess) {
                DWORD pid = GetProcessId(sei.hProcess);
                if (pid) onPid(pid);
                CloseHandle(sei.hProcess);
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
        else {
            DWORD e = GetLastError();
            std::wcerr << L"[!] ShellExecuteExW failed (" << e << L"): " << FormatWinErr(e) << L"\n";
        }
    }
}

namespace Installer {

    void Install(TITAN::Watchdog& wd) {
        TsService::SectHeader("Roblox Installation", 203);

        std::wstring diag;
        const auto& pick = GetPreferredCached(/*forceRescan=*/false, diag);

        if (!diag.empty()) {
            std::wcerr << diag << L"\n";
        }

        if (!pick.valid || pick.which == Target::None || pick.path.empty()) {
            std::wcerr << L"[!] No launcher/installer found.\n";
            return;
        }

        const bool needsPlayerArg =
            (pick.which == Target::Bloxstrap || pick.which == Target::Fishstrap);

        LaunchSelected(pick.path, needsPlayerArg, [&](DWORD pid) {
            if (pid) wd.addIgnoredPid(pid); // ignore our own Roblox launch
            });
    }

    void Install() {
        TsService::SectHeader("Roblox Installation (DLL)", 203);

        std::wstring diag;
        const auto& pick = GetPreferredCached(/*forceRescan=*/false, diag);

        if (!diag.empty()) {
            std::wcerr << diag << L"\n";
        }

        if (!pick.valid || pick.which == Target::None || pick.path.empty()) {
            std::wcerr << L"[!] No launcher/installer found.\n";
            return;
        }

        const bool needsPlayerArg =
            (pick.which == Target::Bloxstrap || pick.which == Target::Fishstrap);

        LaunchSelected(pick.path, needsPlayerArg, [](DWORD) {
            // no-op: DLL doesn't track PIDs
            });
    }

} // namespace Installer