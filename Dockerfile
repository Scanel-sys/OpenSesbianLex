# syntax=docker/dockerfile:1

FROM ubuntu:24.04 AS opencl-test-base

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        bison \
        clang \
        cmake \
        flex \
        g++ \
        libclang-dev \
        ninja-build \
        ocl-icd-opencl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

FROM opencl-test-base AS pocl-base

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        pocl-opencl-icd \
    && rm -rf /var/lib/apt/lists/*

FROM pocl-base AS test-pocl

COPY . .

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DOPEN_SLEX_FRONTEND=CLANG \
        -DOPEN_SLEX_ENABLE_OPENCL_RUNTIME_TESTS=ON \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure

FROM opencl-test-base AS oclgrind-base

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        oclgrind \
    && rm -rf /var/lib/apt/lists/*

FROM oclgrind-base AS test-oclgrind

COPY . .

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DOPEN_SLEX_FRONTEND=CLANG \
        -DOPEN_SLEX_ENABLE_OPENCL_RUNTIME_TESTS=ON \
        -DOPEN_SLEX_OPENCL_RUNTIME_LAUNCHER=oclgrind \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure

FROM opencl-test-base AS sanitizer-base

FROM sanitizer-base AS test-sanitizers

COPY . .

RUN cmake -S . -B build-sanitized -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DOPEN_SLEX_FRONTEND=CLANG \
        -DOPEN_SLEX_ENABLE_SANITIZERS=ON \
        -DOPEN_SLEX_BUILD_FUZZER=ON \
    && cmake --build build-sanitized \
    && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
       UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
       ctest --test-dir build-sanitized --output-on-failure

# Running `docker build .` without --target uses the faster PoCL test target.
FROM test-pocl AS test
