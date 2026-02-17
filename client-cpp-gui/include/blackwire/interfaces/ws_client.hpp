#pragma once

#include <functional>
#include <string>

#include "blackwire/models/dto.hpp"

namespace blackwire {

class IWsClient {
public:
    using MessageHandler = std::function<void(const WsEventMessageNew&)>;
    using CallIncomingHandler = std::function<void(const WsEventCallIncoming&)>;
    using CallRingingHandler = std::function<void(const WsEventCallRinging&)>;
    using CallAcceptedHandler = std::function<void(const WsEventCallAccepted&)>;
    using CallRejectedHandler = std::function<void(const WsEventCallRejected&)>;
    using CallBusyHandler = std::function<void(const WsEventCallBusy&)>;
    using CallEndedHandler = std::function<void(const WsEventCallEnded&)>;
    using CallAudioHandler = std::function<void(const WsEventCallAudio&)>;
    using CallErrorHandler = std::function<void(const WsEventCallError&)>;
    using ErrorHandler = std::function<void(const std::string&)>;
    using StatusHandler = std::function<void(bool)>;

    virtual ~IWsClient() = default;

    virtual void SetHandlers(
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
        StatusHandler on_status) = 0;
    virtual void Connect(const std::string& base_url, const std::string& access_token) = 0;
    virtual void Disconnect() = 0;
    virtual void SendAck(const std::string& message_id) = 0;
    virtual void SendCallOffer(const VoiceCallOffer& offer) = 0;
    virtual void SendCallAccept(const VoiceCallAccept& accept) = 0;
    virtual void SendCallReject(const VoiceCallReject& reject) = 0;
    virtual void SendCallEnd(const VoiceCallEnd& end) = 0;
    virtual void SendCallAudioChunk(const VoiceAudioChunk& chunk) = 0;
};

}  // namespace blackwire
