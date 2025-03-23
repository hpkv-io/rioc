#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include "rioc.h"
#include "rioc_platform.h"

// TLS chunk size (slightly less than 16KB to account for TLS overhead)
#define RIOC_TLS_CHUNK_SIZE 16000

// TLS error logging helper
static void log_ssl_error(const char *prefix) {
    unsigned long err;
    char buf[256];
    while ((err = ERR_get_error()) != 0) {
        ERR_error_string_n(err, buf, sizeof(buf));
        fprintf(stderr, "%s: %s\n", prefix, buf);
    }
}

// Initialize OpenSSL once
int rioc_tls_init(void) {
    static int initialized = 0;
    if (!initialized) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        initialized = 1;
    }
    return RIOC_SUCCESS;
}

void rioc_tls_cleanup(void) {
    EVP_cleanup();
    ERR_free_strings();
}

// Create server TLS context
int rioc_tls_server_ctx_create(rioc_tls_context *tls_ctx, const rioc_tls_config *config) {
    if (!tls_ctx || !config || !config->cert_path || !config->key_path) {
        return RIOC_ERR_PARAM;
    }

    rioc_tls_init();

    // Create TLS context
    const SSL_METHOD *method = TLS_server_method();
    tls_ctx->ctx = SSL_CTX_new(method);
    if (!tls_ctx->ctx) {
        log_ssl_error("Failed to create SSL context");
        return RIOC_ERR_IO;
    }

    // Set TLS version to 1.3 only
    SSL_CTX_set_min_proto_version(tls_ctx->ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(tls_ctx->ctx, TLS1_3_VERSION);

    // Set verification mode
    int verify_mode = config->verify_peer ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT) : SSL_VERIFY_NONE;

    SSL_CTX_set_verify(tls_ctx->ctx, verify_mode, NULL);

    // Set strict certificate checking
    if (config->verify_peer) {
        //SSL_CTX_set_verify_depth(tls_ctx->ctx, 4);
        // Disable legacy renegotiation
        //SSL_CTX_set_options(tls_ctx->ctx, SSL_OP_NO_RENEGOTIATION);
        // Enable strict certificate checking
        //SSL_CTX_set_options(tls_ctx->ctx, SSL_OP_NO_TICKET);
    }

    // Set verification mode
    if (config->verify_peer) {
        SSL_CTX_set_verify_depth(tls_ctx->ctx, 4);
        // Load CA certificate if provided for client verification
        if (config->ca_path) {
            if (!SSL_CTX_load_verify_locations(tls_ctx->ctx, config->ca_path, NULL)) {
                log_ssl_error("Failed to load CA certificate");
                SSL_CTX_free(tls_ctx->ctx);
                return RIOC_ERR_IO;
            }
        }
    }

    // Load certificate and private key
    if (SSL_CTX_use_certificate_file(tls_ctx->ctx, config->cert_path, SSL_FILETYPE_PEM) <= 0) {
        log_ssl_error("Failed to load certificate file");
        SSL_CTX_free(tls_ctx->ctx);
        return RIOC_ERR_IO;
    }

    if (SSL_CTX_use_PrivateKey_file(tls_ctx->ctx, config->key_path, SSL_FILETYPE_PEM) <= 0) {
        log_ssl_error("Failed to load private key file");
        SSL_CTX_free(tls_ctx->ctx);
        return RIOC_ERR_IO;
    }

    // Verify private key
    if (!SSL_CTX_check_private_key(tls_ctx->ctx)) {
        log_ssl_error("Private key does not match certificate");
        SSL_CTX_free(tls_ctx->ctx);
        return RIOC_ERR_IO;
    }

    tls_ctx->is_server = true;
    tls_ctx->ssl = NULL;

    return RIOC_SUCCESS;
}

