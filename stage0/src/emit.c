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

// Helpers emitted per type, only when the program uses them: C compilers warn
// about unused static functions.
enum {
    USE_INIT = 1,
    USE_LIST_PUSH = 2,
    USE_LIST_CLEAR = 4,
    USE_LIST_ITEMS = 8,
    USE_EQ = 16,
    USE_LIST_POP = 32,
    USE_MAP_PUT = 64,
    USE_MAP_GET = 128,
    USE_MAP_REMOVE = 256,
    USE_HASH = 512,
};

static void use(const Type *t, unsigned what) {
    ((Type *)t)->used |= what;
}

// Struct equality calls the equality of the struct fields too.
static void use_equality(const Type *t) {
    if (t->kind != TY_STRUCT || (t->used & USE_EQ)) {
        return;
    }
    use(t, USE_EQ);
    for (int i = 0; i < t->nfields; i++) {
        use_equality(t->fields[i].type);
    }
}

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
    case TY_ERROR_SET:
        return "a0_err";
    case TY_ERROR_UNION:
    case TY_OPTIONAL:
    case TY_MAP:
    case TY_SLICE:
    case TY_ARRAY:
    case TY_LIST:
    case TY_STRUCT:
    case TY_ENUM:
        return t->c_name;
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

// A C expression for e's value that can be named more than once: places as
// they are, other values in a temporary.
static const char *emit_lvalue(Emitter *em, const Expr *e);

