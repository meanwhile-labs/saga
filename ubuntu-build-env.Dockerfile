FROM --platform=linux/amd64 ubuntu:26.04

ENV RUSTUP_HOME=/usr/local/rustup \
    CARGO_HOME=/usr/local/cargo \
    PATH=/usr/local/cargo/bin:$PATH

RUN dpkg --add-architecture i386
RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y curl gcc-multilib pkg-config protobuf-compiler python3
RUN bazel_version="9.2.0" \
    bazel_binary="bazel-${bazel_version}-linux-x86_64" \
    && curl -fsSLo "/tmp/${bazel_binary}" \
          "https://github.com/bazelbuild/bazel/releases/download/${bazel_version}/${bazel_binary}" \
    && curl -fsSLo "/tmp/${bazel_binary}.sha256" \
          "https://github.com/bazelbuild/bazel/releases/download/${bazel_version}/${bazel_binary}.sha256" \
     && cd /tmp \
     && sha256sum -c "${bazel_binary}.sha256" \
     && install -m 0755 "${bazel_binary}" /usr/local/bin/bazel

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
RUN rustup default stable
RUN cargo install --locked --git https://github.com/opensagadev/objdiff.git --rev 08fd60d328046baf5f8246d4a6fb2cb0553aacb7 objdiff-cli

# hack to work around rounding issues only observed in Rosetta
# contents of ubuntu-build-env.bazelrc:
# `common --extra_toolchains=@rules_python//python/runtime_env_toolchains:all`
COPY ubuntu-build-env.bazelrc /etc/bazel.bazelrc

WORKDIR /workspace
