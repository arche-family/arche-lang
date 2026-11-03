// C emitter.
//
// Every expression is evaluated into a temporary before it is used. C leaves
// the order of argument evaluation unspecified; Arche evaluates left to
// right (§7.3), and temporaries make that order explicit. C compilers remove
// the copies again. Places (lvalues) are emitted as C lvalue expressions.

#include "arche0.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    StrBuf *out;
    int indent;
    int temps;
    const FnDecl *fn;
    Vec files; // const SourceFile *, indexed by a0_file_N
} Emitter;

// ---- helpers ----------------------------------------------------------------

static void line(Emitter *em, const char *fmt, ...) A0_PRINTF(2, 3);

static void line(Emitter *em, const char *fmt, ...) {
    for (int i = 0; i < em->indent; i++) {
        sb_puts(em->out, "    ");
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = malloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb_add(em->out, s, (size_t)n);
    sb_puts(em->out, "\n");
    free(s);
}

static const char *c_type(const Type *t) {
    switch (t->kind) {
    case TY_VOID:
    case TY_NORETURN:
        return "void";
    case TY_BOOL:
        return "bool";
    case TY_INT:
        switch (t->int_kind) {
        case I8:
            return "int8_t";
        case I16:
            return "int16_t";
        case I32:
            return "int32_t";
        case I64:
        case ISIZE:
            return "int64_t";
        case U8:
            return "uint8_t";
        case U16:
            return "uint16_t";
        case U32:
            return "uint32_t";
        case U64:
        case USIZE:
        case INT_KIND_COUNT:
            return "uint64_t";
        }
        break;
    case TY_STR:
        return "a0_str";
    case TY_REF:
        return arena_printf("%s *", c_type(t->elem));
    case TY_ERROR_UNION:
        if (t->elem->kind == TY_VOID) {
            return "a0_err";
        }
        break;
    case TY_BUILTIN:
        return t->builtin->c_name;
    }
    fatal("internal error: no C type for %s", type_name(t));
}

// A C string literal holding exactly these bytes. '?' is escaped so that no
// trigraph can form.
static const char *c_string(const char *s, size_t n) {
    StrBuf b = {0};
    sb_puts(&b, "\"");
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\' || c == '?') {
            sb_printf(&b, "\\%c", c);
        } else if (c >= 0x20 && c < 0x7F) {
            sb_add(&b, (const char *)&c, 1);
        } else {
            sb_printf(&b, "\\%03o", c);
        }
    }
    sb_puts(&b, "\"");
    return b.data;
}

static const char *loc_arg(Emitter *em, Loc loc) {
    size_t index = em->files.len;
    for (size_t i = 0; i < em->files.len; i++) {
        if (em->files.items[i] == loc.file) {
            index = i;
        }
    }
    if (index == em->files.len) {
        vec_push(&em->files, (void *)loc.file);
    }
    return arena_printf("A0_LOC(a0_file_%zu, %u, %u)", index, (unsigned)loc.line, (unsigned)loc.col);
}

static const char *new_temp(Emitter *em) {
    return arena_printf("t%d", ++em->temps);
}

// Stores a value in a fresh temporary and returns its name.
static const char *to_temp(Emitter *em, const Type *t, const char *value) {
    const char *name = new_temp(em);
    line(em, "%s %s = %s;", c_type(t), name, value);
    return name;
}

// ---- expressions ------------------------------------------------------------

static const char *emit_expr(Emitter *em, const Expr *e);

static const char *emit_place(Emitter *em, const Expr *e) {
    switch (e->kind) {
    case E_IDENT:
        return arena_printf("v_%s", e->name);
    case E_FIELD: {
        const Field *f = field_lookup(e->base->type->kind == TY_REF ? e->base->type->elem : e->base->type, e->name);
        if (e->base->type->kind == TY_REF) {
            return arena_printf("(*%s).%s", emit_expr(em, e->base), f->c_name);
        }
        return arena_printf("%s.%s", emit_place(em, e->base), f->c_name);
    }
    default:
        fatal("internal error: not a place");
    }
}

static const char *int_literal(const Type *t, uint64_t v) {
    bool is_signed = t->int_kind <= ISIZE;
    return arena_printf("((%s)%s(%llu))", c_type(t), is_signed ? "INT64_C" : "UINT64_C", (unsigned long long)v);
}

