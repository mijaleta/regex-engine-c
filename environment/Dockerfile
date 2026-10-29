FROM debian:bookworm-slim@sha256:99c0b25c46d0214d5a2091092e7908907dcaecd6a962cde0ac700acee222b6f3

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8

# A C toolchain and procps (pkill) for the verifier. Python is deliberately
# not installed: its re module is the reference the expected outputs came
# from, so the agent has to work from the specification.
RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc libc6-dev make procps \
    && rm -rf /var/lib/apt/lists/* \
    && ! command -v python3

# Non-root agent user. The compiler stays root-owned so the agent cannot
# replace what the verifier builds with.
RUN useradd --create-home --shell /bin/bash agent

WORKDIR /app
COPY workspace/ /app/
RUN chmod +x /app/run_examples.sh && chown -R agent:agent /app

# The starting code must build.
RUN su agent -c "cd /app && make -s rx && rm rx"
