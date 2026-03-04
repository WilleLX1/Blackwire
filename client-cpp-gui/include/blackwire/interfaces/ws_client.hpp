#pragma once

#include <functional>
#include <string>

#include "blackwire/models/dto.hpp"

namespace blackwire {

class IWsClient {
public:
    using MessageHandler = std::function<void(const WsEventMessageNew&)>;
    using CallIncomingHandler = std::function<void(const WsEventCallIncoming&)>;
    using CallGroupStateHandler = std::function<void(const WsEventCallGroupState&)>;
    using CallRingingHandler = std::function<void(const WsEventCallRinging&)>;
    using CallAcceptedHandler = std::function<void(const WsEventCallAccepted&)>;
    using CallRejectedHandler = std::function<void(const WsEventCallRejected&)>;
    using CallBusyHandler = std::function<void(const WsEventCallBusy&)>;
    using CallEndedHandler = std::function<void(const WsEventCallEnded&)>;
    using CallAudioHandler = std::function<void(const WsEventCallAudio&)>;
    using CallErrorHandler = std::function<void(const WsEventCallError&)>;
    using CallWebRtcOfferHandler = std::function<void(const WsEventCallWebRtcOffer&)>;
    using CallWebRtcAnswerHandler = std::function<void(const WsEventCallWebRtcAnswer&)>;
    using CallWebRtcIceHandler = std::function<void(const WsEventCallWebRtcIce&)>;
    using GroupRenamedHandler = std::function<void(const WsEventGroupRenamed&)>;
    using ConversationTypingHandler = std::function<void(const WsEventConversationTyping&)>;
    using ConversationReadHandler = std::function<void(const WsEventConversationRead&)>;
    using ErrorHandler = std::function<void(const std::string&)>;
    using StatusHandler = std::function<void(bool)>;
    using TokenRefreshCallback = std::function<std::string()>;

    virtual ~IWsClient() = default;

    virtual void SetHandlers(
        MessageHandler on_message,
        CallIncomingHandler on_call_incoming,
        CallGroupStateHandler on_call_group_state,
        CallRingingHandler on_call_ringing,
        CallAcceptedHandler on_call_accepted,
        CallRejectedHandler on_call_rejected,
        CallBusyHandler on_call_busy,
        CallEndedHandler on_call_ended,
        CallAudioHandler on_call_audio,
        CallErrorHandler on_call_error,
        CallWebRtcOfferHandler on_call_webrtc_offer,
        CallWebRtcAnswerHandler on_call_webrtc_answer,
        CallWebRtcIceHandler on_call_webrtc_ice,
        GroupRenamedHandler on_group_renamed,
        ConversationTypingHandler on_conversation_typing,
        ConversationReadHandler on_conversation_read,
        ErrorHandler on_error,
        StatusHandler on_status) = 0;
    virtual void Connect(const std::string& base_url, const std::string& access_token) = 0;
    virtual void SetTokenRefreshCallback(TokenRefreshCallback callback) = 0;
    virtual void Disconnect() = 0;
    virtual void SendAck(const std::string& message_id) = 0;
    virtual void SendCallOffer(const VoiceCallOffer& offer) = 0;
    virtual void SendCallAccept(const VoiceCallAccept& accept) = 0;
    virtual void SendCallReject(const VoiceCallReject& reject) = 0;
    virtual void SendCallEnd(const VoiceCallEnd& end) = 0;
    virtual void SendCallAudioChunk(const VoiceAudioChunk& chunk) = 0;
    virtual void SendCallWebRtcOffer(const VoiceCallWebRtcOffer& offer) = 0;
    virtual void SendCallWebRtcAnswer(const VoiceCallWebRtcAnswer& answer) = 0;
    virtual void SendCallWebRtcIce(const VoiceCallWebRtcIce& ice) = 0;
};

}  // namespace blackwire
