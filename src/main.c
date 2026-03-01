#include "nova.h"
#include <signal.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

/* ── ANSI color codes ── */
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_CYAN    "\033[36m"
#define C_DIM     "\033[2m"

static char *get_exe_dir(const char *argv0) {
    char *path = realpath(argv0, NULL);
    if (!path) path = strdup(argv0);
    char *dir = strdup(dirname(path));
    free(path);
    return dir;
}

/* ── Read file helper ── */
static char *read_file_source(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *source = malloc(size + 1);
    size_t rd = fread(source, 1, size, f);
    source[rd] = '\0';
    fclose(f);
    return source;
}

/* ── Run a file ── */
static int run_file(const char *path, const char *exe_dir) {
    char *source = read_file_source(path);
    if (!source) {
        fprintf(stderr, C_RED "Error: File '%s' not found" C_RESET "\n", path);
        return 1;
    }

    /* Skip shebang line */
    char *code = source;
    if (code[0] == '#' && code[1] == '!') {
        while (*code && *code != '\n') code++;
        if (*code == '\n') code++;
    }

    char *path_copy = strdup(path);
    char *base_dir = realpath(dirname(path_copy), NULL);
    if (!base_dir) base_dir = strdup(dirname(path_copy));
    free(path_copy);

    ErrorContext err_ctx;
    err_ctx.prev = nova_error_ctx;
    nova_error_ctx = &err_ctx;

    if (setjmp(err_ctx.jump)) {
        fprintf(stderr, C_RED "%s" C_RESET "\n", err_ctx.message);
        free(source);
        free(base_dir);
        nova_error_ctx = err_ctx.prev;
        return 1;
    }

    Lexer lexer;
    nova_lexer_init(&lexer, code, path);
    int token_count;
    nova_lexer_tokenize(&lexer, &token_count);

    Parser parser;
    nova_parser_init(&parser, lexer.tokens, token_count);
    AstNode *program = nova_parse(&parser);

    ModuleLoader loader;
    nova_module_loader_init(&loader, base_dir, exe_dir);
    loader.project_root = strdup(base_dir);

    Interpreter interp;
    nova_interpreter_init(&interp, &loader);
    nova_interpret_program(&interp, program);

    nova_error_ctx = err_ctx.prev;
    nova_interpreter_free(&interp);
    nova_module_loader_free(&loader);
    nova_parser_free(&parser);
    nova_lexer_free(&lexer);
    free(source);
    free(base_dir);
    return 0;
}

/* ── Watch mode ── */
static time_t get_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_mtime;
}

static void run_watch(const char *path, const char *exe_dir) {
    printf(C_CYAN "Watching " C_BOLD "%s" C_RESET C_CYAN " for changes..." C_RESET "\n", path);
    time_t last_mtime = 0;

    while (1) {
        time_t mtime = get_mtime(path);
        if (mtime != last_mtime) {
            last_mtime = mtime;
            printf(C_DIM "── run ──────────────────────────────────" C_RESET "\n");
            run_file(path, exe_dir);
            printf(C_DIM "── done ─────────────────────────────────" C_RESET "\n\n");
        }
        usleep(500000); /* 500ms */
    }
}

/* ── Test runner ── */
static int run_test_file(const char *path, const char *exe_dir, int *passed, int *failed) {
    char *source = read_file_source(path);
    if (!source) return 0;

    /* Skip shebang */
    char *code = source;
    if (code[0] == '#' && code[1] == '!') {
        while (*code && *code != '\n') code++;
        if (*code == '\n') code++;
    }

    char *path_copy = strdup(path);
    char *base_dir = realpath(dirname(path_copy), NULL);
    if (!base_dir) base_dir = strdup(dirname(path_copy));
    free(path_copy);

    ErrorContext err_ctx;
    err_ctx.prev = nova_error_ctx;
    nova_error_ctx = &err_ctx;

    if (setjmp(err_ctx.jump)) {
        fprintf(stderr, C_RED "  FAIL" C_RESET " %s: %s\n", path, err_ctx.message);
        (*failed)++;
        free(source);
        free(base_dir);
        nova_error_ctx = err_ctx.prev;
        return 0;
    }

    Lexer lexer;
    nova_lexer_init(&lexer, code, path);
    int token_count;
    nova_lexer_tokenize(&lexer, &token_count);

    Parser parser;
    nova_parser_init(&parser, lexer.tokens, token_count);
    AstNode *program = nova_parse(&parser);

    ModuleLoader loader;
    nova_module_loader_init(&loader, base_dir, exe_dir);
    loader.project_root = strdup(base_dir);

    Interpreter interp;
    nova_interpreter_init(&interp, &loader);
    nova_interpret_program(&interp, program);

    printf(C_GREEN "  PASS" C_RESET " %s\n", path);
    (*passed)++;

    nova_error_ctx = err_ctx.prev;
    nova_interpreter_free(&interp);
    nova_module_loader_free(&loader);
    nova_parser_free(&parser);
    nova_lexer_free(&lexer);
    free(source);
    free(base_dir);
    return 1;
}

