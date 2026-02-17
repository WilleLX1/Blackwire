#include "blackwire/util/conversation_view.hpp"

#include <algorithm>

namespace blackwire {

namespace {

QString ShortConversationId(const QString& conversation_id) {
    if (conversation_id.size() <= 8) {
        return conversation_id;
    }
    return conversation_id.left(8);
}

}  // namespace

QString BuildConversationTitle(
    const QString& peer_username,
    const QString& conversation_id,
    const QString& server_authority) {
    if (!peer_username.trimmed().isEmpty()) {
        if (peer_username.contains('@')) {
            return peer_username.trimmed();
        }
        if (server_authority.trimmed().isEmpty()) {
            return peer_username.trimmed();
        }
        return QString("%1@%2").arg(peer_username.trimmed(), server_authority);
    }
    return QString("Conversation %1").arg(ShortConversationId(conversation_id));
}

void SortConversationItems(std::vector<ConversationListItemView>* items) {
    if (items == nullptr) {
        return;
    }

    std::sort(
        items->begin(),
        items->end(),
        [](const ConversationListItemView& a, const ConversationListItemView& b) {
            if (a.last_activity_at == b.last_activity_at) {
                return a.title.toLower() < b.title.toLower();
            }
            return a.last_activity_at < b.last_activity_at;
        });
}

}  // namespace blackwire
