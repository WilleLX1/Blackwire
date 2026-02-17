#include "blackwire/util/peer_address.hpp"

#include <QUrl>

namespace blackwire {

namespace {

int DefaultPortForScheme(const QString& scheme) {
    if (scheme == "https" || scheme == "wss") {
        return 443;
    }
    return 80;
}

QString NormalizeAuthority(const QString& host, int port, const QString& scheme) {
    const int normalized_port = port > 0 ? port : DefaultPortForScheme(scheme);
    return QString("%1:%2").arg(host.trimmed().toLower()).arg(normalized_port);
}

}  // namespace

std::optional<ParsedPeerAddress> ParsePeerAddress(const QString& value) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    const int at_index = trimmed.indexOf('@');
    if (at_index < 0) {
        return ParsedPeerAddress{trimmed.toLower(), QString(), false};
    }

    if (at_index == 0 || at_index >= trimmed.size() - 1 || trimmed.indexOf('@', at_index + 1) >= 0) {
        return std::nullopt;
    }

    const QString username = trimmed.left(at_index).trimmed().toLower();
    const QString raw_server = trimmed.mid(at_index + 1).trimmed().toLower();
    if (username.isEmpty() || raw_server.isEmpty()) {
        return std::nullopt;
    }
    if (raw_server.contains('/') || raw_server.contains(' ') || raw_server.contains('?')) {
        return std::nullopt;
    }
    return ParsedPeerAddress{username, raw_server, true};
}

QString ServerAuthorityFromBaseUrl(const QString& base_url) {
    QUrl url(base_url.trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        return {};
    }
    return NormalizeAuthority(url.host(), url.port(), url.scheme());
}

bool ServerAuthorityMatchesBaseUrl(const QString& server_authority, const QString& base_url) {
    return server_authority.trimmed().toLower() == ServerAuthorityFromBaseUrl(base_url).trimmed().toLower();
}

}  // namespace blackwire
