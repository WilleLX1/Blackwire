#include "blackwire/storage/windows_credential_store.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace blackwire {

namespace {

std::wstring ToWide(const std::string& value) {
    if (value.empty()) {
        return std::wstring();
    }

    const int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), count);
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    return wide;
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), count, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0') {
        utf8.pop_back();
    }
    return utf8;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

}  // namespace

bool WindowsCredentialStore::SetSecret(const std::string& key, const std::string& value, std::string* error) {
    CREDENTIALW cred = {};
    const std::wstring target = ToWide(key);
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(value.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.data()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<LPWSTR>(L"blackwire");

    if (!CredWriteW(&cred, 0)) {
        if (error != nullptr) {
            *error = "CredWriteW failed";
        }
        return false;
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::optional<std::string> WindowsCredentialStore::GetSecret(const std::string& key, std::string* error) {
    PCREDENTIALW cred = nullptr;
    const std::wstring target = ToWide(key);
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) {
        if (GetLastError() == ERROR_NOT_FOUND) {
            if (error != nullptr) {
                error->clear();
            }
            return std::nullopt;
        }
        if (error != nullptr) {
            *error = "CredReadW failed";
        }
        return std::nullopt;
    }

    std::string result;
    if (cred->CredentialBlobSize > 0 && cred->CredentialBlob != nullptr) {
        result.assign(
            reinterpret_cast<const char*>(cred->CredentialBlob),
            reinterpret_cast<const char*>(cred->CredentialBlob) + cred->CredentialBlobSize);
    }
    CredFree(cred);

    if (error != nullptr) {
        error->clear();
    }
    return result;
}

bool WindowsCredentialStore::DeleteSecret(const std::string& key, std::string* error) {
    const std::wstring target = ToWide(key);
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) {
        if (error != nullptr) {
            *error = "CredDeleteW failed";
        }
        return false;
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool WindowsCredentialStore::DeleteSecretsMatching(const std::string& needle, std::string* error) {
    if (needle.empty()) {
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    DWORD count = 0;
    PCREDENTIALW* credentials = nullptr;
    if (!CredEnumerateW(nullptr, 0, &count, &credentials)) {
        if (error != nullptr) {
            *error = "CredEnumerateW failed";
        }
        return false;
    }

    const std::string lowered_needle = Lower(needle);
    for (DWORD i = 0; i < count; ++i) {
        const auto* cred = credentials[i];
        if (cred == nullptr || cred->TargetName == nullptr) {
            continue;
        }

        const std::string target = ToUtf8(cred->TargetName);
        // Use prefix match instead of substring to prevent accidentally
        // deleting unrelated system credentials that happen to contain
        // the needle somewhere in their name.
        const std::string lowered_target = Lower(target);
        if (lowered_target.rfind(lowered_needle, 0) != 0) {
            continue;
        }

        CredDeleteW(cred->TargetName, CRED_TYPE_GENERIC, 0);
    }

    CredFree(credentials);

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace blackwire

#else

namespace blackwire {

bool WindowsCredentialStore::SetSecret(const std::string&, const std::string&, std::string* error) {
    if (error != nullptr) {
        *error = "Windows credential store only supported on Windows";
    }
    return false;
}

std::optional<std::string> WindowsCredentialStore::GetSecret(const std::string&, std::string* error) {
    if (error != nullptr) {
        *error = "Windows credential store only supported on Windows";
    }
    return std::nullopt;
}

bool WindowsCredentialStore::DeleteSecret(const std::string&, std::string* error) {
    if (error != nullptr) {
        *error = "Windows credential store only supported on Windows";
    }
    return false;
}

bool WindowsCredentialStore::DeleteSecretsMatching(const std::string&, std::string* error) {
    if (error != nullptr) {
        *error = "Windows credential store only supported on Windows";
    }
    return false;
}

}  // namespace blackwire

#endif
