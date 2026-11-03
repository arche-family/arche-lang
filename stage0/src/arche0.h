// Shared declarations of stage0, the Arche-0 → C11 translator.
//
// Pipeline (main.c): read source → lex (lexer.c) → parse (parser.c) →
// check (check.c, with types.c) → emit C (emit.c).
// Memory comes from an arena and is never freed: stage0 runs once and exits.
// The first error ends the run: stage0 reports file, line and column only.

#ifndef ARCHE0_H
#define ARCHE0_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define A0_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define A0_PRINTF(fmt, args)
#endif

// ---- memory and text (common.c) -------------------------------------------

void *arena_alloc(size_t size); // zeroed
char *arena_strndup(const char *s, size_t n);
char *arena_printf(const char *fmt, ...) A0_PRINTF(1, 2);
#define NEW(T) ((T *)arena_alloc(sizeof(T)))

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_add(StrBuf *b, const char *s, size_t n);
void sb_puts(StrBuf *b, const char *s);
void sb_printf(StrBuf *b, const char *fmt, ...) A0_PRINTF(2, 3);

typedef struct {
    void **items;
    size_t len;
    size_t cap;
} Vec;

void vec_push(Vec *v, void *item);
#define VEC_AT(v, T, i) ((T *)(v).items[i])

// Length (1-4) of the UTF-8 sequence at s, or 0 if it is not well-formed.
size_t utf8_decode(const uint8_t *s, size_t n, uint32_t *cp);

// ---- sources and diagnostics (common.c) -------------------------------------

typedef struct {
    const char *path;    // as given on the command line; used in diagnostics
    const char *display; // relative to the package root; used in panics
    const char *text;
    size_t len;
} SourceFile;

typedef struct {
    const SourceFile *file;
    uint32_t line;
    uint32_t col; // bytes, from 1
} Loc;

_Noreturn void fatal_at(Loc loc, const char *fmt, ...) A0_PRINTF(2, 3);
_Noreturn void fatal(const char *fmt, ...) A0_PRINTF(1, 2);

// ---- tokens (lexer.c) -------------------------------------------------------

typedef enum {
    T_EOF,
    T_IDENT,
    T_INT,
    T_CHAR,
    T_STRING,
    // keywords
    T_AND,
    T_AS,
    T_AS_WRAP, // as%
    T_BREAK,
    T_CATCH,
    T_CONST,
    T_CONTINUE,
    T_DEFER,
    T_ELSE,
    T_ENUM,
    T_ERR,
    T_ERROR,
    T_FALSE,
    T_FN,
    T_IF,
    T_IMPORT,
    T_LET,
    T_MATCH,
    T_MUT,
    T_NONE,
    T_NOT,
    T_OK,
    T_OR,
    T_ORELSE,
    T_PUB,
    T_RETURN,
    T_SOME,
    T_STRUCT,
    T_TEST,
    T_TRUE,
    T_TRY,
    T_TYPE,
    T_VAR,
    T_WHILE,
    // operators and punctuation
    T_PLUS,
    T_MINUS,
    T_STAR,
    T_SLASH,
    T_PERCENT,
    T_PLUS_WRAP,
    T_MINUS_WRAP,
    T_STAR_WRAP,
    T_AMP,
    T_PIPE,
    T_CARET,
    T_TILDE,
    T_SHL,
    T_SHR,
    T_EQ_EQ,
    T_NOT_EQ,
    T_LT,
    T_LE,
    T_GT,
    T_GE,
    T_ASSIGN,
    T_PLUS_ASSIGN,
    T_MINUS_ASSIGN,
    T_STAR_ASSIGN,
    T_SLASH_ASSIGN,
    T_PERCENT_ASSIGN,
    T_PLUS_WRAP_ASSIGN,
    T_MINUS_WRAP_ASSIGN,
    T_STAR_WRAP_ASSIGN,
    T_AMP_ASSIGN,
    T_PIPE_ASSIGN,
    T_CARET_ASSIGN,
    T_SHL_ASSIGN,
    T_SHR_ASSIGN,
    T_DOT,
    T_COMMA,
    T_COLON,
    T_SEMI,
    T_ARROW,
    T_FAT_ARROW,
    T_DOT_DOT,
    T_DOT_DOT_EQ,
    T_DOT_STAR,
    T_QUESTION,
    T_BANG,
    T_UNDERSCORE,
    T_LPAREN,
    T_RPAREN,
    T_LBRACKET,
    T_RBRACKET,
    T_LBRACE,
    T_RBRACE,
    T_COUNT
} TokKind;

typedef struct {
    TokKind kind;
    Loc loc;
    const char *text; // the token's source text
    size_t text_len;
    uint64_t value;    // T_INT, T_CHAR
    const char *bytes; // T_STRING: the decoded bytes, valid UTF-8
    size_t bytes_len;
} Token;

// Tokens of the whole file, ending with T_EOF. Doc comments are dropped:
// stage0 does not use them.
Token *lex_file(const SourceFile *f);
const char *tok_kind_name(TokKind k);

// ---- syntax (parser.c) ------------------------------------------------------

typedef struct TypeExpr TypeExpr;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Block Block;
typedef struct FnDecl FnDecl;
typedef struct Module Module;
typedef struct Type Type;
typedef struct Binding Binding;
typedef struct Method Method;

typedef enum { TE_PATH, TE_REF, TE_OPTIONAL, TE_ERROR_UNION, TE_SLICE, TE_ARRAY } TypeExprKind;

