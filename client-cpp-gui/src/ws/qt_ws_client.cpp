#include "blackwire/ws/qt_ws_client.hpp"

#include <algorithm>
#include <utility>

#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>

#include <nlohmann/json.hpp>

namespace blackwire {

namespace {

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

void ApplySocketProxy(QWebSocket* socket, const QUrl& base_url) {
    if (socket == nullptr) {
        return;
    }
    if (IsOnionHost(base_url.host())) {
        socket->setProxy(BuildTorProxyFromEnv());
        return;
    }
    socket->setProxy(QNetworkProxy::NoProxy);
}

QNetworkRequest BuildWsRequest(const QUrl& url, const std::string& access_token) {
    QNetworkRequest request(url);
    request.setRawHeader(
        "Authorization",
        QString("Bearer %1").arg(QString::fromStdString(access_token)).toUtf8());
    return request;
}

}  // namespace

QtWsClient::QtWsClient(QObject* parent) : QObject(parent) {
    reconnect_timer_.setSingleShot(true);

    QObject::connect(&socket_, &QWebSocket::connected, this, [this]() {
        reconnect_attempt_ = 0;
        if (on_status_) {
            on_status_(true);
        }
    });

    QObject::connect(&socket_, &QWebSocket::disconnected, this, [this]() {
        if (on_status_) {
            on_status_(false);
        }
        if (should_reconnect_) {
            ScheduleReconnect();
        }
    });

    QObject::connect(&socket_, &QWebSocket::textMessageReceived, this, [this](const QString& text) {
        HandleTextMessage(text);
    });

    QObject::connect(&socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (on_error_) {
            QString error = socket_.errorString();
            const QUrl base(QString::fromStdString(base_url_));
            if (IsOnionHost(base.host())) {
                error += QString(" (configure Tor SOCKS5 proxy at %1)").arg(TorProxyFromEnv());
            }
            on_error_(error.toStdString());
        }
    });

    QObject::connect(&reconnect_timer_, &QTimer::timeout, this, [this]() {
        if (should_reconnect_) {
            socket_.open(BuildWsRequest(BuildWsUrl(), access_token_));
        }
    });
}

void QtWsClient::SetHandlers(
    MessageHandler on_message,
    CallIncomingHandler on_call_incoming,
    CallRingingHandler on_call_ringing,
    CallAcceptedHandler on_call_accepted,
    CallRejectedHandler on_call_rejected,
    CallBusyHandler on_call_busy,
    CallEndedHandler on_call_ended,
    CallAudioHandler on_call_audio,
    CallErrorHandler on_call_error,
    ErrorHandler on_error,
    StatusHandler on_status) {
    on_message_ = std::move(on_message);
    on_call_incoming_ = std::move(on_call_incoming);
    on_call_ringing_ = std::move(on_call_ringing);
    on_call_accepted_ = std::move(on_call_accepted);
    on_call_rejected_ = std::move(on_call_rejected);
    on_call_busy_ = std::move(on_call_busy);
    on_call_ended_ = std::move(on_call_ended);
    on_call_audio_ = std::move(on_call_audio);
    on_call_error_ = std::move(on_call_error);
    on_error_ = std::move(on_error);
    on_status_ = std::move(on_status);
}

void QtWsClient::Connect(const std::string& base_url, const std::string& access_token) {
    base_url_ = base_url;
    access_token_ = access_token;
    should_reconnect_ = true;
    ApplySocketProxy(&socket_, QUrl(QString::fromStdString(base_url_)));
    socket_.open(BuildWsRequest(BuildWsUrl(), access_token_));
}

void QtWsClient::Disconnect() {
    should_reconnect_ = false;
    reconnect_timer_.stop();
    socket_.close();
}

void QtWsClient::SendAck(const std::string& message_id) {
    nlohmann::json ack = {{"type", "message.ack"}, {"message_id", message_id}};
    socket_.sendTextMessage(QString::fromStdString(ack.dump()));
}

void QtWsClient::SendCallOffer(const VoiceCallOffer& offer) {
    nlohmann::json payload = offer;
    payload["type"] = "call.offer";
    socket_.sendTextMessage(QString::fromStdString(payload.dump()));
}