// Create client TLS context
int rioc_tls_client_ctx_create(rioc_tls_context *tls_ctx, const rioc_tls_config *config) {
    if (!tls_ctx || !config) {
        return RIOC_ERR_PARAM;
    }

    rioc_tls_init();

    // Create TLS context
    const SSL_METHOD *method = TLS_client_method();
    tls_ctx->ctx = SSL_CTX_new(method);
    if (!tls_ctx->ctx) {
        log_ssl_error("Failed to create SSL context");
        return RIOC_ERR_IO;
    }

    // Set TLS version to 1.3 only
    SSL_CTX_set_min_proto_version(tls_ctx->ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(tls_ctx->ctx, TLS1_3_VERSION);
    
    // Set verification mode
    int verify_mode = config->verify_peer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
    
    SSL_CTX_set_verify(tls_ctx->ctx, verify_mode, NULL);

    // Set strict certificate checking
    if (config->verify_peer) {
        //SSL_CTX_set_verify_depth(tls_ctx->ctx, 4);
        // Disable legacy renegotiation
        //SSL_CTX_set_options(tls_ctx->ctx, SSL_OP_NO_RENEGOTIATION);
        // Enable strict certificate checking
        //SSL_CTX_set_options(tls_ctx->ctx, SSL_OP_NO_TICKET);
    }

    // Set verification mode
    if (config->verify_peer) {
        SSL_CTX_set_verify_depth(tls_ctx->ctx, 4);
        if (config->ca_path) {
            if (!SSL_CTX_load_verify_locations(tls_ctx->ctx, config->ca_path, NULL)) {
                log_ssl_error("Failed to load CA certificate");
                SSL_CTX_free(tls_ctx->ctx);
                return RIOC_ERR_IO;
            }
        }
    }

    // Load certificate and private key
    if (SSL_CTX_use_certificate_file(tls_ctx->ctx, config->cert_path, SSL_FILETYPE_PEM) <= 0) {
        log_ssl_error("Failed to load certificate file");
        SSL_CTX_free(tls_ctx->ctx);
        return RIOC_ERR_IO;
    }

    if (SSL_CTX_use_PrivateKey_file(tls_ctx->ctx, config->key_path, SSL_FILETYPE_PEM) <= 0) {
        log_ssl_error("Failed to load private key file");
        SSL_CTX_free(tls_ctx->ctx);
        return RIOC_ERR_IO;
    }

    // Verify private key
    if (!SSL_CTX_check_private_key(tls_ctx->ctx)) {
        log_ssl_error("Private key does not match certificate");
        SSL_CTX_free(tls_ctx->ctx);
        return RIOC_ERR_IO;
    }

    tls_ctx->is_server = false;
    tls_ctx->ssl = NULL;

    return RIOC_SUCCESS;
}

// Accept TLS connection (server)
int rioc_tls_server_accept(rioc_tls_context *tls_ctx, int client_fd) {
    if (!tls_ctx || !tls_ctx->ctx || !tls_ctx->is_server) {
        return RIOC_ERR_PARAM;
    }

    // Create new SSL connection
    tls_ctx->ssl = SSL_new(tls_ctx->ctx);
    if (!tls_ctx->ssl) {
        log_ssl_error("Failed to create SSL object");
        return RIOC_ERR_IO;
    }

    // Set socket for SSL
    if (!SSL_set_fd(tls_ctx->ssl, client_fd)) {
        log_ssl_error("Failed to set SSL file descriptor");
        SSL_free(tls_ctx->ssl);
        tls_ctx->ssl = NULL;
        return RIOC_ERR_IO;
    }

    // Accept TLS connection
    int ret = SSL_accept(tls_ctx->ssl);
    if (ret <= 0) {
        int err = SSL_get_error(tls_ctx->ssl, ret);
        log_ssl_error("SSL accept failed");
        SSL_free(tls_ctx->ssl);
        tls_ctx->ssl = NULL;
        return (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) ? 
               -EAGAIN : RIOC_ERR_IO;
    }

    return RIOC_SUCCESS;
}

// Connect TLS (client)
int rioc_tls_client_connect(rioc_tls_context *tls_ctx, int fd, const char *hostname) {
    if (!tls_ctx || !tls_ctx->ctx || tls_ctx->is_server) {
        return RIOC_ERR_PARAM;
    }

    // Create new SSL connection
    tls_ctx->ssl = SSL_new(tls_ctx->ctx);
    if (!tls_ctx->ssl) {
        log_ssl_error("Failed to create SSL object");
        return RIOC_ERR_IO;
    }

    // Set hostname/IP verification
    if (strchr(hostname, '.') && strspn(hostname, "0123456789.") == strlen(hostname)) {
        // IP address
        X509_VERIFY_PARAM *param = SSL_get0_param(tls_ctx->ssl);
        X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_NO_CHECK_TIME);
        X509_VERIFY_PARAM_set1_ip_asc(param, hostname);
    } else {
        // Hostname
        SSL_set_tlsext_host_name(tls_ctx->ssl, hostname);  // SNI
        X509_VERIFY_PARAM *param = SSL_get0_param(tls_ctx->ssl);
        X509_VERIFY_PARAM_set1_host(param, hostname, strlen(hostname));
    }

//     // Set hostname for SNI
//     if (hostname) {
//         SSL_set_tlsext_host_name(tls_ctx->ssl, hostname);
// #if OPENSSL_VERSION_NUMBER >= 0x10100000L
//         SSL_set_hostflags(tls_ctx->ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
//         SSL_set1_host(tls_ctx->ssl, hostname);
// #endif
//     }

    // Set socket for SSL
    if (!SSL_set_fd(tls_ctx->ssl, fd)) {
        log_ssl_error("Failed to set SSL file descriptor");
        SSL_free(tls_ctx->ssl);
        tls_ctx->ssl = NULL;
        return RIOC_ERR_IO;
    }

    // Connect TLS
    int ret = SSL_connect(tls_ctx->ssl);
    if (ret <= 0) {
        int err = SSL_get_error(tls_ctx->ssl, ret);
        log_ssl_error("SSL connect failed");
        SSL_free(tls_ctx->ssl);
        tls_ctx->ssl = NULL;
        return (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) ? 
               -EAGAIN : RIOC_ERR_IO;
    }

    return RIOC_SUCCESS;
}

