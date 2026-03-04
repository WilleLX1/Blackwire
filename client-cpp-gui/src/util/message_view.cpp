#include "blackwire/util/message_view.hpp"

#include <algorithm>

#include <QDateTime>

namespace blackwire {

namespace {

const char* kFileMessagePrefix = "bwfile://v1:";

QDateTime ParseMessageTime(const std::string& value) {
    const QString iso = QString::fromStdString(value);
    QDateTime parsed = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(iso, Qt::ISODate);
    }
    return parsed;
}

bool IsBefore(const LocalMessage& lhs, const LocalMessage& rhs) {
    const QDateTime left_time = ParseMessageTime(lhs.created_at);
    const QDateTime right_time = ParseMessageTime(rhs.created_at);
    if (left_time.isValid() && right_time.isValid() && left_time != right_time) {
        return left_time < right_time;
    }
    if (lhs.created_at != rhs.created_at) {
        return lhs.created_at < rhs.created_at;
    }
    return lhs.id < rhs.id;
}

QString LabelFromSenderAddress(const std::string& sender_address) {
    const QString address = QString::fromStdString(sender_address).trimmed();
    if (address.isEmpty()) {
        return {};
    }
    const int at = address.indexOf('@');
    if (at > 0) {
        return address.left(at).trimmed();
    }
    return address;
}

}  // namespace

QString ExtractLegacyPlaintext(const QString& rendered_text) {
    const QString simplified = rendered_text.trimmed();
    if (simplified.isEmpty()) {
        return {};
    }

    const int delimiter = simplified.indexOf(": ");
    if (delimiter < 0) {
        return simplified;
    }

    return simplified.mid(delimiter + 2).trimmed();
}

QString FormatThreadTimestamp(const QString& created_at_iso) {
    if (created_at_iso.trimmed().isEmpty()) {
        return "-";
    }

    QDateTime created = QDateTime::fromString(created_at_iso, Qt::ISODateWithMs);
    if (!created.isValid()) {
        created = QDateTime::fromString(created_at_iso, Qt::ISODate);
    }
    if (!created.isValid()) {
        return created_at_iso;
    }

    const QDateTime local = created.toLocalTime();
    return local.toString("MMM d, h:mm AP");
}

std::vector<ThreadMessageView> BuildThreadMessageViews(
    const std::vector<LocalMessage>& messages,
    const std::string& self_user_id,
    const QString& peer_sender_label) {
    std::vector<const LocalMessage*> ordered;
    ordered.reserve(messages.size());
    for (const auto& message : messages) {
        ordered.push_back(&message);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const LocalMessage* lhs, const LocalMessage* rhs) {
        return IsBefore(*lhs, *rhs);
    });

    std::vector<ThreadMessageView> views;
    views.reserve(ordered.size());

    for (const auto* item : ordered) {
        ThreadMessageView view;
        view.id = QString::fromStdString(item->id);
        view.created_at_iso = QString::fromStdString(item->created_at);
        view.created_at_display = FormatThreadTimestamp(view.created_at_iso);
        view.system = item->sender_user_id.empty() && item->sender_address.empty();
        view.outgoing = !view.system && item->sender_user_id == self_user_id;
        if (view.system) {
            view.sender_label.clear();
        } else if (view.outgoing) {
            view.sender_label = "You";
        } else {
            const QString from_address = LabelFromSenderAddress(item->sender_address);
            view.sender_label = from_address.trimmed().isEmpty()
                                    ? (peer_sender_label.trimmed().isEmpty() ? "Peer" : peer_sender_label.trimmed())
                                    : from_address.trimmed();
        }

        const QString plaintext = QString::fromStdString(item->plaintext);
        view.body = plaintext.isEmpty() ? ExtractLegacyPlaintext(QString::fromStdString(item->rendered_text)) : plaintext;
        view.sent_at_ms = item->sent_at_ms;
        view.attachment_name = QString::fromStdString(item->attachment_name);
        view.attachment_mime_type = QString::fromStdString(item->attachment_mime_type);
        view.attachment_media_kind = QString::fromStdString(item->attachment_media_kind);
        view.attachment_status = QString::fromStdString(item->attachment_status.empty() ? "success" : item->attachment_status);
        view.attachment_retryable = !item->retry_payload.empty();
        if (view.system) {
            view.render_mode = "system";
            view.grouped_with_previous = false;
        } else {
            view.render_mode = view.body.startsWith(kFileMessagePrefix, Qt::CaseInsensitive) ? "attachment" : "markdown";
            view.grouped_with_previous =
                !views.empty() &&
                !views.back().system &&
                views.back().outgoing == view.outgoing &&
                views.back().sender_label == view.sender_label;
        }
        views.push_back(view);
    }

    return views;
}

}  // namespace blackwire
