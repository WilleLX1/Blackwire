#include "blackwire/storage/in_memory_secret_store.hpp"

namespace blackwire {

bool InMemorySecretStore::SetSecret(const std::string& key, const std::string& value, std::string* error) {
    values_[key] = value;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::optional<std::string> InMemorySecretStore::GetSecret(const std::string& key, std::string* error) {
    const auto iter = values_.find(key);
    if (iter == values_.end()) {
        if (error != nullptr) {
            error->clear();
        }
        return std::nullopt;
    }

    if (error != nullptr) {
        error->clear();
    }
    return iter->second;
}

bool InMemorySecretStore::DeleteSecret(const std::string& key, std::string* error) {
    values_.erase(key);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool InMemorySecretStore::DeleteSecretsMatching(const std::string& needle, std::string* error) {
    if (needle.empty()) {
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    for (auto it = values_.begin(); it != values_.end();) {
        if (it->first.find(needle) != std::string::npos) {
            it = values_.erase(it);
        } else {
            ++it;
        }
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace blackwire
