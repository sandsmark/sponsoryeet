#pragma once

extern "C" {
#include <openssl/ssl.h>
#include <netdb.h>
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

static std::string downloadFile(const std::string &hostname, const int port, const std::string &filePath)
{
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);

    struct hostent *hostinfo = gethostbyname(hostname.c_str());

    if (hostinfo == nullptr) {
        perror(("Failed to resolve " + hostname).c_str());
        return "";
    }

    serverAddress.sin_addr = *(struct in_addr*) hostinfo->h_addr;

    Connection connection;
    if (!connection.connect(serverAddress)) {
        std::cerr << "Failed to connect to " << hostname << std::endl;
        return "";
    }

    const std::string request =
        "GET " + filePath + " HTTP/1.1\r\n" +
        "Host: " + hostname + "\r\n"
        "User-Agent: fuckifiknow/1.0\r\n"
        "Accept: */*\r\n"
        "\r\n";

    if (!connection.write(request)) {
        puts("Failed to write GET request");
        return "";
    }
    const std::string response = connection.read(1024 * 1024); // idk, 1MB should be enough

    const char *endOfHeaderMarker = "\r\n\r\n";
    // Just ignore all the header shit
    std::string::size_type endOfHeader = response.find(endOfHeaderMarker);
    if (endOfHeader == std::string::npos) {
        puts("Failed to find end of header in returned data");
        return "";
    }

    return response.substr(endOfHeader + strlen(endOfHeaderMarker));
}
