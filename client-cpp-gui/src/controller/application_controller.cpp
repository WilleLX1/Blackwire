#include "blackwire/controller/application_controller.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
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
constexpr qint64 kConservativeInlineAttachmentFallbackBytes = 1 * 1024 * 1024;
constexpr qint64 kSealedBoxOverheadBytes = 64;
const char* kFileMessagePrefix = "bwfile://v1:";

QString SanitizeDiagnosticText(QString text) {
    return SanitizeDiagnosticsText(text);
}

bool EnvFlagEnabled(const char* name, bool default_value) {
    const QString raw = qEnvironmentVariable(name).trimmed().toLower();
    if (raw.isEmpty()) {
        return default_value;
    }
    return raw == "1" || raw == "true" || raw == "yes" || raw == "on";
}

QString NormalizePresenceValue(const QString& status) {
    const QString normalized = status.trimmed().toLower();
    if (normalized == "active" || normalized == "inactive" || normalized == "offline" || normalized == "dnd") {
        return normalized;
    }
    return "active";
}

QString FormatBytesHuman(qint64 bytes) {
    if (bytes >= 1024LL * 1024LL) {
        const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        return QString("%1 MB").arg(mb, 0, 'f', mb >= 100 ? 0 : 1);
    }
    if (bytes >= 1024LL) {
        const double kb = static_cast<double>(bytes) / 1024.0;
        return QString("%1 KB").arg(kb, 0, 'f', kb >= 100 ? 0 : 1);
    }
    return QString("%1 B").arg(bytes);
}

QString UsernameFromAddress(const QString& actor_address) {
    const QString normalized = actor_address.trimmed().toLower();
    if (normalized.isEmpty()) {
        return "A member";
    }
    const int at = normalized.indexOf('@');
    if (at > 0) {
        return normalized.left(at).trimmed();
    }
    return normalized;
}

std::string CanonicalMessageSignature(
    const std::string& sender_address,
    const std::string& sender_device_uid,
    const std::string& recipient_address,
    const std::string& recipient_device_uid,
    const std::string& client_message_id,
    long long sent_at_ms,
    const std::string& sender_prev_hash,
    const std::string& sender_chain_hash,
    const std::string& ciphertext_hash,
    const std::string& aad_hash) {
    return sender_address + "\n" + sender_device_uid + "\n" + recipient_address + "\n" + recipient_device_uid + "\n" +
           client_message_id + "\n" + std::to_string(sent_at_ms) + "\n" + sender_prev_hash + "\n" + sender_chain_hash +
           "\n" + ciphertext_hash + "\n" + aad_hash;
}

std::string AggregateChainHash(
    ICryptoService& crypto,
    const std::string& sender_prev_hash,
    const std::string& client_message_id,
    long long sent_at_ms,
    const std::vector<std::string>& hash_material) {
    std::vector<std::string> ordered = hash_material;
    std::sort(ordered.begin(), ordered.end());
    std::string aggregate;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        if (i > 0) {
            aggregate.append("|");
        }
        aggregate.append(ordered[i]);
    }
    const std::string aggregate_hash = crypto.Sha256(aggregate);
    return crypto.Sha256(
        sender_prev_hash + "\n" + client_message_id + "\n" + std::to_string(sent_at_ms) + "\n" + aggregate_hash);
}

