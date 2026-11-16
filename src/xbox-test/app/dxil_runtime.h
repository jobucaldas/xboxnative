// Runtime DXIL validator download for Mesa's OpenGL-on-D3D12 path.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "vendor/zlib/zlib.h"
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace dxil_runtime {
static constexpr char kUrl[] = "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/dxc_2025_07_14.zip";
static constexpr char kArchiveSha256[] = "9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4";
static constexpr char kDllSha256[] = "0cf5fa4b90f74550166108b3870c883f4a85243976b246b1da3ea90ad5b92015";
static constexpr size_t kMaxArchive = 64u * 1024u * 1024u;
static constexpr size_t kMaxMember = 16u * 1024u * 1024u;

inline uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t* p) { return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16; }

// Extract exactly one named ZIP member; reject encrypted, oversized, malformed,
// or unsupported entries. The digest callback returns lowercase SHA-256 hex.
inline bool extract(const std::vector<uint8_t>& zip, const char* wanted,
                    const char* expectedSha, std::vector<uint8_t>& output,
                    const std::function<std::string(const void*, size_t)>& digest,
                    std::string& reason) {
    if (zip.size() < 22 || zip.size() > kMaxArchive) { reason = "archive size"; return false; }
    size_t eocd = std::string::npos;
    const size_t begin = zip.size() > 65557 ? zip.size() - 65557 : 0;
    for (size_t i = zip.size() - 22;; --i) {
        if (u32(zip.data() + i) == 0x06054b50u) { eocd = i; break; }
        if (i == begin) break;
    }
    if (eocd == std::string::npos || eocd + 22 > zip.size()) { reason = "zip directory missing"; return false; }
    const uint16_t count = u16(zip.data() + eocd + 10);
    const uint32_t cdBytes = u32(zip.data() + eocd + 12), cdOffset = u32(zip.data() + eocd + 16);
    if (uint64_t(cdOffset) + cdBytes > eocd) { reason = "zip directory bounds"; return false; }
    size_t at = cdOffset; bool found = false;
    uint32_t local = 0, packed = 0, unpacked = 0, crc = 0; uint16_t method = 0;
    for (uint16_t n = 0; n < count; ++n) {
        if (at + 46 > zip.size() || u32(zip.data() + at) != 0x02014b50u) { reason = "zip directory entry"; return false; }
        const uint16_t flags = u16(zip.data() + at + 8), m = u16(zip.data() + at + 10);
        const uint32_t c = u32(zip.data() + at + 16), ps = u32(zip.data() + at + 20), us = u32(zip.data() + at + 24);
        const uint16_t nameLen = u16(zip.data() + at + 28), extraLen = u16(zip.data() + at + 30), commentLen = u16(zip.data() + at + 32);
        const uint32_t lo = u32(zip.data() + at + 42);
        if (at + 46u + nameLen + extraLen + commentLen > zip.size()) { reason = "zip directory name bounds"; return false; }
        if (nameLen == strlen(wanted) && !memcmp(zip.data() + at + 46, wanted, nameLen)) {
            if (flags & 1) { reason = "encrypted zip member"; return false; }
            if (us > kMaxMember || ps > kMaxMember || (m != 0 && m != 8)) { reason = "zip member limit or method"; return false; }
            found = true; local = lo; packed = ps; unpacked = us; crc = c; method = m;
        }
        at += 46u + nameLen + extraLen + commentLen;
    }
    if (!found || uint64_t(local) + 30 > zip.size() || u32(zip.data() + local) != 0x04034b50u) { reason = "zip member missing"; return false; }
    const size_t dataAt = size_t(local) + 30u + u16(zip.data() + local + 26) + u16(zip.data() + local + 28);
    if (uint64_t(dataAt) + packed > zip.size()) { reason = "zip member bounds"; return false; }
    output.assign(unpacked, 0);
    if (method == 0) {
        if (packed != unpacked) { reason = "stored zip size"; return false; }
        if (unpacked) memcpy(output.data(), zip.data() + dataAt, unpacked);
    } else {
        z_stream z{}; z.next_in = const_cast<Bytef*>(zip.data() + dataAt); z.avail_in = packed;
        z.next_out = output.empty() ? nullptr : output.data(); z.avail_out = unpacked;
        if (inflateInit2(&z, -15) != Z_OK) { reason = "inflate init"; return false; }
        const int result = inflate(&z, Z_FINISH); inflateEnd(&z);
        if (result != Z_STREAM_END || z.total_out != unpacked || z.total_in != packed) { reason = "inflate failed"; return false; }
    }
    if (crc32(0, output.data(), static_cast<uInt>(output.size())) != crc) { reason = "zip crc mismatch"; return false; }
    if (*expectedSha && digest(output.data(), output.size()) != expectedSha) { reason = "dll hash mismatch"; return false; }
    return true;
}

