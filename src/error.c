#include "nova.h"

ErrorContext  *nova_error_ctx  = NULL;
SignalContext *nova_signal_ctx = NULL;

static const char *error_prefix(NovaErrorType type) {
    switch (type) {
        case ERR_SYNTAX:  return "SyntaxError";
        case ERR_RUNTIME: return "RuntimeError";
        case ERR_TYPE:    return "TypeError";
        case ERR_NAME:    return "NameError";
    }
    return "Error";
}

void nova_format_error(NovaErrorType type, int line, char *buf, size_t buf_size,
                       const char *fmt, ...) {
    const char *prefix = error_prefix(type);
    int offset;
    if (line > 0) {
        offset = snprintf(buf, buf_size, "%s [line %d]: ", prefix, line);
    } else {
        offset = snprintf(buf, buf_size, "%s: ", prefix);
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf + offset, buf_size - offset, fmt, args);
    va_end(args);
}

static void nova_error_throw(NovaErrorType type, int line,
                             const char *fmt, va_list args)
    __attribute__((noreturn));

static void nova_error_throw(NovaErrorType type, int line,
                             const char *fmt, va_list args) {
    if (nova_error_ctx == NULL) {
        /* No error handler — print and exit */
        const char *prefix = error_prefix(type);
        if (line > 0)
            fprintf(stderr, "%s [line %d]: ", prefix, line);
        else
            fprintf(stderr, "%s: ", prefix);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        exit(1);
    }
    nova_error_ctx->err_type = type;
    nova_error_ctx->line = line;
    const char *prefix = error_prefix(type);
    int offset;
    if (line > 0)
        offset = snprintf(nova_error_ctx->message, sizeof(nova_error_ctx->message),
                          "%s [line %d]: ", prefix, line);
    else
        offset = snprintf(nova_error_ctx->message, sizeof(nova_error_ctx->message),
                          "%s: ", prefix);
    vsnprintf(nova_error_ctx->message + offset,
              sizeof(nova_error_ctx->message) - offset, fmt, args);
    longjmp(nova_error_ctx->jump, 1);
}

static void try_catch_signal(NovaErrorType type, int line,
                             const char *fmt, va_list args)
    __attribute__((noreturn));

static void try_catch_signal(NovaErrorType type, int line,
                             const char *fmt, va_list args) {
    SignalContext *ctx = nova_signal_ctx;
    ctx->signal_type = SIGNAL_TRY_ERROR;
    const char *prefix = error_prefix(type);
    int offset;
    if (line > 0)
        offset = snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                          "%s [line %d]: ", prefix, line);
    else
        offset = snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                          "%s: ", prefix);
    vsnprintf(ctx->error_msg + offset, sizeof(ctx->error_msg) - offset, fmt, args);
    va_end(args);
    longjmp(ctx->jump, SIGNAL_TRY_ERROR);
}

void nova_syntax_error(int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    nova_error_throw(ERR_SYNTAX, line, fmt, args);
}

void nova_runtime_error(int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (nova_signal_ctx != NULL)
        try_catch_signal(ERR_RUNTIME, line, fmt, args);
    nova_error_throw(ERR_RUNTIME, line, fmt, args);
}

void nova_type_error(int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (nova_signal_ctx != NULL)
        try_catch_signal(ERR_TYPE, line, fmt, args);
    nova_error_throw(ERR_TYPE, line, fmt, args);
}

void nova_name_error(int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (nova_signal_ctx != NULL)
        try_catch_signal(ERR_NAME, line, fmt, args);
    nova_error_throw(ERR_NAME, line, fmt, args);
}
