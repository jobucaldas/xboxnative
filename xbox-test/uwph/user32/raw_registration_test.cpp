#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
using Register = decltype(&RegisterRawInputDevices);
using Query = decltype(&GetRegisteredRawInputDevices);
static unsigned checks;
static void contracts(Register reg, Query query) {
 const UINT size = sizeof(RAWINPUTDEVICE);
 UINT count = 0;
 SetLastError(0xdead); CHECK(query(nullptr, &count, size) == 0 && count == 0);
 CHECK(GetLastError() == 0xdead);
 CHECK(reg(nullptr, 0, size));
 RAWINPUTDEVICE output[5]; memset(output, 0xa5, sizeof(output));
 auto invalid = [&](RAWINPUTDEVICE* data, UINT* n, UINT bytes) {
  SetLastError(0xdead); CHECK(query(data, n, bytes) == UINT(-1));
  CHECK(GetLastError() == ERROR_INVALID_PARAMETER); ++checks;
 };
 invalid(output, &count, size); CHECK(count == 0);
 invalid(nullptr, nullptr, size);
 invalid(nullptr, &count, size - 1);
 RAWINPUTDEVICE devices[] = {{2, 5, 0, nullptr}, {1, 6, 0, nullptr}, {1, 2, 0, nullptr}};
 SetLastError(0xdead); CHECK(reg(devices, 3, size)); CHECK(GetLastError() == 0xdead);
 count = 0; CHECK(query(nullptr, &count, size) == 0 && count == 3);
 count = 2; SetLastError(0xdead); CHECK(query(output, &count, size) == UINT(-1));
 CHECK(count == 3 && GetLastError() == ERROR_INSUFFICIENT_BUFFER);
 for (auto byte : reinterpret_cast<const BYTE(&)[sizeof(output)]>(output)) CHECK(byte == 0xa5);
 count = 5; SetLastError(0xdead); CHECK(query(output, &count, size) == 3 && count == 3);
 CHECK(GetLastError() == 0xdead);
 CHECK(output[0].usUsagePage == 1 && output[0].usUsage == 2);
 CHECK(output[1].usUsagePage == 1 && output[1].usUsage == 6);
 CHECK(output[2].usUsagePage == 2 && output[2].usUsage == 5);
 for (unsigned i = 3 * size; i < sizeof(output); ++i) CHECK(reinterpret_cast<BYTE*>(output)[i] == 0xa5);
 RAWINPUTDEVICE replacement = {1, 2, RIDEV_NOLEGACY, nullptr};
 CHECK(reg(&replacement, 1, size));
 count = 5; CHECK(query(output, &count, size) == 3 && output[0].dwFlags == RIDEV_NOLEGACY);
 // All records must be validated before any registration changes.
 RAWINPUTDEVICE bad[] = {{3, 1, 0, nullptr}, {1, 2, RIDEV_INPUTSINK, nullptr}};
 SetLastError(0xdead); CHECK(!reg(bad, 2, size)); CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
 count = 5; CHECK(query(output, &count, size) == 3);
 bad[1] = {1, 2, RIDEV_REMOVE, reinterpret_cast<HWND>(0x12345678)};
 SetLastError(0xdead); CHECK(!reg(bad, 2, size)); CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
 count = 5; CHECK(query(output, &count, size) == 3 && output[0].dwFlags == RIDEV_NOLEGACY);
 for (auto& device : devices) device.dwFlags = RIDEV_REMOVE;
 CHECK(reg(devices, 3, size));
 count = 5; CHECK(query(output, &count, size) == 0 && count == 0);
 CHECK(reg(devices, 3, size)); // Removing absent records succeeds.
 checks += 16;
}
int main() {
 HMODULE module = LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 auto reg = reinterpret_cast<Register>(GetProcAddress(module, "RegisterRawInputDevices")); CHECK(reg);
 auto query = reinterpret_cast<Query>(GetProcAddress(module, "GetRegisteredRawInputDevices")); CHECK(query);
 contracts(&RegisterRawInputDevices, &GetRegisteredRawInputDevices);
 contracts(reg, query);
 printf("PASS: %u raw-registration contract groups (Wine and record backend, off-device)\n", checks);
}
