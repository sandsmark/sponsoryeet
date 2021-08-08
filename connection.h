#pragma once

extern "C" {
#include <openssl/ssl.h>
}

#include <cassert>

struct Connection
{
    // Chromecasts use self-signed certs
    static int verify_callback(int, x509_store_ctx_st*) {
        return 1;
    }

    Connection()
    {
        method = SSLv23_client_method();
        SSLeay_add_ssl_algorithms();
        ctx = SSL_CTX_new(method);
        assert(ctx != nullptr);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, verify_callback);
        handle = SSL_new(ctx);
    }

    ~Connection() {
        if (handle) {
            SSL_free(handle);
        }
        if (ctx) {
            SSL_CTX_free(ctx);
        }
        if (fd > 0) {
            close(fd);
        }
    }

    bool connect(const sockaddr_in &address)
    {
	fd = ::socket(PF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            perror("Failed to open socket");
            return false;
        }

        int ret = ::connect(fd, (const struct sockaddr*)(&address),sizeof(address));
        if (ret != 0) {
            perror("Failed to connect to chromecast");
            return false;
        }

        // Do ssl connection
        SSL_set_fd(handle, fd);
        ret = SSL_connect(handle);
        if (ret != 1) {
            fprintf(stderr, "SSL error: %d\n", SSL_get_error(handle, ret));
            return false;
        }

        return true;
    }

    bool write(const std::string &data) {
        return SSL_write(handle, data.data(), data.size()) == data.size();
    }

    std::string read(int size) {
        std::string buffer(size, '\0');
        int amount = SSL_read(handle, buffer.data(), buffer.size());
        if (amount < 0) {
            fprintf(stderr, "SSL read error: %d\n", SSL_get_error(handle, amount));
            return "";
        }
        if (amount == 0) {
            eof = true;
        }

        buffer.resize(amount);
        return buffer;
    }

    bool eof = false;
    const SSL_METHOD *method = nullptr;
    SSL_CTX *ctx = nullptr;
    SSL *handle = nullptr;
    int fd = -1;
};
