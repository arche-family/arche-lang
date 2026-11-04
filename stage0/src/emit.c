// C emitter.
//
// Every expression is evaluated into a temporary before it is used. C leaves
// the order of argument evaluation unspecified; Arche evaluates left to
// right (§7.3), and temporaries make that order explicit. C compilers remove
// the copies again. Places (lvalues) are emitted as C lvalue expressions.
//
// Integer operators call the runtime's helpers (arche0_rt.h), which trap on
// overflow, division by zero and oversized shifts and never rely on C's
// undefined or implementation-defined behaviour.
//
// `defer` is emitted again at every exit of its block: the end of the block,
// `return`, `break` and `continue`. Loops become `for (;;)` with `goto`
// labels, so `break` and `continue` work from any depth.

#include "arche0.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Vec defers; // Stmt *: the deferred statements, in order of declaration
} Scope;

typedef struct {
    int id;
    size_t depth; // scopes.len outside the body: break runs the defers above it
    bool broke;
    bool continued;
} Loop;

typedef struct {
    StrBuf *out;
    int indent;
    int temps;
    int labels;
    const FnDecl *fn;
    Vec files;  // const SourceFile *, indexed by a0_file_N
    Vec scopes; // Scope *
    Vec loops;  // Loop *
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

// The unsigned C type of the same width.
static const char *c_unsigned_type(IntKind k) {
    switch (int_bits(k)) {
    case 8:
        return "uint8_t";
    case 16:
        return "uint16_t";
    case 32:
        return "uint32_t";
    default:
        return "uint64_t";
    }
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

// An integer constant of type t: -magnitude when `negative`.
static const char *c_int_value(const Type *t, bool negative, uint64_t magnitude) {
    const char *ct = c_type(t);
    if (!negative) {
        return arena_printf("((%s)%s(%llu))", ct, int_signed(t->int_kind) ? "INT64_C" : "UINT64_C",
                            (unsigned long long)magnitude);
    }
    if (magnitude == (uint64_t)1 << 63) {
        return arena_printf("((%s)(-INT64_C(9223372036854775807) - 1))", ct);
    }
    return arena_printf("((%s)-INT64_C(%llu))", ct, (unsigned long long)magnitude);
}

// An integer constant from its two's complement bits.
static const char *c_int_bits(const Type *t, uint64_t bits) {
    if (int_signed(t->int_kind) && bits >> 63) {
        return c_int_value(t, true, ~bits + 1);
    }
    return c_int_value(t, false, bits);
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

// ---- integer operators -------------------------------------------------------

// The C expression for `l op r` on integers of type t.
static const char *int_op(Emitter *em, BinaryOp op, const Type *t, const char *l, const char *r, Loc loc) {
    const char *s = int_suffix(t->int_kind);
    const char *ct = c_type(t);
    switch (op) {
    case B_ADD:
        return arena_printf("a0_add_%s(%s, %s, %s)", s, l, r, loc_arg(em, loc));
    case B_SUB:
        return arena_printf("a0_sub_%s(%s, %s, %s)", s, l, r, loc_arg(em, loc));
    case B_MUL:
        return arena_printf("a0_mul_%s(%s, %s, %s)", s, l, r, loc_arg(em, loc));
    case B_DIV:
        return arena_printf("a0_div_%s(%s, %s, %s)", s, l, r, loc_arg(em, loc));
    case B_REM:
        return arena_printf("a0_rem_%s(%s, %s, %s)", s, l, r, loc_arg(em, loc));
    case B_WADD:
        return arena_printf("a0_wadd_%s(%s, %s)", s, l, r);
    case B_WSUB:
        return arena_printf("a0_wsub_%s(%s, %s)", s, l, r);
    case B_WMUL:
        return arena_printf("a0_wmul_%s(%s, %s)", s, l, r);
    case B_SHL:
        return arena_printf("a0_shl_%s(%s, (uint64_t)%s, %s)", s, l, r, loc_arg(em, loc));
    case B_SHR:
        return arena_printf("a0_shr_%s(%s, (uint64_t)%s, %s)", s, l, r, loc_arg(em, loc));
    case B_BITAND:
        return arena_printf("((%s)(%s & %s))", ct, l, r);
    case B_BITOR:
        return arena_printf("((%s)(%s | %s))", ct, l, r);
    case B_BITXOR:
        return arena_printf("((%s)(%s ^ %s))", ct, l, r);
    default:
        fatal("internal error: not an integer operator");
    }
}

static BinaryOp assign_binary(AssignOp op) {
    switch (op) {
    case A_ADD:
        return B_ADD;
    case A_SUB:
        return B_SUB;
    case A_MUL:
        return B_MUL;
    case A_DIV:
        return B_DIV;
    case A_REM:
        return B_REM;
    case A_WADD:
        return B_WADD;
    case A_WSUB:
        return B_WSUB;
    case A_WMUL:
        return B_WMUL;
    case A_BITAND:
        return B_BITAND;
    case A_BITOR:
        return B_BITOR;
    case A_BITXOR:
        return B_BITXOR;
    case A_SHL:
        return B_SHL;
    case A_SHR:
        return B_SHR;
    case A_SET:
        break;
    }
    fatal("internal error: plain assignment has no operator");
}

static bool range_contains(IntKind outer, IntKind inner) {
    return int_min(outer) <= int_min(inner) && int_max(inner) <= int_max(outer);
}

static const char *cast(Emitter *em, const Expr *e, const char *v) {
    const Type *from = e->base->type;
    const Type *to = e->type;
    const char *ct = c_type(to);
    if (from == to) {
        return v;
    }
    if (e->wrapping) {
        if (!int_signed(to->int_kind)) {
            return arena_printf("((%s)(uint64_t)%s)", ct, v);
        }
        return arena_printf("a0_from_bits_%s((%s)(uint64_t)%s)", int_suffix(to->int_kind), c_unsigned_type(to->int_kind),
                            v);
    }
    if (range_contains(to->int_kind, from->int_kind)) {
        return arena_printf("((%s)%s)", ct, v);
    }
    const char *name = c_string(type_name(to), strlen(type_name(to)));
    const char *loc = loc_arg(em, e->loc);
    const char *max = c_int_bits(type_int(int_signed(to->int_kind) ? I64 : U64), int_max(to->int_kind));
    if (int_signed(from->int_kind)) {
        if (int_signed(to->int_kind)) {
            const char *min = c_int_bits(type_int(I64), (uint64_t)int_min(to->int_kind));
            return arena_printf("((%s)a0_fit_ss((int64_t)%s, %s, %s, %s, %s))", ct, v, min, max, name, loc);
        }
        return arena_printf("((%s)a0_fit_su((int64_t)%s, %s, %s, %s))", ct, v, max, name, loc);
    }
    if (int_signed(to->int_kind)) {
        return arena_printf("((%s)a0_fit_us((uint64_t)%s, %s, %s, %s))", ct, v, max, name, loc);
    }
    return arena_printf("((%s)a0_fit_uu((uint64_t)%s, %s, %s, %s))", ct, v, max, name, loc);
}

// ---- expressions ------------------------------------------------------------

static const char *emit_expr(Emitter *em, const Expr *e);

static const char *emit_place(Emitter *em, const Expr *e) {
    switch (e->kind) {
    case E_IDENT:
        return arena_printf("v_%s", e->name);
    case E_FIELD: {
        const Type *bt = e->base->type;
        const Field *f = field_lookup(bt->kind == TY_REF ? bt->elem : bt, e->name);
        if (bt->kind == TY_REF) {
            return arena_printf("(*%s).%s", emit_expr(em, e->base), f->c_name);
        }
        return arena_printf("%s.%s", emit_place(em, e->base), f->c_name);
    }
    case E_DEREF:
        return arena_printf("(*%s)", emit_expr(em, e->base));
    default:
        fatal("internal error: not a place");
    }
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

// `and` and `or` evaluate their right side only when it decides the result.
static const char *emit_logical(Emitter *em, const Expr *e) {
    const char *l = emit_expr(em, e->base);
    const char *t = to_temp(em, type_bool(), l);
    line(em, e->binary == B_AND ? "if (%s) {" : "if (!%s) {", t);
    em->indent++;
    const char *r = emit_expr(em, e->rhs);
    line(em, "%s = %s;", t, r);
    em->indent--;
    line(em, "}");
    return t;
}

static const char *emit_const(const ConstDecl *k) {
    switch (k->type->kind) {
    case TY_BOOL:
        return k->val.bits ? "true" : "false";
    case TY_STR:
        return arena_printf("A0_STR(%s, %zu)", c_string(k->val.bytes, k->val.len), k->val.len);
    default:
        return c_int_bits(k->type, k->val.bits);
    }
}

static const char *emit_expr(Emitter *em, const Expr *e) {
    switch (e->kind) {
    case E_INT:
    case E_CHAR:
        return c_int_value(e->type, false, e->value);
    case E_BOOL:
        return e->value ? "true" : "false";
    case E_STRING:
        return arena_printf("A0_STR(%s, %zu)", c_string(e->bytes, e->bytes_len), e->bytes_len);
    case E_IDENT:
        if (e->name_kind == NAME_CONST) {
            return emit_const(e->konst);
        }
        return to_temp(em, e->type, emit_place(em, e));
    case E_FIELD:
    case E_DEREF:
        return to_temp(em, e->type, emit_place(em, e));
    case E_CALL:
        return emit_call(em, e);
    case E_REF:
        return arena_printf("(&%s)", emit_place(em, e->base));
    case E_UNARY: {
        if (e->negative_literal) {
            return c_int_value(e->type, true, e->value);
        }
        const char *v = emit_expr(em, e->base);
        switch (e->unary) {
        case U_NOT:
            return to_temp(em, e->type, arena_printf("(!%s)", v));
        case U_BITNOT:
            return to_temp(em, e->type, arena_printf("((%s)(~%s))", c_type(e->type), v));
        case U_NEG:
            return to_temp(em, e->type,
                           arena_printf("a0_neg_%s(%s, %s)", int_suffix(e->type->int_kind), v, loc_arg(em, e->loc)));
        }
        break;
    }
    case E_BINARY: {
        if (e->binary == B_AND || e->binary == B_OR) {
            return emit_logical(em, e);
        }
        const char *l = emit_expr(em, e->base);
        const char *r = emit_expr(em, e->rhs);
        static const char *const cmp[] = {[B_EQ] = "==", [B_NE] = "!=", [B_LT] = "<",
                                          [B_LE] = "<=", [B_GT] = ">",  [B_GE] = ">="};
        if (e->binary >= B_EQ) {
            return to_temp(em, type_bool(), arena_printf("(%s %s %s)", l, cmp[e->binary], r));
        }
        return to_temp(em, e->type, int_op(em, e->binary, e->type, l, r, e->loc));
    }
    case E_CAST:
        return to_temp(em, e->type, cast(em, e, emit_expr(em, e->base)));
    default:
        break;
    }
    fatal("internal error: cannot emit this expression yet");
}

// ---- statements -------------------------------------------------------------

static void emit_stmt(Emitter *em, const Stmt *s);

static bool returns_error_code(const Type *t) {
    return t->kind == TY_ERROR_UNION && t->elem->kind == TY_VOID;
}

// Runs the defers of scopes[depth..], innermost first, each in reverse order.
static void run_defers(Emitter *em, size_t depth) {
    for (size_t i = em->scopes.len; i > depth; i--) {
        Scope *sc = em->scopes.items[i - 1];
        for (size_t j = sc->defers.len; j > 0; j--) {
            emit_stmt(em, sc->defers.items[j - 1]);
        }
    }
}

// The statements of a block, without braces, in a new scope.
static void emit_scoped(Emitter *em, const Block *b) {
    Scope *sc = NEW(Scope);
    vec_push(&em->scopes, sc);
    for (size_t i = 0; i < b->stmts.len; i++) {
        emit_stmt(em, VEC_AT(b->stmts, Stmt, i));
    }
    if (!block_ends(b)) {
        run_defers(em, em->scopes.len - 1);
    }
    em->scopes.len--;
}

static void emit_braced(Emitter *em, const Block *b) {
    line(em, "{");
    em->indent++;
    emit_scoped(em, b);
    em->indent--;
    line(em, "}");
}

static void emit_assign(Emitter *em, const Stmt *s) {
    const char *place = emit_place(em, s->target);
    const char *v = emit_expr(em, s->expr);
    if (s->op == A_SET) {
        line(em, "%s = %s;", place, v);
        return;
    }
    line(em, "%s = %s;", place, int_op(em, assign_binary(s->op), s->target->type, place, v, s->loc));
}

static void emit_if(Emitter *em, const Stmt *s) {
    const char *c = emit_expr(em, s->expr);
    line(em, "if (%s) {", c);
    em->indent++;
    emit_scoped(em, s->block);
    em->indent--;
    if (s->els) {
        line(em, "} else {");
        em->indent++;
        if (s->els->kind == S_IF) {
            emit_if(em, s->els);
        } else {
            emit_scoped(em, s->els->block);
        }
        em->indent--;
    }
    line(em, "}");
}

static void emit_while(Emitter *em, const Stmt *s) {
    Loop *loop = NEW(Loop);
    loop->id = ++em->labels;
    loop->depth = em->scopes.len;
    line(em, "for (;;) {");
    em->indent++;
    const char *c = emit_expr(em, s->expr);
    line(em, "if (!%s) break;", c);
    vec_push(&em->loops, loop);
    emit_braced(em, s->block);
    em->loops.len--;
    if (loop->continued) {
        line(em, "a0_continue_%d:;", loop->id);
    }
    if (s->step) {
        emit_stmt(em, s->step);
    }
    em->indent--;
    line(em, "}");
    if (loop->broke) {
        line(em, "a0_break_%d:;", loop->id);
    }
}

static void emit_stmt(Emitter *em, const Stmt *s) {
    switch (s->kind) {
    case S_EXPR:
        emit_expr(em, s->expr);
        return;
    case S_LET:
        line(em, "%s v_%s = %s;", c_type(s->binding->type), s->name, emit_expr(em, s->expr));
        return;
    case S_ASSIGN:
        emit_assign(em, s);
        return;
    case S_DISCARD: {
        const char *v = emit_expr(em, s->expr);
        if (v) {
            line(em, "(void)%s;", v);
        }
        return;
    }
    case S_IF:
        emit_if(em, s);
        return;
    case S_WHILE:
        emit_while(em, s);
        return;
    case S_RETURN: {
        const char *v = s->expr ? emit_expr(em, s->expr) : NULL;
        run_defers(em, 0);
        if (v) {
            line(em, "return %s;", v);
        } else if (returns_error_code(em->fn->ret)) {
            line(em, "return 0;");
        } else {
            line(em, "return;");
        }
        return;
    }
    case S_BREAK:
    case S_CONTINUE: {
        Loop *loop = em->loops.items[em->loops.len - 1];
        run_defers(em, loop->depth);
        if (s->kind == S_BREAK) {
            loop->broke = true;
            line(em, "goto a0_break_%d;", loop->id);
        } else {
            loop->continued = true;
            line(em, "goto a0_continue_%d;", loop->id);
        }
        return;
    }
    case S_DEFER: {
        Scope *sc = em->scopes.items[em->scopes.len - 1];
        vec_push(&sc->defers, s->step);
        return;
    }
    case S_BLOCK:
        emit_braced(em, s->block);
        return;
    case S_MATCH:
        break;
    }
    fatal("internal error: cannot emit this statement yet");
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
    em->labels = 0;
    em->scopes.len = 0;
    em->loops.len = 0;
    line(em, "%s {", signature(fn));
    em->indent++;
    emit_scoped(em, fn->body);
    if (returns_error_code(fn->ret) && !block_ends(fn->body)) {
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
