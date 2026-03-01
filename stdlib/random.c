#include "nova.h"
#include <time.h>

static bool seeded = false;

static void ensure_seeded(void) {
    if (!seeded) {
        srand48(time(NULL));
        seeded = true;
    }
}

static NovaValue rand_random(Interpreter *interp, int argc, NovaValue *argv, int line) {
    ensure_seeded();
    return NOVA_FLOAT(drand48());
}

static NovaValue rand_randint(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_INT(argv[0]) || !IS_INT(argv[1]))
        nova_type_error(line, "randint() expects two integers");
    ensure_seeded();
    int64_t lo = AS_INT(argv[0]);
    int64_t hi = AS_INT(argv[1]);
    if (lo > hi) nova_runtime_error(line, "randint() min must be <= max");
    int64_t range = hi - lo + 1;
    int64_t val = lo + (int64_t)(drand48() * range);
    if (val > hi) val = hi; /* clamp in case of floating point edge */
    return NOVA_INT(val);
}

static NovaValue rand_choice(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "choice() expects a list");
    ObjList *list = AS_LIST(argv[0]);
    if (list->count == 0)
        nova_runtime_error(line, "choice() cannot choose from empty list");
    ensure_seeded();
    int idx = (int)(drand48() * list->count);
    if (idx >= list->count) idx = list->count - 1;
    return list->items[idx];
}

static NovaValue rand_shuffle(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "shuffle() expects a list");
    ObjList *list = AS_LIST(argv[0]);
    ensure_seeded();
    /* Fisher-Yates shuffle in place */
    for (int i = list->count - 1; i > 0; i--) {
        int j = (int)(drand48() * (i + 1));
        if (j > i) j = i;
        NovaValue tmp = list->items[i];
        list->items[i] = list->items[j];
        list->items[j] = tmp;
    }
    return argv[0];
}

static NovaValue rand_seed(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_INT(argv[0]))
        nova_type_error(line, "seed() expects an integer");
    srand48(AS_INT(argv[0]));
    seeded = true;
    return NOVA_NONE();
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "random",  NOVA_OBJ(nova_builtin_new("random",  rand_random,  0)));
    nova_env_define(env, "randint", NOVA_OBJ(nova_builtin_new("randint", rand_randint, 2)));
    nova_env_define(env, "choice",  NOVA_OBJ(nova_builtin_new("choice",  rand_choice,  1)));
    nova_env_define(env, "shuffle", NOVA_OBJ(nova_builtin_new("shuffle", rand_shuffle, 1)));
    nova_env_define(env, "seed",    NOVA_OBJ(nova_builtin_new("seed",    rand_seed,    1)));
}
