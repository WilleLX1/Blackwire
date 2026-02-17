#pragma once

#include <unordered_map>

#include "blackwire/interfaces/secret_store.hpp"

namespace blackwire {

class InMemorySecretStore final : public ISecretStore {
public:
    bool SetSecret(const std::string& key, const std::string& value, std::string* error) override;
    std::optional<std::string> GetSecret(const std::string& key, std::string* error) override;
    bool DeleteSecret(const std::string& key, std::string* error) override;
    bool DeleteSecretsMatching(const std::string& needle, std::string* error) override;

private:
    std::unordered_map<std::string, std::string> values_;
};

}  // namespace blackwire
