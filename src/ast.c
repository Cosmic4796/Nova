#include "nova.h"

#define ARENA_BLOCK_SIZE (64 * 1024)  /* 64 KB blocks */

void nova_arena_init(AstArena *arena) {
    arena->buffer = malloc(ARENA_BLOCK_SIZE);
    arena->used = 0;
    arena->capacity = ARENA_BLOCK_SIZE;
    arena->next = NULL;
}

void nova_arena_free(AstArena *arena) {
    AstArena *block = arena->next;
    while (block) {
        AstArena *next = block->next;
        free(block->buffer);
        free(block);
        block = next;
    }
    free(arena->buffer);
    arena->buffer = NULL;
    arena->used = 0;
    arena->capacity = 0;
    arena->next = NULL;
}

void *nova_arena_alloc(AstArena *arena, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~7;

    if (arena->used + size > arena->capacity) {
        /* Allocate a new block */
        size_t block_size = size > ARENA_BLOCK_SIZE ? size : ARENA_BLOCK_SIZE;
        AstArena *new_block = malloc(sizeof(AstArena));
        new_block->buffer = malloc(block_size);
        new_block->used = 0;
        new_block->capacity = block_size;
        new_block->next = arena->next;
        arena->next = new_block;

        void *ptr = new_block->buffer;
        new_block->used = size;
        return ptr;
    }

    void *ptr = arena->buffer + arena->used;
    arena->used += size;
    return ptr;
}

AstNode *nova_ast_new(AstArena *arena, AstNodeType type, int line) {
    AstNode *node = nova_arena_alloc(arena, sizeof(AstNode));
    memset(node, 0, sizeof(AstNode));
    node->type = type;
    node->line = line;
    return node;
}

char *nova_arena_strdup(AstArena *arena, const char *s) {
    int len = strlen(s);
    char *copy = nova_arena_alloc(arena, len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

char *nova_arena_strndup(AstArena *arena, const char *s, int len) {
    char *copy = nova_arena_alloc(arena, len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}
