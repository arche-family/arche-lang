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
typedef struct Field Field;
typedef struct FieldDecl FieldDecl;
typedef struct VariantDecl VariantDecl;
typedef struct ErrorName ErrorName;

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

typedef struct ConstDecl ConstDecl;
typedef struct TypeDecl TypeDecl;
typedef struct Pattern Pattern;

typedef enum {
    E_INT,
    E_CHAR,
    E_STRING,
    E_BOOL,
    E_NONE,
    E_SOME,
    E_IDENT,
    E_FIELD,
    E_CALL,
    E_INDEX,
    E_SLICE,
    E_DEREF,
    E_STRUCT_LIT,
    E_ARRAY_LIT,
    E_UNARY,
    E_BINARY,
    E_CAST,
    E_TRY,
    E_CATCH,
    E_ORELSE,
    E_REF,
} ExprKind;

typedef enum { U_NEG, U_BITNOT, U_NOT } UnaryOp;

typedef enum {
    B_ADD,
    B_SUB,
    B_MUL,
    B_DIV,
    B_REM,
    B_WADD,
    B_WSUB,
    B_WMUL,
    B_SHL,
    B_SHR,
    B_BITAND,
    B_BITOR,
    B_BITXOR,
    B_EQ,
    B_NE,
    B_LT,
    B_LE,
    B_GT,
    B_GE,
    B_AND,
    B_OR,
} BinaryOp;

typedef struct {
    const char *label; // NULL when unlabeled
    Loc loc;
    Expr *value;
} Arg;

typedef struct {
    const char *name;
    Loc loc;
    Expr *value;
} FieldInit;

// What an identifier names, set by the checker.
typedef enum { NAME_NONE, NAME_BINDING, NAME_CONST, NAME_FN, NAME_MODULE, NAME_BUILTIN_FN } NameKind;

// CALL_INIT: T.init(a) of alloc.List, alloc.Map, alloc.String and alloc.Interner.
typedef enum { CALL_FN, CALL_BUILTIN_FN, CALL_METHOD, CALL_INIT } CallKind;

typedef enum { FIELD_VALUE, FIELD_VARIANT, FIELD_ERROR, FIELD_CONST } FieldUse;

typedef enum { BUILTIN_PANIC, BUILTIN_ASSERT, BUILTIN_UNREACHABLE } BuiltinFn;

typedef enum { SELF_VALUE, SELF_REF, SELF_MUT_REF } SelfMode;

struct Expr {
    ExprKind kind;
    Loc loc;     // binary and cast expressions: the operator
    Type *type;  // set by the checker
    bool parens; // written inside parentheses

    uint64_t value;    // E_INT, E_CHAR; E_BOOL: 0 or 1
    const char *bytes; // E_STRING
    size_t bytes_len;
    const char *name;   // E_IDENT; E_FIELD: the field
    Expr *base;         // the operand of E_FIELD, E_INDEX, E_SLICE, E_DEREF, E_SOME, E_UNARY, E_CAST,
                        // E_TRY and E_REF; the left side of E_BINARY, E_CATCH and E_ORELSE
    Expr *rhs;          // E_BINARY, E_CATCH, E_ORELSE
    Expr *index;        // E_INDEX; E_SLICE: the start
    Expr *end;          // E_SLICE
    bool mut;           // E_REF
    bool wrapping;      // E_CAST: as%
    UnaryOp unary;      // E_UNARY
    BinaryOp binary;    // E_BINARY
    TypeExpr *cast_to;  // E_CAST
    Expr *callee;       // E_CALL
    Vec args;           // E_CALL: Arg *
    Vec elems;          // E_ARRAY_LIT: Expr *
    Expr *type_path;    // E_STRUCT_LIT: the type, an E_IDENT or E_FIELD path
    Vec fields;         // E_STRUCT_LIT: FieldInit *

