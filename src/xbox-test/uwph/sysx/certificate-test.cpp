// Off-device crypt32 operation proof. Never changes a system trust store.
#include <windows.h>
#include <wincrypt.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s error=%lu\n", __LINE__, #x, GetLastError()); std::exit(1); } } while (0)
struct Route { const char* dll; const char* kind; };
static const Route routes[] = {
#include "../../app/host/mlp_routes.h"
};
template<class T> T api(const char* name) {
    bool system = false;
    for (const auto& route : routes)
        if (!strcmp(route.dll, "crypt32.dll")) system = !strcmp(route.kind, "system");
    CHECK(system);
    HMODULE module = LoadLibraryExW(L"crypt32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(module);
    auto function = reinterpret_cast<T>(GetProcAddress(module, name));
    CHECK(function);
    return function;
}
#define API(name) auto name = api<decltype(&::name)>(#name)
int main(int argc, char** argv) {
    CHECK(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    CHECK(input.good());
    std::vector<BYTE> encoded((std::istreambuf_iterator<char>(input)), {});
    CHECK(!encoded.empty());
    API(CertOpenStore);
    API(CertAddEncodedCertificateToStore);
    API(CertCloseStore);
    API(CertFreeCertificateContext);
    API(CertGetCertificateChain);
    API(CertVerifyCertificateChainPolicy);
    API(CertFreeCertificateChain);
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0,
        CERT_STORE_CREATE_NEW_FLAG, nullptr);
    CHECK(store);
    PCCERT_CONTEXT certificate = nullptr;
    const BYTE invalid[] = {0x30, 0x01, 0xff};
    CHECK(!CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, invalid,
        sizeof(invalid), CERT_STORE_ADD_ALWAYS, &certificate));
    CHECK(certificate == nullptr);
    CHECK(CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, encoded.data(),
        static_cast<DWORD>(encoded.size()), CERT_STORE_ADD_NEW, &certificate));
    CHECK(certificate && certificate->cbCertEncoded == encoded.size());
    CHECK(!memcmp(certificate->pbCertEncoded, encoded.data(), encoded.size()));
    CHECK(!CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, encoded.data(),
        static_cast<DWORD>(encoded.size()), CERT_STORE_ADD_NEW, nullptr));
    CHECK(GetLastError() == CRYPT_E_EXISTS);
    CERT_CHAIN_PARA parameters{};
    parameters.cbSize = sizeof(parameters);
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    // Generated self-signed fixture has no AIA/CRL endpoints. Cache-only also
    // prevents this regression from making any external certificate requests.
    CHECK(CertGetCertificateChain(nullptr, certificate, nullptr, store, &parameters,
        CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL, nullptr, &chain));
    CHECK(chain && chain->cChain == 1 && chain->rgpChain[0]->cElement == 1);
    CHECK(chain->TrustStatus.dwErrorStatus & CERT_TRUST_IS_UNTRUSTED_ROOT);
    CHECK(!(chain->TrustStatus.dwErrorStatus & CERT_TRUST_IS_NOT_SIGNATURE_VALID));
    CHECK(!(chain->TrustStatus.dwErrorStatus & CERT_TRUST_IS_NOT_TIME_VALID));
    CERT_CHAIN_POLICY_PARA policy{};
    policy.cbSize = sizeof(policy);
    CERT_CHAIN_POLICY_STATUS status{};
    status.cbSize = sizeof(status);
    // TRUE means policy evaluation completed, not that the certificate passed.
    CHECK(CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain, &policy, &status));
    CHECK(status.dwError == CERT_E_UNTRUSTEDROOT);
    CHECK(status.lChainIndex == 0 && status.lElementIndex == 0);
    CertFreeCertificateChain(chain);
    CHECK(CertCloseStore(store, 0));
    // The returned context owns its lifetime independently of the open store.
    CHECK(certificate->cbCertEncoded == encoded.size());
    CHECK(!memcmp(certificate->pbCertEncoded, encoded.data(), encoded.size()));
    CHECK(CertFreeCertificateContext(certificate));
    // Alter only the signature bit string, leaving a parseable certificate.
    // Parsing and building a chain must not turn corruption into trust.
    encoded.back() ^= 1;
    store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
    CHECK(store);
    certificate = nullptr;
    CHECK(CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, encoded.data(),
        static_cast<DWORD>(encoded.size()), CERT_STORE_ADD_NEW, &certificate));
    chain = nullptr;
    CHECK(CertGetCertificateChain(nullptr, certificate, nullptr, store, &parameters,
        CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL, nullptr, &chain));
    CHECK(chain && (chain->TrustStatus.dwErrorStatus & CERT_TRUST_IS_NOT_SIGNATURE_VALID));
    status = {};
    status.cbSize = sizeof(status);
    CHECK(CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain, &policy, &status));
    CHECK(status.dwError == TRUST_E_CERT_SIGNATURE);
    CertFreeCertificateChain(chain);
    CHECK(CertFreeCertificateContext(certificate));
    CHECK(CertCloseStore(store, CERT_CLOSE_STORE_CHECK_FLAG));
    std::puts("SYSX CERTIFICATE OFFDEVICE PASS: DER, duplicate rejection, chain signature/time, untrusted-root and corrupted-signature policies, store/context lifetime (not Xbox proof)");
}
