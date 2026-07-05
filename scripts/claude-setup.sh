#!/usr/bin/env bash
# Idempotent environment bootstrap for this repo's Docker+Task toolchain.
# Safe to run repeatedly (every check is "if missing, do X"); safe on a
# plain dev machine too — steps that only apply to the Claude sandbox
# (dockerd, the CA shim) no-op elsewhere.
set -uo pipefail

log() { echo "[claude-setup] $*"; }

# --- 1. Docker daemon --------------------------------------------------
if ! docker info >/dev/null 2>&1; then
	if command -v dockerd >/dev/null 2>&1; then
		log "starting dockerd in background"
		(dockerd >/tmp/claude-setup-dockerd.log 2>&1 &)
		for _ in $(seq 1 15); do
			docker info >/dev/null 2>&1 && break
			sleep 1
		done
	fi
	if ! docker info >/dev/null 2>&1; then
		log "docker still unreachable — Docker-based tasks (build/test/simulate) will fail"
	fi
fi

# --- 2. go-task ---------------------------------------------------------
if ! command -v task >/dev/null 2>&1; then
	log "installing go-task"
	TASK_VERSION="v3.44.0"
	ARCH="$(uname -m)"; case "$ARCH" in x86_64) ARCH=amd64;; aarch64) ARCH=arm64;; esac
	OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
	URL="https://github.com/go-task/task/releases/download/${TASK_VERSION}/task_${OS}_${ARCH}.tar.gz"
	TMPDIR="$(mktemp -d)"
	if curl -fsSL "$URL" -o "$TMPDIR/task.tar.gz" 2>/dev/null; then
		tar -xzf "$TMPDIR/task.tar.gz" -C "$TMPDIR" task
		install -m 0755 "$TMPDIR/task" /usr/local/bin/task
		log "task installed: $(task --version)"
	else
		log "task download failed — falling back to 'go install' (slower)"
		go install github.com/go-task/task/v3/cmd/task@latest 2>&1 | tail -3
		ln -sf "$(go env GOPATH)/bin/task" /usr/local/bin/task 2>/dev/null
	fi
	rm -rf "$TMPDIR"
fi

# --- 3. Proxy CA shim for Docker base images -----------------------------
# Only relevant inside the Claude Code remote sandbox: its egress proxy
# TLS-terminates and re-signs everything, so a base image's own trust
# store (used by apt-get/pip during our Dockerfile builds) rejects it
# until the proxy's CA is installed into the image.
CA_BUNDLE="/root/.ccr/ca-bundle.crt"
if [ -f "$CA_BUNDLE" ] && docker info >/dev/null 2>&1; then
	SHIM_DIR="$(mktemp -d)"
	cp "$CA_BUNDLE" "$SHIM_DIR/ccr-ca.crt"
	cat > "$SHIM_DIR/Dockerfile" <<'EOF'
ARG BASE
FROM ${BASE}
COPY ccr-ca.crt /usr/local/share/ca-certificates/ccr-ca.crt
RUN if command -v update-ca-certificates >/dev/null 2>&1; then update-ca-certificates; fi || true
ENV SSL_CERT_FILE=/usr/local/share/ca-certificates/ccr-ca.crt \
    PIP_CERT=/usr/local/share/ca-certificates/ccr-ca.crt \
    REQUESTS_CA_BUNDLE=/usr/local/share/ca-certificates/ccr-ca.crt \
    CURL_CA_BUNDLE=/usr/local/share/ca-certificates/ccr-ca.crt \
    NODE_EXTRA_CA_CERTS=/usr/local/share/ca-certificates/ccr-ca.crt
EOF
	for BASE in python:3.12-slim debian:bookworm-slim; do
		if ! docker image inspect "$BASE-orig" >/dev/null 2>&1; then
			log "shimming proxy CA into $BASE"
			docker pull -q "$BASE" >/dev/null 2>&1
			docker tag "$BASE" "$BASE-orig"
			docker build -q --build-arg BASE="$BASE-orig" -t "$BASE" "$SHIM_DIR" >/dev/null
		fi
	done
	rm -rf "$SHIM_DIR"
fi

log "done"
