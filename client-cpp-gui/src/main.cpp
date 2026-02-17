#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

#include "blackwire/api/qt_api_client.hpp"
#include "blackwire/audio/qt_audio_call_engine.hpp"
#include "blackwire/controller/application_controller.hpp"
#include "blackwire/crypto/sodium_crypto_service.hpp"
#include "blackwire/smoke/smoke_runner.hpp"
#include "blackwire/storage/state_store.hpp"
#include "blackwire/storage/windows_credential_store.hpp"
#include "blackwire/ui/main_window.hpp"
#include "blackwire/ui/theme.hpp"
#include "blackwire/ws/qt_ws_client.hpp"

namespace {

QString NormalizeProfileName(QString value) {
    value = value.trimmed().toLower();
    if (value.isEmpty()) {
        return "default";
    }

    QString normalized;
    normalized.reserve(value.size());
    for (const QChar ch : value) {
        if (ch.isLetterOrNumber() || ch == '-' || ch == '_') {
            normalized.append(ch);
        } else {
            normalized.append('_');
        }
    }

    normalized.replace(QRegularExpression("_+"), "_");
    normalized = normalized.trimmed();
    if (normalized.isEmpty()) {
        return "default";
    }
    return normalized;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("blackwire_client");
    blackwire::ApplyAppTheme(app);

    QCommandLineParser parser;
    parser.setApplicationDescription("Blackwire C++ GUI client");
    parser.addHelpOption();

    QCommandLineOption smoke_option(QStringList() << "smoke", "Run headless smoke flow and exit");
    QCommandLineOption base_url_option(
        QStringList() << "base-url",
        "Base URL used by smoke mode",
        "base-url",
        "http://localhost:8000");
    QCommandLineOption profile_option(
        QStringList() << "profile",
        "Local profile name used to isolate state and credentials",
        "profile",
        "default");

    parser.addOption(smoke_option);
    parser.addOption(base_url_option);
    parser.addOption(profile_option);
    parser.process(app);

    if (parser.isSet(smoke_option)) {
        return blackwire::SmokeRunner::Run(parser.value(base_url_option));
    }

    const QString profile_name = NormalizeProfileName(parser.value(profile_option));
    const QString app_data_root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString state_dir = app_data_root;
    if (profile_name != "default") {
        state_dir = QDir(app_data_root).filePath(QString("profiles/%1").arg(profile_name));
    }
    QDir().mkpath(state_dir);
    const auto state_path = QDir(state_dir).filePath("client_state.json").toStdString();

    blackwire::QtApiClient api_client;
    blackwire::QtWsClient ws_client;
    blackwire::QtAudioCallEngine audio_engine;
    blackwire::SodiumCryptoService crypto;
    blackwire::WindowsCredentialStore secret_store;
    blackwire::StateStore state_store(state_path);

    blackwire::ApplicationController controller(
        api_client,
        ws_client,
        audio_engine,
        crypto,
        secret_store,
        state_store,
        profile_name);
    blackwire::MainWindow window(controller);
    if (profile_name != "default") {
        window.setWindowTitle(QString("Blackwire Client [%1]").arg(profile_name));
    }
    window.show();

    controller.Initialize();

    return app.exec();
}
