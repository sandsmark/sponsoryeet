#pragma once

namespace ssl
{
    using SSL = void;
    using SSL_CTX = void;
    using SSL_METHOD = void;
    using X509_STORE_CTX = void;

    static constexpr int SSL_VERIFY_NONE = 0;

    bool initialize();

    void SSL_CTX_free(SSL_CTX *ctx);
    SSL_CTX *SSL_CTX_new(const SSL_METHOD *method);
    void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, int (*verify_callback)(int, X509_STORE_CTX *));
    int SSL_connect(SSL *ssl);
    void SSL_free(SSL *ssl);
    int SSL_get_error(SSL *ssl, int rc);
    SSL *SSL_new(SSL_CTX *ctx);
    int SSL_read(SSL *ssl, void *buf, int num);
    int SSL_set_fd(SSL *ssl, int fd);
    int SSL_write(SSL *ssl, const void *buf, int num);
    SSL_METHOD *TLS_client_method();

} // namespace ssl

