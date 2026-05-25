#include "blackwire/smoke/smoke_runner.hpp"

#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>

#include <iostream>

#include "blackwire/api/qt_api_client.hpp"
#include "blackwire/crypto/sodium_crypto_service.hpp"
#include "blackwire/ws/qt_ws_client.hpp"

namespace blackwire {

namespace {

std::string CanonicalMessageSignature(
    const std::string& sender_address,
    const std::string& sender_device_uid,
    const std::string& recipient_address,
    const std::string& recipient_device_uid,
    const std::string& client_message_id,
    long long sent_at_ms,
    const std::string& sender_prev_hash,
    const std::string& sender_chain_hash,
    const std::string& ciphertext_hash,
    const std::string& aad_hash) {
    return sender_address + "\n" + sender_device_uid + "\n" + recipient_address + "\n" + recipient_device_uid + "\n" +
           client_message_id + "\n" + std::to_string(sent_at_ms) + "\n" + sender_prev_hash + "\n" + sender_chain_hash +
           "\n" + ciphertext_hash + "\n" + aad_hash;
}

std::string AggregateChainHash(
    ICryptoService& crypto,
    const std::string& sender_prev_hash,
    const std::string& client_message_id,
    long long sent_at_ms,
    const std::string& hash_material) {
    const std::string aggregate_hash = crypto.Sha256(hash_material);
    return crypto.Sha256(
        sender_prev_hash + "\n" + client_message_id + "\n" + std::to_string(sent_at_ms) + "\n" + aggregate_hash);
}

}  // namespace

int SmokeRunner::Run(const QString& base_url) {
    try {
        QtApiClient api;
        SodiumCryptoService crypto;
        QtWsClient ws;

        const auto suffix = QString::number(QDateTime::currentMSecsSinceEpoch());
        const auto alice_name = QString("alice_%1").arg(suffix).toStdString();
        const auto bob_name = QString("bob_%1").arg(suffix).toStdString();
        const std::string password = "Password123!";

        const auto alice_auth = api.Register(base_url.toStdString(), alice_name, password);
        const auto bob_auth = api.Register(base_url.toStdString(), bob_name, password);
        const std::string alice_address = alice_auth.user.user_address.empty()
                                              ? alice_name + "@local.invalid"
                                              : alice_auth.user.user_address;
        const std::string bob_address = bob_auth.user.user_address.empty()
                                            ? bob_name + "@local.invalid"
                                            : bob_auth.user.user_address;

        const auto alice_keys = crypto.GenerateDeviceKeys();
        const auto bob_keys = crypto.GenerateDeviceKeys();

        DeviceRegisterRequest alice_device_req;
        alice_device_req.label = "smoke-alice";
        alice_device_req.ik_ed25519_pub = alice_keys.ik_ed25519_public_b64;
        alice_device_req.enc_x25519_pub = alice_keys.enc_x25519_public_b64;
        const auto alice_device_auth = api.RegisterDevice(
            base_url.toStdString(),
            alice_auth.tokens.bootstrap_token,
            alice_device_req);

        DeviceRegisterRequest bob_device_req;
        bob_device_req.label = "smoke-bob";
        bob_device_req.ik_ed25519_pub = bob_keys.ik_ed25519_public_b64;
        bob_device_req.enc_x25519_pub = bob_keys.enc_x25519_public_b64;
        const auto bob_device_auth = api.RegisterDevice(
            base_url.toStdString(),
            bob_auth.tokens.bootstrap_token,
            bob_device_req);

        const auto alice_access = alice_device_auth.tokens.access_token;
        const auto bob_access = bob_device_auth.tokens.access_token;
        const auto alice_device_uid = alice_device_auth.tokens.device_uid;

        const auto conversation = api.CreateDm(
            base_url.toStdString(),
            alice_access,
            "",
            bob_name);

        const auto bob_lookup = api.GetUserDevice(
            base_url.toStdString(),
            alice_access,
            bob_address);
        if (bob_lookup.devices.empty()) {
            throw std::runtime_error("Smoke test: no bob devices resolved");
        }
        const auto bob_device = bob_lookup.devices.front();

        const std::string plaintext = "blackwire-smoke-message";
        const std::string ciphertext = crypto.EncryptForRecipient(bob_device.enc_x25519_pub, plaintext);

        const std::string client_message_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        const long long sent_at_ms = QDateTime::currentMSecsSinceEpoch();
        const std::string sender_prev_hash;
        const std::string ciphertext_hash = crypto.Sha256(QByteArray::fromBase64(QByteArray::fromStdString(ciphertext)).toStdString());
        const std::string aad_hash = crypto.Sha256(std::string());
        const std::string chain_material = bob_device.device_uid + ":" + ciphertext_hash + ":" + aad_hash;
        const std::string sender_chain_hash = AggregateChainHash(
            crypto,
            sender_prev_hash,
            client_message_id,
            sent_at_ms,
            chain_material);
        const std::string sender_address = alice_address;
        const std::string canonical = CanonicalMessageSignature(
            sender_address,
            alice_device_uid,
            bob_address,
            bob_device.device_uid,
            client_message_id,
            sent_at_ms,
            sender_prev_hash,
            sender_chain_hash,
            ciphertext_hash,
            aad_hash);
        const std::string signature = crypto.SignDetached(alice_keys.ik_ed25519_private_b64, canonical);

        bool received = false;
        bool decrypted_ok = false;

        ws.SetHandlers(
            [&](const WsEventMessageNew& event) {
                ws.SendAck(event.copy_id.empty() ? event.message.id : event.copy_id);
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
            [&](const WsEventCallGroupState&) {},
            [&](const WsEventCallRinging&) {},
            [&](const WsEventCallAccepted&) {},
            [&](const WsEventCallRejected&) {},
            [&](const WsEventCallBusy&) {},
            [&](const WsEventCallEnded&) {},
            [&](const WsEventCallAudio&) {},
            [&](const WsEventCallError&) {},
            [&](const WsEventCallWebRtcOffer&) {},
            [&](const WsEventCallWebRtcAnswer&) {},
            [&](const WsEventCallWebRtcIce&) {},
            [&](const WsEventGroupRenamed&) {},
            [&](const WsEventConversationTyping&) {},
            [&](const WsEventConversationRead&) {},
            [&](const std::string& error) {
                std::cerr << "WS error: " << error << '\n';
            },
            [&](bool) {});

        ws.Connect(base_url.toStdString(), bob_access);

        MessageSendRequest request;
        request.conversation_id = conversation.id;
        request.client_message_id = client_message_id;
        request.sent_at_ms = sent_at_ms;
        request.sender_prev_hash = sender_prev_hash;
        request.sender_chain_hash = sender_chain_hash;
        CipherEnvelope env;
        env.recipient_user_address = bob_address;
        env.recipient_device_uid = bob_device.device_uid;
        env.ciphertext_b64 = ciphertext;
        env.aad_b64.clear();
        env.signature_b64 = signature;
        env.sender_device_pubkey = alice_keys.ik_ed25519_public_b64;
        request.envelopes = {env};

        api.SendMessage(base_url.toStdString(), alice_access, request);

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
