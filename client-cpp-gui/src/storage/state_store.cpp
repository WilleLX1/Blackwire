#include "blackwire/storage/state_store.hpp"

#include <fstream>
#include <stdexcept>

namespace blackwire {

StateStore::StateStore(std::string path) : path_(std::move(path)) {}

const std::string& StateStore::path() const { return path_; }

ClientState StateStore::Load() const {
    std::ifstream input(path_);
    if (!input.good()) {
        ClientState state;
        state.base_url = "http://localhost:8000";
        return state;
    }

    nlohmann::json json;
    input >> json;
    return json.get<ClientState>();
}

void StateStore::Save(const ClientState& state) const {
    nlohmann::json json = state;
    std::ofstream output(path_, std::ios::trunc);
    if (!output.good()) {
        throw std::runtime_error("Unable to write client state file");
    }
    output << json.dump(2);
}

}  // namespace blackwire
