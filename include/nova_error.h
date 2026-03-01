#ifndef NOVA_ERROR_H
#define NOVA_ERROR_H

#include "nova_common.h"
#include "nova_value.h"

typedef enum {
    ERR_SYNTAX,
    ERR_RUNTIME,
    ERR_TYPE,
    ERR_NAME,
} NovaErrorType;

/* Global error context using setjmp/longjmp */
typedef struct ErrorContext {
    jmp_buf             jump;
    struct ErrorContext *prev;
    NovaErrorType       err_type;
    char                message[512];
    int                 line;
} ErrorContext;

/* Control flow signals (also via longjmp) */
typedef enum {
    SIGNAL_RETURN = 1,
    SIGNAL_BREAK = 2,
    SIGNAL_CONTINUE = 3,
    SIGNAL_ERROR = 4,
    SIGNAL_TRY_ERROR = 5,
} SignalType;

typedef struct SignalContext {
    jmp_buf              jump;
    struct SignalContext *prev;
    SignalType           signal_type;
    NovaValue            return_value;  /* for SIGNAL_RETURN */
    char                 error_msg[512]; /* for SIGNAL_TRY_ERROR */
} SignalContext;

/* Global error/signal stacks */
extern ErrorContext  *nova_error_ctx;
extern SignalContext *nova_signal_ctx;

/* Error reporting — these longjmp to the nearest error context */
void nova_syntax_error(int line, const char *fmt, ...) __attribute__((noreturn));
void nova_runtime_error(int line, const char *fmt, ...) __attribute__((noreturn));
void nova_type_error(int line, const char *fmt, ...) __attribute__((noreturn));
void nova_name_error(int line, const char *fmt, ...) __attribute__((noreturn));

/* Format an error message without longjmp */
void nova_format_error(NovaErrorType type, int line, char *buf, size_t buf_size,
                       const char *fmt, ...);

#endif
