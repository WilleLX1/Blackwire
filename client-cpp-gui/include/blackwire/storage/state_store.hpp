#pragma once

#include <string>

#include "blackwire/storage/client_state.hpp"

namespace blackwire {

class StateStore {
public:
    explicit StateStore(std::string path);

    const std::string& path() const;

    ClientState Load() const;
    void Save(const ClientState& state) const;

private:
    std::string path_;
};

}  // namespace blackwire
