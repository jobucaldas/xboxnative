// Off-device operation checks for all game-imported IP Helper exports.
// Read-only: no adapter changes, external connections or account access.
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s error=%lu\n", __LINE__, #x, GetLastError()); return 1; } } while (0)
struct Route { const char* dll; const char* kind; };
static const Route routes[] = {
#include "../../app/host/mlp_routes.h"
};
int main() {
    bool system = false;
    for (const auto& route : routes)
        if (!_stricmp(route.dll, "iphlpapi.dll")) system = !strcmp(route.kind, "system");
    CHECK(system);
    HMODULE module = LoadLibraryExW(L"iphlpapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(module);
    auto adapters = reinterpret_cast<decltype(&GetAdaptersAddresses)>(GetProcAddress(module, "GetAdaptersAddresses"));
    auto entry = reinterpret_cast<decltype(&GetIfEntry)>(GetProcAddress(module, "GetIfEntry"));
    auto params = reinterpret_cast<decltype(&GetNetworkParams)>(GetProcAddress(module, "GetNetworkParams"));
    CHECK(adapters && entry && params);
    CHECK(entry(nullptr) == ERROR_INVALID_PARAMETER);
    CHECK(params(nullptr, nullptr) == ERROR_INVALID_PARAMETER);
    ULONG bytes = 0;
    CHECK(adapters(AF_INET, 0, nullptr, nullptr, &bytes) == ERROR_BUFFER_OVERFLOW);
    CHECK(bytes >= sizeof(IP_ADAPTER_ADDRESSES));
    std::vector<BYTE> storage;
    ULONG status = ERROR_BUFFER_OVERFLOW;
    // Adapter changes can race the sizing call. Retry only sizing failures.
    for (unsigned attempt = 0; attempt < 4 && status == ERROR_BUFFER_OVERFLOW; ++attempt) {
        storage.resize(bytes);
        status = adapters(AF_INET, 0, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &bytes);
    }
    CHECK(status == NO_ERROR);
    auto first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    unsigned count = 0;
    bool loopback = false;
    for (auto adapter = first; adapter; adapter = adapter->Next) {
        CHECK(++count < 1024);
        CHECK(adapter->Length >= offsetof(IP_ADAPTER_ADDRESSES, Ipv6IfIndex));
        CHECK(adapter->AdapterName && adapter->AdapterName[0]);
        CHECK(adapter->IfIndex != 0);
        MIB_IFROW row{};
        row.dwIndex = adapter->IfIndex;
        CHECK(entry(&row) == NO_ERROR);
        CHECK(row.dwIndex == adapter->IfIndex);
        CHECK(row.dwType == adapter->IfType);
        CHECK(row.dwMtu == adapter->Mtu);
        CHECK(row.dwPhysAddrLen == adapter->PhysicalAddressLength);
        CHECK(row.dwPhysAddrLen <= MAXLEN_PHYSADDR);
        CHECK(!std::memcmp(row.bPhysAddr, adapter->PhysicalAddress, row.dwPhysAddrLen));
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) loopback = true;
    }
    CHECK(count > 0 && loopback);
    bytes = 0;
    CHECK(params(nullptr, &bytes) == ERROR_BUFFER_OVERFLOW);
    CHECK(bytes >= sizeof(FIXED_INFO));
    storage.clear();
    status = ERROR_BUFFER_OVERFLOW;
    for (unsigned attempt = 0; attempt < 4 && status == ERROR_BUFFER_OVERFLOW; ++attempt) {
        storage.resize(bytes);
        status = params(reinterpret_cast<FIXED_INFO*>(storage.data()), &bytes);
    }
    CHECK(status == NO_ERROR);
    auto info = reinterpret_cast<const FIXED_INFO*>(storage.data());
    CHECK(std::memchr(info->HostName, 0, sizeof(info->HostName)));
    CHECK(std::memchr(info->DomainName, 0, sizeof(info->DomainName)));
    CHECK(std::memchr(info->DnsServerList.IpAddress.String, 0, sizeof(info->DnsServerList.IpAddress.String)));
    CHECK(FreeLibrary(module));
    std::puts("PASS iphlpapi: adapter sizing, interface metadata, loopback, network parameters and null errors");
    return 0;
}
