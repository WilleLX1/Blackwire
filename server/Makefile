PYTHON ?= python

.PHONY: install install-dev run test lint type migrate smoke

install:
	$(PYTHON) -m pip install -e .

install-dev:
	$(PYTHON) -m pip install -e .[dev]

run:
	uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload

test:
	pytest -q

lint:
	ruff check app tests

type:
	mypy app

migrate:
	alembic upgrade head

smoke:
	docker compose -f ../infra/docker-compose.yml up --build --abort-on-container-exit
