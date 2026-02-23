#include "blackwire/api/qt_api_client.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace blackwire {

namespace {

nlohmann::json ParseJsonPayload(const QByteArray& payload) {
    if (payload.isEmpty()) {
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(payload.constData(), payload.constData() + payload.size());
}

QString JoinUrl(const std::string& base_url, const QString& path) {
    QString base = QString::fromStdString(base_url);
    if (base.endsWith('/')) {
        base.chop(1);
    }
    return base + "/api/v2" + path;
}

bool IsOnionHost(const QString& host) {
    return host.trimmed().toLower().endsWith(".onion");
}

QString TorProxyFromEnv() {
    const QString configured = qEnvironmentVariable("BLACKWIRE_TOR_SOCKS5_URL").trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }
    return "socks5h://127.0.0.1:9050";
}

QNetworkProxy BuildTorProxyFromEnv() {
    QString proxy_value = TorProxyFromEnv();

    QUrl proxy_url(proxy_value);
    QString host;
    int port = 9050;
    QString username;
    QString password;

    if (proxy_url.isValid() && !proxy_url.host().trimmed().isEmpty()) {
        host = proxy_url.host().trimmed();
        port = proxy_url.port(9050);
        username = proxy_url.userName();
        password = proxy_url.password();
    } else {
        const QStringList parts = proxy_value.split(':', Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            host = parts.front().trimmed();
        }
        if (parts.size() >= 2) {
            const int parsed_port = parts.back().trimmed().toInt();
            if (parsed_port > 0) {
                port = parsed_port;
            }
        }
    }

    if (host.isEmpty()) {
        host = "127.0.0.1";
    }

    QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, host, static_cast<quint16>(port), username, password);
    proxy.setCapabilities(proxy.capabilities() | QNetworkProxy::HostNameLookupCapability);
    return proxy;
}

void ApplyProxyForUrl(QNetworkAccessManager* manager, const QUrl& url) {
    if (manager == nullptr) {
        return;
    }
    if (IsOnionHost(url.host())) {
        manager->setProxy(BuildTorProxyFromEnv());
        return;
    }
    manager->setProxy(QNetworkProxy::NoProxy);
}

}  // namespace

QtApiClient::QtApiClient(QObject* parent) : QObject(parent) {}

AuthResponse QtApiClient::Register(
    const std::string& base_url,
    const std::string& username,
    const std::string& password) {
    nlohmann::json body = {{"username", username}, {"password", password}};
    const auto json = RequestJson("POST", JoinUrl(base_url, "/auth/register"), "", &body);
    return json.get<AuthResponse>();
}

AuthResponse QtApiClient::Login(
    const std::string& base_url,
    const std::string& username,
    const std::string& password) {
    nlohmann::json body = {{"username", username}, {"password", password}};
    const auto json = RequestJson("POST", JoinUrl(base_url, "/auth/login"), "", &body);
    return json.get<AuthResponse>();
}

AuthResponse QtApiClient::Refresh(const std::string& base_url, const std::string& refresh_token) {
    nlohmann::json body = {{"refresh_token", refresh_token}};
    const auto json = RequestJson("POST", JoinUrl(base_url, "/auth/refresh"), "", &body);
    return json.get<AuthResponse>();
}

void QtApiClient::Logout(const std::string& base_url, const std::string& refresh_token) {
    nlohmann::json body = {{"refresh_token", refresh_token}};
    RequestJson("POST", JoinUrl(base_url, "/auth/logout"), "", &body);
}

UserOut QtApiClient::Me(const std::string& base_url, const std::string& access_token) {
    const auto json = RequestJson("GET", JoinUrl(base_url, "/me"), QString::fromStdString(access_token), nullptr);
    return json.get<UserOut>();
}

AuthResponse QtApiClient::RegisterDevice(
    const std::string& base_url,
    const std::string& bootstrap_token,
    const DeviceRegisterRequest& request) {
    const nlohmann::json body = request;
    const auto json = RequestJson(
        "POST",
        JoinUrl(base_url, "/devices/register"),
        QString::fromStdString(bootstrap_token),
        &body);
    return json.get<AuthResponse>();
}

AuthResponse QtApiClient::BindDevice(
    const std::string& base_url,
    const std::string& bootstrap_token,
    const std::string& device_uid,
    const std::string& nonce,
    long long timestamp_ms,
    const std::string& proof_signature_b64) {
    nlohmann::json body = {
        {"device_uid", device_uid},
        {"nonce", nonce},
        {"timestamp_ms", timestamp_ms},
        {"proof_signature_b64", proof_signature_b64},
    };
    const auto json = RequestJson(
        "POST",
        JoinUrl(base_url, "/auth/bind-device"),
        QString::fromStdString(bootstrap_token),
        &body);
    return json.get<AuthResponse>();
}

std::vector<DeviceOut> QtApiClient::ListDevices(
    const std::string& base_url,
    const std::string& access_token) {
    const auto json = RequestJson(
        "GET",
        JoinUrl(base_url, "/devices"),
        QString::fromStdString(access_token),
        nullptr);
    return json.get<std::vector<DeviceOut>>();
}

DeviceOut QtApiClient::RevokeDevice(
    const std::string& base_url,
    const std::string& access_token,
    const std::string& device_uid) {
    const auto json = RequestJson(
        "POST",
        JoinUrl(base_url, QString("/devices/%1/revoke").arg(QString::fromStdString(device_uid))),
        QString::fromStdString(access_token),
        nullptr);
    return json.get<DeviceOut>();
}

