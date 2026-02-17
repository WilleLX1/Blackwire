#pragma once

#include <optional>
#include <string>

namespace blackwire {

class ISecretStore {
public:
    virtual ~ISecretStore() = default;
    virtual bool SetSecret(const std::string& key, const std::string& value, std::string* error) = 0;
    virtual std::optional<std::string> GetSecret(const std::string& key, std::string* error) = 0;
    virtual bool DeleteSecret(const std::string& key, std::string* error) = 0;
    virtual bool DeleteSecretsMatching(const std::string& needle, std::string* error) = 0;
};

}  // namespace blackwire
