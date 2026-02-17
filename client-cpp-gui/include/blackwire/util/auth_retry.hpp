#pragma once

#include <chrono>
#include <functional>
#include <thread>

#include "blackwire/interfaces/api_client.hpp"

namespace blackwire {

template <typename T>
T CallWithAuthRetryOnce(const std::function<T()>& operation, const std::function<void()>& refresh) {
    bool refreshed = false;
    int rate_limit_retries = 0;

    while (true) {
        try {
            return operation();
        } catch (const ApiException& ex) {
            if (ex.status_code() == 401 && !refreshed) {
                refresh();
                refreshed = true;
                continue;
            }

            if (ex.status_code() == 429 && rate_limit_retries < 2) {
                const int backoff_ms = 200 * (rate_limit_retries + 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                rate_limit_retries += 1;
                continue;
            }

            throw;
        }
    }
}

}  // namespace blackwire
