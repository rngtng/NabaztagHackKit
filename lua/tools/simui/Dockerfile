# Browser UI for the lua-firmware simulator (#43): NiceGUI + the embedded
# Unicorn sim in one image. Built with the lua layer root as the Docker context
# (see Taskfile.yaml), like tools/luac / tools/simulator, so it can COPY the
# sibling tools/simulator/simulate.py it runs in-process.
FROM python:3.12-slim

RUN pip install --no-cache-dir unicorn==2.1.1 pyelftools==0.31 nicegui==2.11.1

WORKDIR /sim
COPY tools/simulator/simulate.py /sim/simulate.py
COPY tools/simui/app.py /sim/app.py

EXPOSE 8080
ENTRYPOINT ["python", "/sim/app.py"]
