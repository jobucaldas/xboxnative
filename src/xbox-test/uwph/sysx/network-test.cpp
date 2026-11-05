// Off-device operation regression for the game's System32 Winsock routes.
// Uses only IPv4 TCP loopback; no external service or AF_UNIX transport.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s wsa=%d win32=%lu\n", __LINE__, #x, WSAGetLastError(), GetLastError()); std::exit(1); } } while (0)
struct Route { const char* dll; const char* kind; };
static const Route routes[] = {
#include "../../app/host/mlp_routes.h"
};
template<class T> T api(const char* name, WORD ordinal = 0) {
    static HMODULE module = LoadLibraryExW(L"ws2_32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(module);
    auto address = GetProcAddress(module, name);
    CHECK(address);
    if (ordinal && GetProcAddress(module, MAKEINTRESOURCEA(ordinal)) != address) {
        std::printf("FAIL ordinal %u does not resolve %s\n", unsigned(ordinal), name);
        std::exit(1);
    }
    return reinterpret_cast<T>(address);
}
#define API(name, ordinal) auto name = api<decltype(&::name)>(#name, ordinal)
int main() {
    bool system = false;
    for (const auto& route : routes)
        if (!_stricmp(route.dll, "ws2_32.dll")) system = !strcmp(route.kind, "system");
    CHECK(system);
    API(WSAStartup, 115); API(WSACleanup, 116); API(WSAGetLastError, 111);
    API(socket, 23); API(bind, 2); API(listen, 13); API(getsockname, 6);
    API(connect, 4); API(accept, 1); API(closesocket, 3); API(setsockopt, 21);
    API(shutdown, 22); API(send, 19); API(recv, 16); API(ioctlsocket, 10);
    API(select, 18); API(htons, 9); API(ntohs, 15);
    API(getaddrinfo, 0); API(freeaddrinfo, 0); API(getnameinfo, 0);
    API(inet_pton, 0); API(inet_ntop, 0);
    API(WSACreateEvent, 0); API(WSASetEvent, 0); API(WSAResetEvent, 0);
    API(WSAWaitForMultipleEvents, 0); API(WSACloseEvent, 0);
    API(WSASend, 0); API(WSARecv, 0);
    WSADATA data{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    CHECK(data.wVersion == MAKEWORD(2, 2));
    IN_ADDR loopback{};
    CHECK(inet_pton(AF_INET, "127.0.0.1", &loopback) == 1);
    char text[INET_ADDRSTRLEN]{};
    CHECK(inet_ntop(AF_INET, &loopback, text, sizeof(text)) == text);
    CHECK(!strcmp(text, "127.0.0.1"));
    addrinfo hints{}, *addresses = nullptr;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    CHECK(getaddrinfo("127.0.0.1", "12345", &hints, &addresses) == 0);
    CHECK(addresses && addresses->ai_addrlen == sizeof(sockaddr_in));
    char host[64]{}, service[16]{};
    CHECK(getnameinfo(addresses->ai_addr, addresses->ai_addrlen,
        host, sizeof(host), service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) == 0);
    CHECK(!strcmp(host, "127.0.0.1") && !strcmp(service, "12345"));
    freeaddrinfo(addresses);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(listener != INVALID_SOCKET);
    sockaddr_in local{};
    local.sin_family = AF_INET; local.sin_addr = loopback;
    CHECK(bind(listener, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0);
    CHECK(listen(listener, 1) == 0);
    int length = sizeof(local);
    CHECK(getsockname(listener, reinterpret_cast<sockaddr*>(&local), &length) == 0);
    CHECK(length == sizeof(local) && ntohs(local.sin_port) != 0);
    CHECK(htons(ntohs(local.sin_port)) == local.sin_port);
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(client != INVALID_SOCKET);
    CHECK(connect(client, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0);
    SOCKET peer = accept(listener, nullptr, nullptr);
    CHECK(peer != INVALID_SOCKET);
    CHECK(closesocket(listener) == 0);
    const DWORD timeout = 3000;
    CHECK(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0);
    CHECK(setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0);
    CHECK(send(client, "sysx", 4, 0) == 4);
    fd_set readable{}; FD_ZERO(&readable); FD_SET(peer, &readable);
    timeval wait{3, 0};
    CHECK(select(0, &readable, nullptr, nullptr, &wait) == 1);
    CHECK(FD_ISSET(peer, &readable));
    char payload[8]{};
    int received = 0;
    while (received < 4) {
        int count = recv(peer, payload + received, 4 - received, 0);
        CHECK(count > 0); received += count;
    }
    CHECK(!memcmp(payload, "sysx", 4));
    char reply[] = "pong";
    WSABUF buffer{4, reply}; DWORD sent = 0;
    CHECK(WSASend(peer, &buffer, 1, &sent, 0, nullptr, nullptr) == 0 && sent == 4);
    received = 0;
    while (received < 4) {
        buffer.buf = payload + received; buffer.len = 4 - received;
        DWORD count = 0, flags = 0;
        CHECK(WSARecv(client, &buffer, 1, &count, &flags, nullptr, nullptr) == 0);
        CHECK(count > 0); received += count;
    }
    CHECK(!memcmp(payload, "pong", 4));
    u_long nonblocking = 1;
    CHECK(ioctlsocket(client, FIONBIO, &nonblocking) == 0);
    CHECK(recv(client, payload, sizeof(payload), 0) == SOCKET_ERROR);
    CHECK(WSAGetLastError() == WSAEWOULDBLOCK);
    nonblocking = 0;
    CHECK(ioctlsocket(client, FIONBIO, &nonblocking) == 0);
    CHECK(shutdown(peer, SD_SEND) == 0);
    CHECK(recv(client, payload, sizeof(payload), 0) == 0);
    CHECK(closesocket(peer) == 0 && closesocket(client) == 0);
    CHECK(recv(client, payload, sizeof(payload), 0) == SOCKET_ERROR);
    CHECK(WSAGetLastError() == WSAENOTSOCK);
    WSAEVENT event = WSACreateEvent();
    CHECK(event != WSA_INVALID_EVENT);
    CHECK(WSAWaitForMultipleEvents(1, &event, TRUE, 0, FALSE) == WSA_WAIT_TIMEOUT);
    CHECK(WSASetEvent(event));
    CHECK(WSAWaitForMultipleEvents(1, &event, TRUE, 0, FALSE) == WSA_WAIT_EVENT_0);
    CHECK(WSAResetEvent(event));
    CHECK(WSAWaitForMultipleEvents(1, &event, TRUE, 0, FALSE) == WSA_WAIT_TIMEOUT);
    CHECK(WSACloseEvent(event));
    CHECK(WSACleanup() == 0);
    std::puts("SYSX NETWORK OFFDEVICE PASS: System32 ordinals, numeric addresses, TCP, scatter/gather, errors, events (not Xbox proof)");
}