static void run_tests(const char *dir, const char *exe_dir) {
    printf(C_BOLD "\nNova Test Runner" C_RESET "\n");
    printf(C_DIM "────────────────────────────────────────" C_RESET "\n");

    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, C_RED "Cannot open directory '%s'" C_RESET "\n", dir);
        return;
    }

    int passed = 0, failed = 0, total = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        int namelen = strlen(ent->d_name);
        /* Match *_test.nova files */
        if (namelen > 10 &&
            strcmp(ent->d_name + namelen - 5, ".nova") == 0 &&
            memcmp(ent->d_name + namelen - 10, "_test", 5) == 0) {
            char filepath[1024];
            snprintf(filepath, sizeof(filepath), "%s/%s", dir, ent->d_name);
            total++;
            run_test_file(filepath, exe_dir, &passed, &failed);
        }
    }
    closedir(d);

    printf(C_DIM "────────────────────────────────────────" C_RESET "\n");
    if (total == 0) {
        printf(C_YELLOW "No test files found" C_RESET " (looking for *_test.nova in %s)\n", dir);
    } else {
        printf(C_BOLD "%d" C_RESET " tests: ", total);
        if (passed > 0) printf(C_GREEN "%d passed" C_RESET, passed);
        if (passed > 0 && failed > 0) printf(", ");
        if (failed > 0) printf(C_RED "%d failed" C_RESET, failed);
        printf("\n");
    }
}

/* ── Project initializer ── */
static void init_project(const char *name) {
    char dir[1024] = ".";
    bool in_subdir = false;

    if (name) {
        snprintf(dir, sizeof(dir), "%s", name);
        if (mkdir(dir, 0755) != 0) {
            fprintf(stderr, C_RED "Error: Could not create directory '%s'" C_RESET "\n", dir);
            return;
        }
        in_subdir = true;
    }

    /* Create main.nova */
    char main_path[1024];
    snprintf(main_path, sizeof(main_path), "%s/main.nova", dir);
    FILE *f = fopen(main_path, "w");
    if (!f) {
        fprintf(stderr, C_RED "Error: Could not create main.nova" C_RESET "\n");
        return;
    }
    fprintf(f, "# %s — A Nova project\n\n", name ? name : "my-project");
    fprintf(f, "print(\"Hello, Nova!\")\n");
    fclose(f);

    /* Create nova.json */
    char json_path[1024];
    snprintf(json_path, sizeof(json_path), "%s/nova.json", dir);
    f = fopen(json_path, "w");
    if (!f) {
        fprintf(stderr, C_RED "Error: Could not create nova.json" C_RESET "\n");
        return;
    }
    fprintf(f, "{\n");
    fprintf(f, "    \"name\": \"%s\",\n", name ? name : "my-project");
    fprintf(f, "    \"version\": \"0.1.0\",\n");
    fprintf(f, "    \"main\": \"main.nova\",\n");
    fprintf(f, "    \"test_dir\": \"tests\",\n");
    fprintf(f, "    \"dependencies\": {}\n");
    fprintf(f, "}\n");
    fclose(f);

    if (in_subdir)
        printf(C_GREEN "Created project '%s/'" C_RESET "\n", name);
    else
        printf(C_GREEN "Initialized project in current directory" C_RESET "\n");
    printf("  %s/main.nova\n", dir);
    printf("  %s/nova.json\n", dir);
    printf("\nRun with: " C_BOLD "nova %s/main.nova" C_RESET "\n", dir);
}

/* ── Formatter ── */

