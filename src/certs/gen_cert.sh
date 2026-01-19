#!/bin/bash
# Copyright (c) 2024 OpenJBOD Project
# SPDX-License-Identifier: Apache-2.0

# Generate self-signed certificate for OpenJBOD HTTPS support

set -e

# Generate a server private key (ECDSA using prime256v1)
openssl ecparam \
    -name prime256v1 \
    -genkey \
    -out server_privkey.pem

# Create a file containing certificate extensions
cat > server_cert.ext << EOF
subjectKeyIdentifier=hash
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:openjbod.local
EOF

# Generate a self-signed certificate directly (no CA needed)
openssl req \
    -new \
    -x509 \
    -sha256 \
    -key server_privkey.pem \
    -out server_cert.pem \
    -days 365 \
    -extensions v3_req \
    -config <(cat <<EOF
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
O = OpenJBOD Project
CN = openjbod.local

[v3_req]
subjectKeyIdentifier=hash
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:openjbod.local
EOF
)

# Create DER encoded versions of server certificate and private key
openssl ec \
    -outform der \
    -in server_privkey.pem \
    -out server_privkey.der

openssl x509 \
    -outform der \
    -in server_cert.pem \
    -out server_cert.der

# Clean up temporary files
rm -f server_cert.ext
rm -f server_cert.pem
rm -f server_privkey.pem