// SPDX-License-Identifier: GPL-3.0-or-later
// Owned /MD C++ DLL using the C++ standard library DLL (msvcp140.dll), as games
// built with the VC++ redistributable do. Play maps Wine's msvcp140 from its
// runtime directory (no redistributable on the console); exceptions thrown
// inside msvcp140 must be caught here, and streams, strings, mutexes and
// exception_ptr must work through it.
#include <windows.h>
#include <exception>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" __declspec(dllexport) int native_msvcp_test() {
    if (!GetModuleHandleW(L"msvcp140.dll")) return 10;
    // Formatted output through the library's streams and locale facets.
    std::ostringstream out;
    out << "mistria " << 42 << ' ' << 2.5;
    if (out.str() != "mistria 42 2.5") return 20;
    std::istringstream in("7 11");
    int a = 0, b = 0; in >> a >> b;
    if (a != 7 || b != 11) return 21;
    // Exceptions thrown by msvcp140 itself (std::_Xout_of_range), caught here.
    std::vector<int> values{1, 2, 3};
    try {(void)values.at(9); return 30;}
    catch (const std::out_of_range& error) {if (!error.what() || !*error.what()) return 31;}
    catch (...) {return 32;}
    std::string text = "abc";
    try {(void)text.substr(10); return 33;}
    catch (const std::out_of_range&) {}
    // std::mutex is implemented by msvcp140 (_Mtx_*).
    std::mutex mutex;
    {std::lock_guard<std::mutex> lock(mutex);}
    if (!mutex.try_lock()) return 40;
    mutex.unlock();
    // exception_ptr capture and rethrow (msvcp140 __ExceptionPtr*).
    std::exception_ptr saved;
    try {throw std::runtime_error("kept");} catch (...) {saved = std::current_exception();}
    if (!saved) return 50;
    try {std::rethrow_exception(saved);}
    catch (const std::runtime_error& error) {if (std::string(error.what()) != "kept") return 51;}
    catch (...) {return 52;}
    return 42;
}