/* Check if a keyword at position pos in a line is outside strings and brackets */
static bool keyword_at(const char *line, int len, int pos, const char *kw, int kwlen) {
    if (pos + kwlen > len) return false;
    if (strncmp(line + pos, kw, kwlen) != 0) return false;
    /* Must be at word boundary */
    if (pos + kwlen < len && isalnum((unsigned char)line[pos + kwlen])) return false;
    if (pos > 0 && (isalnum((unsigned char)line[pos - 1]) || line[pos - 1] == '_')) return false;

    /* Check we're not inside a string or bracket */
    bool in_string = false;
    int brackets = 0; /* [], {} depth */
    for (int i = 0; i < pos; i++) {
        char c = line[i];
        if (c == '"' && (i == 0 || line[i - 1] != '\\')) {
            in_string = !in_string;
        } else if (!in_string) {
            if (c == '[' || c == '{') brackets++;
            else if (c == ']' || c == '}') { if (brackets > 0) brackets--; }
        }
    }
    return !in_string && brackets == 0;
}

/* Check if line starts with a keyword (first non-comment token) */
static bool line_starts_with_keyword(const char *start, int len, const char *kw, int kwlen) {
    return keyword_at(start, len, 0, kw, kwlen);
}

/* Check if line ends with "do" outside strings/brackets */
static bool line_ends_with_do(const char *start, int len) {
    if (len < 2) return false;
    /* Check if last word is "do" */
    int pos = len - 2;
    if (strncmp(start + pos, "do", 2) != 0) return false;
    if (pos > 0 && (isalnum((unsigned char)start[pos - 1]) || start[pos - 1] == '_')) return false;
    return keyword_at(start, len, pos, "do", 2);
}

static void format_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, C_RED "Cannot open '%s'" C_RESET "\n", path);
        return;
    }

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc(size + 1);
    size_t rd = fread(src, 1, size, f);
    src[rd] = '\0';
    fclose(f);

    /* Format line by line */
    int cap = size * 2 + 256;
    char *out = malloc(cap);
    int out_len = 0;
    int indent = 0;
    int blank_count = 0;

    char *line = src;
    while (*line) {
        /* Find end of line */
        char *eol = strchr(line, '\n');
        int len = eol ? (int)(eol - line) : (int)strlen(line);

        /* Trim leading/trailing whitespace */
        char *start = line;
        while (start < line + len && (*start == ' ' || *start == '\t')) start++;
        char *end = line + len - 1;
        while (end >= start && (*end == ' ' || *end == '\t')) end--;
        int trimmed_len = (end >= start) ? (int)(end - start + 1) : 0;

        if (trimmed_len == 0) {
            blank_count++;
            if (blank_count <= 2) {
                while (out_len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
                out[out_len++] = '\n';
            }
            line = eol ? eol + 1 : line + len;
            continue;
        }

        blank_count = 0;

        /* Skip comment lines — don't parse keywords in them */
        bool is_comment = (start[0] == '#');

        /* Check if line starts with dedent keyword */
        bool dedent = false;
        if (!is_comment) {
            if (line_starts_with_keyword(start, trimmed_len, "done", 4))
                dedent = true;
            else if (line_starts_with_keyword(start, trimmed_len, "else", 4))
                dedent = true;
            else if (line_starts_with_keyword(start, trimmed_len, "catch", 5))
                dedent = true;
        }

        if (dedent && indent > 0) indent--;

        /* Write indentation + line */
        int indent_size = indent * 4;
        while (out_len + indent_size + trimmed_len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
        for (int i = 0; i < indent_size; i++) out[out_len++] = ' ';
        memcpy(out + out_len, start, trimmed_len);
        out_len += trimmed_len;
        out[out_len++] = '\n';

        /* Check if line ends with "do" keyword outside strings/brackets */
        if (!is_comment && line_ends_with_do(start, trimmed_len))
            indent++;

        line = eol ? eol + 1 : line + len;
    }

    /* Ensure final newline */
    if (out_len > 0 && out[out_len - 1] != '\n') out[out_len++] = '\n';
    out[out_len] = '\0';

    /* Write back */
    f = fopen(path, "w");
    if (f) {
        fwrite(out, 1, out_len, f);
        fclose(f);
        printf(C_GREEN "  formatted" C_RESET " %s\n", path);
    }

    free(src);
    free(out);
}

static void format_path(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, C_RED "Cannot access '%s'" C_RESET "\n", path);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return;
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            int nlen = strlen(ent->d_name);
            if (nlen > 5 && strcmp(ent->d_name + nlen - 5, ".nova") == 0) {
                char filepath[1024];
                snprintf(filepath, sizeof(filepath), "%s/%s", path, ent->d_name);
                format_file(filepath);
            }
        }
        closedir(d);
    } else {
        format_file(path);
    }
}

