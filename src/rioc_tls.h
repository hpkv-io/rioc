#ifndef RIOC_TLS_H
#define RIOC_TLS_H

#include <stdbool.h>
#include <openssl/ssl.h>
#include "rioc.h"

// TLS context structure
struct rioc_tls_context {
    SSL_CTX *ctx;      // OpenSSL context
    SSL *ssl;          // OpenSSL connection
    bool is_server;    // Whether this is a server context
};

// TLS functions
int rioc_tls_server_ctx_create(struct rioc_tls_context *tls, rioc_tls_config *config);
int rioc_tls_client_ctx_create(struct rioc_tls_context *tls, rioc_tls_config *config);
int rioc_tls_server_accept(struct rioc_tls_context *tls, int fd);
int rioc_tls_client_connect(struct rioc_tls_context *tls, int fd, const char *hostname);
ssize_t rioc_tls_read(struct rioc_tls_context *tls, void *buf, size_t len);
ssize_t rioc_tls_write(struct rioc_tls_context *tls, const void *buf, size_t len);
void rioc_tls_server_ctx_free(struct rioc_tls_context *tls);
void rioc_tls_client_ctx_free(struct rioc_tls_context *tls);

#endif // RIOC_TLS_H 