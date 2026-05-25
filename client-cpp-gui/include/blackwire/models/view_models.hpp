#pragma once

#include <vector>

#include <QString>

namespace blackwire {

struct ConversationListItemView {
    QString id;
    QString title;
    QString subtitle;
    QString last_activity_at;
    QString status = "offline";
    QString conversation_type = "direct";
    bool can_manage_members = false;
    QString peer_address;
    QString group_name;
    int member_count = 0;
};

struct GroupInviteCandidateView {
    QString peer_address;
    QString title;
    QString subtitle;
    QString status = "offline";
};

struct GroupInvitePickerView {
    std::vector<GroupInviteCandidateView> candidates;
    int remaining_slots = 0;
};

struct RegistrationServerInfoView {
    QString base_url;
    QString server_onion;
    QString federation_version;
    QString signing_public_key;
    QString identity_binding_mode;
    QString supported_message_modes;
    QString supported_call_modes;
    QString attachment_limits;
};

struct AudioDeviceOptionView {
    QString id;
    QString name;
};

struct CallParticipantView {
    QString user_address;
    QString label;
    bool self = false;
};

struct CallStateView {
    QString state = "idle";
    QString call_id;
    QString conversation_id;
    QString peer_user_id;
    bool muted = false;
    QString reason;
    std::vector<CallParticipantView> participants;
};

struct ThreadMessageView {
    QString id;
    QString sender_label;
    QString body;
    QString render_mode = "plain";  // plain|markdown|attachment
    bool system = false;
    QString created_at_iso;
    QString created_at_display;
    long long sent_at_ms = 0;
    bool outgoing = false;
    bool grouped_with_previous = false;
    QString attachment_name;
    QString attachment_mime_type;
    QString attachment_media_kind;
    QString attachment_status = "success";  // queued|sending|success|failed
    bool attachment_retryable = false;
    QString delivery_badge;
};

}  // namespace blackwire