std::string CanonicalSignedPrekeyString(
    const std::string& device_uid,
    int key_id,
    const std::string& pub_x25519_b64,
    const std::string& expires_at) {
    return "SIGNED_PREKEY\n" + device_uid + "\n" + std::to_string(key_id) + "\n" + pub_x25519_b64 + "\n" + expires_at;
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
    qRegisterMetaType<DeviceOut>("blackwire::DeviceOut");
    qRegisterMetaType<std::vector<DeviceOut>>("std::vector<blackwire::DeviceOut>");
    qRegisterMetaType<AudioDeviceOptionView>("blackwire::AudioDeviceOptionView");
    qRegisterMetaType<std::vector<AudioDeviceOptionView>>("std::vector<blackwire::AudioDeviceOptionView>");
    qRegisterMetaType<CallStateView>("blackwire::CallStateView");
    qRegisterMetaType<ThreadMessageView>("blackwire::ThreadMessageView");
    qRegisterMetaType<std::vector<ThreadMessageView>>("std::vector<blackwire::ThreadMessageView>");

    presence_poll_timer_ = new QTimer(this);
    presence_poll_timer_->setInterval(5000);
    connect(presence_poll_timer_, &QTimer::timeout, this, [this]() {
        if (!state_.has_user || !state_.has_device) {
            return;
        }
        LoadConversations();
    });
    presence_poll_timer_->start();

    ws_client_.SetHandlers(
        [this](const WsEventMessageNew& event) {
            try {
                const auto& msg = event.message;
                const std::string ack_id = event.copy_id.empty() ? msg.id : event.copy_id;
                const std::string dedupe_id = ack_id.empty() ? msg.id : ack_id;
                if (!state_.MarkMessageSeen(dedupe_id)) {
                    ws_client_.SendAck(ack_id.empty() ? msg.id : ack_id);
                    return;
                }

                if (!msg.sender_device_uid.empty() && !msg.sender_device_pubkey.empty() &&
                    !msg.envelope.signature_b64.empty()) {
                    const auto pin_it = state_.pinned_sender_sign_keys_by_device_uid.find(msg.sender_device_uid);
                    if (pin_it != state_.pinned_sender_sign_keys_by_device_uid.end() &&
                        pin_it->second != msg.sender_device_pubkey) {
                        const QString line = QString("Integrity warning: sender key changed for device %1")
                                                 .arg(QString::fromStdString(msg.sender_device_uid));
                        RecordDiagnostic(line);
                        emit IntegrityWarningOccurred(line);
                        return;
                    }
                    state_.pinned_sender_sign_keys_by_device_uid[msg.sender_device_uid] = msg.sender_device_pubkey;

                    const QByteArray ciphertext_bytes = QByteArray::fromBase64(
                        QByteArray::fromStdString(msg.envelope.ciphertext_b64));
                    const QByteArray aad_bytes = QByteArray::fromBase64(
                        QByteArray::fromStdString(msg.envelope.aad_b64));
                    const std::string ciphertext_hash = crypto_.Sha256(ciphertext_bytes.toStdString());
                    const std::string aad_hash = crypto_.Sha256(aad_bytes.toStdString());
                    const std::string recipient_address = msg.envelope.recipient_user_address.empty()
                                                              ? QString("%1@%2")
                                                                    .arg(
                                                                        QString::fromStdString(state_.user.username).trimmed().toLower(),
                                                                        QString::fromStdString(
                                                                            state_.user.home_server_onion.empty()
                                                                                ? ServerAuthority().toStdString()
                                                                                : state_.user.home_server_onion))
                                                                    .toStdString()
                                                              : msg.envelope.recipient_user_address;
                    const std::string canonical = CanonicalMessageSignature(
                        msg.sender_address,
                        msg.sender_device_uid,
                        recipient_address,
                        msg.envelope.recipient_device_uid.empty() ? msg.envelope.recipient_device_id
                                                                  : msg.envelope.recipient_device_uid,
                        msg.client_message_id,
                        msg.sent_at_ms,
                        msg.sender_prev_hash,
                        msg.sender_chain_hash,
                        ciphertext_hash,
                        aad_hash);
                    if (!crypto_.VerifyDetached(
                            msg.sender_device_pubkey,
                            canonical,
                            msg.envelope.signature_b64)) {
                        const QString line = QString("Integrity warning: invalid sender signature from %1")
                                                 .arg(QString::fromStdString(msg.sender_address));
                        RecordDiagnostic(line);
                        emit IntegrityWarningOccurred(line);
                        return;
                    }

                    const std::string chain_key = msg.conversation_id + "|" + msg.sender_device_uid;
                    const auto chain_it = state_.last_verified_chain_hash_by_conversation_sender.find(chain_key);
                    const std::string expected_prev = chain_it == state_.last_verified_chain_hash_by_conversation_sender.end()
                                                          ? std::string()
                                                          : chain_it->second;
                    if (msg.sender_prev_hash != expected_prev) {
                        const QString line = QString("Integrity warning: missing/reordered message detected");
                        RecordDiagnostic(line);
                        emit IntegrityWarningOccurred(line);
                    }
                    state_.last_verified_chain_hash_by_conversation_sender[chain_key] = msg.sender_chain_hash;
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

                ws_client_.SendAck(ack_id.empty() ? msg.id : ack_id);

                LocalMessage local;
                local.id = msg.id;
                local.conversation_id = msg.conversation_id;
                local.sender_user_id = msg.sender_user_id;
                local.sender_address = msg.sender_address;
                local.created_at = msg.created_at;
                local.rendered_text = RenderMessage(msg, plaintext).toStdString();
                local.plaintext = plaintext;
                if (state_.dismissed_conversation_ids.contains(msg.conversation_id)) {
                    state_.dismissed_conversation_ids.erase(msg.conversation_id);
                }
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
                if (!call_state_.call_id.trimmed().isEmpty() &&
                    call_state_.call_id == QString::fromStdString(event.call_id)) {
                    return;
                }
                VoiceCallReject reject;
                reject.call_id = event.call_id;
                reject.reason = "busy";
                ws_client_.SendCallReject(reject);
                return;
            }

            pending_outgoing_end_request_ = false;
            call_initiated_locally_ = false;
            TransitionCallState(
                "incoming_ringing",
                QString::fromStdString(event.call_id),
                QString::fromStdString(event.conversation_id),
                QString::fromStdString(
                    event.from_user_address.empty() ? event.from_user_id : event.from_user_address),
                QString());
            emit IncomingCallReceived(call_state_);
        },
        [this](const WsEventCallGroupState& event) {
            const QString state = QString::fromStdString(event.state).trimmed().toLower();
            const QString call_id = QString::fromStdString(event.call_id);
            const QString conversation_id = QString::fromStdString(event.conversation_id);
            QString title = FriendlyConversationTitle(event.conversation_id);
            if (title.trimmed().isEmpty()) {
                title = "Group";
            }
            const QString local_server = QString::fromStdString(
                                             state_.user.home_server_onion.empty()
                                                 ? ServerAuthority().toStdString()
                                                 : state_.user.home_server_onion)
                                             .trimmed()
                                             .toLower();
            const QString self_address = QString("%1@%2")
                                             .arg(QString::fromStdString(state_.user.username).trimmed().toLower(), local_server)
                                             .trimmed()
                                             .toLower();
            QString self_participant_state;
            std::vector<CallParticipantView> joined_participants;
            joined_participants.reserve(event.participants.size());
            for (const auto& participant : event.participants) {
                const QString participant_address = QString::fromStdString(participant.member_address).trimmed().toLower();
                if (participant_address == self_address) {
                    self_participant_state = QString::fromStdString(participant.state).trimmed().toLower();
                }
                const QString participant_state = QString::fromStdString(participant.state).trimmed().toLower();
                if (participant_state != "joined") {
                    continue;
                }
                const bool participant_is_self = participant_address == self_address;
                joined_participants.push_back(CallParticipantView{
                    participant_address,
                    participant_is_self ? "You" : UsernameFromAddress(participant_address),
                    participant_is_self,
                });
            }
            std::sort(
                joined_participants.begin(),
                joined_participants.end(),
                [](const CallParticipantView& lhs, const CallParticipantView& rhs) {
                    if (lhs.self != rhs.self) {
                        return lhs.self > rhs.self;
                    }
                    return lhs.label.trimmed().toLower() < rhs.label.trimmed().toLower();
                }
            );

            if (state == "ringing") {
                if (self_participant_state == "left" ||
                    self_participant_state == "declined" ||
                    self_participant_state == "missed" ||
                    self_participant_state == "removed") {
                    pending_outgoing_end_request_ = false;
                    StopAudioEngine();
                    TransitionCallState(
                        "idle",
                        QString(),
                        conversation_id,
                        QString(),
                        self_participant_state.isEmpty() ? "left" : self_participant_state);
                    return;
                }
                if (IsCallState("idle")) {
                    pending_outgoing_end_request_ = false;
                    call_initiated_locally_ = false;
                    TransitionCallState(
                        "incoming_ringing",
                        call_id,
                        conversation_id,
                        title,
                        QString());
                    emit IncomingCallReceived(call_state_);
                    return;
                }
                if (!IsCallState("outgoing_ringing") &&
                    !IsCallState("incoming_ringing") &&
                    !(pending_outgoing_end_request_ && IsCallState("ending"))) {
                    return;
                }
                if (!call_state_.call_id.isEmpty() && call_state_.call_id != call_id) {
                    return;
                }
                const QString next_state = IsCallState("incoming_ringing") ? "incoming_ringing" : "outgoing_ringing";
                TransitionCallState(
                    next_state,
                    call_id,
                    conversation_id,
                    title,
                    QString());
                if (pending_outgoing_end_request_ && !call_state_.call_id.trimmed().isEmpty()) {
                    VoiceCallEnd end_request;
                    end_request.call_id = call_state_.call_id.toStdString();
                    end_request.reason = "ended";
                    ws_client_.SendCallEnd(end_request);
                    pending_outgoing_end_request_ = false;
                    TransitionCallState(
                        "ending",
                        call_state_.call_id,
                        call_state_.conversation_id,
                        call_state_.peer_user_id,
                        "ending");
                }
                return;
            }

            if (state == "active") {
                if (self_participant_state == "left" ||
                    self_participant_state == "declined" ||
                    self_participant_state == "missed" ||
                    self_participant_state == "removed") {
                    pending_outgoing_end_request_ = false;
                    StopAudioEngine();
                    TransitionCallState(
                        "idle",
                        QString(),
                        conversation_id,
                        QString(),
                        self_participant_state.isEmpty() ? "left" : self_participant_state);
                    return;
                }

                // Another participant joined, but this local user has not joined yet.
                // Keep local UI in ringing state until this user explicitly accepts.
                if (self_participant_state != "joined") {
                    if (!call_state_.call_id.isEmpty() && call_state_.call_id != call_id) {
                        return;
                    }
                    if (IsCallState("idle")) {
                        pending_outgoing_end_request_ = false;
                        call_initiated_locally_ = false;
                        TransitionCallState(
                            "incoming_ringing",
                            call_id,
                            conversation_id,
                            title,
                            QString());
                        emit IncomingCallReceived(call_state_);
                        return;
                    }
                    if (IsCallState("active")) {
                        StopAudioEngine();
                        TransitionCallState(
                            "incoming_ringing",
                            call_id,
                            conversation_id,
                            title,
                            QString());
                        emit IncomingCallReceived(call_state_);
                        return;
                    }
                    if (IsCallState("incoming_ringing") || IsCallState("outgoing_ringing") || IsCallState("ending")) {
                        const QString keep_state = IsCallState("outgoing_ringing") ? "outgoing_ringing" : "incoming_ringing";
                        TransitionCallState(
                            keep_state,
                            call_id,
                            conversation_id,
                            title,
                            QString());
                    }
                    return;
                }

                const bool was_active_same_call = IsCallState("active") && call_state_.call_id == call_id;
                if (!call_state_.call_id.isEmpty() && call_state_.call_id != call_id) {
                    return;
                }
                TransitionCallState(
                    "active",
                    call_id,
                    conversation_id,
                    title,
                    QString());
                call_state_.participants = joined_participants;
                call_state_.reason = "Group call active";
                EmitCallState();

                if (!was_active_same_call) {
                    const bool legacy_audio_enabled = EnvFlagEnabled("BLACKWIRE_ENABLE_LEGACY_CALL_AUDIO_WS", true);
                    if (legacy_audio_enabled) {
                        QString warning;
                        QString error;
                        if (!StartAudioEngineForActiveCall(&warning, &error)) {
                            ReportCallError(QString("Voice call audio start failed: %1").arg(error));
                            if (!call_state_.call_id.trimmed().isEmpty()) {
                                VoiceCallEnd end_request;
                                end_request.call_id = call_state_.call_id.toStdString();
                                end_request.reason = "audio_error";
                                ws_client_.SendCallEnd(end_request);
                            }
                            StopAudioEngine();
                            TransitionCallState("idle", QString(), QString(), QString(), "audio_error");
                            return;
                        }
                        if (!warning.trimmed().isEmpty()) {
                            ReportCallError(warning);
                        }
                    }
                }
                return;
            }

            if (state == "ended") {
                if (!call_state_.call_id.isEmpty() && call_state_.call_id != call_id) {
                    return;
                }
                pending_outgoing_end_request_ = false;
                StopAudioEngine();
                TransitionCallState(
                    "idle",
                    QString(),
                    conversation_id,
                    QString(),
                    "ended");
            }
        },
        [this](const WsEventCallRinging& event) {
            if (!IsCallState("outgoing_ringing") && !(pending_outgoing_end_request_ && IsCallState("ending"))) {
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
            if (pending_outgoing_end_request_ && !call_state_.call_id.trimmed().isEmpty()) {
                VoiceCallEnd end_request;
                end_request.call_id = call_state_.call_id.toStdString();
                end_request.reason = "ended";
                ws_client_.SendCallEnd(end_request);
                pending_outgoing_end_request_ = false;
                TransitionCallState(
                    "ending",
                    call_state_.call_id,
                    call_state_.conversation_id,
                    call_state_.peer_user_id,
                    "ending");
            }
        },
        [this](const WsEventCallAccepted& event) {
            const QString call_id = QString::fromStdString(event.call_id);
            const QString conversation_id = QString::fromStdString(event.conversation_id);
            const auto* conversation = FindConversation(conversation_id.toStdString());
            if (conversation != nullptr && conversation->conversation_type == "group") {
                // Group-call state is driven by call.group.state; ignore generic accepted events.
                return;
            }
            if (!call_state_.call_id.isEmpty() && call_state_.call_id != call_id) {
                return;
            }
            if (IsCallState("incoming_ringing")) {
                call_initiated_locally_ = false;
            } else if (IsCallState("outgoing_ringing")) {
                call_initiated_locally_ = true;
            }

            TransitionCallState(
                "active",
                call_id,
                conversation_id,
                QString::fromStdString(
                    event.peer_user_address.empty() ? event.peer_user_id : event.peer_user_address),
                QString());
            call_state_.participants = BuildDirectCallParticipants(call_state_.peer_user_id);
            EmitCallState();

            const bool webrtc_enabled = EnvFlagEnabled("BLACKWIRE_ENABLE_WEBRTC_V2B2", false) ||
                                        event.call_mode == "webrtc";
            const bool legacy_audio_enabled = EnvFlagEnabled("BLACKWIRE_ENABLE_LEGACY_CALL_AUDIO_WS", true);
            if (webrtc_enabled) {
                VoiceCallWebRtcOffer offer;
                const QString local_server = QString::fromStdString(
                    state_.user.home_server_onion.empty() ? ServerAuthority().toStdString() : state_.user.home_server_onion);
                const QString self_address = QString("%1@%2")
                                                 .arg(QString::fromStdString(state_.user.username).trimmed().toLower(), local_server)
                                                 .trimmed()
                                                 .toLower();
                offer.call_id = event.call_id;
                offer.sdp = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=Blackwire\r\nt=0 0\r\nm=audio 9 RTP/AVP 0\r\n";
                offer.source_user_address = self_address.toStdString();
                if (call_state_.peer_user_id.contains('@')) {
                    offer.target_user_address = call_state_.peer_user_id.trimmed().toLower().toStdString();
                }
                ws_client_.SendCallWebRtcOffer(offer);
                call_state_.reason = "WebRTC negotiating";
                if (event.ice_servers.is_array()) {
                    RecordDiagnostic(
                        QString("webrtc ice_servers=%1").arg(static_cast<int>(event.ice_servers.size())));
                }
                EmitCallState();
                if (!legacy_audio_enabled) {
                    return;
                }
            }

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
            pending_outgoing_end_request_ = false;
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
            pending_outgoing_end_request_ = false;
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

            pending_outgoing_end_request_ = false;
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
        [this](const WsEventCallWebRtcOffer& event) {
            RecordDiagnostic(
                QString("received webrtc offer call_id=%1").arg(QString::fromStdString(event.call_id)));
            if (IsCallState("active") && call_state_.call_id == QString::fromStdString(event.call_id)) {
                const QString source_address = QString::fromStdString(
                    event.source_user_address.empty() ? event.from_user_address : event.source_user_address)
                                                   .trimmed()
                                                   .toLower();
                const QString local_server = QString::fromStdString(
                    state_.user.home_server_onion.empty() ? ServerAuthority().toStdString() : state_.user.home_server_onion);
                const QString self_address = QString("%1@%2")
                                                 .arg(QString::fromStdString(state_.user.username).trimmed().toLower(), local_server)
                                                 .trimmed()
                                                 .toLower();
                VoiceCallWebRtcAnswer answer;
                answer.call_id = event.call_id;
                answer.sdp = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=Blackwire\r\nt=0 0\r\nm=audio 9 RTP/AVP 0\r\n";
                answer.source_user_address = self_address.toStdString();
                answer.target_user_address = source_address.toStdString();
                ws_client_.SendCallWebRtcAnswer(answer);
                call_state_.reason = "WebRTC negotiating";
                EmitCallState();
            }
        },
        [this](const WsEventCallWebRtcAnswer& event) {
            RecordDiagnostic(
                QString("received webrtc answer call_id=%1").arg(QString::fromStdString(event.call_id)));
            if (IsCallState("active") && call_state_.call_id == QString::fromStdString(event.call_id)) {
                call_state_.reason = "WebRTC connected";
                EmitCallState();
            }
        },
        [this](const WsEventCallWebRtcIce& event) {
            RecordDiagnostic(
                QString("received webrtc ice call_id=%1").arg(QString::fromStdString(event.call_id)));
            if (IsCallState("active") && call_state_.call_id == QString::fromStdString(event.call_id)) {
                call_state_.reason = "WebRTC negotiating";
                EmitCallState();
            }
        },
        [this](const WsEventGroupRenamed& event) {
            const QString conversation_id = QString::fromStdString(event.conversation_id).trimmed();
            const QString next_group_name = QString::fromStdString(event.group_name).trimmed();
            if (conversation_id.isEmpty() || next_group_name.isEmpty()) {
                return;
            }

            QString previous_group_name;
            for (auto& conversation : state_.conversations) {
                if (conversation.id != conversation_id.toStdString()) {
                    continue;
                }
                previous_group_name = QString::fromStdString(conversation.group_name).trimmed();
                conversation.group_name = next_group_name.toStdString();
                break;
            }

            QString dedupe_suffix;
            if (!previous_group_name.isEmpty()) {
                dedupe_suffix = QString("sync-%1->%2")
                                    .arg(previous_group_name.toLower(), next_group_name.toLower());
            } else if (event.event_seq > 0) {
                dedupe_suffix = QString("event-%1").arg(event.event_seq);
            } else {
                dedupe_suffix = QString("event-%1-%2")
                                    .arg(next_group_name.toLower(), QString::fromStdString(event.actor_address).toLower());
            }
            AppendGroupRenameHistoryEntry(
                conversation_id,
                QString::fromStdString(event.actor_address),
                next_group_name,
                dedupe_suffix);

            PersistState();
            RefreshConversationList();
            if (selected_conversation_id_ == conversation_id.toStdString()) {
                emit ConversationSelected(conversation_id, RenderThread(selected_conversation_id_));
            }
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
            if (!connected) {
                emit UserPresenceChanged("offline");
            } else {
                emit UserPresenceChanged(user_presence_status_);
                RefreshPresenceCache();
                RefreshConversationList();
            }

            if (!connected && !IsCallState("idle")) {
                pending_outgoing_end_request_ = false;
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
        user_presence_status_ = NormalizePresenceValue(QString::fromStdString(state_.social_preferences.presence_status));
        state_.social_preferences.presence_status = user_presence_status_.toStdString();
        const QString preferred_presence_status = user_presence_status_;
        DecryptPlaintextCacheInState();

        TransitionCallState("idle", QString(), QString(), QString(), QString());
        LoadAudioDevices();

        RecordDiagnostic("controller initialized");
        emit AuthStateChanged(
            state_.has_user,
            state_.has_user ? QString::fromStdString(state_.user.username) : QString());
        emit DeviceStateChanged(state_.has_device);
        emit UserPresenceChanged(state_.has_user && state_.has_device ? user_presence_status_ : "offline");

        if (state_.has_user && state_.has_device) {
            LoadConversations();
            StartRealtime();
            SetPresenceStatus(preferred_presence_status);
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
        peer_presence_status_by_address_.clear();
        pending_request_messages_.clear();
        pending_request_senders_.clear();
        state_.social_preferences.presence_status = "active";
        user_presence_status_ = "active";

        SaveBootstrapToken(response.tokens);
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
        peer_presence_status_by_address_.clear();
        user_presence_status_ = NormalizePresenceValue(QString::fromStdString(state_.social_preferences.presence_status));
        state_.social_preferences.presence_status = user_presence_status_.toStdString();
        const QString preferred_presence_status = user_presence_status_;

        if (state_.has_device && state_.device.user_id != state_.user.id) {
            state_.has_device = false;
            state_.conversations.clear();
            state_.conversation_meta.clear();
            state_.local_messages.clear();
            state_.seen_message_ids.clear();
            peer_device_cache_.clear();
            peer_presence_status_by_address_.clear();
            pending_request_messages_.clear();
            pending_request_senders_.clear();
        }

        SaveBootstrapToken(response.tokens);
        PersistState();

        RecordDiagnostic(QString("login success user=%1").arg(QString::fromStdString(state_.user.username)));
        emit AuthStateChanged(true, QString::fromStdString(state_.user.username));
        emit DeviceStateChanged(state_.has_device);
        LoadAudioDevices();

        if (state_.has_device) {
            try {
                std::string error;
                const auto ik_private = secret_store_.GetSecret(SecretKey("ik_private"), &error);
                if (!ik_private.has_value()) {
                    throw std::runtime_error("Device signing key missing");
                }
                const std::string nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
                const long long timestamp_ms = QDateTime::currentMSecsSinceEpoch();
                const std::string canonical =
                    "BIND_DEVICE\n" + state_.user.id + "\n" + state_.device.id + "\n" + nonce + "\n" +
                    std::to_string(timestamp_ms);
                const std::string signature = crypto_.SignDetached(ik_private.value(), canonical);
                const auto bound = api_client_.BindDevice(
                    state_.base_url,
                    RequireBootstrapToken(),
                    state_.device.id,
                    nonce,
                    timestamp_ms,
                    signature);
                SaveTokenPair(bound.tokens);
                DecryptPlaintextCacheInState();
                UploadCurrentDevicePrekeys();
                PersistState();
                LoadConversations();
                StartRealtime();
                SetPresenceStatus(preferred_presence_status);
            } catch (const std::exception& ex) {
                const QString line = QString("Device re-bind failed: %1").arg(ex.what());
                RecordDiagnostic(line);
                emit ErrorOccurred(line);
                state_.has_device = false;
                PersistState();
                emit DeviceStateChanged(false);
            }
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
    secret_store_.DeleteSecret(SecretKey("bootstrap_token"), &error);
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
    emit AccountDevicesChanged(std::vector<DeviceOut>{});
    emit ConversationListChanged(std::vector<ConversationListItemView>{});
    emit ConversationSelected(QString(), {});
    emit UserPresenceChanged("offline");
    LoadAudioDevices();
}

void ApplicationController::SetupDevice(const QString& label) {
    try {
        const auto keys = crypto_.GenerateDeviceKeys();
        DeviceRegisterRequest request;
        request.label = label.toStdString();
        request.ik_ed25519_pub = keys.ik_ed25519_public_b64;
        request.enc_x25519_pub = keys.enc_x25519_public_b64;
        request.pub_sign_key = keys.ik_ed25519_public_b64;
        request.pub_dh_key = keys.enc_x25519_public_b64;

        const auto operation = [this, &request]() {
            return api_client_.RegisterDevice(state_.base_url, RequireBootstrapToken(), request);
        };

        const auto response = operation();
        SaveTokenPair(response.tokens);

        state_.device.id = response.tokens.device_uid;
        state_.device.device_uid = response.tokens.device_uid;
        state_.device.user_id = response.user.id;
        state_.device.label = label.toStdString();
        state_.device.ik_ed25519_pub = keys.ik_ed25519_public_b64;
        state_.device.enc_x25519_pub = keys.enc_x25519_public_b64;
        state_.device.status = "active";
        state_.has_device = true;
        user_presence_status_ = NormalizePresenceValue(QString::fromStdString(state_.social_preferences.presence_status));
        state_.social_preferences.presence_status = user_presence_status_.toStdString();
        const QString preferred_presence_status = user_presence_status_;

        std::string error;
        if (!secret_store_.SetSecret(SecretKey("ik_private"), keys.ik_ed25519_private_b64, &error)) {
            throw std::runtime_error(error);
        }
        if (!secret_store_.SetSecret(SecretKey("enc_private"), keys.enc_x25519_private_b64, &error)) {
            throw std::runtime_error(error);
        }

        UploadCurrentDevicePrekeys();
        PersistState();
        RecordDiagnostic(QString("device setup complete label=%1").arg(label));
        emit DeviceStateChanged(true);
        LoadAudioDevices();

        LoadConversations();
        StartRealtime();
        SetPresenceStatus(preferred_presence_status);
    } catch (const std::exception& ex) {
        const QString line = QString("Device setup failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::LoadAccountDevices() {
    try {
        if (!state_.has_user || !state_.has_device) {
            emit AccountDevicesChanged(std::vector<DeviceOut>{});
            return;
        }

        const auto operation = [this]() {
            return api_client_.ListDevices(state_.base_url, RequireAccessToken());
        };

        auto devices = CallWithAuthRetryOnce<std::vector<DeviceOut>>(operation, [this]() { RefreshAccessToken(); });
        std::sort(devices.begin(), devices.end(), [](const DeviceOut& lhs, const DeviceOut& rhs) {
            const std::string lhs_uid = lhs.device_uid.empty() ? lhs.id : lhs.device_uid;
            const std::string rhs_uid = rhs.device_uid.empty() ? rhs.id : rhs.device_uid;
            if (lhs.status != rhs.status) {
                return lhs.status < rhs.status;
            }
            if (lhs.created_at != rhs.created_at) {
                return lhs.created_at > rhs.created_at;
            }
            return lhs_uid < rhs_uid;
        });
        emit AccountDevicesChanged(devices);
    } catch (const std::exception& ex) {
        const QString line = QString("Load account devices failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::RevokeDevice(const QString& device_uid) {
    try {
        if (!state_.has_user || !state_.has_device) {
            return;
        }
        const std::string target_uid = device_uid.trimmed().toStdString();
        if (target_uid.empty()) {
            return;
        }

        const auto operation = [this, &target_uid]() {
            return api_client_.RevokeDevice(state_.base_url, RequireAccessToken(), target_uid);
        };
        const auto revoked = CallWithAuthRetryOnce<DeviceOut>(operation, [this]() { RefreshAccessToken(); });
        const std::string revoked_uid = revoked.device_uid.empty() ? revoked.id : revoked.device_uid;
        RecordDiagnostic(QString("revoked device uid=%1").arg(QString::fromStdString(revoked_uid)));

        peer_device_cache_.clear();
        if (state_.has_device && revoked_uid == state_.device.id) {
            emit IntegrityWarningOccurred("This device has been revoked. Sign in again.");
            Logout();
            return;
        }

        LoadAccountDevices();
    } catch (const std::exception& ex) {
        const QString line = QString("Revoke device failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
}

void ApplicationController::LoadConversations() {
    try {
        std::unordered_map<std::string, std::pair<std::string, std::string>> previous_group_state;
        previous_group_state.reserve(state_.conversations.size());
        for (const auto& conversation : state_.conversations) {
            if (conversation.conversation_type != "group") {
                continue;
            }
            previous_group_state[conversation.id] = std::make_pair(conversation.group_name, conversation.owner_address);
        }

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

            if (conv.conversation_type != "group") {
                continue;
            }
            const auto previous = previous_group_state.find(conv.id);
            if (previous == previous_group_state.end()) {
                continue;
            }
            const QString previous_name = QString::fromStdString(previous->second.first).trimmed();
            const QString next_name = QString::fromStdString(conv.group_name).trimmed();
            if (previous_name.isEmpty() || next_name.isEmpty() || previous_name == next_name) {
                continue;
            }
            QString actor_address = QString::fromStdString(conv.owner_address).trimmed().toLower();
            if (actor_address.isEmpty()) {
                actor_address = QString::fromStdString(previous->second.second).trimmed().toLower();
            }
            AppendGroupRenameHistoryEntry(
                QString::fromStdString(conv.id),
                actor_address,
                next_name,
                QString("sync-%1->%2").arg(previous_name.toLower(), next_name.toLower()));
        }

        RefreshPresenceCache();
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
        RefreshPresenceCache();
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

        const ConversationOut* selected = FindConversation(selected_conversation_id_);
        if (selected != nullptr && selected->conversation_type == "group") {
            const QString membership_state = QString::fromStdString(selected->membership_state).trimmed().toLower();
            if (membership_state == "invited") {
                const auto accept_op = [this]() {
                    return api_client_.AcceptConversationInvite(
                        state_.base_url,
                        RequireAccessToken(),
                        selected_conversation_id_);
                };
                (void)CallWithAuthRetryOnce<ConversationMemberOut>(accept_op, [this]() { RefreshAccessToken(); });
                LoadConversations();
                RecordDiagnostic(
                    QString("accepted group invite conversation_id=%1")
                        .arg(QString::fromStdString(selected_conversation_id_)));
            } else if (membership_state != "active" && membership_state != "none") {
                emit ErrorOccurred("You are not an active member of this group.");
                return;
            }
        }

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
        std::unordered_set<std::string> rebuilt_ids;
        const QString self_address = QString("%1@%2")
                                         .arg(
                                             QString::fromStdString(state_.user.username).trimmed().toLower(),
                                             QString::fromStdString(
                                                 state_.user.home_server_onion.empty()
                                                     ? ServerAuthority().toStdString()
                                                     : state_.user.home_server_onion))
                                         .trimmed()
                                         .toLower();

        std::string error;
        const auto private_key = secret_store_.GetSecret(SecretKey("enc_private"), &error);
        auto parse_time = [](const std::string& value) {
            const QString iso = QString::fromStdString(value);
            QDateTime parsed = QDateTime::fromString(iso, Qt::ISODateWithMs);
            if (!parsed.isValid()) {
                parsed = QDateTime::fromString(iso, Qt::ISODate);
            }
            return parsed;
        };
        QDateTime oldest_remote_time;
        for (const auto& message : remote_messages) {
            const QDateTime created = parse_time(message.created_at);
            if (!created.isValid()) {
                continue;
            }
            if (!oldest_remote_time.isValid() || created < oldest_remote_time) {
                oldest_remote_time = created;
            }
        }

        for (const auto& message : remote_messages) {
            auto existing = existing_by_id.find(message.id);
            if (existing != existing_by_id.end()) {
                if (existing->second.plaintext.empty()) {
                    existing->second.plaintext = ExtractLegacyPlaintext(QString::fromStdString(existing->second.rendered_text)).toStdString();
                }
                if (existing->second.sender_address.empty()) {
                    if (!message.sender_address.empty()) {
                        existing->second.sender_address = message.sender_address;
                    } else if (existing->second.sender_user_id == state_.user.id) {
                        existing->second.sender_address = self_address.toStdString();
                    }
                }
                rebuilt.push_back(existing->second);
                rebuilt_ids.insert(existing->second.id);
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
            local.sender_address = message.sender_address.empty() && message.sender_user_id == state_.user.id
                                       ? self_address.toStdString()
                                       : message.sender_address;
            local.created_at = message.created_at;
            local.rendered_text = RenderMessage(message, plaintext).toStdString();
            local.plaintext = plaintext;
            rebuilt.push_back(local);
            rebuilt_ids.insert(local.id);

            state_.MarkMessageSeen(message.id);
        }

        if (existing_it != state_.local_messages.end()) {
            for (const auto& existing : existing_it->second) {
                if (rebuilt_ids.contains(existing.id)) {
                    continue;
                }
                // Server listing is device-copy scoped and paginated, so preserve
                // self/system/older-cached entries that are not in this window.
                const bool self_sent = existing.sender_user_id == state_.user.id;
                const bool local_system_entry = existing.sender_user_id.empty();
                bool older_than_fetch_window = false;
                if (oldest_remote_time.isValid()) {
                    const QDateTime existing_time = parse_time(existing.created_at);
                    older_than_fetch_window = existing_time.isValid() && existing_time < oldest_remote_time;
                }
                if (self_sent || local_system_entry || older_than_fetch_window) {
                    LocalMessage carry = existing;
                    if (carry.sender_address.empty()) {
                        carry.sender_address = self_address.toStdString();
                    }
                    rebuilt.push_back(std::move(carry));
                }
            }
        }
        std::stable_sort(rebuilt.begin(), rebuilt.end(), [&parse_time](const LocalMessage& lhs, const LocalMessage& rhs) {
            const QDateTime lhs_time = parse_time(lhs.created_at);
            const QDateTime rhs_time = parse_time(rhs.created_at);
            if (lhs_time.isValid() && rhs_time.isValid() && lhs_time != rhs_time) {
                return lhs_time < rhs_time;
            }
            if (lhs.created_at != rhs.created_at) {
                return lhs.created_at < rhs.created_at;
            }
            return lhs.id < rhs.id;
        });

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

bool ApplicationController::DismissDirectConversation(const QString& conversation_id) {
    try {
        const std::string conversation_key = conversation_id.trimmed().toStdString();
        if (conversation_key.empty()) {
            return false;
        }
        const ConversationOut* selected = FindConversation(conversation_key);
        if (selected == nullptr || selected->conversation_type != "direct") {
            emit ErrorOccurred("Only direct messages can be dismissed.");
            return false;
        }

        state_.dismissed_conversation_ids.insert(conversation_key);
        state_.local_messages.erase(conversation_key);
        state_.conversation_meta.erase(conversation_key);
        pending_request_messages_.erase(conversation_key);
        pending_request_senders_.erase(conversation_key);

        if (selected_conversation_id_ == conversation_key) {
            selected_conversation_id_.clear();
            emit ConversationSelected(QString(), {});
        }

        PersistState();
        RefreshConversationList();
        RecordDiagnostic(QString("dismissed direct conversation id=%1").arg(conversation_id));
        return true;
    } catch (const std::exception& ex) {
        const QString line = QString("Dismiss direct conversation failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
        return false;
    }
}

bool ApplicationController::LeaveGroupConversation(const QString& conversation_id) {
    try {
        const std::string conversation_key = conversation_id.trimmed().toStdString();
        if (conversation_key.empty()) {
            return false;
        }
        const ConversationOut* selected = FindConversation(conversation_key);
        if (selected == nullptr || selected->conversation_type != "group") {
            emit ErrorOccurred("Only group conversations can be left.");
            return false;
        }

        const auto operation = [this, &conversation_key]() {
            return api_client_.LeaveConversationGroup(
                state_.base_url,
                RequireAccessToken(),
                conversation_key);
        };
        (void)CallWithAuthRetryOnce<ConversationMemberOut>(operation, [this]() { RefreshAccessToken(); });

        state_.local_messages.erase(conversation_key);
        state_.conversation_meta.erase(conversation_key);
        state_.blocked_conversation_ids.erase(conversation_key);
        state_.dismissed_conversation_ids.erase(conversation_key);
        pending_request_messages_.erase(conversation_key);
        pending_request_senders_.erase(conversation_key);

        if (selected_conversation_id_ == conversation_key) {
            selected_conversation_id_.clear();
            emit ConversationSelected(QString(), {});
        }

        LoadConversations();
        RecordDiagnostic(QString("left group conversation id=%1").arg(conversation_id));
        return true;
    } catch (const ApiException& ex) {
        QString message = QString::fromStdString(ex.what());
        if (ex.status_code() == 404 && message.contains("disabled", Qt::CaseInsensitive)) {
            message = "Group DMs are disabled on this federation.";
        }
        const QString line = QString("Leave group conversation failed: %1").arg(message);
        RecordDiagnostic(line);
        emit ErrorOccurred(message);
        return false;
    } catch (const std::exception& ex) {
        const QString line = QString("Leave group conversation failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
        return false;
    }
}

bool ApplicationController::CreateGroupFromCurrentDm() {
    try {
        if (!state_.has_user || !state_.has_device) {
            emit ErrorOccurred("Sign in before creating a group.");
            return false;
        }
        if (selected_conversation_id_.empty()) {
            emit ErrorOccurred("Select a direct message first.");
            return false;
        }

        const ConversationOut* selected = FindConversation(selected_conversation_id_);
        if (selected == nullptr || selected->conversation_type != "direct") {
            emit ErrorOccurred("Group creation is only available from a direct message.");
            return false;
        }

        const QString peer_address = ResolvePeerAddressForConversation(selected->id).trimmed().toLower();
        if (peer_address.isEmpty()) {
            emit ErrorOccurred("Cannot determine the selected peer address.");
            return false;
        }

        const auto parsed_peer = ParsePeerAddress(peer_address);
        if (!parsed_peer.has_value() || parsed_peer->username.trimmed().isEmpty()) {
            emit ErrorOccurred("Selected DM peer is invalid for group creation.");
            return false;
        }

        const QString self_username = QString::fromStdString(state_.user.username).trimmed().toLower();
        const QString peer_username = parsed_peer->username.trimmed().toLower();
        QStringList name_parts;
        if (!self_username.isEmpty()) {
            name_parts.push_back(self_username);
        }
        if (!peer_username.isEmpty()) {
            name_parts.push_back(peer_username);
        }
        name_parts.removeDuplicates();
        std::sort(name_parts.begin(), name_parts.end(), [](const QString& lhs, const QString& rhs) {
            return lhs.toLower() < rhs.toLower();
        });
        const QString group_name = name_parts.isEmpty() ? "group" : name_parts.join(" + ");

        CreateGroupConversationRequest request;
        request.name = group_name.toStdString();
        request.member_addresses = {peer_address.toStdString()};

        const auto operation = [this, &request]() {
            return api_client_.CreateGroup(state_.base_url, RequireAccessToken(), request);
        };
        const auto created = CallWithAuthRetryOnce<ConversationOut>(operation, [this]() { RefreshAccessToken(); });

        LoadConversations();
        RevealConversation(created.id, false);
        SelectConversation(QString::fromStdString(created.id));
        RecordDiagnostic(
            QString("created group conversation id=%1 with peer=%2")
                .arg(QString::fromStdString(created.id), peer_address));
        return true;
    } catch (const ApiException& ex) {
        QString message = QString::fromStdString(ex.what());
        if (ex.status_code() == 404 && message.contains("disabled", Qt::CaseInsensitive)) {
            message = "Group DMs are disabled on this federation.";
        }
        const QString line = QString("Create group failed: %1").arg(message);
        RecordDiagnostic(line);
        emit ErrorOccurred(message);
        return false;
    } catch (const std::exception& ex) {
        const QString line = QString("Create group failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
        return false;
    }
}

GroupInvitePickerView ApplicationController::LoadInvitableContactsForCurrentGroup(const QString& query) {
    GroupInvitePickerView picker;
    try {
        bool max_members_ok = false;
        int configured_group_max_members = qEnvironmentVariableIntValue("BLACKWIRE_GROUP_MAX_MEMBERS", &max_members_ok);
        if (!max_members_ok || configured_group_max_members <= 0) {
            configured_group_max_members = 32;
        }
        if (!state_.has_user || !state_.has_device || selected_conversation_id_.empty()) {
            return picker;
        }

        const ConversationOut* selected = FindConversation(selected_conversation_id_);
        if (selected == nullptr || selected->conversation_type != "group" || !selected->can_manage_members) {
            return picker;
        }

        const auto members_op = [this]() {
            return api_client_.ListConversationMembers(
                state_.base_url,
                RequireAccessToken(),
                selected_conversation_id_);
        };
        const auto members = CallWithAuthRetryOnce<std::vector<ConversationMemberOut>>(
            members_op,
            [this]() { RefreshAccessToken(); });

        std::unordered_set<std::string> excluded_addresses;
        std::unordered_set<std::string> active_or_invited_identities;
        for (const auto& member : members) {
            const std::string normalized = QString::fromStdString(member.member_address).trimmed().toLower().toStdString();
            const QString status = QString::fromStdString(member.status).trimmed().toLower();
            if (status == "active" || status == "invited") {
                if (!normalized.empty()) {
                    excluded_addresses.insert(normalized);
                }
                const QString identity = !member.member_user_id.empty()
                                             ? QString("user:%1")
                                                   .arg(QString::fromStdString(member.member_user_id).trimmed().toLower())
                                             : QString("addr:%1").arg(QString::fromStdString(normalized));
                active_or_invited_identities.insert(identity.toStdString());
            }
        }
        picker.remaining_slots = std::max(0, configured_group_max_members - static_cast<int>(active_or_invited_identities.size()));

        const QString local_server = QString::fromStdString(
                                         state_.user.home_server_onion.empty()
                                             ? ServerAuthority().toStdString()
                                             : state_.user.home_server_onion)
                                         .trimmed()
                                         .toLower();
        const QString self_address = QString("%1@%2")
                                         .arg(QString::fromStdString(state_.user.username).trimmed().toLower(), local_server);
        excluded_addresses.insert(self_address.toStdString());

        const QString normalized_query = query.trimmed().toLower();
        std::unordered_set<std::string> seen_candidates;

        for (const auto& conv : state_.conversations) {
            if (conv.conversation_type != "direct") {
                continue;
            }
            if (state_.blocked_conversation_ids.contains(conv.id)) {
                continue;
            }

            const QString peer_address = ResolvePeerAddressForConversation(conv.id).trimmed().toLower();
            const std::string key = peer_address.toStdString();
            if (key.empty() || excluded_addresses.contains(key) || seen_candidates.contains(key)) {
                continue;
            }

            QString title = FriendlyConversationTitle(conv.id).trimmed();
            if (title.isEmpty()) {
                title = peer_address.section('@', 0, 0);
            }
            const QString subtitle = peer_address;
            const QString haystack = QString("%1 %2").arg(title, subtitle).toLower();
            if (!normalized_query.isEmpty() && !haystack.contains(normalized_query)) {
                continue;
            }

            GroupInviteCandidateView candidate;
            candidate.peer_address = peer_address;
            candidate.title = title;
            candidate.subtitle = subtitle;
            candidate.status = PresenceForPeerAddress(peer_address);
            picker.candidates.push_back(candidate);
            seen_candidates.insert(key);
        }

        std::sort(
            picker.candidates.begin(),
            picker.candidates.end(),
            [](const GroupInviteCandidateView& lhs, const GroupInviteCandidateView& rhs) {
                return lhs.title.trimmed().toLower() < rhs.title.trimmed().toLower();
            });
    } catch (const std::exception& ex) {
        const QString line = QString("Load invitable contacts failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
    }
    return picker;
}

bool ApplicationController::InviteContactsToCurrentGroup(const std::vector<QString>& peer_addresses) {
    try {
        if (!IsSelectedConversationOwnerManagedGroup()) {
            emit ErrorOccurred("Only the group owner can invite members.");
            return false;
        }
        if (selected_conversation_id_.empty()) {
            emit ErrorOccurred("Select a group conversation first.");
            return false;
        }

        std::vector<std::string> normalized_addresses;
        normalized_addresses.reserve(peer_addresses.size());
        std::unordered_set<std::string> seen;
        for (const auto& raw : peer_addresses) {
            const QString normalized = raw.trimmed().toLower();
            if (normalized.isEmpty()) {
                continue;
            }
            const auto parsed = ParsePeerAddress(normalized);
            if (!parsed.has_value()) {
                continue;
            }
            const std::string canonical = parsed->has_server
                                              ? QString("%1@%2")
                                                    .arg(parsed->username.trimmed().toLower(), parsed->server_authority.trimmed().toLower())
                                                    .toStdString()
                                              : parsed->username.trimmed().toLower().toStdString();
            if (canonical.empty() || seen.contains(canonical)) {
                continue;
            }
            seen.insert(canonical);
            normalized_addresses.push_back(canonical);
        }

        if (normalized_addresses.empty()) {
            emit ErrorOccurred("Select at least one contact to invite.");
            return false;
        }

        GroupInviteRequest request;
        request.member_addresses = normalized_addresses;
        const auto operation = [this, &request]() {
            return api_client_.InviteConversationMembers(
                state_.base_url,
                RequireAccessToken(),
                selected_conversation_id_,
                request);
        };
        (void)CallWithAuthRetryOnce<std::vector<ConversationMemberOut>>(operation, [this]() { RefreshAccessToken(); });

        const QString keep_selected = QString::fromStdString(selected_conversation_id_);
        LoadConversations();
        SelectConversation(keep_selected);
        RecordDiagnostic(
            QString("invited %1 member(s) to group id=%2")
                .arg(static_cast<int>(normalized_addresses.size()))
                .arg(keep_selected));
        return true;
    } catch (const ApiException& ex) {
        QString message = QString::fromStdString(ex.what());
        if (ex.status_code() == 404 && message.contains("disabled", Qt::CaseInsensitive)) {
            message = "Group DMs are disabled on this federation.";
        }
        const QString line = QString("Group invite failed: %1").arg(message);
        RecordDiagnostic(line);
        emit ErrorOccurred(message);
        return false;
    } catch (const std::exception& ex) {
        const QString line = QString("Group invite failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
        return false;
    }
}

bool ApplicationController::RenameSelectedGroup(const QString& new_name) {
    try {
        if (!IsSelectedConversationOwnerManagedGroup()) {
            emit ErrorOccurred("Only the group owner can rename this group.");
            return false;
        }
        if (selected_conversation_id_.empty()) {
            emit ErrorOccurred("Select a group conversation first.");
            return false;
        }

        const QString normalized_name = new_name.trimmed();
        if (normalized_name.isEmpty()) {
            emit ErrorOccurred("Group name cannot be empty.");
            return false;
        }

        const ConversationOut* selected = FindConversation(selected_conversation_id_);
        if (selected == nullptr || selected->conversation_type != "group") {
            emit ErrorOccurred("Selected conversation is not a group.");
            return false;
        }
        const QString current_name = QString::fromStdString(selected->group_name).trimmed();
        if (!current_name.isEmpty() && current_name == normalized_name) {
            return true;
        }

        GroupRenameRequest request;
        request.name = normalized_name.toStdString();
        const auto operation = [this, &request]() {
            return api_client_.RenameConversationGroup(
                state_.base_url,
                RequireAccessToken(),
                selected_conversation_id_,
                request);
        };
        (void)CallWithAuthRetryOnce<ConversationOut>(operation, [this]() { RefreshAccessToken(); });

        const QString keep_selected = QString::fromStdString(selected_conversation_id_);
        LoadConversations();
        SelectConversation(keep_selected);
        RecordDiagnostic(
            QString("renamed group id=%1 to '%2'")
                .arg(keep_selected, normalized_name));
        return true;
    } catch (const std::exception& ex) {
        const QString line = QString("Group rename failed: %1").arg(ex.what());
        RecordDiagnostic(line);
        emit ErrorOccurred(line);
        return false;
    }
}

bool ApplicationController::IsSelectedConversationOwnerManagedGroup() const {
    if (selected_conversation_id_.empty()) {
        return false;
    }
    const ConversationOut* selected = FindConversation(selected_conversation_id_);
    if (selected == nullptr) {
        return false;
    }
    return selected->conversation_type == "group" && selected->can_manage_members;
}

void ApplicationController::SendMessageToPeer(const QString& peer_username, const QString& message_text) {
    try {
        if (message_text.trimmed().isEmpty()) {
            return;
        }

        const QString home_server = QString::fromStdString(
            state_.user.home_server_onion.empty() ? ServerAuthority().toStdString() : state_.user.home_server_onion);
        const std::string self_address =
            QString("%1@%2")
                .arg(QString::fromStdString(state_.user.username).trimmed().toLower(), home_server)
                .toStdString();

        bool group_send = false;
        QString normalized_peer;
        std::string conversation_id;
        std::vector<ConversationRecipientDeviceOut> group_recipients;

        if (!peer_username.trimmed().isEmpty()) {
            QString error;
            const auto normalized = NormalizePeerUsername(peer_username, &error);
            if (!normalized.has_value()) {
                emit ErrorOccurred(error);
                return;
            }
            normalized_peer = normalized->trimmed().toLower();
            conversation_id = ResolveConversationIdForPeer(normalized_peer);
            RevealConversation(conversation_id, false);
        } else if (!selected_conversation_id_.empty()) {
            const ConversationOut* selected = FindConversation(selected_conversation_id_);
            if (selected != nullptr && selected->conversation_type == "group") {
                group_send = true;
                conversation_id = selected->id;
                const auto recipients_op = [this, &conversation_id]() {
                    return api_client_.GetConversationRecipients(
                        state_.base_url,
                        RequireAccessToken(),
                        conversation_id);
                };
                const auto recipients = CallWithAuthRetryOnce<ConversationRecipientsOut>(
                    recipients_op,
                    [this]() { RefreshAccessToken(); });
                group_recipients = recipients.recipients;
                RevealConversation(conversation_id, false);
            } else {
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
                conversation_id = ResolveConversationIdForPeer(normalized_peer);
                RevealConversation(conversation_id, false);
            }
        } else {
            emit ErrorOccurred("Please enter a valid username or username@onion");
            return;
        }

        struct SendTarget {
            std::string recipient_address;
            DeviceOut device;
            bool self_mirror = false;
            std::optional<ResolvedPrekeyDevice> prekey;
        };

        std::vector<SendTarget> targets;
        std::set<std::string> seen_targets;
        std::string peer_address;

        if (group_send) {
            for (const auto& recipient : group_recipients) {
                DeviceOut device = recipient.device;
                if (device.status == "revoked") {
                    continue;
                }
                const std::string target_uid = device.device_uid.empty() ? device.id : device.device_uid;
                if (target_uid.empty() || seen_targets.contains(target_uid) || target_uid == state_.device.id) {
                    continue;
                }
                seen_targets.insert(target_uid);
                if (device.device_uid.empty()) {
                    device.device_uid = target_uid;
                }
                if (device.id.empty()) {
                    device.id = target_uid;
                }
                targets.push_back(SendTarget{
                    recipient.member_address,
                    device,
                    recipient.member_address == self_address,
                    recipient.prekey,
                });
            }
        } else {
            const std::vector<DeviceOut> recipient_devices = ResolveRecipientDevices(normalized_peer);
            const std::vector<DeviceOut> own_devices = ResolveOwnActiveDevices();
            peer_address = normalized_peer.contains('@')
                               ? normalized_peer.toStdString()
                               : QString("%1@%2").arg(normalized_peer, home_server).toStdString();

            for (const auto& device : recipient_devices) {
                const std::string target_uid = device.device_uid.empty() ? device.id : device.device_uid;
                if (target_uid.empty() || seen_targets.contains(target_uid)) {
                    continue;
                }
                seen_targets.insert(target_uid);
                DeviceOut normalized_device = device;
                if (normalized_device.device_uid.empty()) {
                    normalized_device.device_uid = target_uid;
                }
                if (normalized_device.id.empty()) {
                    normalized_device.id = target_uid;
                }
                targets.push_back(SendTarget{peer_address, normalized_device, false, std::nullopt});
            }

            for (const auto& device : own_devices) {
                const std::string target_uid = device.device_uid.empty() ? device.id : device.device_uid;
                if (target_uid.empty() || target_uid == state_.device.id || seen_targets.contains(target_uid)) {
                    continue;
                }
                seen_targets.insert(target_uid);
                DeviceOut normalized_device = device;
                if (normalized_device.device_uid.empty()) {
                    normalized_device.device_uid = target_uid;
                }
                if (normalized_device.id.empty()) {
                    normalized_device.id = target_uid;
                }
                targets.push_back(SendTarget{self_address, normalized_device, true, std::nullopt});
            }
        }

        bool use_ratchet_mode = PreferRatchetV2b1() && !targets.empty();
        if (use_ratchet_mode) {
            for (const auto& target : targets) {
                if (!DeviceSupportsMessageMode(target.device, "ratchet_v0_2b1")) {
                    use_ratchet_mode = false;
                    break;
                }
            }
        }
        std::unordered_map<std::string, ResolvedPrekeyDevice> prekeys_by_device;
        for (const auto& target : targets) {
            const std::string target_uid =
                target.device.device_uid.empty() ? target.device.id : target.device.device_uid;
            if (target_uid.empty() || !target.prekey.has_value()) {
                continue;
            }
            prekeys_by_device[target_uid] = target.prekey.value();
        }
        if (use_ratchet_mode) {
            if (!group_send) {
                try {
                    const auto prekey_op = [this, &peer_address]() {
                        return api_client_.ResolvePrekeys(state_.base_url, RequireAccessToken(), peer_address);
                    };
                    const auto prekeys =
                        CallWithAuthRetryOnce<ResolvePrekeysResponse>(prekey_op, [this]() { RefreshAccessToken(); });
                    for (const auto& item : prekeys.devices) {
                        prekeys_by_device[item.device_uid] = item;
                    }
                } catch (const std::exception& ex) {
                    use_ratchet_mode = false;
                    RecordDiagnostic(QString("ratchet fallback: prekey resolve failed (%1)").arg(ex.what()));
                }
            }
            if (use_ratchet_mode) {
                for (const auto& target : targets) {
                    if (target.self_mirror) {
                        continue;
                    }
                    const std::string target_uid =
                        target.device.device_uid.empty() ? target.device.id : target.device.device_uid;
                    const auto it = prekeys_by_device.find(target_uid);
                    if (it == prekeys_by_device.end() || !it->second.signed_prekey.has_value()) {
                        use_ratchet_mode = false;
                        break;
                    }
                }
                if (!use_ratchet_mode) {
                    RecordDiagnostic("ratchet fallback: missing signed prekeys for one or more recipient devices");
                }
            }
        }

        MessageSendRequest request;
        request.conversation_id = conversation_id;
        request.encryption_mode = use_ratchet_mode ? "ratchet_v0_2b1" : "sealedbox_v0_2a";
        if (use_ratchet_mode) {
            RecordDiagnostic("message send mode=ratchet_v0_2b1");
        } else if (PreferRatchetV2b1()) {
            RecordDiagnostic("message send mode=sealedbox_v0_2a (fallback)");
        }
        request.client_message_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        request.sent_at_ms = QDateTime::currentMSecsSinceEpoch();
        const std::string sender_chain_key = conversation_id + "|" + state_.device.id;
        const auto chain_it = state_.last_verified_chain_hash_by_conversation_sender.find(sender_chain_key);
        request.sender_prev_hash =
            chain_it == state_.last_verified_chain_hash_by_conversation_sender.end() ? "" : chain_it->second;

        struct PendingEnvelope {
            CipherEnvelope envelope;
            std::string ciphertext_hash;
            std::string aad_hash;
        };
        std::vector<PendingEnvelope> pending;

        for (const auto& target : targets) {
            const auto& device = target.device;
            const std::string target_uid = device.device_uid.empty() ? device.id : device.device_uid;
            if (target_uid.empty()) {
                continue;
            }
            PendingEnvelope pending_env;
            pending_env.envelope.recipient_user_address = target.recipient_address;
            pending_env.envelope.recipient_device_uid = target_uid;
            pending_env.envelope.recipient_device_id = target_uid;
            pending_env.envelope.ciphertext_b64 =
                crypto_.EncryptForRecipient(device.enc_x25519_pub, message_text.toStdString());
            pending_env.envelope.aad_b64.clear();
            pending_env.envelope.sender_device_pubkey = state_.device.ik_ed25519_pub;
            if (use_ratchet_mode) {
                pending_env.envelope.ratchet_header = nlohmann::json{
                    {"v", "dr_v1"},
                    {"dh_pub", state_.device.enc_x25519_pub},
                    {"n", 0},
                    {"pn", 0},
                };
                if (target.self_mirror) {
                    pending_env.envelope.ratchet_header["self_mirror"] = true;
                }
                const auto prekey_it = prekeys_by_device.find(target_uid);
                if (prekey_it != prekeys_by_device.end()) {
                    nlohmann::json init = {
                        {"scheme", "x3dh_v1"},
                        {"sender_ephemeral_pub", state_.device.enc_x25519_pub},
                        {"opk_missing", prekey_it->second.opk_missing},
                    };
                    if (prekey_it->second.signed_prekey.has_value()) {
                        init["signed_prekey_id"] = prekey_it->second.signed_prekey->key_id;
                    }
                    if (prekey_it->second.one_time_prekey.has_value()) {
                        init["one_time_prekey_id"] = prekey_it->second.one_time_prekey->key_id;
                    } else {
                        init["one_time_prekey_id"] = nullptr;
                    }
                    pending_env.envelope.ratchet_init = init;
                }
            }
            const QByteArray ciphertext_bytes = QByteArray::fromBase64(
                QByteArray::fromStdString(pending_env.envelope.ciphertext_b64));
            pending_env.ciphertext_hash = crypto_.Sha256(ciphertext_bytes.toStdString());
            pending_env.aad_hash = crypto_.Sha256(std::string());
            pending.push_back(pending_env);
        }

        if (pending.empty()) {
            use_ratchet_mode = false;
            request.encryption_mode = "sealedbox_v0_2a";
            DeviceOut self_device;
            self_device.id = state_.device.id;
            self_device.device_uid = state_.device.id;
            self_device.user_id = state_.user.id;
            self_device.label = state_.device.label;
            self_device.ik_ed25519_pub = state_.device.ik_ed25519_pub;
            self_device.enc_x25519_pub = state_.device.enc_x25519_pub;
            self_device.status = "active";

            PendingEnvelope fallback_env;
            fallback_env.envelope.recipient_user_address = self_address;
            fallback_env.envelope.recipient_device_uid = state_.device.id;
            fallback_env.envelope.recipient_device_id = state_.device.id;
            fallback_env.envelope.ciphertext_b64 =
                crypto_.EncryptForRecipient(self_device.enc_x25519_pub, message_text.toStdString());
            fallback_env.envelope.aad_b64.clear();
            fallback_env.envelope.sender_device_pubkey = state_.device.ik_ed25519_pub;
            const QByteArray fallback_ciphertext_bytes = QByteArray::fromBase64(
                QByteArray::fromStdString(fallback_env.envelope.ciphertext_b64));
            fallback_env.ciphertext_hash = crypto_.Sha256(fallback_ciphertext_bytes.toStdString());
            fallback_env.aad_hash = crypto_.Sha256(std::string());
            pending.push_back(fallback_env);

            RecordDiagnostic("send fallback: no recipient devices resolved; message retained for sender device");
        }

        std::vector<std::string> chain_material;
        chain_material.reserve(pending.size());
        for (const auto& item : pending) {
            const std::string target_uid =
                item.envelope.recipient_device_uid.empty() ? item.envelope.recipient_device_id
                                                           : item.envelope.recipient_device_uid;
            chain_material.push_back(target_uid + ":" + item.ciphertext_hash + ":" + item.aad_hash);
        }
        request.sender_chain_hash = AggregateChainHash(
            crypto_,
            request.sender_prev_hash,
            request.client_message_id,
            request.sent_at_ms,
            chain_material);

        std::string secret_error;
        const auto sender_ik_private = secret_store_.GetSecret(SecretKey("ik_private"), &secret_error);
        if (!sender_ik_private.has_value()) {
            throw std::runtime_error("Device signing key unavailable");
        }

        for (auto& item : pending) {
            const std::string canonical = CanonicalMessageSignature(
                self_address,
                state_.device.id,
                item.envelope.recipient_user_address,
                item.envelope.recipient_device_uid.empty() ? item.envelope.recipient_device_id
                                                           : item.envelope.recipient_device_uid,
                request.client_message_id,
                request.sent_at_ms,
                request.sender_prev_hash,
                request.sender_chain_hash,
                item.ciphertext_hash,
                item.aad_hash);
            item.envelope.signature_b64 = crypto_.SignDetached(sender_ik_private.value(), canonical);
            request.envelopes.push_back(item.envelope);
        }

        const auto send_op = [this, &request]() {
            return api_client_.SendMessage(state_.base_url, RequireAccessToken(), request);
        };
        const auto sent = CallWithAuthRetryOnce<MessageSendResponse>(send_op, [this]() { RefreshAccessToken(); });

        const std::string resolved_conversation_id =
            sent.message.conversation_id.empty() ? conversation_id : sent.message.conversation_id;
        selected_conversation_id_ = resolved_conversation_id;
        state_.MarkMessageSeen(sent.message.id);

        LocalMessage local;
        local.id = sent.message.id;
        local.conversation_id = resolved_conversation_id;
        local.sender_user_id = state_.user.id;
        local.sender_address = self_address;
        local.created_at = sent.message.created_at;
        local.rendered_text = RenderMessage(sent.message, message_text.toStdString()).toStdString();
        local.plaintext = message_text.toStdString();
        state_.local_messages[selected_conversation_id_].push_back(local);
        state_.last_verified_chain_hash_by_conversation_sender[sender_chain_key] = sent.message.sender_chain_hash;
        if (resolved_conversation_id != conversation_id) {
            const std::string canonical_chain_key = resolved_conversation_id + "|" + state_.device.id;
            state_.last_verified_chain_hash_by_conversation_sender[canonical_chain_key] = sent.message.sender_chain_hash;
        }

        if (group_send) {
            UpsertConversationMeta(
                selected_conversation_id_,
                QString(),
                QString(),
                MessagePreview(message_text),
                QString::fromStdString(sent.message.created_at));
        } else {
            UpsertConversationMeta(
                selected_conversation_id_,
                normalized_peer.contains('@') ? normalized_peer.section('@', 0, 0) : normalized_peer,
                normalized_peer.contains('@') ? normalized_peer : QString(),
                MessagePreview(message_text),
                QString::fromStdString(sent.message.created_at));
        }
        if (resolved_conversation_id != conversation_id) {
            try {
                LoadConversations();
            } catch (...) {
                // Keep optimistic local state even if conversation refresh fails.
            }
        }

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

void ApplicationController::SendFileToPeer(const QString& peer_username, const QString& file_path) {
    QString normalized_peer;
    bool selected_group_conversation = false;
    if (!peer_username.trimmed().isEmpty()) {
        QString error;
        const auto normalized = NormalizePeerUsername(peer_username, &error);
        if (!normalized.has_value()) {
            emit ErrorOccurred(error);
            return;
        }
        normalized_peer = normalized->trimmed().toLower();
    } else if (!selected_conversation_id_.empty()) {
        const ConversationOut* selected = FindConversation(selected_conversation_id_);
        if (selected != nullptr && selected->conversation_type == "group") {
            selected_group_conversation = true;
        } else {
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
        }
    } else {
        emit ErrorOccurred("Please enter a valid username or username@onion");
        return;
    }

    const QString normalized_path = file_path.trimmed();
    if (normalized_path.isEmpty()) {
        return;
    }

    QFile input(normalized_path);
    if (!input.open(QIODevice::ReadOnly)) {
        emit ErrorOccurred(QString("Unable to open file: %1").arg(normalized_path));
        return;
    }

    const QByteArray bytes = input.readAll();
    input.close();
    if (bytes.isEmpty()) {
        emit ErrorOccurred("Cannot send an empty file.");
        return;
    }

    if (!selected_group_conversation && !normalized_peer.trimmed().isEmpty()) {
        try {
            ResolveRecipientDevices(normalized_peer);
        } catch (...) {
            // Sending will surface canonical errors; policy fallback below remains available.
        }
    }

    const std::string policy_key = normalized_peer.trimmed().toLower().toStdString();
    qint64 effective_inline_limit = 0;
    qint64 max_ciphertext_bytes = 0;
    QString policy_source = "fallback_local";

    const auto policy_it = peer_attachment_policy_cache_.find(policy_key);
    if (policy_it != peer_attachment_policy_cache_.end() && policy_it->second.attachment_inline_max_bytes > 0) {
        effective_inline_limit = policy_it->second.attachment_inline_max_bytes;
        max_ciphertext_bytes = policy_it->second.max_ciphertext_bytes;
        policy_source = QString::fromStdString(policy_it->second.source);
    } else if (local_attachment_policy_cache_.has_value() &&
               local_attachment_policy_cache_->attachment_inline_max_bytes > 0) {
        effective_inline_limit = local_attachment_policy_cache_->attachment_inline_max_bytes;
        max_ciphertext_bytes = local_attachment_policy_cache_->max_ciphertext_bytes;
        policy_source = "fallback_local";
    } else {
        effective_inline_limit = kConservativeInlineAttachmentFallbackBytes;
        max_ciphertext_bytes = kConservativeInlineAttachmentFallbackBytes + (4 * 1024 * 1024);
        policy_source = "fallback_local";
    }

    if (bytes.size() > effective_inline_limit) {
        const QString source_label = policy_source == "remote"
                                         ? "remote federation policy"
                                         : (policy_source == "local" ? "local federation policy" : "local fallback policy");
        emit ErrorOccurred(
            QString("File is too large for encrypted inline share (%1 max, source: %2).")
                .arg(FormatBytesHuman(effective_inline_limit), source_label));
        return;
    }

    const QFileInfo info(normalized_path);
    QJsonObject payload;
    payload.insert("name", info.fileName());
    payload.insert("size", static_cast<double>(bytes.size()));
    payload.insert("data_b64", QString::fromLatin1(bytes.toBase64()));
    const QByteArray serialized = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QString marker =
        QString("%1%2").arg(kFileMessagePrefix, QString::fromLatin1(serialized.toBase64()));
    const qint64 marker_bytes = marker.toUtf8().size();
    const qint64 projected_ciphertext_bytes = marker_bytes + kSealedBoxOverheadBytes;
    if (max_ciphertext_bytes > 0 && projected_ciphertext_bytes > max_ciphertext_bytes) {
        const QString source_label = policy_source == "remote"
                                         ? "remote federation policy"
                                         : (policy_source == "local" ? "local federation policy" : "local fallback policy");
        emit ErrorOccurred(
            QString("Encrypted file envelope exceeds ciphertext policy (%1 max, source: %2).")
                .arg(FormatBytesHuman(max_ciphertext_bytes), source_label));
        return;
    }

    SendMessageToPeer(selected_group_conversation ? QString() : normalized_peer, marker);
}

void ApplicationController::SetPresenceStatus(const QString& status) {
    const QString normalized = NormalizePresenceValue(status);
    user_presence_status_ = normalized;
    state_.social_preferences.presence_status = normalized.toStdString();
    PersistState();
    emit UserPresenceChanged(user_presence_status_);

    if (!state_.has_user || !state_.has_device) {
        return;
    }

    try {
        PresenceSetRequest request;
        request.status = normalized.toStdString();
        const auto op = [this, &request]() {
            return api_client_.SetPresenceStatus(state_.base_url, RequireAccessToken(), request);
        };
        const auto response = CallWithAuthRetryOnce<PresenceSetResponse>(op, [this]() { RefreshAccessToken(); });
        user_presence_status_ = NormalizePresenceValue(QString::fromStdString(response.status));
        state_.social_preferences.presence_status = user_presence_status_.toStdString();
        PersistState();
        emit UserPresenceChanged(user_presence_status_);
        RefreshPresenceCache();
        RefreshConversationList();
    } catch (const std::exception& ex) {
        const QString line = QString("Set presence failed: %1").arg(ex.what());
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
    call_initiated_locally_ = true;
    pending_outgoing_end_request_ = false;

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
        end_request.reason = "left";
        ws_client_.SendCallEnd(end_request);
        pending_outgoing_end_request_ = false;
    } else if (IsCallState("outgoing_ringing")) {
        // Ringing call id arrives asynchronously; cancel as soon as server provides it.
        pending_outgoing_end_request_ = true;
        QTimer::singleShot(3000, this, [this]() {
            if (!pending_outgoing_end_request_) {
                return;
            }
            if (!call_state_.call_id.trimmed().isEmpty()) {
                return;
            }
            if (!IsCallState("ending") && !IsCallState("outgoing_ringing")) {
                return;
            }
            pending_outgoing_end_request_ = false;
            StopAudioEngine();
            TransitionCallState("idle", QString(), QString(), QString(), "left");
        });
    } else {
        pending_outgoing_end_request_ = false;
        StopAudioEngine();
        TransitionCallState("idle", QString(), QString(), QString(), "left");
        return;
    }

    TransitionCallState(
        "ending",
        call_state_.call_id,
        call_state_.conversation_id,
        call_state_.peer_user_id,
        "leaving");
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
    emit AccountDevicesChanged(std::vector<DeviceOut>{});
    emit ConversationListChanged(std::vector<ConversationListItemView>{});
    emit ConversationSelected(QString(), {});
    emit ConnectionStatusChanged(connection_status_);
    emit UserPresenceChanged("offline");
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

std::string ApplicationController::RequireBootstrapToken() {
    std::string error;
    const auto value = secret_store_.GetSecret(SecretKey("bootstrap_token"), &error);
    if (!value.has_value()) {
        throw std::runtime_error("Bootstrap token unavailable");
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

void ApplicationController::SaveBootstrapToken(const TokenBundle& tokens) {
    if (tokens.bootstrap_token.empty()) {
        throw std::runtime_error("Bootstrap token missing");
    }
    std::string error;
    if (!secret_store_.SetSecret(SecretKey("bootstrap_token"), tokens.bootstrap_token, &error)) {
        throw std::runtime_error(error);
    }
}

void ApplicationController::SaveTokenPair(const TokenBundle& tokens) {
    if (tokens.access_token.empty() || tokens.refresh_token.empty()) {
        throw std::runtime_error("Access/refresh token pair missing");
    }
    std::string error;
    if (!secret_store_.SetSecret(SecretKey("access_token"), tokens.access_token, &error)) {
        throw std::runtime_error(error);
    }
    if (!secret_store_.SetSecret(SecretKey("refresh_token"), tokens.refresh_token, &error)) {
        throw std::runtime_error(error);
    }
    secret_store_.DeleteSecret(SecretKey("bootstrap_token"), &error);
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

void ApplicationController::EncryptPlaintextCacheInState() {
    if (!state_.has_device || state_.device.enc_x25519_pub.empty()) {
        return;
    }

    for (auto& [conversation_id, thread] : state_.local_messages) {
        (void)conversation_id;
        for (auto& message : thread) {
            if (message.plaintext.empty()) {
                continue;
            }
            try {
                message.plaintext_cache_b64 = crypto_.EncryptForRecipient(
                    state_.device.enc_x25519_pub,
                    message.plaintext);
            } catch (const std::exception&) {
                message.plaintext_cache_b64.clear();
            }
        }
    }
}

void ApplicationController::DecryptPlaintextCacheInState() {
    if (!state_.has_device) {
        return;
    }
    std::string error;
    const auto private_key = secret_store_.GetSecret(SecretKey("enc_private"), &error);
    if (!private_key.has_value()) {
        return;
    }

    for (auto& [conversation_id, thread] : state_.local_messages) {
        (void)conversation_id;
        for (auto& message : thread) {
            if (!message.plaintext.empty() || message.plaintext_cache_b64.empty()) {
                continue;
            }
            try {
                message.plaintext = crypto_.DecryptWithPrivate(
                    private_key.value(),
                    message.plaintext_cache_b64);
            } catch (const std::exception&) {
                message.plaintext.clear();
            }
        }
    }
}

void ApplicationController::PersistState() {
    EncryptPlaintextCacheInState();
    state_store_.Save(state_);
}

QString ApplicationController::NormalizePresenceStatus(const QString& status) const {
    return NormalizePresenceValue(status);
}

QString ApplicationController::ResolvePeerAddressForConversation(const std::string& conversation_id) const {
    const auto conv_it = std::find_if(
        state_.conversations.begin(),
        state_.conversations.end(),
        [&conversation_id](const ConversationOut& conv) { return conv.id == conversation_id; });
    if (conv_it != state_.conversations.end() && conv_it->conversation_type == "group") {
        return {};
    }

    const auto meta_it = state_.conversation_meta.find(conversation_id);
    if (meta_it != state_.conversation_meta.end() && !meta_it->second.peer_address.empty()) {
        return QString::fromStdString(meta_it->second.peer_address).trimmed().toLower();
    }

    if (conv_it == state_.conversations.end()) {
        return {};
    }
    if (!conv_it->peer_address.empty()) {
        return QString::fromStdString(conv_it->peer_address).trimmed().toLower();
    }
    if (!conv_it->peer_username.empty()) {
        const QString local_server = QString::fromStdString(
            state_.user.home_server_onion.empty() ? ServerAuthority().toStdString() : state_.user.home_server_onion)
                                       .trimmed()
                                       .toLower();
        return QString("%1@%2")
            .arg(QString::fromStdString(conv_it->peer_username).trimmed().toLower(), local_server);
    }
    return {};
}

QString ApplicationController::PresenceForPeerAddress(const QString& peer_address) const {
    const std::string key = peer_address.trimmed().toLower().toStdString();
    if (key.empty()) {
        return "offline";
    }
    const auto it = peer_presence_status_by_address_.find(key);
    if (it == peer_presence_status_by_address_.end()) {
        return "offline";
    }
    return NormalizePresenceValue(it->second);
}

void ApplicationController::RefreshPresenceCache() {
    if (!state_.has_user || !state_.has_device) {
        peer_presence_status_by_address_.clear();
        return;
    }

    std::vector<std::string> peer_addresses;
    peer_addresses.reserve(state_.conversations.size());
    std::unordered_set<std::string> seen;
    for (const auto& conversation : state_.conversations) {
        if (state_.blocked_conversation_ids.contains(conversation.id)) {
            continue;
        }
        if (conversation.conversation_type == "direct" && state_.dismissed_conversation_ids.contains(conversation.id)) {
            continue;
        }
        const QString peer = ResolvePeerAddressForConversation(conversation.id);
        if (peer.isEmpty()) {
            continue;
        }
        const std::string key = peer.toStdString();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        peer_addresses.push_back(key);
    }

    try {
        PresenceResolveRequest request;
        request.peer_addresses = peer_addresses;
        const auto op = [this, &request]() {
            return api_client_.ResolvePresence(state_.base_url, RequireAccessToken(), request);
        };
        const auto response = CallWithAuthRetryOnce<PresenceResolveResponse>(op, [this]() { RefreshAccessToken(); });

        peer_presence_status_by_address_.clear();
        for (const auto& peer : response.peers) {
            peer_presence_status_by_address_[peer.peer_address] =
                NormalizePresenceValue(QString::fromStdString(peer.status));
        }
        emit UserPresenceChanged(user_presence_status_);
    } catch (const std::exception& ex) {
        RecordDiagnostic(QString("presence resolve failed: %1").arg(ex.what()));
    }
}

void ApplicationController::RefreshConversationList() {
    std::vector<ConversationListItemView> items;
    items.reserve(state_.conversations.size());

    for (const auto& conv : state_.conversations) {
        if (state_.blocked_conversation_ids.contains(conv.id)) {
            continue;
        }
        if (conv.conversation_type == "direct" && state_.dismissed_conversation_ids.contains(conv.id)) {
            continue;
        }
        if (pending_request_senders_.find(conv.id) != pending_request_senders_.end()) {
            continue;
        }

        ConversationListItemView item;
        item.id = QString::fromStdString(conv.id);
        item.title = FriendlyConversationTitle(conv.id);
        item.conversation_type = QString::fromStdString(conv.conversation_type).trimmed().toLower();
        item.can_manage_members = conv.can_manage_members;
        item.peer_address = ResolvePeerAddressForConversation(conv.id);
        item.group_name = QString::fromStdString(conv.group_name);
        item.member_count = conv.member_count;

        const auto meta_it = state_.conversation_meta.find(conv.id);
        if (meta_it != state_.conversation_meta.end() && !meta_it->second.last_preview.empty()) {
            item.subtitle = QString::fromStdString(meta_it->second.last_preview);
        } else {
            item.subtitle = "(no messages yet)";
        }

        item.last_activity_at = LastActivityForConversation(conv.id);
        item.status = PresenceForPeerAddress(ResolvePeerAddressForConversation(conv.id));
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
    const auto conv_it = std::find_if(
        state_.conversations.begin(),
        state_.conversations.end(),
        [&conversation_id](const ConversationOut& conv) { return conv.id == conversation_id; });
    if (conv_it != state_.conversations.end() && conv_it->conversation_type == "group") {
        peer_label = "Member";
    }
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

void ApplicationController::UploadCurrentDevicePrekeys() {
    if (!state_.has_user || !state_.has_device) {
        return;
    }

    std::string error;
    const auto ik_private = secret_store_.GetSecret(SecretKey("ik_private"), &error);
    if (!ik_private.has_value()) {
        RecordDiagnostic("prekey upload skipped: device signing key unavailable");
        return;
    }

    if (state_.device.enc_x25519_pub.empty()) {
        RecordDiagnostic("prekey upload skipped: device DH public key missing");
        return;
    }

    const int signed_prekey_id = 1;
    const QString expires_qt = QDateTime::currentDateTimeUtc().addDays(30).toString("yyyy-MM-ddTHH:mm:ss+00:00");
    const std::string expires_at = expires_qt.toStdString();
    const std::string canonical = CanonicalSignedPrekeyString(
        state_.device.id,
        signed_prekey_id,
        state_.device.enc_x25519_pub,
        expires_at);
    const std::string signature = crypto_.SignDetached(ik_private.value(), canonical);

    PrekeyUploadRequest request;
    request.signed_prekey.key_id = signed_prekey_id;
    request.signed_prekey.pub_x25519_b64 = state_.device.enc_x25519_pub;
    request.signed_prekey.sig_by_device_sign_key_b64 = signature;
    request.signed_prekey.expires_at = expires_at;

    try {
        const auto op = [this, &request]() {
            return api_client_.UploadPrekeys(state_.base_url, RequireAccessToken(), request);
        };
        const auto response = CallWithAuthRetryOnce<PrekeyUploadResponse>(op, [this]() { RefreshAccessToken(); });
        RecordDiagnostic(
            QString("prekeys uploaded signed_key_id=%1 accepted_opk=%2")
                .arg(response.uploaded_signed_prekey_key_id)
                .arg(response.accepted_one_time_prekeys));
    } catch (const std::exception& ex) {
        RecordDiagnostic(QString("prekey upload failed: %1").arg(ex.what()));
    }
}

bool ApplicationController::PreferRatchetV2b1() const {
    return EnvFlagEnabled("BLACKWIRE_PREFER_RATCHET_V2B1", true);
}

bool ApplicationController::DeviceSupportsMessageMode(const DeviceOut& device, const std::string& mode) const {
    if (mode.empty()) {
        return false;
    }
    if (device.supported_message_modes.empty()) {
        return mode == "sealedbox_v0_2a";
    }
    return std::find(
               device.supported_message_modes.begin(),
               device.supported_message_modes.end(),
               mode) != device.supported_message_modes.end();
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
    state_.dismissed_conversation_ids.erase(conversation_id);

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
    const auto conv_it = std::find_if(
        state_.conversations.begin(),
        state_.conversations.end(),
        [&conversation_id](const ConversationOut& conv) { return conv.id == conversation_id; });
    if (conv_it != state_.conversations.end() && conv_it->conversation_type == "group") {
        const QString group_name = QString::fromStdString(conv_it->group_name).trimmed();
        if (!group_name.isEmpty()) {
            return group_name;
        }
        const QString group_uid = QString::fromStdString(conv_it->group_uid).trimmed();
        if (!group_uid.isEmpty()) {
            return QString("Group %1").arg(group_uid.left(8));
        }
        return QString("Group %1").arg(QString::fromStdString(conversation_id).left(8));
    }

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

QString ApplicationController::FormatCallDuration(qint64 duration_ms) const {
    const qint64 total_seconds = std::max<qint64>(0, duration_ms / 1000);
    const qint64 hours = total_seconds / 3600;
    const qint64 minutes = (total_seconds % 3600) / 60;
    const qint64 seconds = total_seconds % 60;
    if (hours > 0) {
        return QString("%1 hours").arg(hours);
    }
    if (minutes > 0) {
        return QString("%1 minutes").arg(minutes);
    }
    return QString("%1 seconds").arg(seconds);
}

std::vector<CallParticipantView> ApplicationController::BuildDirectCallParticipants(const QString& peer_user_id) const {
    std::vector<CallParticipantView> participants;
    participants.reserve(2);

    const QString local_server = QString::fromStdString(
                                     state_.user.home_server_onion.empty()
                                         ? ServerAuthority().toStdString()
                                         : state_.user.home_server_onion)
                                     .trimmed()
                                     .toLower();
    const QString self_address = QString("%1@%2")
                                     .arg(QString::fromStdString(state_.user.username).trimmed().toLower(), local_server)
                                     .trimmed()
                                     .toLower();
    participants.push_back(CallParticipantView{
        self_address,
        "You",
        true,
    });

    const QString peer = peer_user_id.trimmed().toLower();
    if (peer.isEmpty()) {
        return participants;
    }

    QString peer_label = peer;
    if (peer.contains('@')) {
        peer_label = peer.section('@', 0, 0).trimmed();
    }
    if (peer_label.isEmpty()) {
        peer_label = "Peer";
    }

    participants.push_back(CallParticipantView{
        peer,
        peer_label,
        false,
    });
    return participants;
}

void ApplicationController::AppendCallHistoryEntry(const QString& reason) {
    const QString previous_state = call_state_.state.trimmed().toLower();
    const QString conversation_id = call_state_.conversation_id.trimmed();
    if (conversation_id.isEmpty()) {
        return;
    }

    const QString normalized_reason = reason.trimmed().toLower();
    if (normalized_reason == "logged_out" || normalized_reason == "reset") {
        return;
    }

    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    qint64 started_ms = call_active_started_at_ms_ > 0 ? call_active_started_at_ms_ : call_started_at_ms_;
    if (started_ms <= 0) {
        started_ms = now_ms;
    }

    QString peer = call_state_.peer_user_id.trimmed().isEmpty()
                       ? FriendlyConversationTitle(conversation_id.toStdString())
                       : call_state_.peer_user_id.trimmed();
    if (peer.contains('@')) {
        peer = peer.section('@', 0, 0);
    }
    if (peer.trimmed().isEmpty()) {
        peer = "Peer";
    }
    const QString duration_text = FormatCallDuration(std::max<qint64>(0, now_ms - started_ms));

    QString text;
    if (previous_state == "incoming_ringing" && normalized_reason == "missed") {
        text = QString("You missed a call from %1 that lasted %2.").arg(peer, duration_text);
    } else if (call_initiated_locally_) {
        text = QString("You started a call with %1 that lasted %2.").arg(peer, duration_text);
    } else {
        text = QString("%1 started a call that lasted %2.").arg(peer, duration_text);
    }

    LocalMessage local;
    local.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    local.conversation_id = conversation_id.toStdString();
    local.sender_user_id.clear();
    local.sender_address.clear();
    local.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    local.rendered_text = text.toStdString();
    local.plaintext = text.toStdString();
    state_.local_messages[conversation_id.toStdString()].push_back(local);

    UpsertConversationMeta(
        conversation_id.toStdString(),
        QString(),
        QString(),
        text,
        QString::fromStdString(local.created_at));

    PersistState();
    RefreshConversationList();

    const auto thread = RenderThread(conversation_id.toStdString());
    if (selected_conversation_id_ == conversation_id.toStdString()) {
        emit ConversationSelected(conversation_id, thread);
    } else if (!thread.empty()) {
        emit IncomingMessage(conversation_id, thread.back());
    }
}

void ApplicationController::AppendGroupRenameHistoryEntry(
    const QString& conversation_id,
    const QString& actor_address,
    const QString& group_name,
    const QString& dedupe_suffix) {
    const QString conversation = conversation_id.trimmed();
    const QString next_name = group_name.trimmed();
    if (conversation.isEmpty() || next_name.isEmpty()) {
        return;
    }

    const QString suffix = dedupe_suffix.trimmed().isEmpty() ? next_name.toLower() : dedupe_suffix.trimmed().toLower();
    const std::string synthetic_id =
        QString("sys-group-rename:%1:%2").arg(conversation, suffix).toStdString();
    if (!state_.MarkMessageSeen(synthetic_id)) {
        return;
    }

    const QString actor_username = UsernameFromAddress(actor_address);
    const QString text = QString("%1 changed group DM name to %2")
                             .arg(actor_username, next_name);

    LocalMessage local;
    local.id = synthetic_id;
    local.conversation_id = conversation.toStdString();
    local.sender_user_id.clear();
    local.sender_address = actor_address.trimmed().toLower().toStdString();
    local.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    local.rendered_text = text.toStdString();
    local.plaintext = text.toStdString();
    state_.local_messages[conversation.toStdString()].push_back(local);

    UpsertConversationMeta(
        conversation.toStdString(),
        QString(),
        QString(),
        text,
        QString::fromStdString(local.created_at));
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
    const QString previous_state = call_state_.state.trimmed().toLower();
    const QString previous_call_id = call_state_.call_id;
    const QString next_state = state.trimmed().isEmpty() ? "idle" : state.trimmed().toLower();
    if (next_state == "idle" && previous_state != "idle") {
        AppendCallHistoryEntry(reason);
    }

    if (next_state == "incoming_ringing" || next_state == "outgoing_ringing") {
        call_started_at_ms_ = QDateTime::currentMSecsSinceEpoch();
        call_active_started_at_ms_ = 0;
    } else if (next_state == "active") {
        if (call_started_at_ms_ <= 0) {
            call_started_at_ms_ = QDateTime::currentMSecsSinceEpoch();
        }
        call_active_started_at_ms_ = QDateTime::currentMSecsSinceEpoch();
    } else if (next_state == "idle") {
        call_started_at_ms_ = 0;
        call_active_started_at_ms_ = 0;
    }

    call_state_.state = state.trimmed().isEmpty() ? "idle" : state.trimmed();
    call_state_.call_id = call_id;
    call_state_.conversation_id = conversation_id;
    call_state_.peer_user_id = peer_user_id;
    call_state_.reason = reason;
    if (next_state != "active" || (!previous_call_id.isEmpty() && previous_call_id != call_id)) {
        call_state_.participants.clear();
    }

    if (IsCallState("idle") || IsCallState("incoming_ringing") || IsCallState("outgoing_ringing")) {
        call_state_.muted = false;
    }
    if (IsCallState("idle")) {
        call_initiated_locally_ = false;
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
    const bool key_has_server = key.find('@') != std::string::npos;

    std::optional<std::string> exact_username_match;
    std::optional<std::string> local_prefix_match;
    for (const auto& conversation : state_.conversations) {
        const auto meta_it = state_.conversation_meta.find(conversation.id);
        if (meta_it == state_.conversation_meta.end()) {
            continue;
        }
        const auto& meta = meta_it->second;
        if (!meta.peer_address.empty() && meta.peer_address == key) {
            return conversation.id;
        }
        if (!key_has_server && !meta.peer_username.empty() && meta.peer_username == key) {
            if (!exact_username_match.has_value()) {
                exact_username_match = conversation.id;
            }
            continue;
        }
        if (!key_has_server && !meta.peer_address.empty() && meta.peer_address.rfind(key + "@", 0) == 0) {
            if (!local_prefix_match.has_value()) {
                local_prefix_match = conversation.id;
            }
        }
    }

    if (exact_username_match.has_value()) {
        return exact_username_match;
    }
    if (local_prefix_match.has_value()) {
        return local_prefix_match;
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

std::vector<DeviceOut> ApplicationController::ResolveRecipientDevices(const QString& normalized_peer) {
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
    AttachmentPolicyCacheEntry policy_entry;
    policy_entry.attachment_inline_max_bytes = std::max<qint64>(0, user_device.attachment_inline_max_bytes);
    policy_entry.max_ciphertext_bytes = std::max<qint64>(0, user_device.max_ciphertext_bytes);
    policy_entry.source = user_device.attachment_policy_source.empty() ? "local" : user_device.attachment_policy_source;
    peer_attachment_policy_cache_[key] = policy_entry;
    if (policy_entry.source == "local" && policy_entry.attachment_inline_max_bytes > 0 &&
        policy_entry.max_ciphertext_bytes > 0) {
        local_attachment_policy_cache_ = policy_entry;
    }

    if (user_device.devices.empty() && (!user_device.device.id.empty() || !user_device.device.device_uid.empty())) {
        peer_device_cache_[key] = {user_device.device};
    } else {
        peer_device_cache_[key] = user_device.devices;
    }
    return peer_device_cache_[key];
}

std::vector<DeviceOut> ApplicationController::ResolveOwnActiveDevices() {
    const auto operation = [this]() {
        return api_client_.ListDevices(state_.base_url, RequireAccessToken());
    };
    auto devices = CallWithAuthRetryOnce<std::vector<DeviceOut>>(operation, [this]() { RefreshAccessToken(); });
    std::vector<DeviceOut> active;
    for (const auto& device : devices) {
        if (device.status == "revoked") {
            continue;
        }
        active.push_back(device);
    }
    return active;
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
    peer_attachment_policy_cache_.clear();
    local_attachment_policy_cache_.reset();
    peer_presence_status_by_address_.clear();
    pending_request_messages_.clear();
    pending_request_senders_.clear();
    selected_conversation_id_.clear();
    connection_status_ = "Disconnected";
    user_presence_status_ = "active";
    call_state_ = CallStateView{};
    call_initiated_locally_ = false;
    call_started_at_ms_ = 0;
    call_active_started_at_ms_ = 0;
    pending_outgoing_end_request_ = false;
    audio_sequence_ = 0;
}

}  // namespace blackwire
