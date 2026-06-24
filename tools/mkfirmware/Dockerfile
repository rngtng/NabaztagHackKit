# Self-contained mkfirmware image.

FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        php-cli \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY mkfirmware.php ./

CMD ["php", "mkfirmware.php"]