UserDeviceLookup QtApiClient::GetUserDevice(
    const std::string& base_url,
    const std::string& access_token,
    const std::string& peer_address) {
    const QString peer = QString::fromStdString(peer_address).trimmed();
    QString endpoint;
    if (peer.contains('@')) {
        QUrl url(JoinUrl(base_url, "/users/resolve-devices"));
        QUrlQuery query;
        query.addQueryItem("peer_address", peer);
        url.setQuery(query);
        endpoint = url.toString();
    } else {
        QUrl url(JoinUrl(base_url, "/users/resolve-devices"));
        QUrlQuery query;
        query.addQueryItem("peer_address", QString("%1@local.invalid").arg(peer));
        url.setQuery(query);
        endpoint = url.toString();
    }

    const auto json = RequestJson(
        "GET",
        endpoint,
        QString::fromStdString(access_token),
        nullptr);
    return json.get<UserDeviceLookup>();
}

ConversationOut QtApiClient::CreateDm(
    const std::string& base_url,
    const std::string& access_token,
    const std::string& peer_address,
    const std::string& peer_username) {
    nlohmann::json body = nlohmann::json::object();
    if (!peer_address.empty()) {
        body["peer_address"] = peer_address;
    }
    if (!peer_username.empty()) {
        body["peer_username"] = peer_username;
    }
    const auto json = RequestJson(
        "POST",
        JoinUrl(base_url, "/conversations/dm"),
        QString::fromStdString(access_token),
        &body);
    return json.get<ConversationOut>();
}

std::vector<ConversationOut> QtApiClient::ListConversations(
    const std::string& base_url,
    const std::string& access_token) {
    const auto json = RequestJson(
        "GET",
        JoinUrl(base_url, "/conversations"),
        QString::fromStdString(access_token),
        nullptr);
    return json.get<std::vector<ConversationOut>>();
}

std::vector<MessageOut> QtApiClient::ListMessages(
    const std::string& base_url,
    const std::string& access_token,
    const std::string& conversation_id,
    int limit,
    int offset) {
    QUrl url(JoinUrl(base_url, QString("/conversations/%1/messages").arg(QString::fromStdString(conversation_id))));
    QUrlQuery query;
    query.addQueryItem("limit", QString::number(limit));
    query.addQueryItem("offset", QString::number(offset));
    url.setQuery(query);

    const auto json = RequestJson("GET", url.toString(), QString::fromStdString(access_token), nullptr);
    std::vector<MessageOut> out;
    if (!json.is_array()) {
        return out;
    }
    for (const auto& item : json) {
        if (item.contains("message")) {
            auto message = item.at("message").get<MessageOut>();
            if (item.contains("envelope_json")) {
                message.envelope = item.at("envelope_json").get<CipherEnvelope>();
            }
            out.push_back(message);
            continue;
        }
        out.push_back(item.get<MessageOut>());
    }
    return out;
}

MessageSendResponse QtApiClient::SendMessage(
    const std::string& base_url,
    const std::string& access_token,
    const MessageSendRequest& request) {
    const nlohmann::json body = request;
    const auto json = RequestJson(
        "POST",
        JoinUrl(base_url, "/messages/send"),
        QString::fromStdString(access_token),
        &body);
    return json.get<MessageSendResponse>();
}

nlohmann::json QtApiClient::RequestJson(
    const QString& method,
    const QString& url,
    const QString& bearer_token,
    const nlohmann::json* body) {
    const QUrl request_url(url);
    ApplyProxyForUrl(&network_, request_url);

    QNetworkRequest request{request_url};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    if (!bearer_token.isEmpty()) {
        request.setRawHeader("Authorization", QString("Bearer %1").arg(bearer_token).toUtf8());
    }

    QNetworkReply* reply = nullptr;
    const QByteArray payload = body == nullptr ? QByteArray{} : QByteArray::fromStdString(body->dump());

    if (method == "GET") {
        reply = network_.get(request);
    } else if (method == "POST") {
        reply = network_.post(request, payload);
    } else {
        reply = network_.sendCustomRequest(request, method.toUtf8(), payload);
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(15000);
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    } else {
        reply->abort();
        const QString message = QString("Request timed out: %1 %2").arg(method, url);
        reply->deleteLater();
        throw ApiException(0, message.toStdString());
    }

    const auto status_attr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status_code = status_attr.isValid() ? status_attr.toInt() : 0;
    const QByteArray data = reply->readAll();

    if (reply->error() != QNetworkReply::NoError && status_code == 0) {
        QString error = reply->errorString();
        if (IsOnionHost(request_url.host())) {
            error += QString(" (configure Tor SOCKS5 proxy at %1)").arg(TorProxyFromEnv());
        }
        reply->deleteLater();
        throw ApiException(0, error.toStdString());
    }

    if (status_code >= 400) {
        std::string message;
        try {
            const auto err_json = ParseJsonPayload(data);
            if (err_json.contains("detail")) {
                message = err_json["detail"].get<std::string>();
            }
        } catch (...) {
        }
        if (message.empty()) {
            message = data.toStdString();
        }
        reply->deleteLater();
        throw ApiException(status_code, message);
    }

    nlohmann::json json = nlohmann::json::object();
    if (!data.isEmpty()) {
        json = ParseJsonPayload(data);
    }

    reply->deleteLater();
    return json;
}

}  // namespace blackwire
