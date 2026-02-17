#pragma once

#include <vector>

#include <QString>

#include "blackwire/models/view_models.hpp"

namespace blackwire {

QString BuildConversationTitle(
    const QString& peer_username,
    const QString& conversation_id,
    const QString& server_authority);
void SortConversationItems(std::vector<ConversationListItemView>* items);

}  // namespace blackwire