struct TypeExpr {
    TypeExprKind kind;
    Loc loc;
    Vec segments;   // TE_PATH: const char *
    Vec args;       // TE_PATH: TypeExpr *, the generic arguments
    bool mut;       // TE_REF, TE_SLICE
    TypeExpr *elem; // TE_REF, TE_OPTIONAL, TE_ERROR_UNION, TE_SLICE, TE_ARRAY
    TypeExpr *set;  // TE_ERROR_UNION: NULL when the set is inferred
    Expr *len;      // TE_ARRAY
};

typedef enum { E_INT, E_CHAR, E_STRING, E_BOOL, E_IDENT, E_FIELD, E_CALL, E_REF } ExprKind;

typedef struct {
    const char *label; // NULL when unlabeled
    Loc loc;
    Expr *value;
} Arg;

// What an identifier names, set by the checker.
typedef enum { NAME_NONE, NAME_BINDING, NAME_FN, NAME_MODULE, NAME_BUILTIN_FN } NameKind;

typedef enum { CALL_FN, CALL_BUILTIN_FN, CALL_METHOD } CallKind;

typedef enum { BUILTIN_PANIC, BUILTIN_ASSERT, BUILTIN_UNREACHABLE } BuiltinFn;

struct Expr {
    ExprKind kind;
    Loc loc;
    Type *type; // set by the checker

    uint64_t value;    // E_INT, E_CHAR; E_BOOL: 0 or 1
    const char *bytes; // E_STRING
    size_t bytes_len;
    const char *name; // E_IDENT, E_FIELD
    Expr *base;       // E_FIELD: the value; E_REF: the place
    bool mut;         // E_REF
    Expr *callee;     // E_CALL
    Vec args;         // E_CALL: Arg *

    // resolution, set by the checker
    NameKind name_kind;
    Binding *binding;     // NAME_BINDING
    FnDecl *fn;           // NAME_FN, CALL_FN
    BuiltinFn builtin_fn; // NAME_BUILTIN_FN, CALL_BUILTIN_FN
    CallKind call;
    const Method *method; // CALL_METHOD
    Expr *receiver;       // CALL_METHOD
};

typedef enum { S_EXPR, S_RETURN, S_BLOCK } StmtKind;

struct Stmt {
    StmtKind kind;
    Loc loc;
    Expr *expr;   // S_EXPR; S_RETURN (NULL for a bare return)
    Block *block; // S_BLOCK
};

struct Block {
    Loc loc;
    Vec stmts; // Stmt *
};

struct Binding {
    const char *name;
    Loc loc;
    Type *type;
    bool mutable;
};

typedef struct {
    const char *name;
    Loc loc;
    TypeExpr *type_expr;
    Binding *binding; // set by the checker
} Param;

struct FnDecl {
    const char *name;
    Loc loc;
    bool pub;
    Vec params;         // Param *
    TypeExpr *ret_expr; // NULL: returns void
    Block *body;
    Module *module;
    Type *ret;          // set by the checker
    const char *c_name; // set by the checker
};

typedef struct {
    Vec path; // const char *
    const char *alias;
    Loc loc;
} Import;

struct Module {
    const char *name; // dotted module path; the root module is "main"
    const SourceFile *file;
    Vec imports; // Import *
    Vec fns;     // FnDecl *
};

Module *parse_module(const SourceFile *f, const char *name);

// ---- types (types.c) --------------------------------------------------------

typedef enum { TY_VOID, TY_NORETURN, TY_BOOL, TY_INT, TY_STR, TY_REF, TY_ERROR_UNION, TY_BUILTIN } TypeKind;

typedef enum { I8, I16, I32, I64, ISIZE, U8, U16, U32, U64, USIZE, INT_KIND_COUNT } IntKind;

typedef struct BuiltinType BuiltinType;

// Types are interned: two types are equal exactly when their pointers are.
struct Type {
    TypeKind kind;
    IntKind int_kind;            // TY_INT
    bool mut;                    // TY_REF
    Type *elem;                  // TY_REF, TY_ERROR_UNION
    const BuiltinType *builtin;  // TY_BUILTIN

    Type *ref_cache[2];          // &T and &mut T of this type
    Type *error_union_cache;     // !T of this type
};

Type *type_void(void);
Type *type_noreturn(void);
Type *type_bool(void);
Type *type_str(void);
Type *type_int(IntKind k);
Type *type_ref(bool mut, Type *elem);
Type *type_error_union(Type *elem); // with an inferred error set
const char *type_name(const Type *t);

typedef enum { SELF_VALUE, SELF_REF, SELF_MUT_REF } SelfMode;

typedef struct {
    const char *name;
    Type *type;
} BuiltinParam;

// A method the runtime implements.
struct Method {
    const char *name;
    SelfMode self;
    int nparams; // not counting self
    BuiltinParam params[3];
    Type *ret;
    const char *c_name;
    bool needs_loc; // the runtime function takes the call's location: it can trap
};

typedef struct {
    const char *name;
    Type *type;
    const char *c_name;
} Field;

struct BuiltinType {
    const char *module;
    const char *name;
    const char *c_name;
    Type *type;
    int nfields;
    Field fields[5];
    int nmethods;
    Method methods[8];
};

const BuiltinType *builtin_type_lookup(const char *module, const char *name);
const Method *method_lookup(const Type *t, const char *name);
const Field *field_lookup(const Type *t, const char *name);
bool builtin_fn_lookup(const char *name, BuiltinFn *out);
bool int_type_lookup(const char *name, IntKind *out);

// ---- checking and emission ---------------------------------------------------

void check_program(Module *root);
bool expr_is_place(const Expr *e);
void emit_program(Module *root, StrBuf *out);

#endif
