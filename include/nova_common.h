#ifndef NOVA_COMMON_H
#define NOVA_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <ctype.h>
#include <math.h>

/* Forward declarations */
typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct ObjList ObjList;
typedef struct ObjDict ObjDict;
typedef struct ObjFunction ObjFunction;
typedef struct ObjClass ObjClass;
typedef struct ObjInstance ObjInstance;
typedef struct ObjBoundMethod ObjBoundMethod;
typedef struct ObjBuiltin ObjBuiltin;
typedef struct NovaTable NovaTable;
typedef struct Environment Environment;
typedef struct AstNode AstNode;
typedef struct Token Token;
typedef struct Lexer Lexer;
typedef struct Parser Parser;
typedef struct Interpreter Interpreter;

#endif
