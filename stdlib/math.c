#include <math.h>
#include "nova.h"

static NovaValue math_abs(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (IS_INT(argv[0])) {
        int64_t v = AS_INT(argv[0]);
        return NOVA_INT(v < 0 ? -v : v);
    }
    if (IS_FLOAT(argv[0])) return NOVA_FLOAT(fabs(AS_FLOAT(argv[0])));
    nova_type_error(line, "abs() expects a number");
    return NOVA_NONE();
}

static NovaValue math_sqrt(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double v = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    double result = sqrt(v);
    /* Return int if result is exact integer */
    if (result == (double)(int64_t)result) return NOVA_INT((int64_t)result);
    return NOVA_FLOAT(result);
}

static NovaValue math_floor(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double v = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    return NOVA_INT((int64_t)floor(v));
}

static NovaValue math_ceil(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double v = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    return NOVA_INT((int64_t)ceil(v));
}

static NovaValue math_round_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double v = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    return NOVA_INT((int64_t)round(v));
}

static NovaValue math_pow(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double base = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    double exp = IS_INT(argv[1]) ? (double)AS_INT(argv[1]) : AS_FLOAT(argv[1]);
    double result = pow(base, exp);
    if (IS_INT(argv[0]) && IS_INT(argv[1]) && AS_INT(argv[1]) >= 0 &&
        result == (double)(int64_t)result) {
        return NOVA_INT((int64_t)result);
    }
    return NOVA_FLOAT(result);
}

static NovaValue math_min(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double a = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    double b = IS_INT(argv[1]) ? (double)AS_INT(argv[1]) : AS_FLOAT(argv[1]);
    double r = a < b ? a : b;
    if (IS_INT(argv[0]) && IS_INT(argv[1])) return NOVA_INT((int64_t)r);
    return NOVA_FLOAT(r);
}

static NovaValue math_max(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double a = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    double b = IS_INT(argv[1]) ? (double)AS_INT(argv[1]) : AS_FLOAT(argv[1]);
    double r = a > b ? a : b;
    if (IS_INT(argv[0]) && IS_INT(argv[1])) return NOVA_INT((int64_t)r);
    return NOVA_FLOAT(r);
}

static NovaValue math_sin(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double v = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    return NOVA_FLOAT(sin(v));
}

static NovaValue math_cos(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double v = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    return NOVA_FLOAT(cos(v));
}

static NovaValue math_tan(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double v = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    return NOVA_FLOAT(tan(v));
}

static NovaValue math_log(Interpreter *interp, int argc, NovaValue *argv, int line) {
    double v = IS_INT(argv[0]) ? (double)AS_INT(argv[0]) : AS_FLOAT(argv[0]);
    return NOVA_FLOAT(log(v));
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "PI", NOVA_FLOAT(3.141592653589793));
    nova_env_define(env, "E", NOVA_FLOAT(2.718281828459045));
    nova_env_define(env, "abs", NOVA_OBJ(nova_builtin_new("abs", math_abs, 1)));
    nova_env_define(env, "sqrt", NOVA_OBJ(nova_builtin_new("sqrt", math_sqrt, 1)));
    nova_env_define(env, "floor", NOVA_OBJ(nova_builtin_new("floor", math_floor, 1)));
    nova_env_define(env, "ceil", NOVA_OBJ(nova_builtin_new("ceil", math_ceil, 1)));
    nova_env_define(env, "round", NOVA_OBJ(nova_builtin_new("round", math_round_fn, 1)));
    nova_env_define(env, "pow", NOVA_OBJ(nova_builtin_new("pow", math_pow, 2)));
    nova_env_define(env, "min", NOVA_OBJ(nova_builtin_new("min", math_min, 2)));
    nova_env_define(env, "max", NOVA_OBJ(nova_builtin_new("max", math_max, 2)));
    nova_env_define(env, "sin", NOVA_OBJ(nova_builtin_new("sin", math_sin, 1)));
    nova_env_define(env, "cos", NOVA_OBJ(nova_builtin_new("cos", math_cos, 1)));
    nova_env_define(env, "tan", NOVA_OBJ(nova_builtin_new("tan", math_tan, 1)));
    nova_env_define(env, "log", NOVA_OBJ(nova_builtin_new("log", math_log, 1)));
}