static const char *emit_place(Emitter *em, const Expr *e) {
    switch (e->kind) {
    case E_IDENT:
        return arena_printf("v_%s", e->name);
    case E_FIELD: {
        if (e->base->type->kind == TY_REF) {
            return arena_printf("(*%s).%s", emit_expr(em, e->base), e->field->c_name);
        }
        return arena_printf("%s.%s", emit_lvalue(em, e->base), e->field->c_name);
    }
    case E_DEREF:
        return arena_printf("(*%s)", emit_expr(em, e->base));
    case E_INDEX: {
        const Type *bt = e->base->type;
        const char *base = bt->kind == TY_SLICE ? emit_expr(em, e->base) : emit_lvalue(em, e->base);
        const char *i = emit_expr(em, e->index);
        const char *loc = loc_arg(em, e->loc);
        switch (bt->kind) {
        case TY_ARRAY:
            line(em, "a0_check_index(%s, UINT64_C(%llu), %s);", i, (unsigned long long)bt->len, loc);
            return arena_printf("%s.items[%s]", base, i);
        case TY_SLICE:
            line(em, "a0_check_index(%s, %s.len, %s);", i, base, loc);
            return arena_printf("%s.ptr[%s]", base, i);
        case TY_LIST:
            line(em, "a0_check_index(%s, %s.len, %s);", i, base, loc);
            return arena_printf("%s.data[%s]", base, i);
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
    fatal("internal error: not a place");
}

static const char *emit_lvalue(Emitter *em, const Expr *e) {
    if (expr_is_place(e)) {
        return emit_place(em, e);
    }
    return to_temp(em, e->type, emit_expr(em, e));
}

// How the receiver of a method call is passed, as its `self` declares.
static const char *emit_self(Emitter *em, const Expr *r, SelfMode mode) {
    if (mode == SELF_VALUE) {
        return r->type->kind == TY_REF ? arena_printf("(*%s)", emit_expr(em, r)) : emit_expr(em, r);
    }
    if (r->type->kind == TY_REF) {
        return emit_expr(em, r);
    }
    return arena_printf("(&%s)", emit_lvalue(em, r));
}

static const char *emit_builtin_method(const Expr *e, const char *self, const char *args) {
    const Type *rt = e->receiver->type->kind == TY_REF ? e->receiver->type->elem : e->receiver->type;
    switch (e->method->op) {
    case M_LEN:
        if (rt->kind == TY_STR) {
            return arena_printf("%s.len", self);
        }
        if (rt->kind == TY_MAP) {
            return arena_printf("(*%s).live", self);
        }
        if (rt->kind == TY_ARRAY) {
            return arena_printf("((void)%s, UINT64_C(%llu))", self, (unsigned long long)rt->len);
        }
        return arena_printf("(*%s).len", self);
    case M_STR_BYTES:
        return arena_printf("((%s){(uint8_t *)%s.ptr, %s.len})", e->type->c_name, self, self);
    case M_LIST_PUSH:
        use(rt, USE_LIST_PUSH);
        return arena_printf("%s_push(%s, %s)", rt->c_name, self, args);
    case M_LIST_POP:
        use(rt, USE_LIST_POP);
        return arena_printf("%s_pop(%s)", rt->c_name, self);
    case M_MAP_PUT:
        use(rt, USE_MAP_PUT);
        return arena_printf("%s_put(%s, %s)", rt->c_name, self, args);
    case M_MAP_GET:
        use(rt, USE_MAP_GET);
        return arena_printf("%s_get(%s, %s)", rt->c_name, self, args);
    case M_MAP_REMOVE:
        use(rt, USE_MAP_REMOVE);
        return arena_printf("%s_remove(%s, %s)", rt->c_name, self, args);
    case M_LIST_CLEAR:
        use(rt, USE_LIST_CLEAR);
        return arena_printf("%s_clear(%s)", rt->c_name, self);
    case M_LIST_ITEMS:
    case M_LIST_ITEMS_MUT:
        use(rt, USE_LIST_ITEMS);
        return arena_printf("%s_items(%s)", rt->c_name, self);
    case M_RUNTIME:
        break;
    }
    return NULL;
}

static const char *emit_call(Emitter *em, const Expr *e) {
    StrBuf args = {0};
    sb_puts(&args, "");
    const char *self = NULL;
    if (e->call == CALL_METHOD) {
        self = emit_self(em, e->receiver, e->method->self);
    } else if (e->call == CALL_FN && e->receiver) {
        self = emit_self(em, e->receiver, e->self_mode);
    }
    if (self && !(e->call == CALL_METHOD && e->method->op != M_RUNTIME)) {
        sb_puts(&args, self);
    }
    for (size_t i = 0; i < e->args.len; i++) {
        const char *v = emit_expr(em, VEC_AT(e->args, Arg, i)->value);
        sb_printf(&args, "%s%s", args.len > 0 ? ", " : "", v);
    }

    const char *call = NULL;
    switch (e->call) {
    case CALL_BUILTIN_FN:
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
    case CALL_FN:
        call = arena_printf("%s(%s)", e->fn->c_name, args.data);
        break;
    case CALL_INIT:
        use(e->type, USE_INIT);
        call = arena_printf("%s_init(%s)", c_type(e->type), args.data);
        break;
    case CALL_METHOD:
        if (e->method->op != M_RUNTIME) {
            call = emit_builtin_method(e, self, args.data);
            break;
        }
        if (e->method->needs_loc) {
            sb_printf(&args, ", %s", loc_arg(em, e->loc));
        }
        call = arena_printf("%s(%s)", e->method->c_name, args.data);
        break;
    }
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

// a == b for a comparable type (§5.5).
static const char *equal(const Type *t, const char *a, const char *b) {
    switch (t->kind) {
    case TY_ENUM:
        return arena_printf("(%s.tag == %s.tag)", a, b);
    case TY_STRUCT:
        use_equality(t);
        return arena_printf("a0_eq_%s(%s, %s)", t->c_name, a, b);
    default:
        return arena_printf("(%s == %s)", a, b);
    }
}

// An expression as an if condition: without the parentheses that enclose it
// whole, which clang warns about inside `if (...)`.
static const char *condition(const char *e) {
    size_t n = strlen(e);
    if (n < 2 || e[0] != '(' || e[n - 1] != ')') {
        return e;
    }
    int depth = 0;
    for (size_t i = 0; i < n - 1; i++) {
        depth += e[i] == '(' ? 1 : e[i] == ')' ? -1 : 0;
        if (depth == 0) {
            return e; // the first '(' closes before the end
        }
    }
    return arena_strndup(e + 1, n - 2);
}

static const char *emit_slice(Emitter *em, const Expr *e) {
    const Type *bt = e->base->type;
    const char *base = bt->kind == TY_STR || bt->kind == TY_SLICE ? emit_expr(em, e->base) : emit_place(em, e->base);
    const char *a = emit_expr(em, e->index);
    const char *b = emit_expr(em, e->end);
    const char *loc = loc_arg(em, e->loc);
    if (bt->kind == TY_STR) {
        return to_temp(em, e->type, arena_printf("a0_str_slice(%s, %s, %s, %s)", base, a, b, loc));
    }
    const char *len;
    const char *ptr;
    switch (bt->kind) {
    case TY_ARRAY:
        len = arena_printf("UINT64_C(%llu)", (unsigned long long)bt->len);
        ptr = arena_printf("%s.items", base);
        break;
    case TY_LIST:
        len = arena_printf("%s.len", base);
        ptr = arena_printf("%s.data", base);
        break;
    default:
        len = arena_printf("%s.len", base);
        ptr = arena_printf("%s.ptr", base);
        break;
    }
    line(em, "a0_check_slice(%s, %s, %s, %s);", a, b, len, loc);
    // An empty list or slice may have a null pointer, and C does not allow
    // adding even 0 to one.
    return to_temp(em, e->type, arena_printf("{%s == 0 ? %s : %s + %s, %s - %s}", a, ptr, ptr, a, b, a));
}

static const char *emit_struct_lit(Emitter *em, const Expr *e) {
    StrBuf vals = {0};
    sb_puts(&vals, "");
    for (size_t i = 0; i < e->fields.len; i++) {
        const char *v = emit_expr(em, ((FieldInit *)e->fields.items[i])->value);
        sb_printf(&vals, "%s%s", i > 0 ? ", " : "", v);
    }
    const char *init;
    if (e->variant) {
        init = arena_printf("{.tag = %d, .as.v_%s = {%s}}", e->variant->tag, e->variant->name, vals.data);
    } else {
        init = vals.len > 0 ? arena_printf("{%s}", vals.data) : "{0}";
    }
    free(vals.data);
    return to_temp(em, e->type, init);
}

static const char *emit_array_lit(Emitter *em, const Expr *e) {
    StrBuf vals = {0};
    sb_puts(&vals, "");
    for (size_t i = 0; i < e->elems.len; i++) {
        sb_printf(&vals, "%s%s", i > 0 ? ", " : "", emit_expr(em, e->elems.items[i]));
    }
    const char *init = vals.len > 0 ? arena_printf("{{%s}}", vals.data) : "{{0}}";
    free(vals.data);
    return to_temp(em, e->type, init);
}

// A zero value of t, to declare a temporary that is assigned on each path.
static const char *zero_value(const Type *t) {
    switch (t->kind) {
    case TY_INT:
    case TY_BOOL:
    case TY_REF:
    case TY_ERROR_SET:
        return "0";
    case TY_ERROR_UNION:
        return t->elem->kind == TY_VOID ? "0" : "{0}";
    default:
        return "{0}";
    }
}

// The C condition that an optional or error union holds a value.
static const char *has_value(const Type *t, const char *x) {
    if (t->kind == TY_OPTIONAL) {
        return arena_printf("%s.has", x);
    }
    return t->elem->kind == TY_VOID ? arena_printf("%s == 0", x) : arena_printf("%s.err == 0", x);
}

// x orelse y, x catch y: y is evaluated only when x has no value.
static const char *emit_fallback(Emitter *em, const Expr *e) {
    const Type *xt = e->base->type;
    const char *x = emit_expr(em, e->base);
    if (xt->elem->kind == TY_VOID) {
        line(em, "if (!(%s)) {", has_value(xt, x));
        em->indent++;
        emit_expr(em, e->rhs);
        em->indent--;
        line(em, "}");
        return NULL;
    }
    const char *r = new_temp(em);
    line(em, "%s %s = %s;", c_type(e->type), r, zero_value(e->type));
    line(em, "if (%s) {", has_value(xt, x));
    line(em, "    %s = %s.value;", r, x);
    line(em, "} else {");
    em->indent++;
    const char *y = emit_expr(em, e->rhs);
    if (y) {
        line(em, "%s = %s;", r, y);
    }
    em->indent--;
    line(em, "}");
    return r;
}

static void run_defers(Emitter *em, size_t depth);

// Returns the error numbered `code` from the current function.
static void return_error(Emitter *em, const char *code) {
    const Type *ret = em->fn->ret;
    if (ret->elem->kind == TY_VOID) {
        line(em, "return %s;", code);
        return;
    }
    const char *r = new_temp(em);
    line(em, "%s %s = {0};", c_type(ret), r);
    line(em, "%s.err = %s;", r, code);
    line(em, "return %s;", r);
}

// try x: on an error, the deferred code runs and the function returns it.
static const char *emit_try(Emitter *em, const Expr *e) {
    const Type *xt = e->base->type;
    const char *x = emit_expr(em, e->base);
    const char *code = xt->elem->kind == TY_VOID ? x : arena_printf("%s.err", x);
    line(em, "if (%s != 0) {", code);
    em->indent++;
    run_defers(em, 0);
    return_error(em, code);
    em->indent--;
    line(em, "}");
    return xt->elem->kind == TY_VOID ? NULL : arena_printf("%s.value", x);
}

static const char *emit_expr(Emitter *em, const Expr *e) {
    switch (e->kind) {
    case E_NONE:
        return to_temp(em, e->type, "{0}");
    case E_SOME:
        return to_temp(em, e->type, arena_printf("{true, %s}", emit_expr(em, e->base)));
    case E_ORELSE:
    case E_CATCH:
        return emit_fallback(em, e);
    case E_TRY:
        return emit_try(em, e);
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
        switch (e->field_use) {
        case FIELD_CONST:
            return emit_const(e->konst);
        case FIELD_VARIANT:
            return arena_printf("((%s){.tag = %d})", e->type->c_name, e->variant->tag);
        case FIELD_ERROR:
            return arena_printf("((a0_err)%d)", e->error->id);
        case FIELD_VALUE:
            break;
        }
        return to_temp(em, e->type, emit_place(em, e));
    case E_DEREF:
        return to_temp(em, e->type, emit_place(em, e));
    case E_INDEX:
        if (e->base->type->kind == TY_STR) {
            const char *s = emit_expr(em, e->base);
            const char *i = emit_expr(em, e->index);
            return to_temp(em, e->type, arena_printf("a0_str_at(%s, %s, %s)", s, i, loc_arg(em, e->loc)));
        }
        return to_temp(em, e->type, emit_place(em, e));
    case E_SLICE:
        return emit_slice(em, e);
    case E_CALL:
        return emit_call(em, e);
    case E_REF:
        return arena_printf("(&%s)", emit_place(em, e->base));
    case E_STRUCT_LIT:
        return emit_struct_lit(em, e);
    case E_ARRAY_LIT:
        return emit_array_lit(em, e);
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
        if (e->binary == B_EQ || e->binary == B_NE) {
            const char *eq = equal(e->base->type, l, r);
            return to_temp(em, type_bool(), e->binary == B_EQ ? eq : arena_printf("(!%s)", eq));
        }
        static const char *const cmp[] = {[B_LT] = "<", [B_LE] = "<=", [B_GT] = ">", [B_GE] = ">="};
        if (e->binary >= B_LT) {
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

// The C condition for one pattern, without enclosing parentheses; `m` is a
// C expression for the value matched, of type t.
static const char *pattern_cond(const Pattern *p, const Type *t, const char *m) {
    switch (p->kind) {
    case P_WILDCARD:
    case P_BIND:
        return "true";
    case P_INT:
        return arena_printf("%s == %s", m, c_int_value(t, p->lo.negative, p->lo.magnitude));
    case P_RANGE:
        return arena_printf("%s >= %s && %s <= %s", m, c_int_value(t, p->lo.negative, p->lo.magnitude), m,
                            c_int_value(t, p->hi.negative, p->hi.magnitude));
    case P_BOOL:
        return p->value ? m : arena_printf("!%s", m);
    case P_VARIANT:
        if (p->error) {
            return arena_printf("%s == %d", m, p->error->id);
        }
        return arena_printf("%s.tag == %d", m, p->variant->tag);
    case P_NONE:
        return arena_printf("!%s.has", m);
    case P_SOME:
    case P_OK:
    case P_ERR: {
        const char *outer;
        const char *inner;
        if (p->kind == P_SOME) {
            outer = arena_printf("%s.has", m);
            inner = pattern_cond(p->inner, t->elem, arena_printf("%s.value", m));
        } else {
            bool v = t->elem->kind == TY_VOID;
            const char *code = v ? m : arena_printf("%s.err", m);
            outer = arena_printf(p->kind == P_OK ? "%s == 0" : "%s != 0", code);
            const Type *it = p->kind == P_OK ? t->elem : (t->decl ? t->decl->type : type_any_error());
            inner = v && p->kind == P_OK ? "true" : pattern_cond(p->inner, it, p->kind == P_OK ? arena_printf("%s.value", m) : code);
        }
        return strcmp(inner, "true") == 0 ? outer : arena_printf("%s && (%s)", outer, inner);
    }
    }
    return "true";
}

// Bindings: copies of the value, or views of it when the scrutinee is a
// reference (§9). At the top, a name binds the scrutinee itself, `temp`.
static void bind_pattern(Emitter *em, const Pattern *p, const Type *t, const char *m, bool by_ref, const char *temp) {
    switch (p->kind) {
    case P_BIND: {
        const char *v = temp ? temp : by_ref ? arena_printf("&%s", m) : m;
        line(em, "%s v_%s = %s;", c_type(p->binding->type), p->name, v);
        return;
    }
    case P_SOME:
        bind_pattern(em, p->inner, t->elem, arena_printf("%s.value", m), by_ref, NULL);
        return;
    case P_OK:
        if (t->elem->kind != TY_VOID) {
            bind_pattern(em, p->inner, t->elem, arena_printf("%s.value", m), by_ref, NULL);
        }
        return;
    case P_ERR: {
        const Type *it = t->decl ? t->decl->type : type_any_error();
        bind_pattern(em, p->inner, it, t->elem->kind == TY_VOID ? m : arena_printf("%s.err", m), by_ref, NULL);
        return;
    }
    case P_VARIANT:
        if (!p->variant) {
            return;
        }
        for (size_t i = 0; i < p->field_pats.len; i++) {
            const FieldPat *fp = p->field_pats.items[i];
            if (!fp->binding) {
                continue;
            }
            const char *field = arena_printf("%s.as.v_%s.f_%s", m, p->variant->name, fp->decl->name);
            line(em, "%s v_%s = %s%s;", c_type(fp->binding->type), fp->bind, by_ref ? "&" : "", field);
        }
        return;
    default:
        return;
    }
}

static void emit_match(Emitter *em, const Stmt *s) {
    const Type *st = s->expr->type;
    bool by_ref = st->kind == TY_REF;
    const Type *t = by_ref ? st->elem : st;
    const char *temp = to_temp(em, st, emit_expr(em, s->expr));
    const char *m = by_ref ? arena_printf("(*%s)", temp) : temp;
    for (size_t i = 0; i < s->arms.len; i++) {
        const MatchArm *arm = s->arms.items[i];
        StrBuf cond = {0};
        for (size_t j = 0; j < arm->patterns.len; j++) {
            const char *c = pattern_cond(arm->patterns.items[j], t, m);
            if (arm->patterns.len > 1) {
                c = arena_printf("(%s)", c);
            }
            sb_printf(&cond, "%s%s", j > 0 ? " || " : "", c);
        }
        line(em, i == 0 ? "if (%s) {" : "} else if (%s) {", cond.data);
        free(cond.data);
        em->indent++;
        if (arm->patterns.len == 1) {
            bind_pattern(em, arm->patterns.items[0], t, m, by_ref, temp);
        }
        emit_scoped(em, arm->body);
        em->indent--;
    }
    line(em, s->arms.len > 0 ? "} else {" : "{");
    line(em, "    a0_trap(\"no match arm matched\", %s);", loc_arg(em, s->loc));
    line(em, "}");
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
        const Type *ret = em->fn->ret;
        if (s->ret_kind == RET_VALUE) {
            v = to_temp(em, ret, ret->kind == TY_OPTIONAL ? arena_printf("{true, %s}", v) : arena_printf("{0, %s}", v));
        } else if (s->ret_kind == RET_ERROR && ret->elem->kind != TY_VOID) {
            v = to_temp(em, ret, arena_printf("{.err = %s}", v));
        }
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
        emit_match(em, s);
        return;
    }
}

// ---- types ------------------------------------------------------------------

static void define_type(StrBuf *out, const Type *t, Vec *done);

// Defines the composite types t holds by value, then t itself.
static void define_deps(StrBuf *out, const Type *t, Vec *done) {
    switch (t->kind) {
    case TY_SLICE:
    case TY_ARRAY:
    case TY_LIST:
    case TY_MAP:
    case TY_OPTIONAL:
    case TY_STRUCT:
    case TY_ENUM:
        define_type(out, t, done);
        return;
    case TY_ERROR_UNION:
        if (t->elem->kind != TY_VOID && strcmp(t->c_name, "a0_res_String") != 0 && strcmp(t->c_name, "a0_res_bytes") != 0) {
            define_type(out, t, done);
        }
        return;
    default:
        return;
    }
}

// Composite types whose C definition is in the runtime (arche0_rt.h).
static bool runtime_defined(const Type *t) {
    return strcmp(t->c_name, "a0_bytes") == 0;
}

static void define_type(StrBuf *out, const Type *t, Vec *done) {
    if (runtime_defined(t)) {
        return;
    }
    for (size_t i = 0; i < done->len; i++) {
        if (done->items[i] == t || ((const Type *)done->items[i])->c_name == t->c_name) {
            return;
        }
    }
    vec_push(done, (void *)t);
    switch (t->kind) {
    case TY_SLICE:
        sb_printf(out, "struct %s {\n    %s *ptr;\n    uint64_t len;\n};\n\n", t->c_name, c_type(t->elem));
        return;
    case TY_LIST:
        sb_printf(out, "struct %s {\n    %s *data;\n    uint64_t len;\n    uint64_t cap;\n    a0_alloc_Allocator alloc;\n};\n\n",
                  t->c_name, c_type(t->elem));
        return;
    case TY_ARRAY:
        define_deps(out, t->elem, done);
        sb_printf(out, "struct %s {\n    %s items[%llu];\n};\n\n", t->c_name, c_type(t->elem),
                  (unsigned long long)(t->len > 0 ? t->len : 1));
        return;
    case TY_OPTIONAL:
        define_deps(out, t->elem, done);
        sb_printf(out, "struct %s {\n    bool has;\n    %s value;\n};\n\n", t->c_name, c_type(t->elem));
        return;
    case TY_ERROR_UNION:
        define_deps(out, t->elem, done);
        sb_printf(out, "struct %s {\n    a0_err err;\n    %s value;\n};\n\n", t->c_name, c_type(t->elem));
        return;
    case TY_MAP:
        // Entries in insertion order, and an open-addressing index into them:
        // a slot holds 0 (empty), UINT64_MAX (removed) or an entry's index + 1.
        define_deps(out, t->key, done);
        define_deps(out, t->elem, done);
        sb_printf(out,
                  "struct %s_entry {\n    %s key;\n    %s value;\n    bool live;\n};\n\n"
                  "struct %s {\n    struct %s_entry *entries;\n    uint64_t len;\n    uint64_t cap;\n"
                  "    uint64_t *slots;\n    uint64_t nslots;\n    uint64_t live;\n    a0_alloc_Allocator alloc;\n};\n\n",
                  t->c_name, c_type(t->key), c_type(t->elem), t->c_name, t->c_name);
        return;
    case TY_STRUCT:
        for (int i = 0; i < t->nfields; i++) {
            define_deps(out, t->fields[i].type, done);
        }
        sb_printf(out, "struct %s {\n", t->c_name);
        if (t->nfields == 0) {
            sb_puts(out, "    char a0_empty;\n");
        }
        for (int i = 0; i < t->nfields; i++) {
            sb_printf(out, "    %s %s;\n", c_type(t->fields[i].type), t->fields[i].c_name);
        }
        sb_puts(out, "};\n\n");
        return;
    case TY_ENUM: {
        bool payload = false;
        for (size_t i = 0; i < t->decl->variants.len; i++) {
            VariantDecl *v = t->decl->variants.items[i];
            payload = payload || v->has_fields;
            for (size_t j = 0; j < v->fields.len; j++) {
                define_deps(out, ((FieldDecl *)v->fields.items[j])->type, done);
            }
        }
        sb_printf(out, "struct %s {\n    uint32_t tag;\n", t->c_name);
        if (payload) {
            sb_puts(out, "    union {\n");
            for (size_t i = 0; i < t->decl->variants.len; i++) {
                VariantDecl *v = t->decl->variants.items[i];
                if (!v->has_fields) {
                    continue;
                }
                sb_puts(out, "        struct {\n");
                for (size_t j = 0; j < v->fields.len; j++) {
                    FieldDecl *f = v->fields.items[j];
                    sb_printf(out, "            %s f_%s;\n", c_type(f->type), f->name);
                }
                sb_printf(out, "        } v_%s;\n", v->name);
            }
            sb_puts(out, "    } as;\n");
        }
        sb_puts(out, "};\n\n");
        return;
    }
    default:
        return;
    }
}

// Operations of alloc.List(T), one set per element type.
static void define_list_ops(StrBuf *out, const Type *t) {
    const char *n = t->c_name;
    const char *e = c_type(t->elem);
    if (t->used & USE_INIT) {
        sb_printf(out, "static inline %s %s_init(a0_alloc_Allocator a) {\n    %s l = {0};\n    l.alloc = a;\n    return l;\n}\n\n",
              n, n, n);
    }
    if (t->used & USE_LIST_PUSH) {
        sb_printf(out,
              "static inline void %s_push(%s *self, %s item) {\n"
              "    if (self->len == self->cap) {\n"
              "        uint64_t cap = self->cap ? self->cap * 2 : 8;\n"
              "        self->data = a0_realloc(self->alloc, self->data, cap, sizeof(%s));\n"
              "        self->cap = cap;\n"
              "    }\n"
              "    self->data[self->len++] = item;\n"
              "}\n\n",
              n, n, e, e);
    }
    if (t->used & USE_LIST_CLEAR) {
        sb_printf(out, "static inline void %s_clear(%s *self) {\n    self->len = 0;\n}\n\n", n, n);
    }
    if (t->used & USE_LIST_POP) {
        const char *opt = t->elem->optional_cache->c_name;
        sb_printf(out,
                  "static inline %s %s_pop(%s *self) {\n    %s r = {0};\n    if (self->len > 0) {\n"
                  "        r.has = true;\n        r.value = self->data[--self->len];\n    }\n    return r;\n}\n\n",
                  opt, n, n, opt);
    }
    if (t->used & USE_LIST_ITEMS) {
        sb_printf(out, "static inline %s %s_items(%s *self) {\n    %s s = {self->data, self->len};\n    return s;\n}\n\n",
              t->elem->slice_cache[0]->c_name, n, n, t->elem->slice_cache[0]->c_name);
    }
}

// The hash of a Map key (§5.5 key types).
static const char *hash(const Type *t, const char *v) {
    switch (t->kind) {
    case TY_ENUM:
        return arena_printf("a0_hash_word((uint64_t)%s.tag)", v);
    case TY_STRUCT:
        return arena_printf("a0_hash_%s(%s)", t->c_name, v);
    default:
        return arena_printf("a0_hash_word((uint64_t)%s)", v);
    }
}

static void use_hash(const Type *t) {
    if (t->kind != TY_STRUCT || (t->used & USE_HASH)) {
        return;
    }
    use(t, USE_HASH);
    for (int i = 0; i < t->nfields; i++) {
        use_hash(t->fields[i].type);
    }
}

static void define_hash(StrBuf *out, const Type *t) {
    sb_printf(out, "static inline uint64_t a0_hash_%s(%s v) {\n    uint64_t h = 0;\n    (void)v;\n", t->c_name, t->c_name);
    for (int i = 0; i < t->nfields; i++) {
        sb_printf(out, "    h = a0_hash_mix(h, %s);\n", hash(t->fields[i].type, arena_printf("v.%s", t->fields[i].c_name)));
    }
    sb_puts(out, "    return h;\n}\n\n");
}

// Operations of alloc.Map(K, V) (§11): entries stay in insertion order; the
// slots index them by hash, with linear probing. A put keeps at least half of
// the slots empty, counting removed entries, so every probe ends.
static void define_map_ops(StrBuf *out, const Type *t) {
    const char *n = t->c_name;
    const char *k = c_type(t->key);
    const char *v = c_type(t->elem);
    const char *opt = t->elem->optional_cache->c_name;
    bool find = t->used & (USE_MAP_PUT | USE_MAP_GET | USE_MAP_REMOVE);
    if (find) {
        use_hash(t->key);
    }
    if (t->used & USE_INIT) {
        sb_printf(out, "static inline %s %s_init(a0_alloc_Allocator a) {\n    %s m = {0};\n    m.alloc = a;\n    return m;\n}\n\n",
                  n, n, n);
    }
    if (find) {
        sb_printf(out,
                  "static inline uint64_t %s_find(%s *self, %s key, bool *found) {\n"
                  "    uint64_t mask = self->nslots - 1;\n"
                  "    uint64_t i = %s & mask;\n"
                  "    uint64_t free = UINT64_MAX;\n"
                  "    for (;;) {\n"
                  "        uint64_t s = self->slots[i];\n"
                  "        if (s == 0) {\n"
                  "            *found = false;\n"
                  "            return free != UINT64_MAX ? free : i;\n"
                  "        }\n"
                  "        if (s == UINT64_MAX) {\n"
                  "            if (free == UINT64_MAX) {\n"
                  "                free = i;\n"
                  "            }\n"
                  "        } else if (%s) {\n"
                  "            *found = true;\n"
                  "            return i;\n"
                  "        }\n"
                  "        i = (i + 1) & mask;\n"
                  "    }\n"
                  "}\n\n",
                  n, n, k, hash(t->key, "key"), condition(equal(t->key, "self->entries[s - 1].key", "key")));
    }
    if (t->used & USE_MAP_PUT) {
        sb_printf(out,
                  "static inline void %s_rebuild(%s *self, uint64_t nslots) {\n"
                  "    uint64_t live = 0;\n"
                  "    for (uint64_t i = 0; i < self->len; i++) {\n"
                  "        if (self->entries[i].live) {\n"
                  "            self->entries[live++] = self->entries[i];\n"
                  "        }\n"
                  "    }\n"
                  "    self->len = live;\n"
                  "    self->slots = a0_realloc(self->alloc, self->slots, nslots, sizeof(uint64_t));\n"
                  "    self->nslots = nslots;\n"
                  "    for (uint64_t i = 0; i < nslots; i++) {\n"
                  "        self->slots[i] = 0;\n"
                  "    }\n"
                  "    for (uint64_t i = 0; i < live; i++) {\n"
                  "        bool found;\n"
                  "        self->slots[%s_find(self, self->entries[i].key, &found)] = i + 1;\n"
                  "    }\n"
                  "}\n\n",
                  n, n, n);
        sb_printf(out,
                  "static inline void %s_put(%s *self, %s key, %s value) {\n"
                  "    if ((self->len + 1) * 2 > self->nslots) {\n"
                  "        uint64_t nslots = 16;\n"
                  "        while (nslots < (self->live + 1) * 4) {\n"
                  "            nslots *= 2;\n"
                  "        }\n"
                  "        %s_rebuild(self, nslots);\n"
                  "    }\n"
                  "    bool found;\n"
                  "    uint64_t s = %s_find(self, key, &found);\n"
                  "    if (found) {\n"
                  "        self->entries[self->slots[s] - 1].value = value;\n"
                  "        return;\n"
                  "    }\n"
                  "    if (self->len == self->cap) {\n"
                  "        self->cap = self->cap ? self->cap * 2 : 8;\n"
                  "        self->entries = a0_realloc(self->alloc, self->entries, self->cap, sizeof *self->entries);\n"
                  "    }\n"
                  "    self->entries[self->len].key = key;\n"
                  "    self->entries[self->len].value = value;\n"
                  "    self->entries[self->len].live = true;\n"
                  "    self->slots[s] = ++self->len;\n"
                  "    self->live++;\n"
                  "}\n\n",
                  n, n, k, v, n, n);
    }
    if (t->used & USE_MAP_GET) {
        sb_printf(out,
                  "static inline %s %s_get(%s *self, %s key) {\n"
                  "    %s r = {0};\n"
                  "    bool found = false;\n"
                  "    uint64_t s = self->nslots ? %s_find(self, key, &found) : 0;\n"
                  "    if (found) {\n"
                  "        r.has = true;\n"
                  "        r.value = self->entries[self->slots[s] - 1].value;\n"
                  "    }\n"
                  "    return r;\n"
                  "}\n\n",
                  opt, n, n, k, opt, n);
    }
    if (t->used & USE_MAP_REMOVE) {
        sb_printf(out,
                  "static inline %s %s_remove(%s *self, %s key) {\n"
                  "    %s r = {0};\n"
                  "    bool found = false;\n"
                  "    uint64_t s = self->nslots ? %s_find(self, key, &found) : 0;\n"
                  "    if (found) {\n"
                  "        uint64_t e = self->slots[s] - 1;\n"
                  "        r.has = true;\n"
                  "        r.value = self->entries[e].value;\n"
                  "        self->entries[e].live = false;\n"
                  "        self->slots[s] = UINT64_MAX;\n"
                  "        self->live--;\n"
                  "    }\n"
                  "    return r;\n"
                  "}\n\n",
                  opt, n, n, k, opt, n);
    }
}

// a0_eq_T for a struct type that == works on, field by field (§5.5).
static void define_equality(StrBuf *out, const Type *t) {
    sb_printf(out, "static inline bool a0_eq_%s(%s a, %s b) {\n    (void)a;\n    (void)b;\n    return true", t->c_name,
              t->c_name, t->c_name);
    for (int i = 0; i < t->nfields; i++) {
        const char *f = t->fields[i].c_name;
        sb_printf(out, " && %s", equal(t->fields[i].type, arena_printf("a.%s", f), arena_printf("b.%s", f)));
    }
    sb_puts(out, ";\n}\n\n");
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
    for (size_t i = 0; i < fn->params.len; i++) {
        line(em, "(void)v_%s;", VEC_AT(fn->params, Param, i)->name); // Arche allows unused parameters
    }
    emit_scoped(em, fn->body);
    if (returns_error_code(fn->ret) && !block_ends(fn->body)) {
        line(em, "return 0;");
    }
    em->indent--;
    line(em, "}");
    line(em, "%s", "");
}

static void all_fns(const Program *p, Vec *out) {
    for (size_t i = 0; i < p->modules.len; i++) {
        const Module *m = p->modules.items[i];
        for (size_t j = 0; j < m->fns.len; j++) {
            vec_push(out, m->fns.items[j]);
        }
        for (size_t j = 0; j < m->types.len; j++) {
            const TypeDecl *td = m->types.items[j];
            for (size_t k = 0; k < td->methods.len; k++) {
                vec_push(out, td->methods.items[k]);
            }
        }
    }
}

void emit_program(Program *p, StrBuf *out) {
    Vec fns = {0};
    all_fns(p, &fns);
    StrBuf body = {0};
    Emitter em = {.out = &body};
    for (size_t i = 0; i < fns.len; i++) {
        emit_fn(&em, fns.items[i]);
    }

    sb_printf(out, "// Generated by arche0 from %s. Do not edit.\n#include \"arche0_rt.h\"\n\n", p->root->file->display);
    for (size_t i = 0; i < em.files.len; i++) {
        const SourceFile *f = em.files.items[i];
        sb_printf(out, "static const char a0_file_%zu[] = %s;\n", i, c_string(f->display, strlen(f->display)));
    }
    sb_puts(out, "\n");

    Vec *types = composite_types();
    for (size_t i = 0; i < types->len; i++) {
        const Type *t = types->items[i];
        if (!runtime_defined(t)) {
            sb_printf(out, "typedef struct %s %s;\n", t->c_name, t->c_name);
        }
    }
    sb_puts(out, "\n");
    Vec done = {0};
    for (size_t i = 0; i < types->len; i++) {
        define_type(out, types->items[i], &done);
    }
    // Container operations go after the equality and hash helpers they
    // call; generating them first records which of those are used.
    StrBuf ops = {0};
    sb_add(&ops, "", 0);
    for (size_t i = 0; i < types->len; i++) {
        const Type *t = types->items[i];
        if (t->kind == TY_LIST) {
            define_list_ops(&ops, t);
        } else if (t->kind == TY_MAP) {
            define_map_ops(&ops, t);
        }
    }
    // Struct equality and hashing, in definition order so that a struct's
    // fields come first.
    for (size_t i = 0; i < done.len; i++) {
        const Type *t = done.items[i];
        if (t->kind == TY_STRUCT && (t->used & USE_EQ)) {
            define_equality(out, t);
        }
        if (t->kind == TY_STRUCT && (t->used & USE_HASH)) {
            define_hash(out, t);
        }
    }
    sb_add(out, ops.data, ops.len);
    free(ops.data);

    for (size_t i = 0; i < fns.len; i++) {
        sb_printf(out, "%s;\n", signature(fns.items[i]));
    }
    sb_puts(out, "\n");
    sb_add(out, body.data, body.len);
    free(body.data);

    FnDecl *main = NULL;
    for (size_t i = 0; i < p->root->fns.len; i++) {
        if (strcmp(VEC_AT(p->root->fns, FnDecl, i)->name, "main") == 0) {
            main = VEC_AT(p->root->fns, FnDecl, i);
        }
    }
    sb_printf(out, "a0_err a0_entry(a0_std_System *sys) {\n    return %s(sys);\n}\n\n", main->c_name);
    sb_puts(out, "const char *const a0_error_names[] = {\"\"");
    for (int id = 1; id <= error_count(); id++) {
        const char *name = error_name(id);
        sb_printf(out, ", %s", c_string(name, strlen(name)));
    }
    sb_puts(out, "};\n");
}
