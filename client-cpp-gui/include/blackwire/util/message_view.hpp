#pragma once

#include <string>
#include <vector>

#include <QString>

#include "blackwire/models/view_models.hpp"
#include "blackwire/storage/client_state.hpp"

namespace blackwire {

QString ExtractLegacyPlaintext(const QString& rendered_text);
QString FormatThreadTimestamp(const QString& created_at_iso);

std::vector<ThreadMessageView> BuildThreadMessageViews(
    const std::vector<LocalMessage>& messages,
    const std::string& self_user_id,
    const QString& peer_sender_label);

}  // namespace blackwire