/* ── Package installer ── */
static void install_package(const char *url) {
    printf(C_CYAN "Installing package from " C_BOLD "%s" C_RESET "\n", url);

    /* Create nova_packages directory */
    mkdir("nova_packages", 0755);

    /* Extract repo name from URL */
    const char *last_slash = strrchr(url, '/');
    if (!last_slash) { fprintf(stderr, C_RED "Invalid URL" C_RESET "\n"); return; }
    char name[256];
    strncpy(name, last_slash + 1, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    /* Remove .git suffix if present */
    char *git_ext = strstr(name, ".git");
    if (git_ext) *git_ext = '\0';

    char cmd[2048];
    char pkg_dir[1024];
    snprintf(pkg_dir, sizeof(pkg_dir), "nova_packages/%s", name);

    struct stat st;
    if (stat(pkg_dir, &st) == 0) {
        printf(C_YELLOW "Package '%s' already installed, updating..." C_RESET "\n", name);
        snprintf(cmd, sizeof(cmd), "cd '%s' && git pull --quiet 2>&1", pkg_dir);
    } else {
        snprintf(cmd, sizeof(cmd), "git clone --quiet '%s' '%s' 2>&1", url, pkg_dir);
    }

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, C_RED "Failed to install package" C_RESET "\n");
        return;
    }
    printf(C_GREEN "Installed '%s' to nova_packages/%s" C_RESET "\n", name, name);

    /* Check for transitive dependencies in the package's nova.json */
    char json_path[1024];
    snprintf(json_path, sizeof(json_path), "%s/nova.json", pkg_dir);
    char *src = read_file_source(json_path);
    if (src) {
        /* Look for "dependencies": { "name": "url", ... } */
        char *deps = strstr(src, "\"dependencies\"");
        if (deps) {
            char *brace = strchr(deps, '{');
            if (brace) {
                char *end_brace = strchr(brace, '}');
                if (end_brace) {
                    /* Parse each "name": "url" pair */
                    char *p = brace + 1;
                    while (p < end_brace) {
                        /* Find key */
                        char *q1 = strchr(p, '"');
                        if (!q1 || q1 >= end_brace) break;
                        char *q2 = strchr(q1 + 1, '"');
                        if (!q2 || q2 >= end_brace) break;
                        /* Find value (the URL) */
                        char *v1 = strchr(q2 + 1, '"');
                        if (!v1 || v1 >= end_brace) break;
                        char *v2 = strchr(v1 + 1, '"');
                        if (!v2 || v2 >= end_brace) break;

                        /* Extract URL */
                        size_t url_len = v2 - v1 - 1;
                        char dep_url[1024];
                        if (url_len < sizeof(dep_url)) {
                            memcpy(dep_url, v1 + 1, url_len);
                            dep_url[url_len] = '\0';

                            /* Check if already installed */
                            const char *dep_slash = strrchr(dep_url, '/');
                            if (dep_slash) {
                                char dep_name[256];
                                strncpy(dep_name, dep_slash + 1, sizeof(dep_name) - 1);
                                dep_name[sizeof(dep_name) - 1] = '\0';
                                char *dep_git = strstr(dep_name, ".git");
                                if (dep_git) *dep_git = '\0';

                                char dep_dir[1024];
                                snprintf(dep_dir, sizeof(dep_dir), "nova_packages/%s", dep_name);
                                struct stat dep_st;
                                if (stat(dep_dir, &dep_st) != 0) {
                                    install_package(dep_url);
                                }
                            }
                        }
                        p = v2 + 1;
                    }
                }
            }
        }
        free(src);
    }
}

/* ── REPL ── */
static volatile sig_atomic_t got_sigint = 0;

static void sigint_handler(int sig) {
    got_sigint = 1;
}

