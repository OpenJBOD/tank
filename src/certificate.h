/*
 * Copyright (c) 2024 OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __CERTIFICATE_H__
#define __CERTIFICATE_H__

enum tls_tag {
	/** Used for both the public and private server keys */
	OPENJBOD_SERVER_CERTIFICATE_TAG,
};

static const unsigned char server_certificate[] = {
#include "server_cert.der.inc"
};

/* This is the private key in PKCS#1 RSA format. */
static const unsigned char private_key[] = {
#include "server_privkey.der.inc"
};

#endif /* __CERTIFICATE_H__ */
