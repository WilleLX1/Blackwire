#pragma once

#include <QString>

namespace blackwire {

struct ConversationListItemView {
    QString id;
    QString title;
    QString subtitle;
    QString last_activity_at;
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
