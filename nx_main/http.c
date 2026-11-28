// SPDX-License-Identifier: MIT
// A deliberately small HTTPS GET client. It uses the console's SSL service
// (the same trust store as system clients) and writes one file at a time.
// Ported from brewstore (MIT); Referer header support added for sounds-resource.
#include "http.h"

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define HTTP_MAX_BODY (1024ull * 1024ull * 1024ull)
#define HTTP_MAX_HEADER 16384
#define HTTP_MAX_REDIRECTS 4
#define HTTP_CONNECT_TIMEOUT_MS 15000
#define HTTP_BUFFER_SIZE (128 * 1024)

typedef struct {
    char host[256];
    char path[1024];
    int port;
} Url;

typedef struct {
    SslContext context;
    SslConnection connection;
    int socket;
    bool context_open;
    bool connection_open;
} Tls;

typedef struct {
    SslConnection* connection;
    unsigned char* buffer;
    size_t position;
    size_t length;
    bool eof;
} Reader;

typedef struct {
    int status;
    unsigned long long content_length;
    bool chunked;
    char location[1024];
} Response;

static bool g_network_ready;
static bool g_ssl_ready;
static bool g_network_owned; /* we initialized sockets (vs. host environment) */
static char g_referer[512];
static char g_proxy_prefix[256];
static unsigned char g_reader_buffer[HTTP_BUFFER_SIZE];
static Mutex g_download_lock;
static bool g_download_lock_ready;

static void set_error(char* err, size_t err_size, const char* format, ...) {
    if (!err || !err_size)
        return;
    va_list args;
    va_start(args, format);
    vsnprintf(err, err_size, format, args);
    va_end(args);
}

static bool starts_with(const char* text, const char* prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool contains_ci(const char* text, const char* needle) {
    const size_t length = strlen(needle);
    for (; *text; text++) {
        if (strncasecmp(text, needle, length) == 0)
            return true;
    }
    return false;
}

static int url_parse(const char* url, Url* out) {
    memset(out, 0, sizeof(*out));
    if (!starts_with(url, "https://"))
        return -1;
    const char* host = url + 8;
    const char* slash = strchr(host, '/');
    const char* host_end = slash ? slash : host + strlen(host);
    const char* colon = memchr(host, ':', (size_t)(host_end - host));
    const char* name_end = colon ? colon : host_end;
    const size_t host_length = (size_t)(name_end - host);
    if (host_length == 0 || host_length >= sizeof(out->host))
        return -1;
    memcpy(out->host, host, host_length);
    out->host[host_length] = '\0';
    out->port = 443;
    if (colon) {
        char port[8];
        const size_t port_length = (size_t)(host_end - colon - 1);
        if (port_length == 0 || port_length >= sizeof(port))
            return -1;
        memcpy(port, colon + 1, port_length);
        port[port_length] = '\0';
        out->port = (int)strtol(port, NULL, 10);
        if (out->port <= 0 || out->port > 65535)
            return -1;
    }
    if (slash) {
        if (strlen(slash) >= sizeof(out->path))
            return -1;
        strcpy(out->path, slash);
    } else {
        strcpy(out->path, "/");
    }
    return 0;
}

static int url_join(const char* base_url, const char* location, char* out, size_t out_size) {
    if (starts_with(location, "https://")) {
        if (strlen(location) >= out_size)
            return -1;
        strcpy(out, location);
        return 0;
    }
    Url base;
    if (url_parse(base_url, &base) != 0)
        return -1;
    if (starts_with(location, "//")) {
        const int written = snprintf(out, out_size, "https:%s", location);
        return written > 0 && (size_t)written < out_size ? 0 : -1;
    }
    if (location[0] == '/') {
        const int written = snprintf(out, out_size, "https://%s%s", base.host, location);
        return written > 0 && (size_t)written < out_size ? 0 : -1;
    }
    char directory[1024];
    strcpy(directory, base.path);
    char* last = strrchr(directory, '/');
    if (last)
        last[1] = '\0';
    else
        strcpy(directory, "/");
    const int written = snprintf(out, out_size, "https://%s%s%s", base.host, directory, location);
    return written > 0 && (size_t)written < out_size ? 0 : -1;
}

static int connect_tcp(const char* host, int port, char* err, size_t err_size) {
    char service[8];
    snprintf(service, sizeof(service), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* addresses = NULL;
    const int lookup = getaddrinfo(host, service, &hints, &addresses);
    if (lookup != 0) {
        set_error(err, err_size, "DNS failed for %s: %s", host, gai_strerror(lookup));
        return -1;
    }

    int result = -1;
    int last_error = 0;
    char last_ip[INET6_ADDRSTRLEN] = "unknown";
    for (struct addrinfo* address = addresses; address; address = address->ai_next) {
        if (address->ai_family == AF_INET) {
            inet_ntop(AF_INET, &((struct sockaddr_in*)address->ai_addr)->sin_addr,
                      last_ip, sizeof(last_ip));
        } else if (address->ai_family == AF_INET6) {
            inet_ntop(AF_INET6, &((struct sockaddr_in6*)address->ai_addr)->sin6_addr,
                      last_ip, sizeof(last_ip));
        }
        const int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            last_error = errno;
            continue;
        }
        int socket_buffer = 512 * 1024;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &socket_buffer, sizeof(socket_buffer));
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &socket_buffer, sizeof(socket_buffer));
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            result = fd;
            break;
        }
        last_error = errno;
        close(fd);
    }
    freeaddrinfo(addresses);
    if (result < 0) {
        set_error(err, err_size, "connect %s:%d (%s) failed: %s",
                  host, port, last_ip,
                  last_error ? strerror(last_error) : "no usable address");
    }
    return result;
}

