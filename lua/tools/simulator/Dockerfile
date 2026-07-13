# Unicorn-based instruction-level simulator for the firmwareV2 ARM7TDMI ELF
# (issue #96). Runs the compiled ELF with no hardware. Host needs only Docker.
FROM python:3.12-slim

RUN pip install --no-cache-dir unicorn==2.1.1 pyelftools==0.31

# Built with the lua layer root as context (see Taskfile.yaml), so this path is
# layer-root-relative - mirroring tools/luac.
WORKDIR /sim
COPY tools/simulator/simulate.py /sim/simulate.py

ENTRYPOINT ["python", "/sim/simulate.py"]
