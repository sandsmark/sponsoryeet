#include "ssl.h"

#include <dlfcn.h>
#include <cstddef>
#include <vector>
#include <stdio.h>
#include <filesystem>

extern "C" {
struct SSL;
struct SSL_CTX;
struct SSL_METHOD;
struct X509_STORE_CTX;
struct OPENSSL_INIT_SETTINGS;

static SSL *(*EXT_SSL_new)        (SSL_CTX *) = nullptr;
static SSL_CTX *(*EXT_SSL_CTX_new)(const SSL_METHOD *) = nullptr;
static SSL_METHOD * (*EXT_TLS_client_method) () = NULL;
static int (*EXT_SSL_connect)     (SSL *) = nullptr;
static int (*EXT_SSL_get_error) (SSL*, int) = nullptr;
static int (*EXT_SSL_read)        (SSL *, void *, int) = nullptr;
static int (*EXT_SSL_set_fd)      (SSL *, int) = nullptr;
static int (*EXT_SSL_write)       (SSL *, const void *, int) = nullptr;
static void (*EXT_SSL_CTX_free)   (SSL_CTX *) = nullptr;
static void (*EXT_SSL_CTX_set_verify)(SSL_CTX *, int, int (*)(int, X509_STORE_CTX *)) = nullptr;
static void (*EXT_SSL_free)       (SSL *) = nullptr;
} // extern "C"

#define RESOLVE_SYMBOL(RET, NAME, ARGS...) if (!(EXT_##NAME = (RET(*)(ARGS))dlsym(libHandle, #NAME))) { fprintf(stderr, "Failed to resolve %s: %s\n", #NAME, dlerror()); }

static bool resolveSymbols(void *libHandle)
{
    dlerror(); // clear existing errors

    RESOLVE_SYMBOL(SSL*, SSL_new, SSL_CTX*);
    RESOLVE_SYMBOL(SSL_CTX*, SSL_CTX_new, const SSL_METHOD*);
    RESOLVE_SYMBOL(int, SSL_connect, SSL*);
    RESOLVE_SYMBOL(int, SSL_get_error, SSL*, int);
    RESOLVE_SYMBOL(int, SSL_read, SSL*, void*, int);
    RESOLVE_SYMBOL(int, SSL_set_fd, SSL*, int);
    RESOLVE_SYMBOL(int, SSL_write, SSL*, const void*, int);
    RESOLVE_SYMBOL(void, SSL_CTX_free, SSL_CTX*);
    RESOLVE_SYMBOL(void, SSL_CTX_set_verify, SSL_CTX*, int, int (*)(int, X509_STORE_CTX*));
    RESOLVE_SYMBOL(void, SSL_free, SSL*);

    EXT_TLS_client_method = (SSL_METHOD *(*)()) dlsym(libHandle, "TLS_client_method");
    if (!EXT_TLS_client_method) {
        EXT_TLS_client_method = (SSL_METHOD *(*)()) dlsym(libHandle, "SSLv23_client_method");
    }

    return EXT_SSL_CTX_free &&
        EXT_SSL_CTX_new &&
        EXT_SSL_CTX_set_verify &&
        EXT_SSL_connect &&
        EXT_SSL_free &&
        EXT_SSL_get_error &&
        EXT_SSL_new &&
        EXT_SSL_read &&
        EXT_SSL_write &&
        EXT_SSL_set_fd &&
        EXT_TLS_client_method
        ;
}

static bool libraryInit(void *libHandle)
{
    void *init = dlsym(libHandle, "SSL_library_init");
    if (init) {
        ((int (*)())init)();
        return true;
    }
    init = dlsym(libHandle, "OPENSSL_init_ssl");
    if (init) {
        ((int (*)(uint64_t, void*))init)(0, nullptr);
        return true;
    }
    return false;
}

static bool attemptOpen(const std::string &file)
{
    if (file.empty()) {
        return false;
    }

    void *libHandle = dlopen(file.c_str(), RTLD_LAZY);
    if (!libHandle) {
        fprintf(stderr, "Failed to open %s: %s\n", file.c_str(), dlerror());
        return false;
    }
    const bool success = resolveSymbols(libHandle) && libraryInit(libHandle);

    if (!success) {
        fprintf(stderr, "Failed to load %s\n", file.c_str());
        EXT_SSL_CTX_free = nullptr;
        EXT_SSL_CTX_new = nullptr;
        EXT_SSL_CTX_set_verify = nullptr;
        EXT_SSL_connect = nullptr;
        EXT_SSL_free = nullptr;
        EXT_SSL_get_error = nullptr;
        EXT_SSL_new = nullptr;
        EXT_SSL_read = nullptr;
        EXT_SSL_write = nullptr;
        EXT_SSL_set_fd = nullptr;
        EXT_TLS_client_method = nullptr;
    }

    dlclose(libHandle);
    return success;
}

namespace ssl {
bool initialize()
{
    static const std::vector<std::string> dirs = {
        "/usr/lib/",
        "./"
        "/usr/local/lib/",
        "/usr/local/openssl/lib/",
        "/usr/local/ssl/lib/",
        "/opt/openssl/lib/",
        "/lib/",
    };
    const char *manualPath = getenv("SPONSORYEET_SSL_LIB");
    if (manualPath && attemptOpen(manualPath)) {
        return true;
    }

    // TODO: more?
    static const std::vector<std::string> libs = {
        "libssl.so",
        "libgnutls-openssl.so"
    };
    for (const std::string &dir : dirs) {
        for (const std::string &filename : libs) {
            if (!std::filesystem::exists(dir + filename)) {
                continue;
            }
            if (attemptOpen(dir + filename)) {
                return true;
            }
        }
    }

    fprintf(stderr, "Failed to find any valid openssl (last error: %s\n)\n", dlerror());
    return false;
}

void SSL_CTX_free(SSL_CTX *ctx)
{
    EXT_SSL_CTX_free((::SSL_CTX*)ctx);
}

SSL_CTX *SSL_CTX_new(const SSL_METHOD *method)
{
    return (SSL_CTX*)EXT_SSL_CTX_new((const ::SSL_METHOD*)method);
}
void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, int (*verify_callback)(int, X509_STORE_CTX *))
{
    return EXT_SSL_CTX_set_verify((::SSL_CTX*)ctx, mode, (int (*)(int, ::X509_STORE_CTX *))verify_callback);
}

int SSL_connect(SSL *ssl)
{
    return EXT_SSL_connect((::SSL*)ssl);
}

void SSL_free(SSL *ssl)
{
    EXT_SSL_free((::SSL*)ssl);
}

int SSL_get_error(SSL *ssl, int rc)
{
    return EXT_SSL_get_error((::SSL*)ssl, rc);
}

SSL *SSL_new(SSL_CTX *ctx)
{
    return (SSL*)EXT_SSL_new((::SSL_CTX*)ctx);
}

int SSL_read(SSL *ssl, void *buf, int num)
{
    return EXT_SSL_read((::SSL*)ssl, buf, num);
}

int SSL_set_fd(SSL *ssl, int fd)
{
    return EXT_SSL_set_fd((::SSL*)ssl, fd);
}

int SSL_write(SSL *ssl, const void *buf, int num)
{
    return EXT_SSL_write((::SSL*)ssl, buf, num);
}

SSL_METHOD *TLS_client_method()
{
    return (SSL_METHOD*)EXT_TLS_client_method();
}
} // namespace ssl
