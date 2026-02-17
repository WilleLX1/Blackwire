#include "blackwire/util/diagnostics.hpp"

namespace blackwire {

QString SanitizeDiagnosticsText(const QString& text) {
    QString value = text;
    value.replace("access_token", "[redacted]", Qt::CaseInsensitive);
    value.replace("refresh_token", "[redacted]", Qt::CaseInsensitive);
    value.replace("enc_private", "[redacted]", Qt::CaseInsensitive);
    value.replace("ik_private", "[redacted]", Qt::CaseInsensitive);
    return value;
}

}  // namespace blackwire
