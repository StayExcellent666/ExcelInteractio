// CI-only harness: starts the real updater with an inheritable handle to an
// already-running isolated OBS process. It does not close or install anything.
#include <windows.h>
#include <string>

static std::wstring quote(const std::wstring &value)
{
    std::wstring result = L"\""; size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'"') result += L'\\'; result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 6) return 2;
    wchar_t *end = nullptr; unsigned long pid = wcstoul(argv[5], &end, 10);
    if (!pid || !end || *end) return 2;
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, pid);
    if (!process) return 3;
    SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    auto *bytes = new unsigned char[size]; auto *list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes);
    if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) { delete[] bytes; CloseHandle(process); return 4; }
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &process, sizeof(process), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(list); delete[] bytes; CloseHandle(process); return 5;
    }
    std::wstring command = quote(argv[1]) + L" --wait-handle " + std::to_wstring(reinterpret_cast<uintptr_t>(process)) +
        L" --parent-pid " + std::to_wstring(pid) + L" --installer " + quote(argv[2]) + L" --sha256 " + quote(argv[3]) +
        L" --obs-root " + quote(argv[4]) + L" --restart-obs 0";
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = list;
    PROCESS_INFORMATION child{};
    BOOL started = CreateProcessW(argv[1], command.data(), nullptr, nullptr, TRUE,
                                  EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
                                  &startup.StartupInfo, &child);
    DeleteProcThreadAttributeList(list); delete[] bytes; CloseHandle(process);
    if (!started) return 6;
    CloseHandle(child.hThread); CloseHandle(child.hProcess); return 0;
}