void QtWsClient::SendCallAccept(const VoiceCallAccept& accept) {
    nlohmann::json payload = accept;
    payload["type"] = "call.accept";
    socket_.sendTextMessage(QString::fromStdString(payload.dump()));
}

void QtWsClient::SendCallReject(const VoiceCallReject& reject) {
    nlohmann::json payload = reject;
    payload["type"] = "call.reject";
    socket_.sendTextMessage(QString::fromStdString(payload.dump()));
}

void QtWsClient::SendCallEnd(const VoiceCallEnd& end) {
    nlohmann::json payload = end;
    payload["type"] = "call.end";
    socket_.sendTextMessage(QString::fromStdString(payload.dump()));
}

void QtWsClient::SendCallAudioChunk(const VoiceAudioChunk& chunk) {
    nlohmann::json payload = chunk;
    payload["type"] = "call.audio";
    socket_.sendTextMessage(QString::fromStdString(payload.dump()));
}

void QtWsClient::ScheduleReconnect() {
    reconnect_attempt_ += 1;
    const int max_ms = 30000;
    const int delay = std::min(max_ms, 1000 * (1 << std::min(5, reconnect_attempt_)));
    reconnect_timer_.start(delay);
}

QUrl QtWsClient::BuildWsUrl() const {
    QUrl base{QString::fromStdString(base_url_)};
    const QString scheme = base.scheme() == "https" ? "wss" : "ws";

    QUrl url;
    url.setScheme(scheme);
    url.setHost(base.host());
    url.setPort(base.port(base.scheme() == "https" ? 443 : 80));
    url.setPath("/api/v1/ws");
    return url;
}

void QtWsClient::HandleTextMessage(const QString& message_text) {
    try {
        const auto payload = nlohmann::json::parse(message_text.toStdString());
        const auto type = payload.value("type", std::string{});
        if (type == "message.new") {
            WsEventMessageNew event = payload.get<WsEventMessageNew>();
            if (on_message_) {
                on_message_(event);
            }
            return;
        }

        if (type == "call.incoming") {
            WsEventCallIncoming event = payload.get<WsEventCallIncoming>();
            if (on_call_incoming_) {
                on_call_incoming_(event);
            }
            return;
        }

        if (type == "call.ringing") {
            WsEventCallRinging event = payload.get<WsEventCallRinging>();
            if (on_call_ringing_) {
                on_call_ringing_(event);
            }
            return;
        }

        if (type == "call.accepted") {
            WsEventCallAccepted event = payload.get<WsEventCallAccepted>();
            if (on_call_accepted_) {
                on_call_accepted_(event);
            }
            return;
        }

        if (type == "call.rejected") {
            WsEventCallRejected event = payload.get<WsEventCallRejected>();
            if (on_call_rejected_) {
                on_call_rejected_(event);
            }
            return;
        }

        if (type == "call.busy") {
            WsEventCallBusy event = payload.get<WsEventCallBusy>();
            if (on_call_busy_) {
                on_call_busy_(event);
            }
            return;
        }

        if (type == "call.ended") {
            WsEventCallEnded event = payload.get<WsEventCallEnded>();
            if (on_call_ended_) {
                on_call_ended_(event);
            }
            return;
        }

        if (type == "call.audio") {
            WsEventCallAudio event = payload.get<WsEventCallAudio>();
            if (on_call_audio_) {
                on_call_audio_(event);
            }
            return;
        }

        if (type == "call.error") {
            WsEventCallError event = payload.get<WsEventCallError>();
            if (on_call_error_) {
                on_call_error_(event);
            } else if (on_error_) {
                const std::string detail = event.detail.empty() ? "Voice call error" : event.detail;
                on_error_(detail);
            }
            return;
        }

        if (type == "error") {
            if (on_error_) {
                on_error_("WebSocket server error: " + message_text.toStdString());
            }
            return;
        }

        if (on_error_) {
            on_error_("WebSocket server error: " + message_text.toStdString());
        }
    } catch (const std::exception& ex) {
        if (on_error_) {
            on_error_(std::string("Invalid websocket payload: ") + ex.what());
        }
    }
}

}  // namespace blackwire