static const char *emit_call(Emitter *em, const Expr *e) {
    StrBuf args = {0};
    sb_puts(&args, "");
    const char *callee = NULL;

    switch (e->call) {
    case CALL_FN:
        callee = e->fn->c_name;
        break;
    case CALL_METHOD: {
        const Expr *r = e->receiver;
        const char *self;
        if (e->method->self == SELF_VALUE) {
            self = r->type->kind == TY_REF ? arena_printf("(*%s)", emit_expr(em, r)) : emit_expr(em, r);
        } else if (r->type->kind == TY_REF) {
            self = emit_expr(em, r);
        } else if (expr_is_place(r)) {
            self = arena_printf("&%s", emit_place(em, r));
        } else {
            self = arena_printf("&%s", emit_expr(em, r));
        }
        sb_puts(&args, self);
        callee = e->method->c_name;
        break;
    }
    case CALL_BUILTIN_FN:
        break;
    }

    for (size_t i = 0; i < e->args.len; i++) {
        const char *v = emit_expr(em, VEC_AT(e->args, Arg, i)->value);
        if (args.len > 0) {
            sb_puts(&args, ", ");
        }
        sb_puts(&args, v);
    }

    if (e->call == CALL_BUILTIN_FN) {
        switch (e->builtin_fn) {
        case BUILTIN_PANIC:
            line(em, "a0_panic(%s, %s);", args.data, loc_arg(em, e->loc));
            break;
        case BUILTIN_ASSERT:
            line(em, "if (!%s) a0_trap(\"assertion failed\", %s);", args.data, loc_arg(em, e->loc));
            break;
        case BUILTIN_UNREACHABLE:
            line(em, "a0_trap(\"reached unreachable code\", %s);", loc_arg(em, e->loc));
            break;
        }
        free(args.data);
        return NULL;
    }

    if (e->call == CALL_METHOD && e->method->needs_loc) {
        sb_printf(&args, "%s%s", args.len > 0 ? ", " : "", loc_arg(em, e->loc));
    }
    const char *call = arena_printf("%s(%s)", callee, args.data);
    free(args.data);
    if (e->type->kind == TY_VOID || e->type->kind == TY_NORETURN) {
        line(em, "%s;", call);
        return NULL;
    }
    return to_temp(em, e->type, call);
}

static const char *emit_expr(Emitter *em, const Expr *e) {
    switch (e->kind) {
    case E_INT:
    case E_CHAR:
        return int_literal(e->type, e->value);
    case E_BOOL:
        return e->value ? "true" : "false";
    case E_STRING:
        return arena_printf("A0_STR(%s, %zu)", c_string(e->bytes, e->bytes_len), e->bytes_len);
    case E_IDENT:
    case E_FIELD:
        return to_temp(em, e->type, emit_place(em, e));
    case E_CALL:
        return emit_call(em, e);
    case E_REF:
        return arena_printf("(&%s)", emit_place(em, e->base));
    }
    fatal("internal error: unknown expression");
}

// ---- statements -------------------------------------------------------------

static void emit_block(Emitter *em, const Block *b);

static bool returns_error_code(const Type *t) {
    return t->kind == TY_ERROR_UNION && t->elem->kind == TY_VOID;
}

static void emit_stmt(Emitter *em, const Stmt *s) {
    switch (s->kind) {
    case S_EXPR:
        emit_expr(em, s->expr);
        return;
    case S_RETURN:
        if (s->expr) {
            line(em, "return %s;", emit_expr(em, s->expr));
        } else if (returns_error_code(em->fn->ret)) {
            line(em, "return 0;");
        } else {
            line(em, "return;");
        }
        return;
    case S_BLOCK:
        line(em, "{");
        em->indent++;
        emit_block(em, s->block);
        em->indent--;
        line(em, "}");
        return;
    }
}

static void emit_block(Emitter *em, const Block *b) {
    for (size_t i = 0; i < b->stmts.len; i++) {
        emit_stmt(em, VEC_AT(b->stmts, Stmt, i));
    }
}

// ---- functions and the program ----------------------------------------------

static const char *signature(const FnDecl *fn) {
    StrBuf b = {0};
    sb_printf(&b, "static %s %s(", c_type(fn->ret), fn->c_name);
    if (fn->params.len == 0) {
        sb_puts(&b, "void");
    }
    for (size_t i = 0; i < fn->params.len; i++) {
        const Param *p = VEC_AT(fn->params, Param, i);
        sb_printf(&b, "%s%s v_%s", i > 0 ? ", " : "", c_type(p->binding->type), p->name);
    }
    sb_puts(&b, ")");
    return b.data;
}

static void emit_fn(Emitter *em, const FnDecl *fn) {
    em->fn = fn;
    em->temps = 0;
    line(em, "%s {", signature(fn));
    em->indent++;
    emit_block(em, fn->body);
    if (returns_error_code(fn->ret)) {
        line(em, "return 0;");
    }
    em->indent--;
    line(em, "}");
    line(em, "%s", "");
}

void emit_program(Module *root, StrBuf *out) {
    StrBuf body = {0};
    Emitter em = {.out = &body};
    for (size_t i = 0; i < root->fns.len; i++) {
        emit_fn(&em, VEC_AT(root->fns, FnDecl, i));
    }
    FnDecl *main = NULL;
    for (size_t i = 0; i < root->fns.len; i++) {
        if (strcmp(VEC_AT(root->fns, FnDecl, i)->name, "main") == 0) {
            main = VEC_AT(root->fns, FnDecl, i);
        }
    }

    em.out = out;
    line(&em, "// Generated by arche0 from %s. Do not edit.", root->file->display);
    line(&em, "#include \"arche0_rt.h\"");
    line(&em, "%s", "");
    for (size_t i = 0; i < em.files.len; i++) {
        const SourceFile *f = em.files.items[i];
        line(&em, "static const char a0_file_%zu[] = %s;", i, c_string(f->display, strlen(f->display)));
    }
    line(&em, "%s", "");
    for (size_t i = 0; i < root->fns.len; i++) {
        line(&em, "%s;", signature(VEC_AT(root->fns, FnDecl, i)));
    }
    line(&em, "%s", "");
    sb_add(out, body.data, body.len);
    free(body.data);
    line(&em, "a0_err a0_entry(a0_std_System *sys) {");
    line(&em, "    return %s(sys);", main->c_name);
    line(&em, "}");
    line(&em, "%s", "");
    line(&em, "const char *const a0_error_names[] = {\"\"};");
}