static void tls_close(Tls* tls) {
    if (tls->connection_open) {
        sslConnectionClose(&tls->connection);
        tls->connection_open = false;
    }
    if (tls->context_open) {
        sslContextClose(&tls->context);
        tls->context_open = false;
    }
    if (tls->socket >= 0) {
        close(tls->socket);
        tls->socket = -1;
    }
}

static int tls_open(Tls* tls, const char* host, int port, char* err, size_t err_size) {
    memset(tls, 0, sizeof(*tls));
    tls->socket = -1;
    tls->socket = connect_tcp(host, port, err, err_size);
    if (tls->socket < 0)
        return -1;

    Result rc = sslCreateContext(&tls->context, SslVersion_Auto24);
    if (R_FAILED(rc)) {
        set_error(err, err_size, "sslCreateContext failed 0x%x", rc);
        tls_close(tls);
        return -1;
    }
    tls->context_open = true;
    rc = sslContextCreateConnection(&tls->context, &tls->connection);
    if (R_FAILED(rc)) {
        set_error(err, err_size, "sslContextCreateConnection failed 0x%x", rc);
        tls_close(tls);
        return -1;
    }
    tls->connection_open = true;
    sslConnectionSetOption(&tls->connection, SslOptionType_DoNotCloseSocket, true);

    const struct timeval timeout = { .tv_sec = 20, .tv_usec = 0 };
    setsockopt(tls->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(tls->socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    const int ssl_fd = socketSslConnectionSetSocketDescriptor(&tls->connection, tls->socket);
    if (ssl_fd < 0 && errno != ENOENT) {
        set_error(err, err_size, "ssl socket setup failed 0x%x", socketGetLastResult());
        tls_close(tls);
        return -1;
    }
    rc = sslConnectionSetHostName(&tls->connection, host, (u32)strlen(host) + 1);
    if (R_FAILED(rc)) {
        set_error(err, err_size, "sslConnectionSetHostName failed 0x%x", rc);
        tls_close(tls);
        return -1;
    }
    rc = sslConnectionSetVerifyOption(&tls->connection,
                                      SslVerifyOption_PeerCa |
                                      SslVerifyOption_HostName |
                                      SslVerifyOption_DateCheck);
    if (R_FAILED(rc)) {
        set_error(err, err_size, "sslConnectionSetVerifyOption failed 0x%x", rc);
        tls_close(tls);
        return -1;
    }
    rc = sslConnectionDoHandshake(&tls->connection, NULL, NULL, NULL, 0);
    if (R_FAILED(rc)) {
        set_error(err, err_size, "TLS handshake failed 0x%x", rc);
        tls_close(tls);
        return -1;
    }
    return 0;
}

static int reader_fill(Reader* reader) {
    u32 received = 0;
    const Result rc = sslConnectionRead(reader->connection, reader->buffer,
                                        HTTP_BUFFER_SIZE, &received);
    if (R_FAILED(rc))
        return -1;
    if (received == 0) {
        reader->eof = true;
        return 0;
    }
    reader->position = 0;
    reader->length = received;
    return 1;
}

static int reader_getc(Reader* reader, int* out) {
    if (reader->position >= reader->length) {
        const int filled = reader_fill(reader);
        if (filled <= 0)
            return filled;
    }
    *out = reader->buffer[reader->position++];
    return 1;
}

static int reader_read(Reader* reader, void* destination, size_t wanted, size_t* received) {
    size_t total = 0;
    unsigned char* out = destination;
    while (total < wanted) {
        if (reader->position >= reader->length) {
            const int filled = reader_fill(reader);
            if (filled < 0)
                return -1;
            if (filled == 0)
                break;
        }
        const size_t available = reader->length - reader->position;
        const size_t take = available < wanted - total ? available : wanted - total;
        memcpy(out + total, reader->buffer + reader->position, take);
        reader->position += take;
        total += take;
    }
    *received = total;
    return total ? 1 : 0;
}

static int reader_line(Reader* reader, char* out, size_t out_size) {
    size_t length = 0;
    for (;;) {
        int c = 0;
        const int got = reader_getc(reader, &c);
        if (got <= 0)
            return got;
        if (c == '\n')
            break;
        if (c != '\r') {
            if (length + 1 >= out_size)
                return -1;
            out[length++] = (char)c;
        }
    }
    out[length] = '\0';
    return 1;
}

static int read_response_headers(Reader* reader, Response* response,
                                 char* err, size_t err_size) {
    char header[HTTP_MAX_HEADER + 1];
    size_t length = 0;
    for (;;) {
        int c = 0;
        const int got = reader_getc(reader, &c);
        if (got <= 0) {
            set_error(err, err_size, "connection closed before HTTP headers");
            return -1;
        }
        if (length >= HTTP_MAX_HEADER) {
            set_error(err, err_size, "HTTP headers too large");
            return -1;
        }
        header[length++] = (char)c;
        if (length >= 4 && memcmp(header + length - 4, "\r\n\r\n", 4) == 0)
            break;
    }
    header[length] = '\0';
    memset(response, 0, sizeof(*response));
    if (sscanf(header, "HTTP/%*d.%*d %d", &response->status) != 1) {
        set_error(err, err_size, "invalid HTTP response");
        return -1;
    }

    char* save = NULL;
    char* line = strtok_r(header, "\r\n", &save);
    while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
        char* colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = '\0';
        char* value = colon + 1;
        while (*value == ' ' || *value == '\t')
            value++;
        if (strcasecmp(line, "Content-Length") == 0) {
            response->content_length = strtoull(value, NULL, 10);
        } else if (strcasecmp(line, "Transfer-Encoding") == 0) {
            response->chunked = contains_ci(value, "chunked");
        } else if (strcasecmp(line, "Location") == 0) {
            snprintf(response->location, sizeof(response->location), "%s", value);
        }
    }
    return 0;
}

static int send_request(Tls* tls, const Url* url, char* err, size_t err_size) {
    char request[2048];
    int length;
    if (url->port == 443) {
        length = snprintf(request, sizeof(request),
                          "GET %s HTTP/1.1\r\n"
                          "Host: %s\r\n"
                          "User-Agent: Mozilla/5.0 (compatible; hbmenu)\r\n"
                          "Accept: */*\r\n"
                          "%s"
                          "Connection: close\r\n\r\n",
                          url->path, url->host, g_referer);
    } else {
        length = snprintf(request, sizeof(request),
                          "GET %s HTTP/1.1\r\n"
                          "Host: %s:%d\r\n"
                          "User-Agent: Mozilla/5.0 (compatible; hbmenu)\r\n"
                          "Accept: */*\r\n"
                          "%s"
                          "Connection: close\r\n\r\n",
                          url->path, url->host, url->port, g_referer);
    }
    if (length <= 0 || (size_t)length >= sizeof(request)) {
        set_error(err, err_size, "request too large");
        return -1;
    }
    size_t sent = 0;
    while (sent < (size_t)length) {
        u32 written = 0;
        const Result rc = sslConnectionWrite(&tls->connection, request + sent,
                                             (u32)(length - (int)sent), &written);
        if (R_FAILED(rc) || written == 0) {
            set_error(err, err_size, "HTTPS write failed 0x%x", rc);
            return -1;
        }
        sent += written;
    }
    return 0;
}

static char hex_digit(unsigned value) {
    return (char)(value < 10 ? '0' + value : 'a' + value - 10);
}

static void sha_to_hex(const u8 hash[SHA256_HASH_SIZE], char out[65]) {
    for (int i = 0; i < SHA256_HASH_SIZE; i++) {
        out[i * 2] = hex_digit(hash[i] >> 4);
        out[i * 2 + 1] = hex_digit(hash[i] & 0xf);
    }
    out[64] = '\0';
}

static int write_body(FILE* file, Sha256Context* sha, unsigned long long* total,
                      unsigned long long expected_total, const void* data, size_t length,
                      HttpProgressCallback progress, void* user,
                      char* err, size_t err_size) {
    if (*total + length > HTTP_MAX_BODY) {
        set_error(err, err_size, "download exceeds 1 GiB limit");
        return -1;
    }
    if (fwrite(data, 1, length, file) != length) {
        set_error(err, err_size, "cannot write download cache");
        return -1;
    }
    sha256ContextUpdate(sha, data, (u32)length);
    *total += length;
    if (progress)
        progress(*total, expected_total, user);
    return 0;
}

static int copy_body(Reader* reader, Response* response, FILE* file,
                     Sha256Context* sha, HttpProgressCallback progress, void* user,
                     HttpCancelCallback cancel, void* cancel_user,
                     char* err, size_t err_size) {
    static unsigned char buffer[HTTP_BUFFER_SIZE];
    unsigned long long total = 0;
    if (response->chunked) {
        for (;;) {
            if (cancel && cancel(cancel_user))
                return -2;
            char line[128];
            if (reader_line(reader, line, sizeof(line)) <= 0) {
                set_error(err, err_size, "truncated chunked response");
                return -1;
            }
            char* end = NULL;
            const unsigned long chunk = strtoul(line, &end, 16);
            if (end == line) {
                set_error(err, err_size, "invalid chunk size");
                return -1;
            }
            if (chunk == 0) {
                while (reader_line(reader, line, sizeof(line)) > 0 && line[0])
                    ;
                return 0;
            }
            unsigned long remaining = chunk;
            while (remaining) {
                const size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
                size_t received = 0;
                const int got = reader_read(reader, buffer, wanted, &received);
                if (got <= 0 || received == 0) {
                    set_error(err, err_size, "truncated chunk");
                    return -1;
                }
                if (write_body(file, sha, &total, response->content_length, buffer, received,
                               progress, user, err, err_size) != 0)
                    return -1;
                remaining -= received;
            }
            if (reader_line(reader, line, sizeof(line)) < 0) {
                set_error(err, err_size, "truncated chunk terminator");
                return -1;
            }
        }
    }

    unsigned long long remaining = response->content_length;
    if (remaining) {
        while (remaining) {
            if (cancel && cancel(cancel_user))
                return -2;
            const size_t wanted = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
            size_t received = 0;
            const int got = reader_read(reader, buffer, wanted, &received);
            if (got <= 0 || received == 0) {
                set_error(err, err_size, "truncated response body");
                return -1;
            }
            if (write_body(file, sha, &total, response->content_length, buffer, received,
                           progress, user, err, err_size) != 0)
                return -1;
            remaining -= received;
        }
        return 0;
    }

    for (;;) {
        if (cancel && cancel(cancel_user))
            return -2;
        size_t received = 0;
        const int got = reader_read(reader, buffer, sizeof(buffer), &received);
        if (got < 0) {
            set_error(err, err_size, "HTTPS read failed");
            return -1;
        }
        if (got == 0 || received == 0)
            return 0;
        if (write_body(file, sha, &total, 0, buffer, received,
                       progress, user, err, err_size) != 0)
            return -1;
    }
}

static int download_once(const char* url, const char* dest, char sha256_hex[65],
                         char* err, size_t err_size, HttpProgressCallback progress,
                         void* user, HttpCancelCallback cancel, void* cancel_user,
                         int depth) {
    char proxied[1400];
    const char* request_url = url;
    if (g_proxy_prefix[0] && starts_with(url, "https://") &&
        !starts_with(url, g_proxy_prefix)) {
        const int written = snprintf(proxied, sizeof(proxied), "%s%s",
                                     g_proxy_prefix, url);
        if (written <= 0 || (size_t)written >= sizeof(proxied)) {
            set_error(err, err_size, "proxy URL is too long");
            return -1;
        }
        request_url = proxied;
    }

    Url parsed;
    if (url_parse(request_url, &parsed) != 0) {
        set_error(err, err_size, "invalid HTTPS URL");
        return -1;
    }
    Tls tls;
    if (tls_open(&tls, parsed.host, parsed.port, err, err_size) != 0)
        return -1;
    if (send_request(&tls, &parsed, err, err_size) != 0) {
        tls_close(&tls);
        return -1;
    }

    Reader reader = { .connection = &tls.connection, .buffer = g_reader_buffer };
    Response response;
    if (read_response_headers(&reader, &response, err, err_size) != 0) {
        tls_close(&tls);
        return -1;
    }

    if (response.status >= 300 && response.status < 400) {
        if (response.location[0] == '\0' || depth >= HTTP_MAX_REDIRECTS) {
            set_error(err, err_size, "HTTP %d without usable redirect", response.status);
            tls_close(&tls);
            return -1;
        }
        char next[1024];
        if (url_join(request_url, response.location, next, sizeof(next)) != 0) {
            set_error(err, err_size, "invalid redirect URL");
            tls_close(&tls);
            return -1;
        }
        tls_close(&tls);
        return download_once(next, dest, sha256_hex, err, err_size, progress, user,
                             cancel, cancel_user, depth + 1);
    }
    if (response.status != 200) {
        set_error(err, err_size, "HTTP status %d", response.status);
        tls_close(&tls);
        return -1;
    }

    char temporary[644];
    const int written = snprintf(temporary, sizeof(temporary), "%.639s.tmp", dest);
    if (written <= 0 || (size_t)written >= sizeof(temporary)) {
        set_error(err, err_size, "download cache path is too long");
        tls_close(&tls);
        return -1;
    }
    FILE* file = fopen(temporary, "wb");
    if (!file) {
        set_error(err, err_size, "cannot create download cache");
        tls_close(&tls);
        return -1;
    }
    setvbuf(file, NULL, _IOFBF, 128 * 1024);
    Sha256Context sha;
    sha256ContextCreate(&sha);
    if (copy_body(&reader, &response, file, &sha, progress, user, cancel, cancel_user,
                  err, err_size) != 0) {
        fclose(file);
        remove(temporary);
        tls_close(&tls);
        return -1;
    }
    const int flush_result = fflush(file);
    const int close_result = fclose(file);
    if (flush_result != 0 || close_result != 0) {
        remove(temporary);
        set_error(err, err_size, "cannot finish download cache");
        tls_close(&tls);
        return -1;
    }
    if (rename(temporary, dest) != 0) {
        remove(dest);
        if (rename(temporary, dest) != 0) {
            remove(temporary);
            set_error(err, err_size, "cannot replace download cache");
            tls_close(&tls);
            return -1;
        }
    }
    u8 digest[SHA256_HASH_SIZE];
    sha256ContextGetHash(&sha, digest);
    sha_to_hex(digest, sha256_hex);
    tls_close(&tls);
    return 0;
}

void http_set_referer(const char* referer) {
    if (!referer || !referer[0]) {
        g_referer[0] = '\0';
        return;
    }
    snprintf(g_referer, sizeof(g_referer), "Referer: %s\r\n", referer);
}

int http_global_init(char* err, size_t err_size) {
    if (!g_download_lock_ready) {
        mutexInit(&g_download_lock);
        g_download_lock_ready = true;
    }
    if (!g_network_ready) {
        const Result rc = socketInitializeDefault();
        if (R_FAILED(rc)) {
            if (rc == MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)) {
                /* Host environment already has sockets up (e.g. kaiHEN):
                 * use them but don't claim ownership. */
                g_network_owned = false;
                g_network_ready = true;
            } else {
                set_error(err, err_size, "socketInitializeDefault failed 0x%x", rc);
                return -1;
            }
        } else {
            g_network_owned = true;
            g_network_ready = true;
        }
    }
    if (!g_ssl_ready) {
        const Result rc = sslInitialize(1);
        if (R_FAILED(rc)) {
            set_error(err, err_size, "sslInitialize failed 0x%x", rc);
            return -1;
        }
        g_ssl_ready = true;
    }
    return 0;
}