static void repl(const char *exe_dir) {
    printf(C_BOLD C_CYAN "Nova" C_RESET " REPL v0.2 " C_DIM "\xe2\x80\x94 Type 'exit' or Ctrl+D to quit" C_RESET "\n");

    /* Open history file for appending */
    char history_path[1024];
    const char *home = getenv("HOME");
    FILE *history_file = NULL;
    if (home) {
        snprintf(history_path, sizeof(history_path), "%s/.nova_history", home);
        history_file = fopen(history_path, "a");
    }

    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, ".");

    ModuleLoader loader;
    nova_module_loader_init(&loader, cwd, exe_dir);
    loader.project_root = strdup(cwd);

    Interpreter interp;
    nova_interpreter_init(&interp, &loader);
    nova_push_env(&interp, interp.globals);

    signal(SIGINT, sigint_handler);

    while (1) {
        got_sigint = 0;
        printf(C_BOLD C_GREEN ">>> " C_RESET);
        fflush(stdout);

        char line[4096];
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        if (got_sigint) {
            printf("\n");
            continue;
        }

        int len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';

        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (strcmp(trimmed, "exit") == 0 || strcmp(trimmed, "quit") == 0) break;
        if (*trimmed == '\0') continue;

        /* Append to history */
        if (history_file) {
            fprintf(history_file, "%s\n", trimmed);
            fflush(history_file);
        }

        /* Multi-line block support */
        char source[65536];
        strncpy(source, line, sizeof(source));
        source[sizeof(source)-1] = '\0';

        int depth = 0;
        char *word = strtok(strdup(line), " \t\n\r");
        while (word) {
            if (strcmp(word, "do") == 0) depth++;
            else if (strcmp(word, "done") == 0) depth--;
            word = strtok(NULL, " \t\n\r");
        }

        while (depth > 0) {
            printf(C_BOLD C_YELLOW "... " C_RESET);
            fflush(stdout);
            char cont[4096];
            if (!fgets(cont, sizeof(cont), stdin)) {
                printf("\n");
                break;
            }
            int clen = strlen(cont);
            if (clen > 0 && cont[clen-1] == '\n') cont[--clen] = '\0';

            strncat(source, "\n", sizeof(source) - strlen(source) - 1);
            strncat(source, cont, sizeof(source) - strlen(source) - 1);

            word = strtok(strdup(cont), " \t\n\r");
            while (word) {
                if (strcmp(word, "do") == 0) depth++;
                else if (strcmp(word, "done") == 0) depth--;
                word = strtok(NULL, " \t\n\r");
            }
        }

        if (depth > 0) continue;

        /* Execute */
        ErrorContext err_ctx;
        err_ctx.prev = nova_error_ctx;
        nova_error_ctx = &err_ctx;

        if (setjmp(err_ctx.jump)) {
            printf(C_RED "%s" C_RESET "\n", err_ctx.message);
            nova_error_ctx = err_ctx.prev;
            continue;
        }

        Lexer lexer;
        nova_lexer_init(&lexer, source, "<stdin>");
        int token_count;
        nova_lexer_tokenize(&lexer, &token_count);

        Parser parser;
        nova_parser_init(&parser, lexer.tokens, token_count);
        AstNode *program = nova_parse(&parser);

        NovaValue result = NOVA_NONE();
        AstNode *last_stmt = NULL;
        for (int i = 0; i < program->as.program.count; i++) {
            last_stmt = program->as.program.stmts[i];
            result = nova_interpret(&interp, last_stmt);
        }

        /* Display result for bare expressions */
        if (last_stmt && !IS_NONE(result) &&
            last_stmt->type != AST_LET_DECLARATION &&
            last_stmt->type != AST_ASSIGNMENT &&
            last_stmt->type != AST_IF_STATEMENT &&
            last_stmt->type != AST_WHILE_STATEMENT &&
            last_stmt->type != AST_FOR_STATEMENT &&
            last_stmt->type != AST_FUNCTION_DEF &&
            last_stmt->type != AST_CLASS_DEF &&
            last_stmt->type != AST_RETURN_STATEMENT &&
            last_stmt->type != AST_IMPORT_STATEMENT &&
            last_stmt->type != AST_TRY_CATCH) {
            char *s = nova_display(result);
            printf(C_CYAN "%s" C_RESET "\n", s);
            free(s);
        }

        nova_error_ctx = err_ctx.prev;
        nova_parser_free(&parser);
        nova_lexer_free(&lexer);
    }

    if (history_file) fclose(history_file);
    nova_pop_env(&interp);
    nova_interpreter_free(&interp);
    nova_module_loader_free(&loader);
}

