// Name resolution and type checking.
//
// stage0 checks what it needs to emit correct C, plus whatever is cheap.
// The checks spec §12 lets it skip are marked "erasable" where they appear.

#include "arche0.h"

#include <string.h>

typedef struct {
    const char *name;
    const char *root; // "std" or "alloc"
    Loc loc;
} ModuleAlias;

typedef struct {
    Module *module;
    Vec aliases;     // ModuleAlias *
    FnDecl *fn;      // the function being checked; NULL inside a constant
    Vec scope;       // Binding *, innermost last
    int loops;       // enclosing `while` loops
    int defer_loops; // `loops` where the innermost `defer` began, or -1 outside defers
} Checker;

typedef struct {
    const char *name;
    Type *type;
} ParamInfo;

// ---- lookup -----------------------------------------------------------------

static FnDecl *find_fn(const Module *m, const char *name) {
    for (size_t i = 0; i < m->fns.len; i++) {
        FnDecl *fn = VEC_AT(m->fns, FnDecl, i);
        if (strcmp(fn->name, name) == 0) {
            return fn;
        }
    }
    return NULL;
}

static ConstDecl *find_const(const Module *m, const char *name) {
    for (size_t i = 0; i < m->consts.len; i++) {
        ConstDecl *k = VEC_AT(m->consts, ConstDecl, i);
        if (strcmp(k->name, name) == 0) {
            return k;
        }
    }
    return NULL;
}

static ModuleAlias *find_alias(const Checker *c, const char *name) {
    for (size_t i = 0; i < c->aliases.len; i++) {
        ModuleAlias *a = VEC_AT(c->aliases, ModuleAlias, i);
        if (strcmp(a->name, name) == 0) {
            return a;
        }
    }
    return NULL;
}

static Binding *find_binding(const Checker *c, const char *name) {
    for (size_t i = c->scope.len; i > 0; i--) {
        Binding *b = VEC_AT(c->scope, Binding, i - 1);
        if (strcmp(b->name, name) == 0) {
            return b;
        }
    }
    return NULL;
}