// Read from TLS connection
int rioc_tls_read(rioc_tls_context *tls_ctx, void *buf, size_t len) {
    if (!tls_ctx || !tls_ctx->ssl || !buf) {
        return RIOC_ERR_PARAM;
    }

    size_t total_read = 0;
    while (total_read < len) {
        size_t remaining = len - total_read;
        size_t chunk_size = (remaining > RIOC_TLS_CHUNK_SIZE) ? RIOC_TLS_CHUNK_SIZE : remaining;
        
        int ret = SSL_read(tls_ctx->ssl, (char*)buf + total_read, chunk_size);
        if (ret <= 0) {
            int err = SSL_get_error(tls_ctx->ssl, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                continue;  // Retry the read
            }
            log_ssl_error("SSL read failed");
            return RIOC_ERR_IO;
        }
        total_read += ret;
    }
    return total_read;
}

// Write to TLS connection
int rioc_tls_write(rioc_tls_context *tls_ctx, const void *buf, size_t len) {
    if (!tls_ctx || !tls_ctx->ssl || !buf) {
        return RIOC_ERR_PARAM;
    }

    size_t total_written = 0;
    while (total_written < len) {
        size_t remaining = len - total_written;
        size_t chunk_size = (remaining > RIOC_TLS_CHUNK_SIZE) ? RIOC_TLS_CHUNK_SIZE : remaining;
        
        int ret = SSL_write(tls_ctx->ssl, (char*)buf + total_written, chunk_size);
        if (ret <= 0) {
            int err = SSL_get_error(tls_ctx->ssl, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                continue;  // Retry the write
            }
            log_ssl_error("SSL write failed");
            return RIOC_ERR_IO;
        }
        total_written += ret;
    }
    return total_written;
}

