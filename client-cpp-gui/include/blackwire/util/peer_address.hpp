#pragma once

#include <optional>

#include <QString>

namespace blackwire {

struct ParsedPeerAddress {
    QString username;
    QString server_authority;
    bool has_server = false;
};

std::optional<ParsedPeerAddress> ParsePeerAddress(const QString& value);
QString ServerAuthorityFromBaseUrl(const QString& base_url);
bool ServerAuthorityMatchesBaseUrl(const QString& server_authority, const QString& base_url);

}  // namespace blackwire
