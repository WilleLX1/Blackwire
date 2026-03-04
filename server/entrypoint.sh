#!/bin/sh
set -e

CERT_DIR="/app/certs"
CERT_FILE="${BLACKWIRE_TLS_CERT_FILE:-$CERT_DIR/server.crt}"
KEY_FILE="${BLACKWIRE_TLS_KEY_FILE:-$CERT_DIR/server.key}"
TLS_ENABLED="${BLACKWIRE_TLS_ENABLED:-true}"

# Run database migrations.
alembic upgrade head

# Build a SAN string from BLACKWIRE_LOCAL_SERVER_ALIASES so the self-signed
# certificate covers all addresses clients might connect to (LAN IPs, etc.).
build_san() {
    SAN="DNS:localhost,IP:127.0.0.1"
    ALIASES="${BLACKWIRE_LOCAL_SERVER_ALIASES:-}"
    if [ -n "$ALIASES" ]; then
        OLD_IFS="$IFS"; IFS=","
        for entry in $ALIASES; do
            # Strip optional :port suffix.
            host=$(echo "$entry" | sed 's/:[0-9]*$//')
            # Skip if already in SAN or empty.
            [ -z "$host" ] && continue
            echo "$SAN" | grep -q "$host" && continue
            # Detect bare IPs vs hostnames.
            if echo "$host" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$'; then
                SAN="$SAN,IP:$host"
            else
                SAN="$SAN,DNS:$host"
            fi
        done
        IFS="$OLD_IFS"
    fi
    echo "$SAN"
}

if [ "$TLS_ENABLED" = "true" ]; then
    # Auto-generate a self-signed certificate if one does not exist.
    if [ ! -f "$CERT_FILE" ] || [ ! -f "$KEY_FILE" ]; then
        SAN_VALUE=$(build_san)
        echo "[blackwire] Generating self-signed TLS certificate..."
        echo "[blackwire] SAN: $SAN_VALUE"
        mkdir -p "$(dirname "$CERT_FILE")" "$(dirname "$KEY_FILE")"
        openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
            -keyout "$KEY_FILE" \
            -out "$CERT_FILE" \
            -days 365 \
            -subj "/CN=blackwire" \
            -addext "subjectAltName=$SAN_VALUE"
        chmod 600 "$KEY_FILE"
        echo "[blackwire] Self-signed certificate created at $CERT_FILE"
    else
        echo "[blackwire] Using existing TLS certificate at $CERT_FILE"
    fi

    exec uvicorn app.main:app --host 0.0.0.0 --port 8000 \
        --ssl-keyfile "$KEY_FILE" --ssl-certfile "$CERT_FILE"
else
    exec uvicorn app.main:app --host 0.0.0.0 --port 8000
fi
