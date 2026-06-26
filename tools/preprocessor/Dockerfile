FROM python:3.12-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends tzdata \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY preproc.py ./

ENTRYPOINT ["python", "/src/preproc.py"]