    // set by the checker
    NameKind name_kind;
    Binding *binding;     // NAME_BINDING
    ConstDecl *konst;     // NAME_CONST
    FnDecl *fn;           // NAME_FN, CALL_FN
    BuiltinFn builtin_fn; // NAME_BUILTIN_FN, CALL_BUILTIN_FN
    CallKind call;
    const Method *method; // CALL_METHOD
    Expr *receiver;       // CALL_METHOD
    bool negative_literal; // E_UNARY: '-' written directly before an integer literal
    FieldUse field_use;    // E_FIELD: what the name after '.' is
    const Field *field;    // FIELD_VALUE
    VariantDecl *variant;  // FIELD_VARIANT; E_STRUCT_LIT of an enum variant
    ErrorName *error;      // FIELD_ERROR
    SelfMode self_mode;    // CALL_FN with a receiver: how `self` is passed
};

typedef enum { S_EXPR, S_LET, S_ASSIGN, S_DISCARD, S_IF, S_WHILE, S_MATCH, S_RETURN, S_BREAK, S_CONTINUE, S_DEFER, S_BLOCK } StmtKind;

// Return lifting (§7.8): RET_VALUE wraps a T as ?T or E!T; RET_ERROR returns an error.
typedef enum { RET_PLAIN, RET_VALUE, RET_ERROR } ReturnKind;

typedef enum { A_SET, A_ADD, A_SUB, A_MUL, A_DIV, A_REM, A_WADD, A_WSUB, A_WMUL, A_BITAND, A_BITOR, A_BITXOR, A_SHL, A_SHR } AssignOp;

typedef struct {
    Vec patterns; // Pattern *: the alternatives
    Block *body;
    Loc loc;
} MatchArm;

struct Stmt {
    StmtKind kind;
    Loc loc;
    Expr *expr;   // S_EXPR, S_DISCARD; S_LET: the initializer; S_ASSIGN: the value; S_IF, S_WHILE: the
                  // condition; S_MATCH: the scrutinee; S_RETURN: NULL for a bare return
    Expr *target; // S_ASSIGN: the place
    AssignOp op;  // S_ASSIGN
    Block *block; // S_BLOCK; S_IF: the then branch; S_WHILE: the body
    Stmt *els;    // S_IF: an S_IF (else if) or an S_BLOCK, or NULL
    Stmt *step;   // S_WHILE: the step, or NULL; S_DEFER: the deferred statement
    Vec arms;     // S_MATCH: MatchArm *

    // S_LET
    const char *name;
    bool mutable; // var
    TypeExpr *type_expr;
    Binding *binding; // set by the checker

    ReturnKind ret_kind; // S_RETURN, set by the checker: how the value becomes the result (§7.8)
};

struct Block {
    Loc loc;
    Vec stmts; // Stmt *
};

typedef enum { P_WILDCARD, P_BIND, P_INT, P_RANGE, P_BOOL, P_NONE, P_SOME, P_OK, P_ERR, P_VARIANT } PatternKind;

typedef struct {
    bool negative;
    uint64_t magnitude;
    bool is_char;
} PatLit;

typedef struct {
    const char *field;
    Loc loc;
    const char *bind;  // the name it binds, or NULL for `_`
    FieldDecl *decl;   // set by the checker
    Binding *binding;  // set by the checker
} FieldPat;

struct Pattern {
    PatternKind kind;
    Loc loc;
    const char *name; // P_BIND, P_VARIANT
    PatLit lo;        // P_INT, P_RANGE
    PatLit hi;        // P_RANGE
    bool value;       // P_BOOL
    Pattern *inner;   // P_SOME, P_OK, P_ERR
    bool has_fields;  // P_VARIANT: written with { }
    Vec field_pats;   // P_VARIANT: FieldPat *
    bool rest;        // P_VARIANT: ends with ..
    VariantDecl *variant; // P_VARIANT of an enum, set by the checker
    ErrorName *error;     // P_VARIANT of an error set, set by the checker
    Binding *binding;     // P_BIND, set by the checker
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
    TypeDecl *owner;    // the type whose body declares it, or NULL
    Type *ret;          // set by the checker
    const char *c_name; // set by the checker
    bool has_self;      // set by the checker: a method, callable as x.name(...) (§4)
    SelfMode self_mode; // has_self: how `self` is passed
};

