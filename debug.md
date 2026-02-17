### Server startup

```bash
cd server
python -m pip install -e ".[dev]"
pytest -q
ruff check app tests
mypy app
```

From repo root:
```bash
docker compose -f infra/docker-compose.yml up --build
```

Optional Redis mode:
```bash
docker compose -f infra/docker-compose.yml -f infra/docker-compose.redis.yml up --build
```

Check these:

http://localhost:8000/health/live

http://localhost:8000/health/ready

http://localhost:8000/docs

---

### Manual client test (python)

Open two PowerShell terminals from repo root.
Terminal A (Alice):
```bash
python -m tools.reference_client.cli register alice password123
python -m tools.reference_client.cli device-init laptop
```

Terminal B (Bob):
```bash
python -m tools.reference_client.cli --state bob.json register bob password123
python -m tools.reference_client.cli --state bob.json device-init phone
python -m tools.reference_client.cli --state bob.json open-ws
```

Terminal A
```bash
python -m tools.reference_client.cli send bob "hello from alice"
```

Stop Bob’s open-ws.
Send again from Alice.
Restart Bob websocket:
```bash
python -m tools.reference_client.cli --state bob.json open-ws --once
```

---

### C++ client:
Building and verifing build:
```bash
cd client-cpp-gui
.\scripts\bootstrap-windows.ps1
.\scripts\build.ps1 -Config Debug
.\scripts\test.ps1 -Config Debug
```

Launch Blackwire
```bash
.\scripts\run.ps1 -Config Debug
```

Package Blackwire
```bash
.\scripts\package-release.ps1
```
Zip here: client-cpp-gui\dist

For full end-to-end (with server running):
```bash
.\scripts\smoke-e2e.ps1 -BaseUrl http://localhost:8000
```

### Debug multible clients on same pc?
Run once:
```bash
cd C:\projects\_other\Blackwire\client-cpp-gui
.\scripts\build.ps1 -Config Release
```

Open Terminal A:
```bash
./scripts/run.ps1 -Config Release -Profile alice
```

Open Terminal B:
```bash
./scripts/run.ps1 -Config Release -Profile bob
```


### Server Full Wipe
```bash
docker compose -f infra/docker-compose.yml down -v --remove-orphans
docker compose -f infra/docker-compose.yml up --build -d
```