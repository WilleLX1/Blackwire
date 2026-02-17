#include "blackwire/smoke/smoke_runner.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>

#include <iostream>

#include "blackwire/api/qt_api_client.hpp"
#include "blackwire/crypto/sodium_crypto_service.hpp"
#include "blackwire/ws/qt_ws_client.hpp"

namespace blackwire {

int SmokeRunner::Run(const QString& base_url) {
    try {
        QtApiClient api;
        SodiumCryptoService crypto;
        QtWsClient ws;

        const auto suffix = QString::number(QDateTime::currentMSecsSinceEpoch());
        const auto alice_name = QString("alice_%1").arg(suffix).toStdString();
        const auto bob_name = QString("bob_%1").arg(suffix).toStdString();
        const std::string password = "password123";

        const auto alice_auth = api.Register(base_url.toStdString(), alice_name, password);
        const auto bob_auth = api.Register(base_url.toStdString(), bob_name, password);

        const auto alice_keys = crypto.GenerateDeviceKeys();
        const auto bob_keys = crypto.GenerateDeviceKeys();

        DeviceRegisterRequest alice_device_req;
        alice_device_req.label = "smoke-alice";
        alice_device_req.ik_ed25519_pub = alice_keys.ik_ed25519_public_b64;
        alice_device_req.enc_x25519_pub = alice_keys.enc_x25519_public_b64;
        api.RegisterDevice(base_url.toStdString(), alice_auth.tokens.access_token, alice_device_req);

        DeviceRegisterRequest bob_device_req;
        bob_device_req.label = "smoke-bob";
        bob_device_req.ik_ed25519_pub = bob_keys.ik_ed25519_public_b64;
        bob_device_req.enc_x25519_pub = bob_keys.enc_x25519_public_b64;
        const auto bob_device = api.RegisterDevice(
            base_url.toStdString(),
            bob_auth.tokens.access_token,
            bob_device_req);

        const auto conversation = api.CreateDm(
            base_url.toStdString(),
            alice_auth.tokens.access_token,
            "",
            bob_name);

        const std::string plaintext = "blackwire-smoke-message";
        const std::string ciphertext = crypto.EncryptForRecipient(bob_device.enc_x25519_pub, plaintext);

        const std::string client_message_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();

        bool received = false;
        bool decrypted_ok = false;

        ws.SetHandlers(
            [&](const WsEventMessageNew& event) {
                ws.SendAck(event.message.id);
                try {
                    const auto decrypted = crypto.DecryptWithPrivate(
                        bob_keys.enc_x25519_private_b64,
                        event.message.envelope.ciphertext_b64);
                    received = true;
                    decrypted_ok = decrypted == plaintext;
                } catch (...) {
                    received = true;
                    decrypted_ok = false;
                }
            },
            [&](const WsEventCallIncoming&) {},
            [&](const WsEventCallRinging&) {},
            [&](const WsEventCallAccepted&) {},
            [&](const WsEventCallRejected&) {},
            [&](const WsEventCallBusy&) {},
            [&](const WsEventCallEnded&) {},
            [&](const WsEventCallAudio&) {},
            [&](const WsEventCallError&) {},
            [&](const std::string& error) {
                std::cerr << "WS error: " << error << '\n';
            },
            [&](bool) {});

        ws.Connect(base_url.toStdString(), bob_auth.tokens.access_token);

        MessageSendRequest request;
        request.conversation_id = conversation.id;
        request.envelope.version = 1;
        request.envelope.alg = "libsodium-sealedbox-v1";
        request.envelope.recipient_device_id = bob_device.id;
        request.envelope.ciphertext_b64 = ciphertext;
        request.envelope.client_message_id = client_message_id;

        api.SendMessage(base_url.toStdString(), alice_auth.tokens.access_token, request);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
            if (received) {
                loop.quit();
            }
        });

        timer.start(10000);
        poll.start(100);
        loop.exec();

        ws.Disconnect();

        if (!received || !decrypted_ok) {
            std::cerr << "Smoke test failed: message not received or decrypt mismatch" << '\n';
            return 1;
        }

        std::cout << "Smoke test passed." << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Smoke test exception: " << ex.what() << '\n';
        return 1;
    }
}

}  // namespace blackwire
