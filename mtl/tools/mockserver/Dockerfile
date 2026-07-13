# python:3.12-slim is the base scripts/claude-setup.sh pre-bakes the proxy CA into,
# so image builds have network inside the Claude sandbox. Stdlib only — no pip deps.
FROM python:3.12-slim

WORKDIR /src

COPY server.py ./

ENTRYPOINT ["python3", "/src/server.py"]
