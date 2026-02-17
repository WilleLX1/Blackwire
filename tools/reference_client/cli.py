import argparse
import asyncio
import json
import sys
import uuid
from pathlib import Path
from urllib.parse import urlparse

import websockets

from tools.reference_client import api
from tools.reference_client.crypto import CryptoError, decrypt_with_private, encrypt_for_recipient, generate_device_bundle
from tools.reference_client.state import default_state_path, load_state, save_state


def ws_url_from_base(base_url: str) -> str:
    parsed = urlparse(base_url)
    scheme = "wss" if parsed.scheme == "https" else "ws"
    host = parsed.netloc
    return f"{scheme}://{host}/api/v1/ws"


def persist_auth(state_path: Path, base_url: str, response: dict) -> None:
    state = load_state(state_path)
    state["base_url"] = base_url
    state["user"] = response["user"]
    state["tokens"] = response["tokens"]
    save_state(state_path, state)


def cmd_register(args: argparse.Namespace) -> None:
    response = api.register(args.base_url, args.username, args.password)
    persist_auth(args.state, args.base_url, response)
    print(json.dumps({"user": response["user"]}, indent=2))


def cmd_login(args: argparse.Namespace) -> None:
    response = api.login(args.base_url, args.username, args.password)
    persist_auth(args.state, args.base_url, response)
    print(json.dumps({"user": response["user"]}, indent=2))


def cmd_device_init(args: argparse.Namespace) -> None:
    state = load_state(args.state)
    if "tokens" not in state:
        raise SystemExit("Missing auth tokens. Run login/register first.")

    bundle = generate_device_bundle()
    device = api.register_device(
        base_url=state.get("base_url", args.base_url),
        access_token=state["tokens"]["access_token"],
        label=args.label,
        ik_ed25519_pub=bundle["ik_ed25519_public"],
        enc_x25519_pub=bundle["enc_x25519_public"],
    )

    state["base_url"] = state.get("base_url", args.base_url)
    state["device"] = {
        "id": device["id"],
        "label": device["label"],
        "ik_ed25519_public": bundle["ik_ed25519_public"],
        "ik_ed25519_private": bundle["ik_ed25519_private"],
        "enc_x25519_public": bundle["enc_x25519_public"],
        "enc_x25519_private": bundle["enc_x25519_private"],
    }
    save_state(args.state, state)
    print(json.dumps({"device": device}, indent=2))


def cmd_send(args: argparse.Namespace) -> None:
    state = load_state(args.state)
    base_url = state.get("base_url", args.base_url)
    tokens = state.get("tokens")
    device = state.get("device")

    if not tokens or not device:
        raise SystemExit("Missing auth/device state. Run login and device-init first.")

    peer_lookup = api.get_user_device(base_url, tokens["access_token"], args.peer_username)
    conversation = api.create_dm(base_url, tokens["access_token"], args.peer_username)

    recipient_device = peer_lookup["device"]
    ciphertext_b64 = encrypt_for_recipient(recipient_device["enc_x25519_pub"], args.message)
    payload = {
        "conversation_id": conversation["id"],
        "envelope": {
            "version": 1,
            "alg": "libsodium-sealedbox-v1",
            "recipient_device_id": recipient_device["id"],
            "ciphertext_b64": ciphertext_b64,
            "aad_b64": None,
            "client_message_id": str(uuid.uuid4()),
        },
    }

    response = api.send_message(base_url, tokens["access_token"], payload)
    print(json.dumps(response, indent=2))


async def _consume_ws(base_url: str, access_token: str, private_key_b64: str | None, once: bool) -> None:
    ws_url = ws_url_from_base(base_url)
    async with websockets.connect(
        ws_url,
        additional_headers={"Authorization": f"Bearer {access_token}"},
    ) as websocket:
        while True:
            raw = await websocket.recv()
            event = json.loads(raw)
            if event.get("type") != "message.new":
                print(json.dumps(event, indent=2))
                if once:
                    return
                continue

            message = event["message"]
            envelope = message["envelope"]
            plaintext = None
            if private_key_b64:
                try:
                    plaintext = decrypt_with_private(private_key_b64, envelope["ciphertext_b64"])
                except CryptoError:
                    plaintext = None

            output = {
                "message_id": message["id"],
                "conversation_id": message["conversation_id"],
                "sender_user_id": message["sender_user_id"],
                "client_message_id": envelope["client_message_id"],
                "plaintext": plaintext,
            }
            print(json.dumps(output, indent=2))

            await websocket.send(
                json.dumps({"type": "message.ack", "message_id": message["id"]})
            )
            if once:
                return


def cmd_open_ws(args: argparse.Namespace) -> None:
    state = load_state(args.state)
    base_url = state.get("base_url", args.base_url)
    tokens = state.get("tokens")
    if not tokens:
        raise SystemExit("Missing auth state. Run login/register first.")

    device = state.get("device", {})
    private_key_b64 = device.get("enc_x25519_private")

    asyncio.run(
        _consume_ws(
            base_url=base_url,
            access_token=tokens["access_token"],
            private_key_b64=private_key_b64,
            once=args.once,
        )
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Blackwire reference client")
    parser.add_argument("--base-url", default="http://localhost:8000", help="Server base URL")
    parser.add_argument("--state", type=Path, default=default_state_path(), help="State file path")

    sub = parser.add_subparsers(dest="command", required=True)

    register = sub.add_parser("register", help="Register and store auth tokens")
    register.add_argument("username")
    register.add_argument("password")
    register.set_defaults(func=cmd_register)

    login = sub.add_parser("login", help="Login and store auth tokens")
    login.add_argument("username")
    login.add_argument("password")
    login.set_defaults(func=cmd_login)

    device_init = sub.add_parser("device-init", help="Create keypair and register active device")
    device_init.add_argument("label")
    device_init.set_defaults(func=cmd_device_init)

    send = sub.add_parser("send", help="Encrypt and send a direct message")
    send.add_argument("peer_username")
    send.add_argument("message")
    send.set_defaults(func=cmd_send)

    open_ws = sub.add_parser("open-ws", help="Open websocket and receive/decrypt messages")
    open_ws.add_argument("--once", action="store_true", help="Exit after first message")
    open_ws.set_defaults(func=cmd_open_ws)

    recv = sub.add_parser("recv", help="Alias for open-ws")
    recv.add_argument("--once", action="store_true", help="Exit after first message")
    recv.set_defaults(func=cmd_open_ws)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        args.func(args)
    except api.ApiError as exc:
        print(f"API error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
