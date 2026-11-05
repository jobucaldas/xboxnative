// Off-device WinHTTP proxy operations through the production System32 route.
// A local PAC response exercises script evaluation without WPAD or external DNS.
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s error=%lu wsa=%d\n", __LINE__, #x, GetLastError(), WSAGetLastError()); std::exit(1); } } while (0)
struct Route { const char* dll; const char* kind; };
static const Route routes[] = {
#include "../../app/host/mlp_routes.h"
};
template<class T> T api(const char* name) {
    static HMODULE module = LoadLibraryExW(L"winhttp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(module);
    auto address = GetProcAddress(module, name);
    CHECK(address);
    return reinterpret_cast<T>(address);
}
#define API(name) auto name = api<decltype(&::name)>(#name)
int main() {
    bool system = false;
    for (const auto& route : routes)
        if (!_stricmp(route.dll, "winhttp.dll")) system = !strcmp(route.kind, "system");
    CHECK(system);
    API(WinHttpOpen); API(WinHttpCloseHandle);
    API(WinHttpGetIEProxyConfigForCurrentUser); API(WinHttpGetProxyForUrl);
    CHECK(!WinHttpGetIEProxyConfigForCurrentUser(nullptr));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config{};
    CHECK(WinHttpGetIEProxyConfigForCurrentUser(&config));
    // Configuration is environment dependent. Release OS-owned strings without
    // changing the user's proxy settings or assuming a particular proxy policy.
    GlobalFree(config.lpszAutoConfigUrl);
    GlobalFree(config.lpszProxy);
    GlobalFree(config.lpszProxyBypass);
    CHECK(!WinHttpGetProxyForUrl(nullptr, nullptr, nullptr, nullptr));
    CHECK(GetLastError() == ERROR_INVALID_HANDLE);
    HINTERNET session = WinHttpOpen(L"sysx-proxy-regression", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    CHECK(session);
    WINHTTP_AUTOPROXY_OPTIONS options{};
    WINHTTP_PROXY_INFO info{};
    CHECK(!WinHttpGetProxyForUrl(session, L"http://sysx.invalid/probe", &options, &info));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    options.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL;
    CHECK(!WinHttpGetProxyForUrl(session, L"http://sysx.invalid/probe", &options, &info));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    WSADATA data{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(listener != INVALID_SOCKET);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(listener, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0);
    CHECK(listen(listener, 1) == 0);
    int bytes = sizeof(local);
    CHECK(getsockname(listener, reinterpret_cast<sockaddr*>(&local), &bytes) == 0);
    wchar_t pac[128];
    swprintf(pac, 128, L"http://127.0.0.1:%u/proxy.pac", unsigned(ntohs(local.sin_port)));
    std::thread server([&] {
        fd_set readable; FD_ZERO(&readable); FD_SET(listener, &readable);
        timeval deadline{10, 0};
        CHECK(select(0, &readable, nullptr, nullptr, &deadline) == 1);
        SOCKET peer = accept(listener, nullptr, nullptr);
        CHECK(peer != INVALID_SOCKET);
        DWORD timeout = 3000;
        CHECK(setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0);
        std::string request;
        while (request.find("\r\n\r\n") == std::string::npos) {
            char buffer[1024]; int count = recv(peer, buffer, sizeof(buffer), 0);
            CHECK(count > 0 && request.size() + count <= 16384);
            request.append(buffer, count);
        }
        CHECK(request.find("GET /proxy.pac HTTP/1.") == 0);
        const std::string script = "function FindProxyForURL(url, host) { "
            "if (host == 'sysx.invalid' && url == 'http://sysx.invalid/probe') "
            "return 'PROXY 127.0.0.1:12345'; return 'DIRECT'; }";
        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/x-ns-proxy-autoconfig\r\nContent-Length: "
            + std::to_string(script.size()) + "\r\nConnection: close\r\n\r\n" + script;
        size_t sent = 0;
        while (sent < response.size()) {
            int count = send(peer, response.data() + sent, int(response.size() - sent), 0);
            CHECK(count > 0); sent += count;
        }
        CHECK(closesocket(peer) == 0);
    });
    options.lpszAutoConfigUrl = pac;
    CHECK(WinHttpGetProxyForUrl(session, L"http://sysx.invalid/probe", &options, &info));
    CHECK(info.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY);
    CHECK(info.lpszProxy && !wcscmp(info.lpszProxy, L"127.0.0.1:12345"));
    CHECK(!info.lpszProxyBypass);
    GlobalFree(info.lpszProxy);
    GlobalFree(info.lpszProxyBypass);
    server.join();
    CHECK(closesocket(listener) == 0);
    CHECK(WSACleanup() == 0);
    CHECK(WinHttpCloseHandle(session));
    CHECK(!WinHttpCloseHandle(session));
    CHECK(GetLastError() == ERROR_INVALID_HANDLE);
    std::puts("SYSX PROXY OFFDEVICE PASS: system route, config ownership, invalid options, local PAC download/evaluation, closed handle (not Xbox proof)");
}
