#pragma once

#include <QObject>
#include <QTimer>
#include <QWebSocket>

#include "blackwire/interfaces/ws_client.hpp"

namespace blackwire {

class QtWsClient final : public QObject, public IWsClient {
    Q_OBJECT

public:
    explicit QtWsClient(QObject* parent = nullptr);

    void SetHandlers(
        MessageHandler on_message,
        CallIncomingHandler on_call_incoming,
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
        ErrorHandler on_error,
        StatusHandler on_status) override;
    void Connect(const std::string& base_url, const std::string& access_token) override;
    void Disconnect() override;
    void SendAck(const std::string& message_id) override;
    void SendCallOffer(const VoiceCallOffer& offer) override;
    void SendCallAccept(const VoiceCallAccept& accept) override;
    void SendCallReject(const VoiceCallReject& reject) override;
    void SendCallEnd(const VoiceCallEnd& end) override;
    void SendCallAudioChunk(const VoiceAudioChunk& chunk) override;
    void SendCallWebRtcOffer(const VoiceCallWebRtcOffer& offer) override;
    void SendCallWebRtcAnswer(const VoiceCallWebRtcAnswer& answer) override;
    void SendCallWebRtcIce(const VoiceCallWebRtcIce& ice) override;

private:
    void ScheduleReconnect();
    QUrl BuildWsUrl() const;
    void HandleTextMessage(const QString& message_text);

    QWebSocket socket_;
    QTimer reconnect_timer_;

    MessageHandler on_message_;
    CallIncomingHandler on_call_incoming_;
    CallRingingHandler on_call_ringing_;
    CallAcceptedHandler on_call_accepted_;
    CallRejectedHandler on_call_rejected_;
    CallBusyHandler on_call_busy_;
    CallEndedHandler on_call_ended_;
    CallAudioHandler on_call_audio_;
    CallErrorHandler on_call_error_;
    CallWebRtcOfferHandler on_call_webrtc_offer_;
    CallWebRtcAnswerHandler on_call_webrtc_answer_;
    CallWebRtcIceHandler on_call_webrtc_ice_;
    ErrorHandler on_error_;
    StatusHandler on_status_;

    std::string base_url_;
    std::string access_token_;
    bool should_reconnect_ = false;
    int reconnect_attempt_ = 0;
};

}  // namespace blackwire
