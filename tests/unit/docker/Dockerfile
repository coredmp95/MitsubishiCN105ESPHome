# Dockerfile — developer image for the 32-bit (-m32) characterization harness
# Role: gives a machine without g++-multilib the same toolchain family as the CI runner (ubuntu-24.04)
# Deps: ubuntu:24.04; used through tests/unit/docker/run.sh (D-20)
FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        g++ g++-multilib cmake git ca-certificates python3 python3-venv make \
    && rm -rf /var/lib/apt/lists/*
