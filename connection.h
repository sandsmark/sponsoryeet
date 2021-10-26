#pragma once

#include "globals.h"
#include "ssl.h"

#include <string>
#include <iostream>
#include <cstring>

extern "C" {
#include <unistd.h>
//#include <openssl/ssl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
}

#include <cassert>

struct Connection
{
    // Chromecasts use self-signed certs
    static int verify_callback(int, ssl::X509_STORE_CTX*) {
        return 1;
    }

    Connection()
    {
        method = ssl::TLS_client_method();
        ctx = ssl::SSL_CTX_new(method);
        assert(ctx != nullptr);
        ssl::SSL_CTX_set_verify(ctx, ssl::SSL_VERIFY_NONE, verify_callback);
        handle = ssl::SSL_new(ctx);
    }

    ~Connection() {
        if (handle) {
            ssl::SSL_free(handle);
        }
        if (ctx) {
            ssl::SSL_CTX_free(ctx);
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
        int timeout = PING_INTERVAL * 1000;
        int ret = ::setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout, sizeof timeout);
        if (ret != 0) {
            perror("Failed to set socket timeout");
            return false;
        }

        std::string addressString = std::string(inet_ntoa(address.sin_addr)) + ":" + std::to_string(ntohs(address.sin_port));
        ret = ::connect(fd, (const struct sockaddr*)(&address), sizeof address);
        if (ret != 0) {
            perror(("Failed to connect to " + addressString).c_str());
            return false;
        }

        // Do ssl connection
        ssl::SSL_set_fd(handle, fd);
        ret = ssl::SSL_connect(handle);
        if (ret != 1) {
            fprintf(stderr, "SSL error: %d\n", ssl::SSL_get_error(handle, ret));
            return false;
        }

        return true;
    }

    template<typename T>
    bool write(const std::basic_string<T> &data) const {
        return size_t(ssl::SSL_write(handle, data.data(), data.size())) == data.size();
    }

    std::string read(int size) {
        std::string buffer(size, '\0');
        int amount = ssl::SSL_read(handle, buffer.data(), buffer.size());
        if (amount < 0) {
            fprintf(stderr, "SSL read error: %d\n", ssl::SSL_get_error(handle, amount));
            return "";
        }
        if (amount == 0) {
            eof = true;
        }

        buffer.resize(amount);
        return buffer;
    }

    bool eof = false;
    const ssl::SSL_METHOD *method = nullptr;
    ssl::SSL_CTX *ctx = nullptr;
    ssl::SSL *handle = nullptr;
    int fd = -1;
};

static std::string downloadFile(const std::string &hostname, const int port, const std::string &filePath)
{
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);

    if (s_verbose) {
        printf("Downloading: %s:%d/%s\n", hostname.c_str(), port, filePath.c_str());
    }

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