// Standard cleanup for SSL object
void rioc_tls_cleanup_ssl(rioc_tls_context *tls_ctx) {
    if (tls_ctx && tls_ctx->ssl) {
        // Attempt graceful bidirectional shutdown
        int ret = SSL_shutdown(tls_ctx->ssl);
        if (ret == 0) {
            // If ret == 0, it means we've sent the close_notify but haven't received one
            // Try once more to complete bidirectional shutdown
            ret = SSL_shutdown(tls_ctx->ssl);
        }
        // Even if shutdown fails, we still need to free the SSL object
        SSL_free(tls_ctx->ssl);
        tls_ctx->ssl = NULL;
    }
}

// Standard cleanup for TLS context
void rioc_tls_server_ctx_free(rioc_tls_context *tls_ctx) {
    if (!tls_ctx) {
        return;
    }

    if (tls_ctx->ssl) {
        rioc_tls_cleanup_ssl(tls_ctx);
    }

    if (tls_ctx->ctx) {
        SSL_CTX_free(tls_ctx->ctx);
        tls_ctx->ctx = NULL;
    }
}

// Free client TLS context
void rioc_tls_client_ctx_free(rioc_tls_context *tls_ctx) {
    rioc_tls_server_ctx_free(tls_ctx);  // Same cleanup process
}

// Vectored I/O operations
int rioc_tls_readv(rioc_tls_context *tls_ctx, struct iovec *iov, int iovcnt) {
    if (!tls_ctx || !tls_ctx->ssl || !iov || iovcnt <= 0) {
        return RIOC_ERR_PARAM;
    }

    int total = 0;
    for (int i = 0; i < iovcnt; i++) {
        size_t bytes_read = 0;
        while (bytes_read < iov[i].iov_len) {
            int ret = rioc_tls_read(tls_ctx, (char*)iov[i].iov_base + bytes_read, 
                                  iov[i].iov_len - bytes_read);
            if (ret < 0) {
                if (ret == -EAGAIN) continue;  // Retry the read
                return ret;
            }
            if (ret == 0) {  // EOF
                return total > 0 ? total : RIOC_ERR_IO;
            }
            bytes_read += ret;
            total += ret;
        }
    }
    return total;
}

int rioc_tls_writev(rioc_tls_context *tls_ctx, const struct iovec *iov, int iovcnt) {
    if (!tls_ctx || !tls_ctx->ssl || !iov || iovcnt <= 0) {
        return RIOC_ERR_PARAM;
    }

    int total = 0;
    char chunk_buffer[RIOC_TLS_CHUNK_SIZE];
    size_t chunk_used = 0;

    for (int i = 0; i < iovcnt; i++) {
        const char *data = (const char *)iov[i].iov_base;
        size_t remaining = iov[i].iov_len;
        size_t offset = 0;

        while (remaining > 0) {
            // Calculate how much we can add to current chunk
            size_t can_add = RIOC_TLS_CHUNK_SIZE - chunk_used;
            size_t to_add = (remaining > can_add) ? can_add : remaining;

            // If chunk would be full or this is last piece of data
            if (to_add == can_add || (i == iovcnt - 1 && remaining == to_add)) {
                // Copy data to chunk buffer
                memcpy(chunk_buffer + chunk_used, data + offset, to_add);
                chunk_used += to_add;

                // Write full chunk
                int ret = rioc_tls_write(tls_ctx, chunk_buffer, chunk_used);
                if (ret < 0) {
                    return ret;
                }
                total += ret;

                // Reset chunk buffer
                chunk_used = 0;
            } else {
                // Just add to chunk buffer
                memcpy(chunk_buffer + chunk_used, data + offset, to_add);
                chunk_used += to_add;
            }

            offset += to_add;
            remaining -= to_add;
        }
    }

    // Write any remaining data in chunk buffer
    if (chunk_used > 0) {
        int ret = rioc_tls_write(tls_ctx, chunk_buffer, chunk_used);
        if (ret < 0) {
            return ret;
        }
        total += ret;
    }

    return total;
} 