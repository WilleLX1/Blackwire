FROM python:3.12-slim

RUN apt-get update && apt-get install -y --no-install-recommends openssl && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY server /app

RUN pip install --no-cache-dir --upgrade pip && \
    pip install --no-cache-dir -e . && \
    chmod +x /app/entrypoint.sh

ENTRYPOINT ["/app/entrypoint.sh"]
