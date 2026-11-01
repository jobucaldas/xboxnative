#include <windows.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main() {
 HMODULE module = LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 auto query = reinterpret_cast<decltype(&SystemParametersInfoW)>(GetProcAddress(module, "SystemParametersInfoW")); CHECK(query);
 const struct { UINT action, value; } cases[] = {
  {SPI_GETKEYBOARDDELAY, 1}, {SPI_GETKEYBOARDSPEED, 31},
  {SPI_GETMOUSEHOVERWIDTH, 4}, {SPI_GETMOUSEHOVERHEIGHT, 4},
  {SPI_GETMOUSEHOVERTIME, 400}, {SPI_GETMOUSESPEED, 10},
  {SPI_GETWHEELSCROLLLINES, 3}, {SPI_GETWHEELSCROLLCHARS, 3}
 };
 unsigned count = 0;
 for (auto test : cases) {
  // Host settings may be customized. Verify the host contract separately
  // from the record backend's documented fixed session defaults.
  struct { UINT before, value, after; } output = {0x11223344, 0, 0x55667788};
  SetLastError(0xdead); CHECK(SystemParametersInfoW(test.action, 0, &output.value, 0));
  CHECK(GetLastError() == 0xdead);
  CHECK(output.before == 0x11223344 && output.after == 0x55667788);
  SetLastError(0xdead); CHECK(query(test.action, 123, &output.value, 0));
  CHECK(output.value == test.value && GetLastError() == 0xdead);
  CHECK(output.before == 0x11223344 && output.after == 0x55667788);
  SetLastError(0xdead); CHECK(!SystemParametersInfoW(test.action, 0, nullptr, 0)); CHECK(GetLastError() == 0xdead);
  SetLastError(0xdead); CHECK(!query(test.action, 0, nullptr, 0)); CHECK(GetLastError() == 0xdead);
  ++count;
 }
 UINT value = 0x12345678;
 SetLastError(0xdead); CHECK(!query(SPI_SETMOUSESPEED, 0, reinterpret_cast<void*>(15), 0));
 CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
 CHECK(query(SPI_GETMOUSESPEED, 0, &value, 0) && value == 10);
 SetLastError(0xdead); CHECK(!query(0xffffffff, 0, &value, 0));
 CHECK(GetLastError() == ERROR_NOT_SUPPORTED && value == 10);
 printf("PASS: %u input query contracts, unsupported setter/action (off-device)\n", count);
}
