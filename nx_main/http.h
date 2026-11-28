// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>

/// Sets a Referer header sent with every request ("" clears it).
void http_set_referer(const char* referer);

/// One-time network/TLS setup for the app.
int http_global_init(char* err, size_t err_size);
void http_set_proxy_prefix(const char* prefix);
void http_global_exit(void);

typedef void (*HttpProgressCallback)(unsigned long long received,
                                     unsigned long long total, void* user);
typedef bool (*HttpCancelCallback)(void* user);

/// Download an HTTPS URL to dest. Follows up to 4 redirects, rejects non-200,
/// caps the body, and returns the SHA-256 of the saved file as lowercase hex.
int http_download(const char* url, const char* dest, char sha256_hex[65],
                  char* err, size_t err_size);

/// Same as http_download, with a progress callback. total is 0 when the
/// response has no Content-Length or uses chunked encoding.
int http_download_progress(const char* url, const char* dest, char sha256_hex[65],
                           char* err, size_t err_size,
                           HttpProgressCallback progress, void* user);

/// Download with progress and cancellation. Returns -2 when cancelled.
int http_download_cancel(const char* url, const char* dest, char sha256_hex[65],
                         char* err, size_t err_size,
                         HttpProgressCallback progress, void* progress_user,
                         HttpCancelCallback cancel, void* cancel_user);

/// copied unchanged. Returns 0 on success.
int http_github_archive_url(const char* url, const char* branch,
                            char* out, size_t out_size);

/// Short result text for user-facing errors.
const char* http_result_text(int result);