// A constant's value. Integers are kept as two's complement bits in `bits`.
typedef struct {
    uint64_t bits;
    const char *bytes; // str
    size_t len;
} ConstValue;

struct ConstDecl {
    const char *name;
    Loc loc;
    bool pub;
    TypeExpr *type_expr;
    Expr *value;
    Module *module;
    Type *type;     // set by the checker
    int state;      // 0: not evaluated, 1: being evaluated, 2: done
    ConstValue val; // set by the checker
};

typedef enum { TD_STRUCT, TD_ENUM, TD_ERROR, TD_ALIAS } TypeDeclKind;

struct FieldDecl {
    const char *name;
    Loc loc;
    bool pub;
    TypeExpr *type_expr;
    Type *type; // set by the checker
};

struct VariantDecl {
    const char *name;
    Loc loc;
    bool has_fields;
    Vec fields; // FieldDecl *
    int tag;    // its position: the enum's tag value
};

struct ErrorName {
    const char *name;
    Loc loc;
    int id; // the error's number across the program, from 1 (§5.2: error names are global)
};

struct TypeDecl {
    TypeDeclKind kind;
    const char *name;
    Loc loc;
    bool pub;
    Vec fields;      // TD_STRUCT: FieldDecl *
    Vec variants;    // TD_ENUM: VariantDecl *
    Vec errors;      // TD_ERROR: ErrorName *
    Vec methods;     // TD_STRUCT, TD_ENUM: FnDecl *
    TypeExpr *alias; // TD_ALIAS
    Module *module;
    Type *type; // set by the checker
    int state;  // 0: not resolved, 1: being resolved, 2: done
};

typedef struct {
    Vec path; // const char *
    const char *alias;
    Loc loc;
} Import;

// A name an import binds: a library root of §11, or a module of the package.
typedef struct {
    const char *name;
    Loc loc;
    const char *library; // "std" or "alloc", or NULL
    Module *module;      // when library is NULL
} ModuleAlias;

struct Module {
    const char *name; // dotted module path; "" for a root file whose name is not a path
    const SourceFile *file;
    Vec imports; // Import *
    Vec fns;     // FnDecl *
    Vec consts;  // ConstDecl *
    Vec types;   // TypeDecl *
    Vec aliases; // ModuleAlias *, set by the checker
};

Module *parse_module(const SourceFile *f, const char *name);

// The root module and every module it reaches through imports (main.c).
typedef struct {
    Vec modules; // Module *, the root first
    Module *root;
} Program;

// ---- types (types.c) --------------------------------------------------------

typedef enum {
    TY_VOID,
    TY_NORETURN,
    TY_BOOL,
    TY_INT,
    TY_STR,
    TY_REF,
    TY_SLICE,
    TY_ARRAY,
    TY_LIST,
    TY_MAP,
    TY_OPTIONAL,
    TY_ERROR_UNION,
    TY_ERROR_SET,
    TY_STRUCT,
    TY_ENUM,
    TY_BUILTIN,
} TypeKind;

typedef enum { I8, I16, I32, I64, ISIZE, U8, U16, U32, U64, USIZE, INT_KIND_COUNT } IntKind;

typedef struct BuiltinType BuiltinType;

// Types are interned: two types are equal exactly when their pointers are.
struct Type {
    TypeKind kind;
    IntKind int_kind;           // TY_INT
    bool mut;                   // TY_REF, TY_SLICE
    Type *elem;                 // TY_REF, TY_SLICE, TY_ARRAY, TY_LIST, TY_OPTIONAL, TY_ERROR_UNION;
                                // TY_MAP: the value type
    Type *key;                  // TY_MAP
    uint64_t len;               // TY_ARRAY
    const BuiltinType *builtin; // TY_BUILTIN
    TypeDecl *decl;             // TY_STRUCT, TY_ENUM, TY_ERROR_SET; TY_ERROR_UNION: its error set,
                                // NULL when inferred. An error set with no decl is any error.
    const char *c_name;         // composite types

