#pragma once

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

struct AudioDeviceOptionView {
    QString id;
    QString name;
};

struct CallStateView {
    QString state = "idle";
    QString call_id;
    QString conversation_id;
    QString peer_user_id;
    bool muted = false;
    QString reason;
};

struct ThreadMessageView {
    QString id;
    QString sender_label;
    QString body;
    QString created_at_iso;
    QString created_at_display;
    bool outgoing = false;
    bool grouped_with_previous = false;
};

}  // namespace blackwire