static bool is_predeclared(const char *name) {
    static const char *const names[] = {"bool", "str", "void", "noreturn"};
    IntKind k;
    BuiltinFn f;
    if (int_type_lookup(name, &k) || builtin_fn_lookup(name, &f)) {
        return true;
    }
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        if (strcmp(names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

// Erasable (§6.3), but cheap: a name cannot be declared while it is visible.
static Binding *declare(Checker *c, const char *name, Loc loc, Type *type, bool mutable) {
    Binding *old = find_binding(c, name);
    if (old) {
        fatal_at(loc, "'%s' is already declared on line %u; Arche has no shadowing", name, (unsigned)old->loc.line);
    }
    if (find_const(c->module, name) || find_fn(c->module, name) || find_alias(c, name) || is_predeclared(name)) {
        fatal_at(loc, "'%s' already names something in this module; Arche has no shadowing", name);
    }
    Binding *b = NEW(Binding);
    b->name = name;
    b->loc = loc;
    b->type = type;
    b->mutable = mutable;
    vec_push(&c->scope, b);
    return b;
}

// ---- types ------------------------------------------------------------------

static const char *path_text(const TypeExpr *te) {
    StrBuf b = {0};
    for (size_t i = 0; i < te->segments.len; i++) {
        if (i > 0) {
            sb_puts(&b, ".");
        }
        sb_puts(&b, VEC_AT(te->segments, const char, i));
    }
    return b.data;
}

static Type *resolve_type(Checker *c, TypeExpr *te) {
    switch (te->kind) {
    case TE_PATH: {
        if (te->args.len > 0) {
            fatal_at(te->loc, "stage0 does not support generic types yet");
        }
        const char *first = VEC_AT(te->segments, const char, 0);
        if (te->segments.len == 1) {
            IntKind k;
            if (int_type_lookup(first, &k)) {
                return type_int(k);
            }
            if (strcmp(first, "bool") == 0) {
                return type_bool();
            }
            if (strcmp(first, "str") == 0) {
                return type_str();
            }
            if (strcmp(first, "void") == 0) {
                return type_void();
            }
            if (strcmp(first, "noreturn") == 0) {
                return type_noreturn();
            }
        } else if (te->segments.len == 2) {
            ModuleAlias *a = find_alias(c, first);
            if (a) {
                const BuiltinType *b = builtin_type_lookup(a->root, VEC_AT(te->segments, const char, 1));
                if (b) {
                    return b->type;
                }
            }
        }
        fatal_at(te->loc, "unknown type '%s'", path_text(te));
    }
    case TE_REF:
        return type_ref(te->mut, resolve_type(c, te->elem));
    case TE_ERROR_UNION:
        if (te->set) {
            fatal_at(te->set->loc, "stage0 does not support error sets yet");
        }
        return type_error_union(resolve_type(c, te->elem));
    case TE_OPTIONAL:
    case TE_SLICE:
    case TE_ARRAY:
        break;
    }
    fatal_at(te->loc, "stage0 does not support this type yet");
}

static bool is_void_result(const Type *t) {
    return t->kind == TY_VOID || (t->kind == TY_ERROR_UNION && t->elem->kind == TY_VOID);
}

static bool is_int(const Type *t) {
    return t->kind == TY_INT;
}

static bool is_unsigned(const Type *t) {
    return t->kind == TY_INT && !int_signed(t->int_kind);
}

// ---- compile-time integers ----------------------------------------------------

static int64_t bits_to_i64(uint64_t u) {
    return u <= (uint64_t)INT64_MAX ? (int64_t)u : (int64_t)(u - (uint64_t)INT64_MAX - 1) + INT64_MIN;
}

// Two's complement bits of v, truncated to T's width and sign-extended.
static uint64_t normalize(const Type *t, uint64_t bits) {
    int n = int_bits(t->int_kind);
    if (n == 64) {
        return bits;
    }
    uint64_t mask = ((uint64_t)1 << n) - 1;
    bits &= mask;
    if (int_signed(t->int_kind) && (bits >> (n - 1)) & 1) {
        bits |= ~mask;
    }
    return bits;
}

static bool signed_fits(const Type *t, int64_t v) {
    return v >= int_min(t->int_kind) && v <= (int64_t)int_max(t->int_kind);
}

static ConstValue const_eval(Checker *c, Expr *e);

static _Noreturn void const_overflow(const Expr *e) {
    fatal_at(e->loc, "this constant expression overflows %s", type_name(e->type));
}

static uint64_t const_int(Checker *c, Expr *e) {
    return const_eval(c, e).bits;
}

static uint64_t const_binary_int(Checker *c, Expr *e) {
    const Type *t = e->type;
    bool sig = int_signed(t->int_kind);
    uint64_t a = const_int(c, e->base);
    uint64_t b = const_int(c, e->rhs);
    int64_t sa = bits_to_i64(a);
    int64_t sb = bits_to_i64(b);
    switch (e->binary) {
    case B_ADD:
        if (sig) {
            if ((sb > 0 && sa > INT64_MAX - sb) || (sb < 0 && sa < INT64_MIN - sb) || !signed_fits(t, sa + sb)) {
                const_overflow(e);
            }
            return (uint64_t)(sa + sb);
        }
        if (a + b < a || a + b > int_max(t->int_kind)) {
            const_overflow(e);
        }
        return a + b;
    case B_SUB:
        if (sig) {
            if ((sb < 0 && sa > INT64_MAX + sb) || (sb > 0 && sa < INT64_MIN + sb) || !signed_fits(t, sa - sb)) {
                const_overflow(e);
            }
            return (uint64_t)(sa - sb);
        }
        if (a < b) {
            const_overflow(e);
        }
        return a - b;
    case B_MUL:
        if (sig) {
            bool over = sa != 0 && sb != 0 &&
                        ((sa > 0 && sb > 0 && sa > INT64_MAX / sb) || (sa > 0 && sb < 0 && sb < INT64_MIN / sa) ||
                         (sa < 0 && sb > 0 && sa < INT64_MIN / sb) || (sa < 0 && sb < 0 && sb < INT64_MAX / sa));
            if (over || !signed_fits(t, sa * sb)) {
                const_overflow(e);
            }
            return (uint64_t)(sa * sb);
        }
        if ((b != 0 && a > UINT64_MAX / b) || a * b > int_max(t->int_kind)) {
            const_overflow(e);
        }
        return a * b;
    case B_DIV:
    case B_REM:
        if (b == 0) {
            fatal_at(e->loc, "division by zero in a constant expression");
        }
        if (sig) {
            if (sb == -1) {
                if (e->binary == B_REM) {
                    return 0;
                }
                if (sa == int_min(t->int_kind)) {
                    const_overflow(e);
                }
                return (uint64_t)-sa;
            }
            return (uint64_t)(e->binary == B_DIV ? sa / sb : sa % sb);
        }
        return e->binary == B_DIV ? a / b : a % b;
    case B_WADD:
        return normalize(t, a + b);
    case B_WSUB:
        return normalize(t, a - b);
    case B_WMUL:
        return normalize(t, a * b);
    case B_SHL:
    case B_SHR:
        if (b >= (uint64_t)int_bits(t->int_kind)) {
            fatal_at(e->loc, "shift amount %llu is too large for %s", (unsigned long long)b, type_name(t));
        }
        if (e->binary == B_SHL) {
            return normalize(t, a << b);
        }
        return sig ? (uint64_t)(sa >= 0 ? sa >> b : ~(~sa >> b)) : a >> b;
    case B_BITAND:
        return a & b;
    case B_BITOR:
        return a | b;
    case B_BITXOR:
        return a ^ b;
    default:
        break;
    }
    fatal_at(e->loc, "internal error: not an integer operator");
}

static bool const_compare(Checker *c, Expr *e) {
    Expr *l = e->base;
    if (l->type->kind == TY_BOOL) {
        bool a = const_int(c, l) != 0;
        bool b = const_int(c, e->rhs) != 0;
        return e->binary == B_EQ ? a == b : a != b;
    }
    uint64_t a = const_int(c, l);
    uint64_t b = const_int(c, e->rhs);
    int cmp;
    if (int_signed(l->type->int_kind)) {
        cmp = bits_to_i64(a) < bits_to_i64(b) ? -1 : bits_to_i64(a) > bits_to_i64(b);
    } else {
        cmp = a < b ? -1 : a > b;
    }
    switch (e->binary) {
    case B_EQ:
        return cmp == 0;
    case B_NE:
        return cmp != 0;
    case B_LT:
        return cmp < 0;
    case B_LE:
        return cmp <= 0;
    case B_GT:
        return cmp > 0;
    default:
        return cmp >= 0;
    }
}

static ConstValue const_eval(Checker *c, Expr *e) {
    ConstValue v = {0};
    switch (e->kind) {
    case E_INT:
    case E_CHAR:
    case E_BOOL:
        v.bits = e->value;
        return v;
    case E_STRING:
        v.bytes = e->bytes;
        v.len = e->bytes_len;
        return v;
    case E_IDENT:
        return e->konst->val;
    case E_UNARY:
        if (e->negative_literal) {
            v.bits = normalize(e->type, ~e->value + 1);
        } else if (e->unary == U_NOT) {
            v.bits = !const_int(c, e->base);
        } else if (e->unary == U_BITNOT) {
            v.bits = normalize(e->type, ~const_int(c, e->base));
        } else {
            int64_t x = bits_to_i64(const_int(c, e->base));
            if (x == int_min(e->type->int_kind)) {
                const_overflow(e);
            }
            v.bits = (uint64_t)-x;
        }
        return v;
    case E_BINARY:
        switch (e->binary) {
        case B_AND:
            v.bits = const_int(c, e->base) && const_int(c, e->rhs);
            return v;
        case B_OR:
            v.bits = const_int(c, e->base) || const_int(c, e->rhs);
            return v;
        case B_EQ:
        case B_NE:
        case B_LT:
        case B_LE:
        case B_GT:
        case B_GE:
            v.bits = const_compare(c, e);
            return v;
        default:
            v.bits = const_binary_int(c, e);
            return v;
        }
    case E_CAST: {
        uint64_t x = const_int(c, e->base);
        const Type *from = e->base->type;
        const Type *to = e->type;
        if (e->wrapping) {
            v.bits = normalize(to, x);
            return v;
        }
        bool fits = int_signed(from->int_kind) ? (int_signed(to->int_kind) ? signed_fits(to, bits_to_i64(x))
                                                                          : bits_to_i64(x) >= 0 && x <= int_max(to->int_kind))
                                               : x <= int_max(to->int_kind);
        if (!fits) {
            fatal_at(e->loc, "this constant does not fit in %s", type_name(to));
        }
        v.bits = x;
        return v;
    }
    default:
        break;
    }
    fatal_at(e->loc, "this is not a constant expression");
}

// ---- expressions ------------------------------------------------------------

bool expr_is_place(const Expr *e) {
    switch (e->kind) {
    case E_IDENT:
        return e->name_kind == NAME_BINDING;
    case E_FIELD:
        return e->base->type->kind == TY_REF || expr_is_place(e->base);
    case E_DEREF:
        return true;
    default:
        return false;
    }
}

static bool place_is_mutable(const Expr *e) {
    switch (e->kind) {
    case E_IDENT:
        return e->binding->mutable;
    case E_FIELD:
        if (e->base->type->kind == TY_REF) {
            return e->base->type->mut;
        }
        return place_is_mutable(e->base);
    case E_DEREF:
        return e->base->type->mut;
    default:
        return false;
    }
}

static Type *check_expr(Checker *c, Expr *e, Type *expected);

static void expect_type(const Expr *e, const Type *got, const Type *want) {
    if (got != want && got->kind != TY_NORETURN) {
        fatal_at(e->loc, "expected %s, found %s", type_name(want), type_name(got));
    }
}

static void check_value(Checker *c, Expr *e, Type *want) {
    expect_type(e, check_expr(c, e, want), want);
}

// An integer literal, or arithmetic of them only, which takes its type from
// context (§5.6).
static bool untyped(const Expr *e) {
    switch (e->kind) {
    case E_INT:
        return true;
    case E_UNARY:
        return e->unary != U_NOT && untyped(e->base);
    case E_BINARY:
        return e->binary <= B_BITXOR && e->binary != B_SHL && e->binary != B_SHR && untyped(e->base) &&
               untyped(e->rhs);
    default:
        return false;
    }
}

static void check_args(Checker *c, Expr *call, const char *callee, const ParamInfo *params, size_t n) {
    if (call->args.len != n) {
        fatal_at(call->loc, "%s takes %zu argument%s, but %zu %s given", callee, n, n == 1 ? "" : "s", call->args.len,
                 call->args.len == 1 ? "is" : "are");
    }
    for (size_t i = 0; i < n; i++) {
        Arg *a = VEC_AT(call->args, Arg, i);
        if (a->label && strcmp(a->label, params[i].name) != 0) {
            // Erasable (§7.5), but a wrong label is almost always a swapped argument.
            fatal_at(a->loc, "this argument is labeled '%s', but parameter %zu of %s is '%s'", a->label, i + 1, callee,
                     params[i].name);
        }
        check_value(c, a->value, params[i].type);
    }
}

static Type *check_builtin_call(Checker *c, Expr *call, BuiltinFn fn) {
    ParamInfo panic_params[] = {{"message", type_str()}};
    ParamInfo assert_params[] = {{"condition", type_bool()}};
    call->call = CALL_BUILTIN_FN;
    call->builtin_fn = fn;
    switch (fn) {
    case BUILTIN_PANIC:
        check_args(c, call, "panic", panic_params, 1);
        return type_noreturn();
    case BUILTIN_ASSERT:
        check_args(c, call, "assert", assert_params, 1);
        return type_void();
    case BUILTIN_UNREACHABLE:
        check_args(c, call, "unreachable", NULL, 0);
        return type_noreturn();
    }
    return type_void();
}

static Type *check_method_call(Checker *c, Expr *call) {
    Expr *callee = call->callee;
    Expr *receiver = callee->base;
    Type *rt = check_expr(c, receiver, NULL);
    Type *t = rt->kind == TY_REF ? rt->elem : rt;
    const Method *m = method_lookup(t, callee->name);
    if (!m) {
        fatal_at(callee->loc, "%s has no method '%s'", type_name(t), callee->name);
    }
    const char *display = arena_printf("%s.%s", type_name(t), m->name);
    if (m->self == SELF_MUT_REF) {
        if (rt->kind == TY_REF ? !rt->mut : !(expr_is_place(receiver) && place_is_mutable(receiver))) {
            fatal_at(receiver->loc, "%s needs a mutable receiver: a 'var', or a place reached through '&mut'", display);
        }
    }
    ParamInfo params[3];
    for (int i = 0; i < m->nparams; i++) {
        params[i] = (ParamInfo){m->params[i].name, m->params[i].type};
    }
    check_args(c, call, display, params, (size_t)m->nparams);
    call->call = CALL_METHOD;
    call->method = m;
    call->receiver = receiver;
    return m->ret;
}

static Type *check_call(Checker *c, Expr *call) {
    Expr *callee = call->callee;
    if (callee->kind == E_FIELD) {
        return check_method_call(c, call);
    }
    if (callee->kind != E_IDENT || find_binding(c, callee->name)) {
        fatal_at(callee->loc, "this expression cannot be called");
    }
    FnDecl *fn = find_fn(c->module, callee->name);
    if (fn) {
        ParamInfo params[64];
        if (fn->params.len > 64) {
            fatal_at(fn->loc, "stage0 supports at most 64 parameters");
        }
        for (size_t i = 0; i < fn->params.len; i++) {
            Param *p = VEC_AT(fn->params, Param, i);
            params[i] = (ParamInfo){p->name, p->binding->type};
        }
        check_args(c, call, arena_printf("'%s'", fn->name), params, fn->params.len);
        call->call = CALL_FN;
        call->fn = fn;
        return fn->ret;
    }
    BuiltinFn bf;
    if (builtin_fn_lookup(callee->name, &bf)) {
        return check_builtin_call(c, call, bf);
    }
    fatal_at(callee->loc, "unknown function '%s'", callee->name);
}

static void check_const(Checker *c, ConstDecl *k);

static Type *check_ident(Checker *c, Expr *e) {
    Binding *b = find_binding(c, e->name);
    if (b) {
        e->name_kind = NAME_BINDING;
        e->binding = b;
        return b->type;
    }
    ConstDecl *k = find_const(c->module, e->name);
    if (k) {
        check_const(c, k);
        e->name_kind = NAME_CONST;
        e->konst = k;
        return k->type;
    }
    BuiltinFn bf;
    if (find_fn(c->module, e->name) || builtin_fn_lookup(e->name, &bf)) {
        fatal_at(e->loc, "'%s' is a function; in Arche-0 functions can only be called", e->name);
    }
    if (find_alias(c, e->name)) {
        fatal_at(e->loc, "'%s' is a module, not a value", e->name);
    }
    fatal_at(e->loc, "unknown name '%s'", e->name);
}

static const char *binary_text(BinaryOp op) {
    static const char *const text[] = {
        [B_ADD] = "+",    [B_SUB] = "-",     [B_MUL] = "*",   [B_DIV] = "/",  [B_REM] = "%",  [B_WADD] = "+%",
        [B_WSUB] = "-%",  [B_WMUL] = "*%",   [B_SHL] = "<<",  [B_SHR] = ">>", [B_BITAND] = "&", [B_BITOR] = "|",
        [B_BITXOR] = "^", [B_EQ] = "==",     [B_NE] = "!=",   [B_LT] = "<",   [B_LE] = "<=",  [B_GT] = ">",
        [B_GE] = ">=",    [B_AND] = "and",   [B_OR] = "or",
    };
    return text[op];
}

// The operands of a binary operator have one type; a literal takes it from the
// other operand.
static Type *check_operands(Checker *c, Expr *e, Type *expected) {
    Type *lt;
    Type *rt;
    if (untyped(e->base) && !untyped(e->rhs)) {
        rt = check_expr(c, e->rhs, expected);
        lt = check_expr(c, e->base, rt);
    } else {
        lt = check_expr(c, e->base, expected);
        rt = check_expr(c, e->rhs, lt);
    }
    if (lt != rt) {
        fatal_at(e->loc, "the operands of '%s' have different types: %s and %s", binary_text(e->binary), type_name(lt),
                 type_name(rt));
    }
    return lt;
}

static Type *check_binary(Checker *c, Expr *e, Type *expected) {
    const char *op = binary_text(e->binary);
    switch (e->binary) {
    case B_AND:
    case B_OR:
        check_value(c, e->base, type_bool());
        check_value(c, e->rhs, type_bool());
        return type_bool();
    case B_EQ:
    case B_NE: {
        Type *t = check_operands(c, e, NULL);
        if (!is_int(t) && t->kind != TY_BOOL) {
            fatal_at(e->loc, "'%s' compares integers and bools; compare text with .eq()", op);
        }
        return type_bool();
    }
    case B_LT:
    case B_LE:
    case B_GT:
    case B_GE: {
        Type *t = check_operands(c, e, NULL);
        if (!is_int(t)) {
            fatal_at(e->loc, "'%s' compares integers, not %s", op, type_name(t));
        }
        return type_bool();
    }
    case B_SHL:
    case B_SHR: {
        Type *t = check_expr(c, e->base, expected && is_int(expected) ? expected : NULL);
        if (!is_int(t)) {
            fatal_at(e->loc, "'%s' shifts integers, not %s", op, type_name(t));
        }
        Type *n = untyped(e->rhs) ? check_expr(c, e->rhs, type_int(U32)) : check_expr(c, e->rhs, NULL);
        if (!is_unsigned(n)) {
            fatal_at(e->rhs->loc, "a shift count is an unsigned integer, not %s", type_name(n));
        }
        return t;
    }
    default: {
        Type *t = check_operands(c, e, expected && is_int(expected) ? expected : NULL);
        if (!is_int(t)) {
            fatal_at(e->loc, "'%s' works on integers, not %s", op, type_name(t));
        }
        return t;
    }
    }
}

static Type *check_unary(Checker *c, Expr *e, Type *expected) {
    if (e->unary == U_NOT) {
        check_value(c, e->base, type_bool());
        return type_bool();
    }
    if (e->unary == U_NEG && e->base->kind == E_INT && !e->base->parens) {
        if (!expected || !is_int(expected)) {
            // Erasable (§5.6), but stage0 needs the type to emit the literal.
            fatal_at(e->loc, "an integer literal takes its type from context, e.g. 'let x: i32 = -5;'");
        }
        if (!int_signed(expected->int_kind)) {
            fatal_at(e->loc, "unary '-' needs a signed integer type, and %s is unsigned", type_name(expected));
        }
        uint64_t limit = (uint64_t)(-(int_min(expected->int_kind) + 1)) + 1; // |min|
        if (e->base->value > limit) {
            fatal_at(e->loc, "-%llu does not fit in %s", (unsigned long long)e->base->value, type_name(expected));
        }
        e->negative_literal = true;
        e->value = e->base->value;
        e->base->type = expected;
        return expected;
    }
    Type *t = check_expr(c, e->base, expected && is_int(expected) ? expected : NULL);
    if (!is_int(t)) {
        fatal_at(e->loc, "'%s' works on integers, not %s", e->unary == U_NEG ? "-" : "~", type_name(t));
    }
    if (e->unary == U_NEG && !int_signed(t->int_kind)) {
        fatal_at(e->loc, "unary '-' needs a signed integer type, and %s is unsigned", type_name(t));
    }
    return t;
}

static Type *check_expr_kind(Checker *c, Expr *e, Type *expected) {
    if (!c->fn) {
        switch (e->kind) {
        case E_INT:
        case E_CHAR:
        case E_STRING:
        case E_BOOL:
        case E_IDENT:
        case E_UNARY:
        case E_BINARY:
        case E_CAST:
            break;
        default:
            fatal_at(e->loc, "this is not a constant expression");
        }
    }
    switch (e->kind) {
    case E_INT:
        if (!expected || !is_int(expected)) {
            // Erasable (§5.6), but stage0 needs the type to emit the literal.
            fatal_at(e->loc, "an integer literal takes its type from context, e.g. 'let x: u32 = 5;'");
        }
        if (e->value > int_max(expected->int_kind)) {
            fatal_at(e->loc, "%llu does not fit in %s", (unsigned long long)e->value, type_name(expected));
        }
        return expected;
    case E_CHAR:
        return type_int(U8);
    case E_STRING:
        return type_str();
    case E_BOOL:
        return type_bool();
    case E_IDENT:
        return check_ident(c, e);
    case E_FIELD: {
        Type *bt = check_expr(c, e->base, NULL);
        Type *t = bt->kind == TY_REF ? bt->elem : bt;
        const Field *f = field_lookup(t, e->name);
        if (!f) {
            fatal_at(e->loc, "%s has no field '%s'", type_name(t), e->name);
        }
        return f->type;
    }
    case E_CALL:
        return check_call(c, e);
    case E_DEREF: {
        Type *t = check_expr(c, e->base, NULL);
        if (t->kind != TY_REF) {
            fatal_at(e->loc, "'.*' needs a reference, not %s", type_name(t));
        }
        return t->elem;
    }
    case E_REF: {
        Type *t = check_expr(c, e->base, NULL);
        if (!expr_is_place(e->base)) {
            fatal_at(e->loc, "'&' needs a place: a variable, a field or '.*'");
        }
        if (e->mut && !place_is_mutable(e->base)) {
            fatal_at(e->loc, "'&mut' needs a mutable place: a 'var', or a place reached through '&mut'");
        }
        return type_ref(e->mut, t);
    }
    case E_UNARY:
        return check_unary(c, e, expected);
    case E_BINARY:
        return check_binary(c, e, expected);
    case E_CAST: {
        if (untyped(e->base)) {
            fatal_at(e->base->loc, "an integer literal takes its type from context; 'as' does not give it one");
        }
        Type *from = check_expr(c, e->base, NULL);
        Type *to = resolve_type(c, e->cast_to);
        if (!is_int(from) || !is_int(to)) {
            fatal_at(e->loc, "'%s' converts between integer types, not from %s to %s", e->wrapping ? "as%" : "as",
                     type_name(from), type_name(to));
        }
        return to;
    }
    case E_NONE:
    case E_SOME:
        fatal_at(e->loc, "stage0 does not support optionals yet");
    case E_INDEX:
    case E_SLICE:
        fatal_at(e->loc, "stage0 does not support indexing yet");
    case E_STRUCT_LIT:
        fatal_at(e->loc, "stage0 does not support struct literals yet");
    case E_ARRAY_LIT:
        fatal_at(e->loc, "stage0 does not support arrays yet");
    case E_TRY:
    case E_CATCH:
    case E_ORELSE:
        fatal_at(e->loc, "stage0 does not support error handling yet");
    }
    fatal_at(e->loc, "internal error: unknown expression");
}

static Type *check_expr(Checker *c, Expr *e, Type *expected) {
    e->type = check_expr_kind(c, e, expected);
    return e->type;
}

// ---- constants ------------------------------------------------------------

static void check_const(Checker *c, ConstDecl *k) {
    if (k->state == 2) {
        return;
    }
    if (k->state == 1) {
        fatal_at(k->loc, "constant '%s' depends on itself", k->name);
    }
    k->state = 1;
    Checker sub = {.module = c->module, .aliases = c->aliases, .defer_loops = -1};
    Type *t = resolve_type(&sub, k->type_expr);
    if (!is_int(t) && t->kind != TY_BOOL && t->kind != TY_STR) {
        fatal_at(k->type_expr->loc, "a constant's type is an integer type, bool or str");
    }
    k->type = t;
    check_value(&sub, k->value, t);
    k->val = const_eval(&sub, k->value);
    k->state = 2;
}

// ---- statements -------------------------------------------------------------

static void check_block(Checker *c, Block *b);
static void check_stmt(Checker *c, Stmt *s);

static void check_assign(Checker *c, Stmt *s) {
    Type *t = check_expr(c, s->target, NULL);
    if (!expr_is_place(s->target)) {
        fatal_at(s->target->loc, "only a place can be assigned: a variable, a field or '.*'");
    }
    if (!place_is_mutable(s->target)) {
        fatal_at(s->target->loc, "this place cannot be assigned: it is not a 'var' and not reached through '&mut'");
    }
    switch (s->op) {
    case A_SET:
        check_value(c, s->expr, t);
        return;
    case A_SHL:
    case A_SHR: {
        if (!is_int(t)) {
            fatal_at(s->loc, "shifts work on integers, not %s", type_name(t));
        }
        Type *n = untyped(s->expr) ? check_expr(c, s->expr, type_int(U32)) : check_expr(c, s->expr, NULL);
        if (!is_unsigned(n)) {
            fatal_at(s->expr->loc, "a shift count is an unsigned integer, not %s", type_name(n));
        }
        return;
    }
    default:
        if (!is_int(t)) {
            fatal_at(s->loc, "this operator works on integers, not %s", type_name(t));
        }
        check_value(c, s->expr, t);
        return;
    }
}

static void check_stmt(Checker *c, Stmt *s) {
    switch (s->kind) {
    case S_EXPR: {
        Type *t = check_expr(c, s->expr, NULL);
        if (t->kind != TY_VOID && t->kind != TY_NORETURN) {
            // Erasable (§7.8).
            fatal_at(s->expr->loc, "the %s result is not used; write '_ = ...' to discard it", type_name(t));
        }
        return;
    }
    case S_LET: {
        Type *t;
        if (s->type_expr) {
            t = resolve_type(c, s->type_expr);
            check_value(c, s->expr, t);
        } else {
            t = check_expr(c, s->expr, NULL);
        }
        if (t->kind == TY_VOID || t->kind == TY_NORETURN) {
            fatal_at(s->expr->loc, "this expression has no value to bind");
        }
        s->binding = declare(c, s->name, s->loc, t, s->mutable);
        return;
    }
    case S_ASSIGN:
        check_assign(c, s);
        return;
    case S_DISCARD:
        check_expr(c, s->expr, NULL);
        return;
    case S_IF:
        check_value(c, s->expr, type_bool());
        check_block(c, s->block);
        if (s->els) {
            check_stmt(c, s->els);
        }
        return;
    case S_WHILE:
        check_value(c, s->expr, type_bool());
        c->loops++;
        check_block(c, s->block);
        c->loops--;
        if (s->step) {
            check_stmt(c, s->step);
        }
        return;
    case S_MATCH:
        fatal_at(s->loc, "stage0 does not support 'match' yet");
    case S_RETURN: {
        if (c->defer_loops >= 0) {
            fatal_at(s->loc, "deferred code cannot 'return'");
        }
        Type *ret = c->fn->ret;
        if (!s->expr) {
            if (!is_void_result(ret)) {
                fatal_at(s->loc, "'%s' returns %s, so 'return' needs a value", c->fn->name, type_name(ret));
            }
            return;
        }
        if (ret->kind == TY_VOID) {
            fatal_at(s->expr->loc, "'%s' returns nothing, so 'return' takes no value", c->fn->name);
        }
        check_value(c, s->expr, ret);
        return;
    }
    case S_BREAK:
    case S_CONTINUE: {
        const char *word = s->kind == S_BREAK ? "break" : "continue";
        if (c->loops == 0) {
            fatal_at(s->loc, "'%s' is allowed only inside a 'while' loop", word);
        }
        if (c->defer_loops >= 0 && c->loops == c->defer_loops) {
            fatal_at(s->loc, "deferred code cannot '%s' out of its block", word);
        }
        return;
    }
    case S_DEFER: {
        int saved = c->defer_loops;
        c->defer_loops = c->loops;
        check_stmt(c, s->step);
        c->defer_loops = saved;
        return;
    }
    case S_BLOCK:
        check_block(c, s->block);
        return;
    }
}

static void check_block(Checker *c, Block *b) {
    size_t depth = c->scope.len;
    for (size_t i = 0; i < b->stmts.len; i++) {
        check_stmt(c, VEC_AT(b->stmts, Stmt, i));
    }
    c->scope.len = depth;
}

// ---- where paths end (§10) -------------------------------------------------------

static bool has_own_break(const Block *b);

static bool stmt_has_own_break(const Stmt *s) {
    switch (s->kind) {
    case S_BREAK:
        return true;
    case S_IF:
        return has_own_break(s->block) || (s->els && stmt_has_own_break(s->els));
    case S_BLOCK:
        return has_own_break(s->block);
    case S_MATCH:
        for (size_t i = 0; i < s->arms.len; i++) {
            if (has_own_break(VEC_AT(s->arms, MatchArm, i)->body)) {
                return true;
            }
        }
        return false;
    default:
        return false; // a nested while owns its breaks
    }
}

static bool has_own_break(const Block *b) {
    for (size_t i = 0; i < b->stmts.len; i++) {
        if (stmt_has_own_break(VEC_AT(b->stmts, Stmt, i))) {
            return true;
        }
    }
    return false;
}

static bool stmt_ends(const Stmt *s) {
    switch (s->kind) {
    case S_RETURN:
        return true;
    case S_EXPR:
        return s->expr->type->kind == TY_NORETURN;
    case S_BLOCK:
        return block_ends(s->block);
    case S_IF:
        return s->els && block_ends(s->block) && stmt_ends(s->els);
    case S_WHILE:
        return s->expr->kind == E_BOOL && s->expr->value && !has_own_break(s->block);
    case S_MATCH:
        for (size_t i = 0; i < s->arms.len; i++) {
            if (!block_ends(VEC_AT(s->arms, MatchArm, i)->body)) {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

bool block_ends(const Block *b) {
    for (size_t i = 0; i < b->stmts.len; i++) {
        if (stmt_ends(VEC_AT(b->stmts, Stmt, i))) {
            return true;
        }
    }
    return false;
}

// ---- items ------------------------------------------------------------------

static const char *mangle(const char *module, const char *name) {
    StrBuf b = {0};
    sb_puts(&b, "a0");
    const char *seg = module;
    for (;;) {
        const char *dot = strchr(seg, '.');
        size_t n = dot ? (size_t)(dot - seg) : strlen(seg);
        sb_printf(&b, "_%zu%.*s", n, (int)n, seg);
        if (!dot) {
            break;
        }
        seg = dot + 1;
    }
    sb_printf(&b, "_%zu%s", strlen(name), name);
    return b.data;
}

static void check_imports(Checker *c) {
    for (size_t i = 0; i < c->module->imports.len; i++) {
        Import *imp = VEC_AT(c->module->imports, Import, i);
        const char *first = VEC_AT(imp->path, const char, 0);
        bool library = strcmp(first, "std") == 0 || strcmp(first, "alloc") == 0;
        if (!library || imp->path.len > 1) {
            if (library || strcmp(first, "core") == 0) {
                fatal_at(imp->loc, "Arche-0 provides only the modules 'std' and 'alloc'");
            }
            fatal_at(imp->loc, "stage0 does not support importing package modules yet");
        }
        ModuleAlias *a = NEW(ModuleAlias);
        a->name = imp->alias ? imp->alias : first;
        a->root = first;
        a->loc = imp->loc;
        if (find_alias(c, a->name)) {
            fatal_at(imp->loc, "'%s' is already imported", a->name);
        }
        vec_push(&c->aliases, a);
    }
}

static void check_item_names(Checker *c) {
    Module *m = c->module;
    Vec names = {0}; // const char *, then the Loc of each in `locs`
    Vec locs = {0};
    for (size_t i = 0; i < m->fns.len; i++) {
        vec_push(&names, (void *)VEC_AT(m->fns, FnDecl, i)->name);
        vec_push(&locs, &VEC_AT(m->fns, FnDecl, i)->loc);
    }
    for (size_t i = 0; i < m->consts.len; i++) {
        vec_push(&names, (void *)VEC_AT(m->consts, ConstDecl, i)->name);
        vec_push(&locs, &VEC_AT(m->consts, ConstDecl, i)->loc);
    }
    for (size_t i = 0; i < names.len; i++) {
        const char *name = names.items[i];
        Loc loc = *(Loc *)locs.items[i];
        for (size_t j = 0; j < i; j++) {
            if (strcmp(names.items[j], name) == 0) {
                fatal_at(loc, "'%s' is already declared", name);
            }
        }
        if (find_alias(c, name) || is_predeclared(name)) {
            fatal_at(loc, "'%s' already names something; Arche has no shadowing", name);
        }
    }
}

static void check_signature(Checker *c, FnDecl *fn) {
    for (size_t i = 0; i < fn->params.len; i++) {
        Param *p = VEC_AT(fn->params, Param, i);
        p->binding = NEW(Binding);
        p->binding->name = p->name;
        p->binding->loc = p->loc;
        p->binding->type = resolve_type(c, p->type_expr);
    }
    fn->ret = fn->ret_expr ? resolve_type(c, fn->ret_expr) : type_void();
    fn->c_name = mangle(fn->module->name, fn->name);
}

static void check_main(Checker *c) {
    FnDecl *main = find_fn(c->module, "main");
    if (!main) {
        fatal_at((Loc){c->module->file, 1, 1}, "the root module has no 'fn main(sys: &mut std.System) -> !void'");
    }
    const BuiltinType *system = builtin_type_lookup("std", "System");
    bool ok = main->params.len == 1 && VEC_AT(main->params, Param, 0)->binding->type == type_ref(true, system->type) &&
              main->ret->kind == TY_ERROR_UNION && main->ret->elem->kind == TY_VOID;
    if (!ok) {
        fatal_at(main->loc, "main must be declared as 'fn main(sys: &mut std.System) -> !void'");
    }
}

static void check_fn(Checker *c, FnDecl *fn) {
    c->fn = fn;
    c->scope.len = 0;
    c->loops = 0;
    c->defer_loops = -1;
    for (size_t i = 0; i < fn->params.len; i++) {
        Param *p = VEC_AT(fn->params, Param, i);
        Binding *b = declare(c, p->name, p->loc, p->binding->type, false);
        p->binding = b;
    }
    check_block(c, fn->body);
    if (!is_void_result(fn->ret) && !block_ends(fn->body)) {
        fatal_at(fn->loc, "'%s' can reach its end without returning %s", fn->name, type_name(fn->ret));
    }
}

void check_program(Module *root) {
    Checker c = {.module = root, .defer_loops = -1};
    check_imports(&c);
    if (root->types.len > 0) {
        fatal_at(VEC_AT(root->types, TypeDecl, 0)->loc, "stage0 does not support type declarations yet");
    }
    check_item_names(&c);
    for (size_t i = 0; i < root->consts.len; i++) {
        check_const(&c, VEC_AT(root->consts, ConstDecl, i));
    }
    for (size_t i = 0; i < root->fns.len; i++) {
        check_signature(&c, VEC_AT(root->fns, FnDecl, i));
    }
    check_main(&c);
    for (size_t i = 0; i < root->fns.len; i++) {
        check_fn(&c, VEC_AT(root->fns, FnDecl, i));
    }
}
