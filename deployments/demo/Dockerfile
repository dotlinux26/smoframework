FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y \
    build-essential cmake git ninja-build \
    libssl-dev libfmt-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src/smoframework
RUN mkdir -p /src/smoframework/build && \
    cmake -S /src/smoframework -B /src/smoframework/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DWITH_PQC=OFF \
        -GNinja && \
    ninja -C /src/smoframework/build smo-node smo-cli smo-admin smo-debug && \
    cp /src/smoframework/build/cmd/smo-node/smo-node /usr/local/bin/ && \
    cp /src/smoframework/build/cmd/smo-cli/smo-cli /usr/local/bin/ && \
    cp /src/smoframework/build/cmd/smo-admin/smo-admin /usr/local/bin/ && \
    cp /src/smoframework/build/cmd/smo-debug/smo-debug /usr/local/bin/

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y \
    libfmt-dev iproute2 netcat-openbsd jq curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/bin/smo-* /usr/local/bin/

RUN mkdir -p /etc/smo /var/lib/smo

COPY deployments/demo/scripts/ /usr/local/lib/smo-demo/
RUN chmod +x /usr/local/lib/smo-demo/*.sh

ENTRYPOINT ["/usr/local/lib/smo-demo/entrypoint.sh"]
