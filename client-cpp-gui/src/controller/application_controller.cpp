#include "blackwire/controller/application_controller.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QUuid>
#include <QByteArray>

#include "blackwire/util/auth_retry.hpp"
#include "blackwire/util/conversation_view.hpp"
#include "blackwire/util/diagnostics.hpp"
#include "blackwire/util/message_view.hpp"
#include "blackwire/util/peer_address.hpp"

namespace blackwire {

namespace {

constexpr int kMaxDiagnostics = 200;

QString SanitizeDiagnosticText(QString text) {
    return SanitizeDiagnosticsText(text);
}

}  // namespace

ApplicationController::ApplicationController(
    IApiClient& api_client,
    IWsClient& ws_client,
    IAudioCallEngine& audio_engine,
    ICryptoService& crypto,
    ISecretStore& secret_store,
    StateStore& state_store,
    const QString& profile_name,
    QObject* parent)
    : QObject(parent),
      api_client_(api_client),
      ws_client_(ws_client),
      audio_engine_(audio_engine),
      crypto_(crypto),
      secret_store_(secret_store),
      state_store_(state_store),
      profile_name_(profile_name.trimmed().isEmpty() ? "default" : profile_name.trimmed().toLower().toStdString()) {
    qRegisterMetaType<ConversationListItemView>("ConversationListItemView");
    qRegisterMetaType<std::vector<ConversationListItemView>>("std::vector<ConversationListItemView>");
    qRegisterMetaType<AudioDeviceOptionView>("blackwire::AudioDeviceOptionView");
    qRegisterMetaType<std::vector<AudioDeviceOptionView>>("std::vector<blackwire::AudioDeviceOptionView>");
    qRegisterMetaType<CallStateView>("blackwire::CallStateView");
    qRegisterMetaType<ThreadMessageView>("blackwire::ThreadMessageView");
    qRegisterMetaType<std::vector<ThreadMessageView>>("std::vector<blackwire::ThreadMessageView>");

    ws_client_.SetHandlers(
        [this](const WsEventMessageNew& event) {
            try {
                const auto& msg = event.message;
                if (!state_.MarkMessageSeen(msg.id)) {
                    ws_client_.SendAck(msg.id);
                    return;
                }

                std::string plaintext = "[unable to decrypt]";
                try {
                    std::string error;
                    const auto priv = secret_store_.GetSecret(SecretKey("enc_private"), &error);
                    if (priv.has_value()) {
                        plaintext = crypto_.DecryptWithPrivate(priv.value(), msg.envelope.ciphertext_b64);
                    }
                } catch (...) {
                    plaintext = "[unable to decrypt]";
                }

                ws_client_.SendAck(msg.id);

                LocalMessage local;
                local.id = msg.id;
                local.conversation_id = msg.conversation_id;
                local.sender_user_id = msg.sender_user_id;
                local.created_at = msg.created_at;
                local.rendered_text = RenderMessage(msg, plaintext).toStdString();
                local.plaintext = plaintext;
                if (state_.blocked_conversation_ids.contains(msg.conversation_id)) {
                    PersistState();
                    return;
                }

                const bool pending_request = pending_request_senders_.find(msg.conversation_id) != pending_request_senders_.end();
                if (!ConversationExists(msg.conversation_id) || pending_request) {
                    const bool first_pending = pending_request_senders_.find(msg.conversation_id) == pending_request_senders_.end();
                    if (first_pending) {
                        pending_request_senders_[msg.conversation_id] = QString();
                    }

                    LoadConversations();

                    QString sender_username;
                    QString sender_address;
                    const auto* conversation = FindConversation(msg.conversation_id);
                    if (conversation != nullptr) {
                        sender_username = QString::fromStdString(conversation->peer_username).trimmed().toLower();
                        sender_address = QString::fromStdString(conversation->peer_address).trimmed().toLower();
                        if (sender_username.isEmpty() && !sender_address.isEmpty()) {
                            sender_username = sender_address;
                        }
                        UpsertConversationMeta(
                            msg.conversation_id,
                            sender_username,
                            sender_address,
                            QString(),
                            QString::fromStdString(conversation->created_at));
                    }

                    if (!state_.social_preferences.accept_messages_from_strangers) {
                        state_.blocked_conversation_ids.insert(msg.conversation_id);
                        pending_request_messages_.erase(msg.conversation_id);
                        pending_request_senders_.erase(msg.conversation_id);
                        PersistState();
                        RefreshConversationList();
                        return;
                    }

                    if (first_pending || pending_request_senders_[msg.conversation_id].trimmed().isEmpty()) {
                        pending_request_senders_[msg.conversation_id] = sender_username;
                    }
                    pending_request_messages_[msg.conversation_id].push_back(local);

                    PersistState();
                    RefreshConversationList();

                    if (first_pending) {
                        emit MessageRequestReceived(
                            QString::fromStdString(msg.conversation_id),
                            pending_request_senders_[msg.conversation_id],
                            MessagePreview(QString::fromStdString(plaintext)));
                    }
                    return;
                }

                state_.local_messages[msg.conversation_id].push_back(local);
                UpsertConversationMeta(
                    msg.conversation_id,
                    QString(),
                    QString(),
                    MessagePreview(QString::fromStdString(plaintext)),
                    QString::fromStdString(msg.created_at));
                PersistState();

                const auto thread = RenderThread(msg.conversation_id);
                if (selected_conversation_id_ == msg.conversation_id) {
                    emit ConversationSelected(
                        QString::fromStdString(msg.conversation_id),
                        thread);
                } else if (!thread.empty()) {
                    emit IncomingMessage(
                        QString::fromStdString(msg.conversation_id),
                        thread.back());
                }

                RefreshConversationList();
            } catch (const std::exception& ex) {
                const QString line = QString("WS message handling error: %1").arg(ex.what());
                RecordDiagnostic(line);
                emit ErrorOccurred(line);
            }
        },
        [this](const WsEventCallIncoming& event) {
            if (!IsCallState("idle")) {
                VoiceCallReject reject;
                reject.call_id = event.call_id;
                reject.reason = "busy";
                ws_client_.SendCallReject(reject);
                return;
            }

            TransitionCallState(
                "incoming_ringing",
                QString::fromStdString(event.call_id),
                QString::fromStdString(event.conversation_id),
                QString::fromStdString(
                    event.from_user_address.empty() ? event.from_user_id : event.from_user_address),
                QString());
            emit IncomingCallReceived(call_state_);
        },
        [this](const WsEventCallRinging& event) {
            if (!IsCallState("outgoing_ringing")) {
                return;
            }
            if (!call_state_.call_id.isEmpty() && call_state_.call_id != QString::fromStdString(event.call_id)) {
                return;
            }

            TransitionCallState(
                "outgoing_ringing",
                QString::fromStdString(event.call_id),
                QString::fromStdString(event.conversation_id),
                QString::fromStdString(
                    event.peer_user_address.empty() ? event.peer_user_id : event.peer_user_address),
                QString());
        },
        [this](const WsEventCallAccepted& event) {
            const QString call_id = QString::fromStdString(event.call_id);
            if (!call_state_.call_id.isEmpty() && call_state_.call_id != call_id) {
                return;
            }

            TransitionCallState(
                "active",
                call_id,
                QString::fromStdString(event.conversation_id),
                QString::fromStdString(
                    event.peer_user_address.empty() ? event.peer_user_id : event.peer_user_address),
                QString());

            QString warning;
            QString error;
            if (!StartAudioEngineForActiveCall(&warning, &error)) {
                ReportCallError(QString("Voice call audio start failed: %1").arg(error));
                VoiceCallEnd end_request;
                end_request.call_id = event.call_id;
                end_request.reason = "audio_error";
                ws_client_.SendCallEnd(end_request);
                StopAudioEngine();
                TransitionCallState("idle", QString(), QString(), QString(), "audio_error");
                return;
            }

            if (!warning.trimmed().isEmpty()) {
                ReportCallError(warning);
            }
        },
        [this](const WsEventCallRejected& event) {
            if (!call_state_.call_id.isEmpty() && call_state_.call_id != QString::fromStdString(event.call_id)) {
                return;
            }
            StopAudioEngine();
            TransitionCallState(
                "idle",
                QString(),
                QString::fromStdString(event.conversation_id),
                QString(),
                QString::fromStdString(event.reason.empty() ? "declined" : event.reason));
        },
        [this](const WsEventCallBusy& event) {
            if (!IsCallState("outgoing_ringing")) {
                return;
            }
            StopAudioEngine();
            TransitionCallState(
                "idle",
                QString(),
                QString::fromStdString(event.conversation_id),
                QString(),
                QString::fromStdString(event.reason.empty() ? "busy" : event.reason));
        },
        [this](const WsEventCallEnded& event) {
            const QString call_id = QString::fromStdString(event.call_id);
            if (!call_state_.call_id.isEmpty() && call_state_.call_id != call_id) {
                return;
            }

            StopAudioEngine();
            TransitionCallState(
                "idle",
                QString(),
                QString(),
                QString(),
                QString::fromStdString(event.reason.empty() ? "ended" : event.reason));
        },
        [this](const WsEventCallAudio& event) {
            if (!IsCallState("active")) {
                return;
            }
            if (call_state_.call_id != QString::fromStdString(event.call_id)) {
                return;
            }

            const QByteArray frame = QByteArray::fromBase64(QByteArray::fromStdString(event.pcm_b64));
            if (frame.isEmpty()) {
                return;
            }
            audio_engine_.PushRemoteFrame(frame);
        },
        [this](const WsEventCallError& event) {
            QString message = QString::fromStdString(event.detail);
            if (message.trimmed().isEmpty()) {
                message = QString("Voice call error (%1)").arg(QString::fromStdString(event.code));
            }
            ReportCallError(message);
        },
        [this](const std::string& error) {
            if (IsWebSocketAuthError(error)) {
                ReauthenticateWebSocket();
                return;
            }
            const QString line = QString::fromStdString(error);
            RecordDiagnostic(line);
            emit ErrorOccurred(line);
        },
        [this](bool connected) {
            connection_status_ = connected ? "Connected" : "Disconnected";
            RecordDiagnostic(QString("connection_status=%1").arg(connection_status_));
            emit ConnectionStatusChanged(connection_status_);

            if (!connected && !IsCallState("idle")) {
                StopAudioEngine();
                TransitionCallState("idle", QString(), QString(), QString(), "connection_lost");
            }
        });
}

void ApplicationController::Initialize() {
    try {
        state_ = state_store_.Load();
        if (state_.base_url.empty()) {
            state_.base_url = "http://localhost:8000";
        }

        TransitionCallState("idle", QString(), QString(), QString(), QString());
        LoadAudioDevices();

        RecordDiagnostic("controller initialized");
        emit AuthStateChanged(
            state_.has_user,
            state_.has_user ? QString::fromStdString(state_.user.username) : QString());
        emit DeviceStateChanged(state_.has_device);

        if (state_.has_user && state_.has_device) {
            LoadConversations();
            StartRealtime();
        }
    } catch (const std::exception& ex) {
        const QString line = QString("Initialization failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::SetBaseUrl(const QString& base_url) {
    if (state_.base_url != base_url.toStdString()) {
        peer_device_cache_.clear();
    }
    state_.base_url = base_url.toStdString();
    RecordDiagnostic(QString("base_url set to %1").arg(base_url));
    PersistState();
}

QString ApplicationController::BaseUrl() const {
    return QString::fromStdString(state_.base_url.empty() ? "http://localhost:8000" : state_.base_url);
}

void ApplicationController::Register(const QString& username, const QString& password) {
    try {
        const auto response = api_client_.Register(state_.base_url, username.toStdString(), password.toStdString());
        state_.user = response.user;
        state_.has_user = true;
        state_.has_device = false;
        state_.conversations.clear();
        state_.conversation_meta.clear();
        state_.local_messages.clear();
        state_.seen_message_ids.clear();
        peer_device_cache_.clear();
        pending_request_messages_.clear();
        pending_request_senders_.clear();

        SaveTokenPair(response.tokens);
        PersistState();

        RecordDiagnostic(QString("register success user=%1").arg(QString::fromStdString(state_.user.username)));
        emit AuthStateChanged(true, QString::fromStdString(state_.user.username));
        emit DeviceStateChanged(false);
        LoadAudioDevices();
    } catch (const std::exception& ex) {
        const QString line = QString("Register failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::Login(const QString& username, const QString& password) {
    try {
        const auto response = api_client_.Login(state_.base_url, username.toStdString(), password.toStdString());
        state_.user = response.user;
        state_.has_user = true;
        peer_device_cache_.clear();

        if (state_.has_device && state_.device.user_id != state_.user.id) {
            state_.has_device = false;
            state_.conversations.clear();
            state_.conversation_meta.clear();
            state_.local_messages.clear();
            state_.seen_message_ids.clear();
            peer_device_cache_.clear();
            pending_request_messages_.clear();
            pending_request_senders_.clear();
        }

        SaveTokenPair(response.tokens);
        PersistState();

        RecordDiagnostic(QString("login success user=%1").arg(QString::fromStdString(state_.user.username)));
        emit AuthStateChanged(true, QString::fromStdString(state_.user.username));
        emit DeviceStateChanged(state_.has_device);
        LoadAudioDevices();

        if (state_.has_device) {
            LoadConversations();
            StartRealtime();
        }
    } catch (const std::exception& ex) {
        const QString line = QString("Login failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::Logout() {
    try {
        const auto refresh = RequireRefreshToken();
        api_client_.Logout(state_.base_url, refresh);
    } catch (...) {
    }

    std::string error;
    secret_store_.DeleteSecret(SecretKey("access_token"), &error);
    secret_store_.DeleteSecret(SecretKey("refresh_token"), &error);
    secret_store_.DeleteSecret(SecretKey("ik_private"), &error);
    secret_store_.DeleteSecret(SecretKey("enc_private"), &error);

    StopAudioEngine();
    TransitionCallState("idle", QString(), QString(), QString(), "logged_out");
    StopRealtime();

    ClearInMemoryState();
    PersistState();

    RecordDiagnostic("logout completed");
    emit AuthStateChanged(false, QString());
    emit DeviceStateChanged(false);
    emit ConversationListChanged(std::vector<ConversationListItemView>{});
    emit ConversationSelected(QString(), {});
    LoadAudioDevices();
}

void ApplicationController::SetupDevice(const QString& label) {
    try {
        const auto keys = crypto_.GenerateDeviceKeys();
        DeviceRegisterRequest request;
        request.label = label.toStdString();
        request.ik_ed25519_pub = keys.ik_ed25519_public_b64;
        request.enc_x25519_pub = keys.enc_x25519_public_b64;

        const auto operation = [this, &request]() {
            return api_client_.RegisterDevice(state_.base_url, RequireAccessToken(), request);
        };

        const auto device = CallWithAuthRetryOnce<DeviceOut>(operation, [this]() { RefreshAccessToken(); });

        state_.device = device;
        state_.has_device = true;

        std::string error;
        if (!secret_store_.SetSecret(SecretKey("ik_private"), keys.ik_ed25519_private_b64, &error)) {
            throw std::runtime_error(error);
        }
        if (!secret_store_.SetSecret(SecretKey("enc_private"), keys.enc_x25519_private_b64, &error)) {
            throw std::runtime_error(error);
        }

        PersistState();
        RecordDiagnostic(QString("device setup complete label=%1").arg(label));
        emit DeviceStateChanged(true);
        LoadAudioDevices();

        LoadConversations();
        StartRealtime();
    } catch (const std::exception& ex) {
        const QString line = QString("Device setup failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::LoadConversations() {
    try {
        const auto operation = [this]() {
            return api_client_.ListConversations(state_.base_url, RequireAccessToken());
        };

        state_.conversations = CallWithAuthRetryOnce<std::vector<ConversationOut>>(
            operation,
            [this]() { RefreshAccessToken(); });

        for (const auto& conv : state_.conversations) {
            state_.conversation_meta.try_emplace(conv.id, ConversationMeta{});
            if (!conv.peer_username.empty() || !conv.peer_address.empty()) {
                UpsertConversationMeta(
                    conv.id,
                    QString::fromStdString(conv.peer_username),
                    QString::fromStdString(conv.peer_address),
                    QString(),
                    QString::fromStdString(conv.created_at));
            }
        }

        PersistState();
        RefreshConversationList();
        RecordDiagnostic(QString("loaded conversations=%1").arg(static_cast<int>(state_.conversations.size())));
    } catch (const std::exception& ex) {
        const QString line = QString("Load conversations failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::OpenConversationByPeer(const QString& username) {
    try {
        QString error;
        const auto normalized = NormalizePeerUsername(username, &error);
        if (!normalized.has_value()) {
            emit ErrorOccurred(error);
            return;
        }

        const auto conversation_id = ResolveConversationIdForPeer(*normalized);
        RevealConversation(conversation_id, false);
        const auto conv_it = std::find_if(
            state_.conversations.begin(),
            state_.conversations.end(),
            [&conversation_id](const ConversationOut& c) { return c.id == conversation_id; });
        const QString created_at =
            conv_it == state_.conversations.end() ? QString() : QString::fromStdString(conv_it->created_at);

        QString peer_username = *normalized;
        QString peer_address;
        if (normalized->contains('@')) {
            peer_address = *normalized;
            peer_username = normalized->section('@', 0, 0);
        }
        UpsertConversationMeta(conversation_id, peer_username, peer_address, QString(), created_at);
        PersistState();
        RefreshConversationList();
        SelectConversation(QString::fromStdString(conversation_id));
    } catch (const std::exception& ex) {
        const QString line = QString("Open conversation failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::SelectConversation(const QString& conversation_id) {
    try {
        selected_conversation_id_ = conversation_id.toStdString();

        const auto operation = [this]() {
            return api_client_.ListMessages(
                state_.base_url,
                RequireAccessToken(),
                selected_conversation_id_,
                200,
                0);
        };

        const auto remote_messages = CallWithAuthRetryOnce<std::vector<MessageOut>>(
            operation,
            [this]() { RefreshAccessToken(); });

        std::unordered_map<std::string, LocalMessage> existing_by_id;
        const auto existing_it = state_.local_messages.find(selected_conversation_id_);
        if (existing_it != state_.local_messages.end()) {
            for (const auto& existing : existing_it->second) {
                existing_by_id[existing.id] = existing;
            }
        }

        std::vector<LocalMessage> rebuilt;
        rebuilt.reserve(remote_messages.size());

        std::string error;
        const auto private_key = secret_store_.GetSecret(SecretKey("enc_private"), &error);

        for (const auto& message : remote_messages) {
            auto existing = existing_by_id.find(message.id);
            if (existing != existing_by_id.end()) {
                if (existing->second.plaintext.empty()) {
                    existing->second.plaintext = ExtractLegacyPlaintext(QString::fromStdString(existing->second.rendered_text)).toStdString();
                }
                rebuilt.push_back(existing->second);
                state_.MarkMessageSeen(message.id);
                continue;
            }

            std::string plaintext;
            if (message.sender_user_id == state_.user.id) {
                plaintext = "[encrypted sent message]";
            } else if (private_key.has_value()) {
                try {
                    plaintext = crypto_.DecryptWithPrivate(private_key.value(), message.envelope.ciphertext_b64);
                } catch (...) {
                    plaintext = "[unable to decrypt]";
                }
            } else {
                plaintext = "[unable to decrypt]";
            }

            LocalMessage local;
            local.id = message.id;
            local.conversation_id = message.conversation_id;
            local.sender_user_id = message.sender_user_id;
            local.created_at = message.created_at;
            local.rendered_text = RenderMessage(message, plaintext).toStdString();
            local.plaintext = plaintext;
            rebuilt.push_back(local);

            state_.MarkMessageSeen(message.id);
        }

        state_.local_messages[selected_conversation_id_] = rebuilt;
        if (!rebuilt.empty()) {
            const auto& last = rebuilt.back();
            const QString preview = last.plaintext.empty()
                                        ? ExtractLegacyPlaintext(QString::fromStdString(last.rendered_text))
                                        : QString::fromStdString(last.plaintext);
            UpsertConversationMeta(
                selected_conversation_id_,
                QString(),
                QString(),
                MessagePreview(preview),
                QString::fromStdString(last.created_at));
        }

        PersistState();
        RefreshConversationList();
        emit ConversationSelected(conversation_id, RenderThread(selected_conversation_id_));
    } catch (const std::exception& ex) {
        const QString line = QString("Select conversation failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::SendMessageToPeer(const QString& peer_username, const QString& message_text) {
    try {
        if (message_text.trimmed().isEmpty()) {
            return;
        }

        QString normalized_peer;
        if (!peer_username.trimmed().isEmpty()) {
            QString error;
            const auto normalized = NormalizePeerUsername(peer_username, &error);
            if (!normalized.has_value()) {
                emit ErrorOccurred(error);
                return;
            }
            normalized_peer = normalized->trimmed().toLower();
        } else if (!selected_conversation_id_.empty()) {
            const auto meta_it = state_.conversation_meta.find(selected_conversation_id_);
            if (meta_it == state_.conversation_meta.end() ||
                (meta_it->second.peer_address.empty() && meta_it->second.peer_username.empty())) {
                emit ErrorOccurred("Missing peer identity for selected conversation. Re-open the DM from username.");
                return;
            }
            if (!meta_it->second.peer_address.empty()) {
                normalized_peer = QString::fromStdString(meta_it->second.peer_address).trimmed().toLower();
            } else {
                normalized_peer = QString::fromStdString(meta_it->second.peer_username).trimmed().toLower();
            }
        } else {
            emit ErrorOccurred("Please enter a valid username or username@onion");
            return;
        }

        const std::string conversation_id = ResolveConversationIdForPeer(normalized_peer);
        RevealConversation(conversation_id, false);
        const DeviceOut recipient_device = ResolveRecipientDevice(normalized_peer);

        const std::string ciphertext = crypto_.EncryptForRecipient(
            recipient_device.enc_x25519_pub,
            message_text.toStdString());

        MessageSendRequest request;
        request.conversation_id = conversation_id;
        request.envelope.version = 1;
        request.envelope.alg = "libsodium-sealedbox-v1";
        request.envelope.recipient_device_id = recipient_device.id;
        request.envelope.ciphertext_b64 = ciphertext;
        request.envelope.aad_b64.clear();
        request.envelope.client_message_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();

        const auto send_op = [this, &request]() {
            return api_client_.SendMessage(state_.base_url, RequireAccessToken(), request);
        };
        const auto sent = CallWithAuthRetryOnce<MessageSendResponse>(send_op, [this]() { RefreshAccessToken(); });

        selected_conversation_id_ = conversation_id;
        state_.MarkMessageSeen(sent.message.id);

        LocalMessage local;
        local.id = sent.message.id;
        local.conversation_id = conversation_id;
        local.sender_user_id = state_.user.id;
        local.created_at = sent.message.created_at;
        local.rendered_text = RenderMessage(sent.message, message_text.toStdString()).toStdString();
        local.plaintext = message_text.toStdString();
        state_.local_messages[selected_conversation_id_].push_back(local);

        UpsertConversationMeta(
            selected_conversation_id_,
            normalized_peer.contains('@') ? normalized_peer.section('@', 0, 0) : normalized_peer,
            normalized_peer.contains('@') ? normalized_peer : QString(),
            MessagePreview(message_text),
            QString::fromStdString(sent.message.created_at));

        PersistState();
        RefreshConversationList();
        emit MessageSendSucceeded(
            QString::fromStdString(selected_conversation_id_),
            QString::fromStdString(sent.message.id));
        emit ConversationSelected(
            QString::fromStdString(selected_conversation_id_),
            RenderThread(selected_conversation_id_));
    } catch (const std::exception& ex) {
        const QString line = QString("Send message failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::StartVoiceCall() {
    if (!state_.has_user || !state_.has_device) {
        ReportCallError("You must be signed in and device-configured to start a call.");
        return;
    }
    if (!IsCallState("idle")) {
        ReportCallError("Another call is already in progress.");
        return;
    }
    if (selected_conversation_id_.empty()) {
        ReportCallError("Select a conversation before starting a voice call.");
        return;
    }

    VoiceCallOffer offer;
    offer.conversation_id = selected_conversation_id_;
    ws_client_.SendCallOffer(offer);

    QString peer_user;
    const auto meta_it = state_.conversation_meta.find(selected_conversation_id_);
    if (meta_it != state_.conversation_meta.end()) {
        if (!meta_it->second.peer_address.empty()) {
            peer_user = QString::fromStdString(meta_it->second.peer_address);
        } else {
            peer_user = QString::fromStdString(meta_it->second.peer_username);
        }
    }

    TransitionCallState(
        "outgoing_ringing",
        QString(),
        QString::fromStdString(selected_conversation_id_),
        peer_user,
        QString());
}

void ApplicationController::AcceptVoiceCall() {
    if (!IsCallState("incoming_ringing") || call_state_.call_id.trimmed().isEmpty()) {
        return;
    }

    VoiceCallAccept accept;
    accept.call_id = call_state_.call_id.toStdString();
    ws_client_.SendCallAccept(accept);
}

void ApplicationController::RejectVoiceCall() {
    if (!IsCallState("incoming_ringing") || call_state_.call_id.trimmed().isEmpty()) {
        return;
    }

    VoiceCallReject reject;
    reject.call_id = call_state_.call_id.toStdString();
    reject.reason = "declined";
    ws_client_.SendCallReject(reject);
    TransitionCallState(
        "ending",
        call_state_.call_id,
        call_state_.conversation_id,
        call_state_.peer_user_id,
        "declined");
}

void ApplicationController::EndVoiceCall() {
    if (IsCallState("idle")) {
        return;
    }

    const QString call_id = call_state_.call_id.trimmed();
    if (!call_id.isEmpty()) {
        VoiceCallEnd end_request;
        end_request.call_id = call_id.toStdString();
        end_request.reason = "ended";
        ws_client_.SendCallEnd(end_request);
    }

    if (call_id.isEmpty()) {
        StopAudioEngine();
        TransitionCallState("idle", QString(), QString(), QString(), "ended");
        return;
    }

    TransitionCallState(
        "ending",
        call_state_.call_id,
        call_state_.conversation_id,
        call_state_.peer_user_id,
        "ending");
}

void ApplicationController::SetCallMuted(bool muted) {
    call_state_.muted = muted;
    audio_engine_.SetMuted(muted);
    EmitCallState();
}

void ApplicationController::LoadAudioDevices() {
    const auto inputs = audio_engine_.InputDevices();
    const auto outputs = audio_engine_.OutputDevices();

    std::vector<AudioDeviceOptionView> input_views;
    std::vector<AudioDeviceOptionView> output_views;
    input_views.reserve(inputs.size());
    output_views.reserve(outputs.size());

    for (const auto& input : inputs) {
        input_views.push_back(AudioDeviceOptionView{
            input.id,
            input.name,
        });
    }
    for (const auto& output : outputs) {
        output_views.push_back(AudioDeviceOptionView{
            output.id,
            output.name,
        });
    }

    QString preferred_input = PreferredInputDeviceId();
    QString preferred_output = PreferredOutputDeviceId();
    bool input_fallback = false;
    bool output_fallback = false;

    const auto has_input = [&preferred_input](const AudioDeviceInfo& device) {
        return device.id == preferred_input;
    };
    const auto has_output = [&preferred_output](const AudioDeviceInfo& device) {
        return device.id == preferred_output;
    };

    if (preferred_input.trimmed().isEmpty() || std::none_of(inputs.begin(), inputs.end(), has_input)) {
        input_fallback = !preferred_input.trimmed().isEmpty();
        preferred_input = audio_engine_.DefaultInputDeviceId();
        if (preferred_input.trimmed().isEmpty() && !inputs.empty()) {
            preferred_input = inputs.front().id;
        }
    }

    if (preferred_output.trimmed().isEmpty() || std::none_of(outputs.begin(), outputs.end(), has_output)) {
        output_fallback = !preferred_output.trimmed().isEmpty();
        preferred_output = audio_engine_.DefaultOutputDeviceId();
        if (preferred_output.trimmed().isEmpty() && !outputs.empty()) {
            preferred_output = outputs.front().id;
        }
    }

    const std::string next_input = preferred_input.toStdString();
    const std::string next_output = preferred_output.toStdString();
    if (state_.audio_preferences.preferred_input_device_id != next_input ||
        state_.audio_preferences.preferred_output_device_id != next_output) {
        state_.audio_preferences.preferred_input_device_id = next_input;
        state_.audio_preferences.preferred_output_device_id = next_output;
        PersistState();
    }

    emit AudioDevicesChanged(input_views, output_views);
    emit AudioDevicePreferenceChanged(preferred_input, preferred_output);

    if (input_fallback || output_fallback) {
        ReportCallError("Saved audio device was unavailable. Using system default device.");
    }
}

void ApplicationController::SetPreferredAudioDevices(const QString& input_device_id, const QString& output_device_id) {
    const auto inputs = audio_engine_.InputDevices();
    const auto outputs = audio_engine_.OutputDevices();

    QString selected_input = input_device_id.trimmed();
    QString selected_output = output_device_id.trimmed();
    if (selected_input.isEmpty()) {
        selected_input = audio_engine_.DefaultInputDeviceId();
        if (selected_input.isEmpty() && !inputs.empty()) {
            selected_input = inputs.front().id;
        }
    }
    if (selected_output.isEmpty()) {
        selected_output = audio_engine_.DefaultOutputDeviceId();
        if (selected_output.isEmpty() && !outputs.empty()) {
            selected_output = outputs.front().id;
        }
    }

    bool input_fallback = false;
    bool output_fallback = false;

    const auto has_input = [&selected_input](const AudioDeviceInfo& device) {
        return device.id == selected_input;
    };
    const auto has_output = [&selected_output](const AudioDeviceInfo& device) {
        return device.id == selected_output;
    };

    if (!selected_input.isEmpty() && std::none_of(inputs.begin(), inputs.end(), has_input)) {
        input_fallback = true;
        selected_input = audio_engine_.DefaultInputDeviceId();
        if (selected_input.isEmpty() && !inputs.empty()) {
            selected_input = inputs.front().id;
        }
    }
    if (!selected_output.isEmpty() && std::none_of(outputs.begin(), outputs.end(), has_output)) {
        output_fallback = true;
        selected_output = audio_engine_.DefaultOutputDeviceId();
        if (selected_output.isEmpty() && !outputs.empty()) {
            selected_output = outputs.front().id;
        }
    }

    state_.audio_preferences.preferred_input_device_id = selected_input.toStdString();
    state_.audio_preferences.preferred_output_device_id = selected_output.toStdString();
    PersistState();

    emit AudioDevicePreferenceChanged(selected_input, selected_output);

    QString warning;
    QString error;
    if (!audio_engine_.ApplyDevices(selected_input, selected_output, &warning, &error)) {
        ReportCallError(error.isEmpty() ? "Unable to apply selected audio devices." : error);
        return;
    }

    if (input_fallback || output_fallback) {
        ReportCallError("One or more selected audio devices were unavailable. Using system default.");
    }
    if (!warning.trimmed().isEmpty()) {
        ReportCallError(warning);
    }
}

bool ApplicationController::AcceptMessagesFromStrangers() const {
    return state_.social_preferences.accept_messages_from_strangers;
}

void ApplicationController::SetAcceptMessagesFromStrangers(bool enabled) {
    if (state_.social_preferences.accept_messages_from_strangers == enabled) {
        return;
    }

    state_.social_preferences.accept_messages_from_strangers = enabled;
    if (!enabled) {
        for (const auto& [conversation_id, _] : pending_request_senders_) {
            state_.blocked_conversation_ids.insert(conversation_id);
        }
        pending_request_senders_.clear();
        pending_request_messages_.clear();
    }

    PersistState();
    RefreshConversationList();
}

void ApplicationController::AcceptMessageRequest(const QString& conversation_id) {
    const std::string key = conversation_id.trimmed().toStdString();
    if (key.empty()) {
        return;
    }
    RevealConversation(key, false);
    SelectConversation(QString::fromStdString(key));
}

void ApplicationController::IgnoreMessageRequest(const QString& conversation_id) {
    const std::string key = conversation_id.trimmed().toStdString();
    if (key.empty()) {
        return;
    }

    state_.blocked_conversation_ids.insert(key);
    pending_request_senders_.erase(key);
    pending_request_messages_.erase(key);

    if (selected_conversation_id_ == key) {
        selected_conversation_id_.clear();
        emit ConversationSelected(QString(), {});
    }

    PersistState();
    RefreshConversationList();
}

void ApplicationController::ResetLocalState() {
    std::string error;
    StopAudioEngine();
    TransitionCallState("idle", QString(), QString(), QString(), "reset");
    StopRealtime();

    if (!secret_store_.DeleteSecretsMatching(SecretNamespaceNeedle(), &error) && !error.empty()) {
        RecordDiagnostic(QString("DeleteSecretsMatching failed: %1").arg(QString::fromStdString(error)));
    }

    const QString state_path = QString::fromStdString(state_store_.path());
    const QFileInfo info(state_path);
    if (info.exists()) {
        QFile::remove(state_path);
    }

    if (profile_name_ != "default") {
        QDir state_dir = info.dir();
        if (state_dir.exists()) {
            state_dir.removeRecursively();
        }
    }

    ClearInMemoryState();
    RecordDiagnostic("local state reset");

    emit AuthStateChanged(false, QString());
    emit DeviceStateChanged(false);
    emit ConversationListChanged(std::vector<ConversationListItemView>{});
    emit ConversationSelected(QString(), {});
    emit ConnectionStatusChanged(connection_status_);
    LoadAudioDevices();
}

QString ApplicationController::UserDisplayId() const {
    if (!state_.has_user) {
        return {};
    }
    if (!state_.user.user_address.empty()) {
        return QString::fromStdString(state_.user.user_address);
    }
    if (!state_.user.home_server_onion.empty()) {
        return QString("%1@%2").arg(
            QString::fromStdString(state_.user.username),
            QString::fromStdString(state_.user.home_server_onion));
    }
    const QString authority = ServerAuthority();
    if (authority.isEmpty()) {
        return QString::fromStdString(state_.user.username);
    }
    return QString("%1@%2").arg(QString::fromStdString(state_.user.username), authority);
}

QString ApplicationController::DeviceId() const {
    if (!state_.has_device) {
        return {};
    }
    return QString::fromStdString(state_.device.id);
}

QString ApplicationController::DeviceLabel() const {
    if (!state_.has_device) {
        return {};
    }
    return QString::fromStdString(state_.device.label);
}

QString ApplicationController::ConnectionStatus() const {
    return connection_status_;
}

QString ApplicationController::DiagnosticsReport() const {
    const char* version = "0.1.0";
#ifdef BLACKWIRE_CLIENT_VERSION
    version = BLACKWIRE_CLIENT_VERSION;
#endif
    QStringList lines;
    lines << QString("client_version=%1").arg(version);
    lines << QString("profile=%1").arg(QString::fromStdString(profile_name_));
    lines << QString("timestamp=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    lines << QString("base_url=%1").arg(BaseUrl());
    lines << QString("user=%1").arg(state_.has_user ? QString::fromStdString(state_.user.username) : "-");
    lines << QString("device_id=%1").arg(state_.has_device ? QString::fromStdString(state_.device.id) : "-");
    lines << QString("connection_status=%1").arg(connection_status_);
    lines << "recent_events:";
    for (const auto& line : diagnostics_) {
        lines << QString("- %1").arg(SanitizeDiagnosticText(line));
    }
    return lines.join('\n');
}

QString ApplicationController::ServerAuthority() const {
    return ServerAuthorityFromBaseUrl(BaseUrl());
}

std::string ApplicationController::SecretNamespacePrefix() const {
    if (profile_name_ == "default") {
        return "blackwire:";
    }
    return "blackwire:profile:" + profile_name_ + ":";
}

std::string ApplicationController::SecretNamespaceNeedle() const {
    if (profile_name_ == "default") {
        if (!state_.base_url.empty()) {
            return "blackwire:" + state_.base_url + ":";
        }
        return "blackwire:";
    }
    return SecretNamespacePrefix();
}

std::string ApplicationController::SecretKey(const std::string& name) const {
    const std::string username = state_.has_user ? state_.user.username : "anonymous";
    return SecretNamespacePrefix() + state_.base_url + ":" + username + ":" + name;
}

std::string ApplicationController::RequireAccessToken() {
    std::string error;
    const auto value = secret_store_.GetSecret(SecretKey("access_token"), &error);
    if (!value.has_value()) {
        throw std::runtime_error("Access token unavailable");
    }
    return value.value();
}

std::string ApplicationController::RequireRefreshToken() {
    std::string error;
    const auto value = secret_store_.GetSecret(SecretKey("refresh_token"), &error);
    if (!value.has_value()) {
        throw std::runtime_error("Refresh token unavailable");
    }
    return value.value();
}

void ApplicationController::SaveTokenPair(const TokenBundle& tokens) {
    std::string error;
    if (!secret_store_.SetSecret(SecretKey("access_token"), tokens.access_token, &error)) {
        throw std::runtime_error(error);
    }
    if (!secret_store_.SetSecret(SecretKey("refresh_token"), tokens.refresh_token, &error)) {
        throw std::runtime_error(error);
    }
}

void ApplicationController::RefreshAccessToken() {
    const auto refreshed = api_client_.Refresh(state_.base_url, RequireRefreshToken());
    SaveTokenPair(refreshed.tokens);
}

void ApplicationController::StartRealtime() {
    try {
        ws_client_.Connect(state_.base_url, RequireAccessToken());
    } catch (const std::exception& ex) {
        const QString line = QString("WS start failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::StopRealtime() {
    ws_client_.Disconnect();
    connection_status_ = "Disconnected";
}

void ApplicationController::PersistState() {
    state_store_.Save(state_);
}

void ApplicationController::RefreshConversationList() {
    std::vector<ConversationListItemView> items;
    items.reserve(state_.conversations.size());

    for (const auto& conv : state_.conversations) {
        if (state_.blocked_conversation_ids.contains(conv.id)) {
            continue;
        }
        if (pending_request_senders_.find(conv.id) != pending_request_senders_.end()) {
            continue;
        }

        ConversationListItemView item;
        item.id = QString::fromStdString(conv.id);
        item.title = FriendlyConversationTitle(conv.id);

        const auto meta_it = state_.conversation_meta.find(conv.id);
        if (meta_it != state_.conversation_meta.end() && !meta_it->second.last_preview.empty()) {
            item.subtitle = QString::fromStdString(meta_it->second.last_preview);
        } else {
            item.subtitle = "(no messages yet)";
        }

        item.last_activity_at = LastActivityForConversation(conv.id);
        items.push_back(item);
    }

    SortConversationItems(&items);

    emit ConversationListChanged(items);
}

QString ApplicationController::RenderMessage(const MessageOut& message, const std::string& plaintext) const {
    (void)plaintext;
    const QString sender = message.sender_user_id == state_.user.id ? "Me" : "Peer";
    return QString("[%1] %2: [encrypted message]")
        .arg(QString::fromStdString(message.created_at), sender);
}

std::vector<ThreadMessageView> ApplicationController::RenderThread(const std::string& conversation_id) const {
    const auto iter = state_.local_messages.find(conversation_id);
    if (iter == state_.local_messages.end()) {
        return {};
    }

    QString peer_label = "Peer";
    const auto meta_it = state_.conversation_meta.find(conversation_id);
    if (meta_it != state_.conversation_meta.end()) {
        if (!meta_it->second.peer_username.empty()) {
            peer_label = QString::fromStdString(meta_it->second.peer_username);
        } else if (!meta_it->second.peer_address.empty()) {
            peer_label = QString::fromStdString(meta_it->second.peer_address);
        }
    }

    return BuildThreadMessageViews(iter->second, state_.user.id, peer_label);
}

std::optional<QString> ApplicationController::NormalizePeerUsername(const QString& value, QString* error) const {
    const auto parsed = ParsePeerAddress(value);
    if (!parsed.has_value()) {
        if (error != nullptr) {
            *error = "Please enter a valid username or username@onion";
        }
        return std::nullopt;
    }

    if (error != nullptr) {
        error->clear();
    }
    if (parsed->has_server) {
        return QString("%1@%2").arg(parsed->username.trimmed().toLower(), parsed->server_authority.trimmed().toLower());
    }
    return parsed->username.trimmed().toLower();
}

bool ApplicationController::IsWebSocketAuthError(const std::string& error) const {
    std::string lowered = error;
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered.find("401") != std::string::npos || lowered.find("403") != std::string::npos;
}

void ApplicationController::ReauthenticateWebSocket() {
    if (ws_reauth_in_progress_) {
        return;
    }
    ws_reauth_in_progress_ = true;

    try {
        RefreshAccessToken();
        ws_client_.Disconnect();
        ws_client_.Connect(state_.base_url, RequireAccessToken());
        connection_status_ = "Reconnecting";
        RecordDiagnostic("websocket reauth reconnecting");
        emit ConnectionStatusChanged(connection_status_);
    } catch (const std::exception& ex) {
        ws_client_.Disconnect();
        connection_status_ = "Auth expired";
        RecordDiagnostic("websocket reauth failed");
        emit ConnectionStatusChanged(connection_status_);
        emit ErrorOccurred(QString("Session expired: %1").arg(ex.what()));
    }

    ws_reauth_in_progress_ = false;
}

void ApplicationController::RecordDiagnostic(const QString& line) {
    const QString stamped = QString("[%1] %2")
                                .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                                     SanitizeDiagnosticText(line));
    diagnostics_.push_back(stamped);
    while (static_cast<int>(diagnostics_.size()) > kMaxDiagnostics) {
        diagnostics_.pop_front();
    }
}

bool ApplicationController::ConversationExists(const std::string& conversation_id) const {
    return FindConversation(conversation_id) != nullptr;
}

const ConversationOut* ApplicationController::FindConversation(const std::string& conversation_id) const {
    const auto it = std::find_if(
        state_.conversations.begin(),
        state_.conversations.end(),
        [&conversation_id](const ConversationOut& conversation) { return conversation.id == conversation_id; });
    if (it == state_.conversations.end()) {
        return nullptr;
    }
    return &(*it);
}

void ApplicationController::RevealConversation(const std::string& conversation_id, bool select_conversation) {
    if (conversation_id.empty()) {
        return;
    }

    state_.blocked_conversation_ids.erase(conversation_id);

    const auto* conversation = FindConversation(conversation_id);
    if (conversation != nullptr && (!conversation->peer_username.empty() || !conversation->peer_address.empty())) {
        UpsertConversationMeta(
            conversation_id,
            QString::fromStdString(conversation->peer_username),
            QString::fromStdString(conversation->peer_address),
            QString(),
            QString::fromStdString(conversation->created_at));
    }

    const auto sender_it = pending_request_senders_.find(conversation_id);
    if (sender_it != pending_request_senders_.end() && !sender_it->second.trimmed().isEmpty()) {
        UpsertConversationMeta(conversation_id, sender_it->second, QString(), QString(), QString());
    }

    const auto pending_it = pending_request_messages_.find(conversation_id);
    if (pending_it != pending_request_messages_.end()) {
        auto& thread = state_.local_messages[conversation_id];
        thread.insert(thread.end(), pending_it->second.begin(), pending_it->second.end());

        if (!thread.empty()) {
            const auto& last = thread.back();
            const QString preview = last.plaintext.empty()
                                        ? ExtractLegacyPlaintext(QString::fromStdString(last.rendered_text))
                                        : QString::fromStdString(last.plaintext);
            UpsertConversationMeta(
                conversation_id,
                QString(),
                QString(),
                MessagePreview(preview),
                QString::fromStdString(last.created_at));
        }
    }

    pending_request_messages_.erase(conversation_id);
    pending_request_senders_.erase(conversation_id);

    PersistState();
    RefreshConversationList();

    if (select_conversation) {
        selected_conversation_id_ = conversation_id;
        emit ConversationSelected(
            QString::fromStdString(conversation_id),
            RenderThread(conversation_id));
    }
}

QString ApplicationController::MessagePreview(const QString& message) const {
    if (message.trimmed().isEmpty()) {
        return {};
    }
    return "(encrypted message)";
}

QString ApplicationController::FriendlyConversationTitle(const std::string& conversation_id) const {
    const auto meta_it = state_.conversation_meta.find(conversation_id);
    if (meta_it != state_.conversation_meta.end()) {
        if (!meta_it->second.peer_address.empty()) {
            return BuildConversationTitle(
                QString::fromStdString(meta_it->second.peer_address),
                QString::fromStdString(conversation_id),
                ServerAuthority());
        }
        if (!meta_it->second.peer_username.empty()) {
            return BuildConversationTitle(
                QString::fromStdString(meta_it->second.peer_username),
                QString::fromStdString(conversation_id),
                ServerAuthority());
        }
    }
    return BuildConversationTitle(QString(), QString::fromStdString(conversation_id), ServerAuthority());
}

QString ApplicationController::LastActivityForConversation(const std::string& conversation_id) const {
    const auto meta_it = state_.conversation_meta.find(conversation_id);
    if (meta_it != state_.conversation_meta.end() && !meta_it->second.last_activity_at.empty()) {
        return QString::fromStdString(meta_it->second.last_activity_at);
    }

    const auto conv_it = std::find_if(
        state_.conversations.begin(),
        state_.conversations.end(),
        [&conversation_id](const ConversationOut& conv) { return conv.id == conversation_id; });
    if (conv_it != state_.conversations.end()) {
        return QString::fromStdString(conv_it->created_at);
    }
    return {};
}

QString ApplicationController::PreferredInputDeviceId() const {
    const QString configured = QString::fromStdString(state_.audio_preferences.preferred_input_device_id).trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }
    return audio_engine_.DefaultInputDeviceId();
}

QString ApplicationController::PreferredOutputDeviceId() const {
    const QString configured = QString::fromStdString(state_.audio_preferences.preferred_output_device_id).trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }
    return audio_engine_.DefaultOutputDeviceId();
}

bool ApplicationController::IsCallState(const QString& value) const {
    return call_state_.state.compare(value, Qt::CaseInsensitive) == 0;
}

void ApplicationController::EmitCallState() {
    emit CallStateChanged(call_state_);
}

void ApplicationController::TransitionCallState(
    const QString& state,
    const QString& call_id,
    const QString& conversation_id,
    const QString& peer_user_id,
    const QString& reason) {
    call_state_.state = state.trimmed().isEmpty() ? "idle" : state.trimmed();
    call_state_.call_id = call_id;
    call_state_.conversation_id = conversation_id;
    call_state_.peer_user_id = peer_user_id;
    call_state_.reason = reason;

    if (IsCallState("idle") || IsCallState("incoming_ringing") || IsCallState("outgoing_ringing")) {
        call_state_.muted = false;
    }

    EmitCallState();
}

bool ApplicationController::StartAudioEngineForActiveCall(QString* warning, QString* error) {
    if (!IsCallState("active") || call_state_.call_id.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = "Call is not active.";
        }
        return false;
    }

    audio_sequence_ = 0;
    const QString active_call_id = call_state_.call_id;
    const QString input_device_id = PreferredInputDeviceId();
    const QString output_device_id = PreferredOutputDeviceId();

    const bool started = audio_engine_.Start(
        input_device_id,
        output_device_id,
        [this, active_call_id](const QByteArray& frame) {
            if (frame.isEmpty()) {
                return;
            }
            if (!IsCallState("active") || call_state_.call_id != active_call_id) {
                return;
            }

            VoiceAudioChunk chunk;
            chunk.call_id = active_call_id.toStdString();
            chunk.sequence = audio_sequence_++;
            chunk.pcm_b64 = frame.toBase64().toStdString();
            ws_client_.SendCallAudioChunk(chunk);
        },
        warning,
        error);

    if (started) {
        audio_engine_.SetMuted(call_state_.muted);
    }
    return started;
}

void ApplicationController::StopAudioEngine() {
    audio_engine_.Stop();
}

void ApplicationController::ReportCallError(const QString& message) {
    if (message.trimmed().isEmpty()) {
        return;
    }
    RecordDiagnostic(QString("call_error=%1").arg(message));
    emit CallErrorOccurred(message);
}

std::optional<std::string> ApplicationController::FindConversationIdForPeer(
    const QString& normalized_peer) const {
    const std::string key = normalized_peer.trimmed().toLower().toStdString();
    if (key.empty()) {
        return std::nullopt;
    }

    const auto key_username = key.find('@') != std::string::npos ? key.substr(0, key.find('@')) : key;

    for (const auto& [conversation_id, meta] : state_.conversation_meta) {
        const bool address_match = !meta.peer_address.empty() && meta.peer_address == key;
        const bool username_match = !meta.peer_username.empty() && meta.peer_username == key;
        const bool loose_username_match =
            !meta.peer_username.empty() && meta.peer_username == key_username;
        if (!address_match && !username_match && !loose_username_match) {
            continue;
        }

        const auto conv_it = std::find_if(
            state_.conversations.begin(),
            state_.conversations.end(),
            [&conversation_id](const ConversationOut& conv) { return conv.id == conversation_id; });
        if (conv_it != state_.conversations.end()) {
            return conversation_id;
        }
    }

    return std::nullopt;
}

std::string ApplicationController::ResolveConversationIdForPeer(const QString& normalized_peer) {
    const std::string key = normalized_peer.trimmed().toLower().toStdString();
    if (key.empty()) {
        throw std::runtime_error("Peer address is required");
    }

    if (!selected_conversation_id_.empty()) {
        const auto selected_it = state_.conversation_meta.find(selected_conversation_id_);
        if (selected_it != state_.conversation_meta.end()) {
            if ((!selected_it->second.peer_address.empty() && selected_it->second.peer_address == key) ||
                (!selected_it->second.peer_username.empty() && selected_it->second.peer_username == key)) {
                return selected_conversation_id_;
            }
            if (key.find('@') == std::string::npos &&
                !selected_it->second.peer_address.empty() &&
                selected_it->second.peer_address.rfind(key + "@", 0) == 0) {
                return selected_conversation_id_;
            }
        }
    }

    const auto existing = FindConversationIdForPeer(QString::fromStdString(key));
    if (existing.has_value()) {
        return existing.value();
    }

    const auto create_op = [this, &key]() {
        const bool has_server = key.find('@') != std::string::npos;
        return api_client_.CreateDm(
            state_.base_url,
            RequireAccessToken(),
            has_server ? key : "",
            has_server ? "" : key);
    };
    const auto conversation = CallWithAuthRetryOnce<ConversationOut>(create_op, [this]() { RefreshAccessToken(); });

    const auto conv_it = std::find_if(
        state_.conversations.begin(),
        state_.conversations.end(),
        [&conversation](const ConversationOut& conv) { return conv.id == conversation.id; });
    if (conv_it == state_.conversations.end()) {
        state_.conversations.push_back(conversation);
    }

    if (!conversation.peer_username.empty() || !conversation.peer_address.empty()) {
        UpsertConversationMeta(
            conversation.id,
            QString::fromStdString(conversation.peer_username),
            QString::fromStdString(conversation.peer_address),
            QString(),
            QString::fromStdString(conversation.created_at));
    }

    return conversation.id;
}

DeviceOut ApplicationController::ResolveRecipientDevice(const QString& normalized_peer) {
    const std::string key = normalized_peer.trimmed().toLower().toStdString();
    if (key.empty()) {
        throw std::runtime_error("Peer address is required");
    }

    const auto cached = peer_device_cache_.find(key);
    if (cached != peer_device_cache_.end()) {
        return cached->second;
    }

    const auto get_device = [this, &key]() {
        return api_client_.GetUserDevice(state_.base_url, RequireAccessToken(), key);
    };
    const auto user_device = CallWithAuthRetryOnce<UserDeviceLookup>(get_device, [this]() { RefreshAccessToken(); });
    peer_device_cache_[key] = user_device.device;
    return user_device.device;
}

void ApplicationController::UpsertConversationMeta(
    const std::string& conversation_id,
    const QString& peer_username,
    const QString& peer_address,
    const QString& preview,
    const QString& created_at) {
    auto& meta = state_.conversation_meta[conversation_id];
    if (!peer_username.trimmed().isEmpty()) {
        meta.peer_username = peer_username.trimmed().toStdString();
    }
    if (!peer_address.trimmed().isEmpty()) {
        meta.peer_address = peer_address.trimmed().toStdString();
    }
    if (!preview.trimmed().isEmpty()) {
        meta.last_preview = preview.trimmed().toStdString();
    }
    if (!created_at.trimmed().isEmpty()) {
        meta.last_activity_at = created_at.trimmed().toStdString();
    }
}

void ApplicationController::ClearInMemoryState() {
    state_ = ClientState{};
    state_.base_url = "http://localhost:8000";
    peer_device_cache_.clear();
    pending_request_messages_.clear();
    pending_request_senders_.clear();
    selected_conversation_id_.clear();
    connection_status_ = "Disconnected";
    call_state_ = CallStateView{};
    audio_sequence_ = 0;
}

}  // namespace blackwire
