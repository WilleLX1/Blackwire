#pragma once

#include <QCryptographicHash>
#include <QList>
#include <QMessageBox>
#include <QSet>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>

namespace blackwire {

/// Shared SSL certificate trust state.
///
/// When the server presents a self-signed or otherwise untrusted certificate,
/// the user is shown a warning dialog with certificate details.  If the user
/// chooses to continue, the certificate is added to the process-wide default
/// CA list so that all subsequent TLS connections (HTTP and WebSocket) trust
/// it natively — no per-request sslErrors overhead.
class SslTrust {
public:
    /// Returns true if the errors should be ignored (user accepted, or all
    /// certs were already accepted previously).
    static bool ShouldIgnoreErrors(const QList<QSslError>& errors) {
        if (errors.isEmpty()) {
            return true;
        }

        // Check whether every certificate in the error list has already been
        // accepted by the user in a previous prompt.
        bool all_accepted = true;
        for (const auto& error : errors) {
            const QByteArray digest =
                error.certificate().digest(QCryptographicHash::Sha256);
            if (!accepted_fingerprints_.contains(digest)) {
                all_accepted = false;
                break;
            }
        }
        if (all_accepted) {
            return true;
        }

        return AskUser(errors);
    }

    /// Returns true if any certificates have been accepted by the user.
    static bool HasAcceptedCerts() {
        return !accepted_fingerprints_.isEmpty();
    }

private:
    /// Install accepted certificates into the process-wide default SSL
    /// configuration so that future connections trust them without triggering
    /// sslErrors at all.
    static void InstallAcceptedCerts(const QList<QSslError>& errors) {
        auto config = QSslConfiguration::defaultConfiguration();
        for (const auto& error : errors) {
            const auto cert = error.certificate();
            if (!cert.isNull()) {
                config.addCaCertificate(cert);
            }
        }
        QSslConfiguration::setDefaultConfiguration(config);
    }

    static bool AskUser(const QList<QSslError>& errors) {
        QString details;
        for (const auto& error : errors) {
            details += error.errorString() + "\n";
            const auto cert = error.certificate();
            if (!cert.isNull()) {
                details += "  Subject: " + cert.subjectDisplayName() + "\n";
                details += "  Issuer:  " + cert.issuerDisplayName() + "\n";
                details +=
                    "  SHA-256: " +
                    QString::fromLatin1(
                        cert.digest(QCryptographicHash::Sha256).toHex(':')) +
                    "\n";
            }
            details += "\n";
        }

        const auto result = QMessageBox::warning(
            nullptr,
            QStringLiteral("Untrusted Server Certificate"),
            QStringLiteral(
                "The server presented a certificate that could not be "
                "verified.\n\n") +
                details +
                QStringLiteral("Do you want to continue anyway?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (result == QMessageBox::Yes) {
            for (const auto& error : errors) {
                accepted_fingerprints_.insert(
                    error.certificate().digest(QCryptographicHash::Sha256));
            }
            // Add the accepted certs to the global default CA list so that
            // subsequent TLS connections (HTTP and WebSocket) trust them
            // without triggering sslErrors.  This eliminates the per-request
            // handshake overhead that was causing timeouts.
            InstallAcceptedCerts(errors);
            return true;
        }
        return false;
    }

    static inline QSet<QByteArray> accepted_fingerprints_;
};

}  // namespace blackwire