#ifdef _WIN32
inline std::string sha256(const void* data, size_t size) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr; UCHAR bytes[32]{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(alg, 0); return {}; }
    for (size_t at = 0; at < size;) { ULONG n = static_cast<ULONG>(std::min<size_t>(size - at, 1u << 30)); if (BCryptHashData(hash, (PUCHAR)data + at, n, 0) < 0) { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0); return {}; } at += n; }
    if (BCryptFinishHash(hash, bytes, sizeof bytes, 0) < 0) { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0); return {}; }
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
    static const char hex[] = "0123456789abcdef"; std::string out; out.reserve(64);
    for (UCHAR b : bytes) { out.push_back(hex[b >> 4]); out.push_back(hex[b & 15]); } return out;
}
inline HANDLE createFile(const std::wstring& path, DWORD access, DWORD share, DWORD disposition) {
    CREATEFILE2_EXTENDED_PARAMETERS parameters{};
    parameters.dwSize = sizeof(parameters);
    parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    return CreateFile2(path.c_str(), access, share, disposition, &parameters);
}
inline bool readFile(const std::wstring& path, std::vector<uint8_t>& bytes) {
    HANDLE h = createFile(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart <= kMaxMember;
    if (ok) { bytes.resize(static_cast<size_t>(size.QuadPart)); DWORD got = 0; ok = ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) && got == bytes.size(); }
    CloseHandle(h); if (!ok) bytes.clear(); return ok;
}
inline bool writeTemp(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    HANDLE h = createFile(path, GENERIC_WRITE, 0, CREATE_ALWAYS);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0; bool ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr) && wrote == bytes.size();
    if (ok) ok = FlushFileBuffers(h) != FALSE; CloseHandle(h); return ok;
}
inline bool exposeDirectory(const std::wstring& dir) {
    static DLL_DIRECTORY_COOKIE cookie = nullptr;
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) return false;
    if (!cookie) cookie = AddDllDirectory(dir.c_str());
    return cookie != nullptr;
}
inline bool moveReplace(const std::wstring& from, const std::wstring& to) {
    using MoveEx = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, DWORD);
    static MoveEx moveEx = reinterpret_cast<MoveEx>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "MoveFileExW"));
    if (moveEx) return moveEx(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (GetFileAttributesW(to.c_str()) != INVALID_FILE_ATTRIBUTES)
        return ReplaceFileW(to.c_str(), from.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != FALSE;
    return MoveFileExW(from.c_str(), to.c_str(), 0) != FALSE;
}
inline bool ensure(const std::wstring& localState, const std::function<void(const char*)>& log) {
    const std::wstring dir = localState + L"\\dxil", dll = dir + L"\\dxil.dll", license = dir + L"\\LICENSE-MS.txt";
    std::vector<uint8_t> cached;
    if (readFile(dll, cached) && sha256(cached.data(), cached.size()) == kDllSha256 && GetFileAttributesW(license.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!exposeDirectory(dir)) { log("dxil: download failed DLL search path; Mesa pipelines may fail PSO creation"); return false; }
        log("dxil: cached"); return true;
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        winrt::Windows::Web::Http::HttpClient http;
        auto response = http.GetAsync(winrt::Windows::Foundation::Uri(winrt::to_hstring(kUrl))).get();
        if (response.StatusCode() != winrt::Windows::Web::Http::HttpStatusCode::Ok) { log("dxil: download failed HTTP status; Mesa pipelines may fail PSO creation"); return false; }
        auto buffer = response.Content().ReadAsBufferAsync().get();
        if (!buffer.Length() || buffer.Length() > kMaxArchive) { log("dxil: download failed archive size; Mesa pipelines may fail PSO creation"); return false; }
        winrt::Windows::Storage::Streams::DataReader reader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
        std::vector<uint8_t> zip(buffer.Length()); reader.ReadBytes(winrt::array_view<uint8_t>(zip));
        if (sha256(zip.data(), zip.size()) != kArchiveSha256) { log("dxil: download failed archive hash mismatch; Mesa pipelines may fail PSO creation"); return false; }
        auto digest = [](const void* p, size_t n) { return sha256(p, n); };
        std::vector<uint8_t> dllBytes, licenseBytes; std::string reason;
        if (!extract(zip, "bin/x64/dxil.dll", kDllSha256, dllBytes, digest, reason) ||
            !extract(zip, "LICENSE-MS.txt", "", licenseBytes, digest, reason)) {
            const std::string message = "dxil: download failed " + reason + "; Mesa pipelines may fail PSO creation";
            log(message.c_str()); return false;
        }
        const std::wstring dllTmp = dll + L".tmp", licenseTmp = license + L".tmp";
        if (!writeTemp(dllTmp, dllBytes) || !writeTemp(licenseTmp, licenseBytes) ||
            !moveReplace(dllTmp, dll) || !moveReplace(licenseTmp, license)) {
            DeleteFileW(dllTmp.c_str()); DeleteFileW(licenseTmp.c_str()); log("dxil: download failed cache write; Mesa pipelines may fail PSO creation"); return false;
        }
        if (!exposeDirectory(dir)) { log("dxil: download failed DLL search path; Mesa pipelines may fail PSO creation"); return false; }
        const std::string digest8 = sha256(dllBytes.data(), dllBytes.size()).substr(0, 8);
        std::string message = "dxil: download ok sha8=" + digest8; log(message.c_str()); return true;
    } catch (...) { log("dxil: download failed HTTP exception; Mesa pipelines may fail PSO creation"); return false; }
}
#endif
} // namespace dxil_runtime