/* ── nova.json reader ── */
static bool read_nova_json(const char *dir, char *main_file, size_t main_size,
                           char *test_dir, size_t test_size) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/nova.json", dir);
    char *src = read_file_source(path);
    if (!src) return false;

    main_file[0] = '\0';
    test_dir[0] = '\0';

    /* Simple JSON field extraction (no full parser needed) */
    char *p;
    if ((p = strstr(src, "\"main\"")) != NULL) {
        p = strchr(p + 6, '"');
        if (p) {
            p++; /* skip opening quote */
            char *end = strchr(p, '"');
            if (end) {
                size_t len = end - p;
                if (len < main_size) {
                    memcpy(main_file, p, len);
                    main_file[len] = '\0';
                }
            }
        }
    }
    if ((p = strstr(src, "\"test_dir\"")) != NULL) {
        p = strchr(p + 10, '"');
        if (p) {
            p++;
            char *end = strchr(p, '"');
            if (end) {
                size_t len = end - p;
                if (len < test_size) {
                    memcpy(test_dir, p, len);
                    test_dir[len] = '\0';
                }
            }
        }
    }
    free(src);
    return true;
}

/* ── Usage ── */
static void print_usage(const char *prog) {
    printf(C_BOLD "Nova" C_RESET " — A modern, elegant programming language\n\n");
    printf(C_BOLD "Usage:" C_RESET "\n");
    printf("  %s                     Start the REPL\n", prog);
    printf("  %s <file.nova>         Run a Nova script\n", prog);
    printf("  %s run <file.nova>     Run a Nova script\n", prog);
    printf("  %s run --watch <file>  Run and re-run on changes\n", prog);
    printf("  %s test [dir]          Run *_test.nova files\n", prog);
    printf("  %s init [name]         Create a new Nova project\n", prog);
    printf("  %s fmt <file|dir>      Format Nova source files\n", prog);
    printf("  %s install <url>       Install a package from git\n", prog);
    printf("  %s help                Show this help\n", prog);
    printf("  %s version             Show version info\n", prog);
}

/* ── Main ── */
int main(int argc, char **argv) {
    nova_gc_init();

    char *exe_dir = get_exe_dir(argv[0]);

    if (argc < 2) {
        repl(exe_dir);
    } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 ||
               strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
    } else if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("Nova v0.2.0\n");
    } else if (strcmp(argv[1], "run") == 0) {
        bool watch = false;
        const char *file = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--watch") == 0 || strcmp(argv[i], "-w") == 0)
                watch = true;
            else
                file = argv[i];
        }
        /* If no file given, try nova.json */
        char json_main[256];
        char json_test[256];
        if (!file) {
            if (read_nova_json(".", json_main, sizeof(json_main), json_test, sizeof(json_test)) && json_main[0]) {
                file = json_main;
            } else {
                fprintf(stderr, C_RED "No file specified and no nova.json found" C_RESET "\n");
                free(exe_dir); return 1;
            }
        }
        if (watch) run_watch(file, exe_dir);
        else { int ret = run_file(file, exe_dir); free(exe_dir); nova_gc_shutdown(); return ret; }
    } else if (strcmp(argv[1], "test") == 0) {
        const char *dir = NULL;
        if (argc > 2) {
            dir = argv[2];
        } else {
            /* Try nova.json for test_dir */
            static char json_main[256];
            static char json_test[256];
            if (read_nova_json(".", json_main, sizeof(json_main), json_test, sizeof(json_test)) && json_test[0]) {
                dir = json_test;
            } else {
                dir = ".";
            }
        }
        run_tests(dir, exe_dir);
    } else if (strcmp(argv[1], "init") == 0) {
        init_project(argc > 2 ? argv[2] : NULL);
    } else if (strcmp(argv[1], "fmt") == 0) {
        if (argc < 3) {
            fprintf(stderr, C_RED "Usage: nova fmt <file|dir>" C_RESET "\n");
            free(exe_dir); return 1;
        }
        format_path(argv[2]);
    } else if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, C_RED "Usage: nova install <git-url>" C_RESET "\n");
            free(exe_dir); return 1;
        }
        install_package(argv[2]);
    } else {
        /* Direct file execution */
        int ret = run_file(argv[1], exe_dir);
        free(exe_dir);
        nova_gc_shutdown();
        return ret;
    }

    free(exe_dir);
    nova_gc_shutdown();
    return 0;
}