void http_global_exit(void) {
    if (g_ssl_ready) {
        sslExit();
        g_ssl_ready = false;
    }
    if (g_network_ready && g_network_owned) {
        socketExit();
    }
    g_network_ready = false;
}

void http_set_proxy_prefix(const char* prefix) {
    g_proxy_prefix[0] = '\0';
    if (!prefix || !prefix[0])
        return;
    snprintf(g_proxy_prefix, sizeof(g_proxy_prefix), "%s", prefix);
    const size_t length = strlen(g_proxy_prefix);
    if (length && g_proxy_prefix[length - 1] != '/' && length + 1 < sizeof(g_proxy_prefix)) {
        g_proxy_prefix[length] = '/';
        g_proxy_prefix[length + 1] = '\0';
    }
}

int http_download_cancel(const char* url, const char* dest, char sha256_hex[65],
                         char* err, size_t err_size,
                         HttpProgressCallback progress, void* progress_user,
                         HttpCancelCallback cancel, void* cancel_user) {
    if (!g_network_ready || !g_ssl_ready) {
        set_error(err, err_size, "network not initialized");
        return -1;
    }
    if (sha256_hex)
        sha256_hex[0] = '\0';
    if (g_download_lock_ready)
        mutexLock(&g_download_lock);
    const int result = download_once(url, dest, sha256_hex, err, err_size, progress,
                                     progress_user, cancel, cancel_user, 0);
    if (g_download_lock_ready)
        mutexUnlock(&g_download_lock);
    return result;
}

int http_download_progress(const char* url, const char* dest, char sha256_hex[65],
                           char* err, size_t err_size,
                           HttpProgressCallback progress, void* user) {
    return http_download_cancel(url, dest, sha256_hex, err, err_size, progress, user,
                                NULL, NULL);
}

int http_download(const char* url, const char* dest, char sha256_hex[65],
                  char* err, size_t err_size) {
    return http_download_cancel(url, dest, sha256_hex, err, err_size, NULL, NULL,
                                NULL, NULL);
}

const char* http_result_text(int result) {
    return result == 0 ? "ok" : "failed";
}