    Field *fields; // TY_STRUCT: the resolved fields
    int nfields;
    Method *methods; // TY_SLICE, TY_ARRAY, TY_LIST: their built-in methods
    int nmethods;

    unsigned used; // emit.c: which generated helpers the program calls (USE_*)

    Type *ref_cache[2];      // &T and &mut T
    Type *slice_cache[2];    // []T and []mut T
    Type *list_cache;        // alloc.List(T)
    Type *optional_cache;    // ?T
    Vec union_cache;         // E!T for each E seen; the first is !T
    Vec map_cache;           // alloc.Map(K, T) for each K seen
    Vec array_cache;         // [N]T for each N seen
};

Type *type_void(void);
Type *type_noreturn(void);
Type *type_bool(void);
Type *type_str(void);
Type *type_int(IntKind k);
Type *type_ref(bool mut, Type *elem);
Type *type_slice(bool mut, Type *elem);
Type *type_array(Type *elem, uint64_t len);
Type *type_list(Type *elem);
Type *type_map(Type *key, Type *value);
Type *type_optional(Type *elem);
Type *type_error_union(Type *elem);                    // with an inferred error set
Type *type_error_union_of(TypeDecl *set, Type *elem); // set NULL: inferred
Type *type_any_error(void);                            // the error of an inferred set
Type *library_type(const char *library, const char *name); // std.Output, std.IoError, alloc.String...
void types_init(void);
Type *type_declared(TypeKind kind, TypeDecl *decl, const char *c_name);
const char *type_name(const Type *t);
// Every slice, array, list, struct and enum type, in the order they were made.
Vec *composite_types(void);

typedef struct {
    const char *name;
    Type *type;
} BuiltinParam;

typedef enum {
    M_RUNTIME,
    M_LEN,
    M_STR_BYTES,
    M_LIST_PUSH,
    M_LIST_POP,
    M_LIST_CLEAR,
    M_LIST_ITEMS,
    M_LIST_ITEMS_MUT,
    M_MAP_PUT,
    M_MAP_GET,
    M_MAP_REMOVE,
} MethodOp;

// A built-in method.
struct Method {
    const char *name;
    SelfMode self;
    int nparams; // not counting self
    BuiltinParam params[3];
    Type *ret;
    const char *c_name; // M_RUNTIME: the runtime function
    bool needs_loc;     // the runtime function takes the call's location: it can trap
    MethodOp op;
};

struct Field {
    const char *name;
    Type *type;
    const char *c_name;
    bool pub;
};

struct BuiltinType {
    const char *module;
    const char *name;
    const char *c_name;
    Type *type;
    bool has_init; // T.init(a: alloc.Allocator) -> T
    int nfields;
    Field fields[5];
    int nmethods;
    Method methods[8];
};

const BuiltinType *builtin_type_lookup(const char *module, const char *name);
const Method *method_lookup(Type *t, const char *name);
const Field *field_lookup(const Type *t, const char *name);
bool builtin_fn_lookup(const char *name, BuiltinFn *out);
bool int_type_lookup(const char *name, IntKind *out);
bool int_signed(IntKind k);
int int_bits(IntKind k);
int64_t int_min(IntKind k);
uint64_t int_max(IntKind k);
const char *int_suffix(IntKind k);
bool type_comparable(const Type *t); // == and != work on it (§5.5)
bool type_is_key(const Type *t);     // it can be a Map key (§5.5)

// ---- checking and emission ---------------------------------------------------

void check_program(Program *p);
bool expr_is_place(const Expr *e);
bool block_ends(const Block *b); // every path through it ends: return, noreturn, ... (§10)
const char *error_name(int id);  // the error numbered id, from 1
int error_count(void);
void emit_program(Program *p, StrBuf *out);

#endif
