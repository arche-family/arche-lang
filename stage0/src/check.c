// Name resolution and type checking.
//
// stage0 checks what it needs to emit correct C, plus whatever is cheap.
// The checks spec §12 lets it skip are marked "erasable" where they appear.
//
// The whole program is checked at once, in passes: imports, then the names
// of every module's items, then type declarations, constants, signatures, and
// last the bodies of functions. Each piece of code is checked in the context
// of the module that contains it.

#include "arche0.h"

#include <string.h>

typedef struct {
    Program *program;
    Module *module;  // the module whose code is being checked
    FnDecl *fn;      // the function being checked; NULL inside a constant
    Vec scope;       // Binding *, innermost last
    int loops;       // enclosing `while` loops
    int defer_loops; // `loops` where the innermost `defer` began, or -1 outside defers
} Checker;

typedef struct {
    const char *name;
    Type *type;
} ParamInfo;

static Vec error_names; // const char *: the error numbered n is error_names[n - 1]

static Checker in_module(const Checker *c, Module *m) {
    Checker sub = {.program = c->program, .module = m, .defer_loops = -1};
    return sub;
}

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

static TypeDecl *find_type(const Module *m, const char *name) {
    for (size_t i = 0; i < m->types.len; i++) {
        TypeDecl *td = VEC_AT(m->types, TypeDecl, i);
        if (strcmp(td->name, name) == 0) {
            return td;
        }
    }
    return NULL;
}

static ModuleAlias *find_alias(const Module *m, const char *name) {
    for (size_t i = 0; i < m->aliases.len; i++) {
        ModuleAlias *a = VEC_AT(m->aliases, ModuleAlias, i);
        if (strcmp(a->name, name) == 0) {
            return a;
        }
    }
    return NULL;
}

static FnDecl *find_method(const TypeDecl *td, const char *name) {
    for (size_t i = 0; i < td->methods.len; i++) {
        FnDecl *fn = VEC_AT(td->methods, FnDecl, i);
        if (strcmp(fn->name, name) == 0) {
            return fn;
        }
    }
    return NULL;
}

static VariantDecl *find_variant(const TypeDecl *td, const char *name) {
    for (size_t i = 0; i < td->variants.len; i++) {
        VariantDecl *v = VEC_AT(td->variants, VariantDecl, i);
        if (strcmp(v->name, name) == 0) {
            return v;
        }
    }
    return NULL;
}

static ErrorName *find_error(const TypeDecl *td, const char *name) {
    for (size_t i = 0; i < td->errors.len; i++) {
        ErrorName *e = VEC_AT(td->errors, ErrorName, i);
        if (strcmp(e->name, name) == 0) {
            return e;
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

static bool names_item(const Module *m, const char *name) {
    return find_const(m, name) || find_fn(m, name) || find_type(m, name) || find_alias(m, name) || is_predeclared(name);
}

// Erasable (§6.3), but cheap: a name cannot be declared while it is visible.
static Binding *declare(Checker *c, const char *name, Loc loc, Type *type, bool mutable) {
    Binding *old = find_binding(c, name);
    if (old) {
        fatal_at(loc, "'%s' is already declared on line %u; Arche has no shadowing", name, (unsigned)old->loc.line);
    }
    if (names_item(c->module, name)) {
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

// Erasable (§4), but cheap: another module reaches only what is `pub`.
static void require_visible(const Checker *c, Loc loc, const Module *owner, bool pub, const char *what) {
    if (owner != c->module && !pub) {
        fatal_at(loc, "%s is private to module '%s'; mark it 'pub' to use it here", what, owner->name);
    }
}

// An error of an inferred set: any error name of the program.
static ErrorName *global_error(const char *name) {
    for (size_t i = 0; i < error_names.len; i++) {
        if (strcmp(error_names.items[i], name) == 0) {
            ErrorName *e = NEW(ErrorName);
            e->name = name;
            e->id = (int)i + 1;
            return e;
        }
    }
    return NULL;
}

static int error_id(const char *name) {
    for (size_t i = 0; i < error_names.len; i++) {
        if (strcmp(error_names.items[i], name) == 0) {
            return (int)i + 1;
        }
    }
    vec_push(&error_names, (void *)name);
    return (int)error_names.len;
}

// ---- types ------------------------------------------------------------------

static Type *resolve_type(Checker *c, TypeExpr *te);
static uint64_t const_usize(Checker *c, Expr *e);

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

static Type *decl_type(Checker *c, TypeDecl *td) {
    if (td->kind != TD_ALIAS || td->state == 2) {
        return td->type;
    }
    if (td->state == 1) {
        fatal_at(td->loc, "type alias '%s' refers to itself", td->name);
    }
    td->state = 1;
    Checker sub = in_module(c, td->module);
    td->type = resolve_type(&sub, td->alias);
    td->state = 2;
    return td->type;
}

static Type *primitive_type(const char *name) {
    IntKind k;
    if (int_type_lookup(name, &k)) {
        return type_int(k);
    }
    if (strcmp(name, "bool") == 0) {
        return type_bool();
    }
    if (strcmp(name, "str") == 0) {
        return type_str();
    }
    if (strcmp(name, "void") == 0) {
        return type_void();
    }
    if (strcmp(name, "noreturn") == 0) {
        return type_noreturn();
    }
    return NULL;
}

static Type *resolve_path_type(Checker *c, TypeExpr *te) {
    const char *first = VEC_AT(te->segments, const char, 0);
    const char *second = te->segments.len > 1 ? VEC_AT(te->segments, const char, 1) : NULL;
    ModuleAlias *a = te->segments.len == 2 ? find_alias(c->module, first) : NULL;
    if (te->args.len > 0) {
        if (a && a->library && strcmp(a->library, "alloc") == 0 && strcmp(second, "List") == 0) {
            if (te->args.len != 1) {
                fatal_at(te->loc, "alloc.List takes one type");
            }
            return type_list(resolve_type(c, te->args.items[0]));
        }
        if (a && a->library && strcmp(a->library, "alloc") == 0 && strcmp(second, "Map") == 0) {
            if (te->args.len != 2) {
                fatal_at(te->loc, "alloc.Map takes two types: the key and the value");
            }
            Type *key = resolve_type(c, te->args.items[0]);
            if (!type_is_key(key)) {
                fatal_at(te->loc, "%s cannot be a Map key (§5.5)", type_name(key));
            }
            return type_map(key, resolve_type(c, te->args.items[1]));
        }
        fatal_at(te->loc, "stage0 does not support the generic type '%s' yet", path_text(te));
    }
    if (te->segments.len == 1) {
        Type *p = primitive_type(first);
        if (p) {
            return p;
        }
        TypeDecl *td = find_type(c->module, first);
        if (td) {
            return decl_type(c, td);
        }
    } else if (a && a->library) {
        Type *t = library_type(a->library, second);
        if (t) {
            return t;
        }
    } else if (a) {
        TypeDecl *td = find_type(a->module, second);
        if (td) {
            require_visible(c, te->loc, a->module, td->pub, arena_printf("type '%s'", path_text(te)));
            return decl_type(c, td);
        }
    }
    fatal_at(te->loc, "unknown type '%s'", path_text(te));
}

static Type *resolve_type(Checker *c, TypeExpr *te) {
    switch (te->kind) {
    case TE_PATH:
        return resolve_path_type(c, te);
    case TE_REF:
        return type_ref(te->mut, resolve_type(c, te->elem));
    case TE_SLICE:
        return type_slice(te->mut, resolve_type(c, te->elem));
    case TE_ARRAY: {
        uint64_t n = const_usize(c, te->len);
        return type_array(resolve_type(c, te->elem), n);
    }
    case TE_ERROR_UNION: {
        TypeDecl *set = NULL;
        if (te->set) {
            Type *s = resolve_type(c, te->set);
            if (s->kind != TY_ERROR_SET) {
                fatal_at(te->set->loc, "%s is not an error set; E!T needs one", type_name(s));
            }
            set = s->decl;
        }
        return type_error_union_of(set, resolve_type(c, te->elem));
    }
    case TE_OPTIONAL:
        return type_optional(resolve_type(c, te->elem));
    }
    fatal_at(te->loc, "internal error: unknown type");
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

// A view may give up the right to write, never gain it (§5.4).
static bool assignable(const Type *got, const Type *want) {
    if (got == want || got->kind == TY_NORETURN) {
        return true;
    }
    return got->kind == want->kind && (got->kind == TY_REF || got->kind == TY_SLICE) && got->elem == want->elem &&
           got->mut && !want->mut;
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
    case E_FIELD:
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

// ---- places -----------------------------------------------------------------

bool expr_is_place(const Expr *e) {
    switch (e->kind) {
    case E_IDENT:
        return e->name_kind == NAME_BINDING;
    case E_FIELD:
        return e->field_use == FIELD_VALUE && (e->base->type->kind == TY_REF || expr_is_place(e->base));
    case E_DEREF:
        return true;
    case E_INDEX:
        switch (e->base->type->kind) {
        case TY_SLICE:
            return true;
        case TY_ARRAY:
        case TY_LIST:
            return expr_is_place(e->base);
        default:
            return false;
        }
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
    case E_INDEX:
        if (e->base->type->kind == TY_SLICE) {
            return e->base->type->mut;
        }
        return place_is_mutable(e->base);
    default:
        return false;
    }
}

// ---- paths ------------------------------------------------------------------
//
// An expression such as `lexer.Token`, `Span` or `alloc.List(Node)` can name
// a type or a module rather than a value. resolve_path says which, without
// failing, so that callers can treat values separately.

typedef enum { PATH_NONE, PATH_TYPE, PATH_MODULE, PATH_LIBRARY } PathKind;

typedef struct {
    PathKind kind;
    Type *type;         // PATH_TYPE
    ModuleAlias *alias; // PATH_MODULE, PATH_LIBRARY
} PathRef;

static PathRef resolve_path(Checker *c, Expr *e) {
    PathRef r = {PATH_NONE, NULL, NULL};
    if (e->parens) {
        return r;
    }
    if (e->kind == E_IDENT) {
        if (find_binding(c, e->name) || find_const(c->module, e->name) || find_fn(c->module, e->name)) {
            return r;
        }
        TypeDecl *td = find_type(c->module, e->name);
        Type *p = primitive_type(e->name);
        ModuleAlias *a = find_alias(c->module, e->name);
        if (td || p) {
            r.kind = PATH_TYPE;
            r.type = td ? decl_type(c, td) : p;
        } else if (a) {
            r.kind = a->library ? PATH_LIBRARY : PATH_MODULE;
            r.alias = a;
        }
        return r;
    }
    if (e->kind == E_FIELD) {
        PathRef b = resolve_path(c, e->base);
        if (b.kind == PATH_MODULE) {
            TypeDecl *td = find_type(b.alias->module, e->name);
            if (td) {
                require_visible(c, e->loc, td->module, td->pub, arena_printf("type '%s'", td->name));
                r.kind = PATH_TYPE;
                r.type = decl_type(c, td);
            }
        } else if (b.kind == PATH_LIBRARY) {
            Type *t = library_type(b.alias->library, e->name);
            if (t) {
                r.kind = PATH_TYPE;
                r.type = t;
            }
        }
        return r;
    }
    // alloc.List(T) and alloc.Map(K, V) written in an expression (§5.3).
    if (e->kind == E_CALL && e->callee->kind == E_FIELD) {
        PathRef b = resolve_path(c, e->callee->base);
        const char *name = e->callee->name;
        bool list = strcmp(name, "List") == 0;
        bool map = strcmp(name, "Map") == 0;
        if (b.kind != PATH_LIBRARY || strcmp(b.alias->library, "alloc") != 0 || (!list && !map)) {
            return r;
        }
        if (e->args.len != (list ? 1u : 2u)) {
            fatal_at(e->loc, list ? "alloc.List takes one type" : "alloc.Map takes two types: the key and the value");
        }
        Type *args[2];
        for (size_t i = 0; i < e->args.len; i++) {
            Arg *a = e->args.items[i];
            PathRef t = a->label ? r : resolve_path(c, a->value);
            if (t.kind != PATH_TYPE) {
                fatal_at(a->value->loc, "alloc.%s takes types, such as alloc.List(u32)", name);
            }
            args[i] = t.type;
        }
        if (map && !type_is_key(args[0])) {
            fatal_at(e->loc, "%s cannot be a Map key (§5.5)", type_name(args[0]));
        }
        r.kind = PATH_TYPE;
        r.type = list ? type_list(args[0]) : type_map(args[0], args[1]);
    }
    return r;
}

// ---- expressions ------------------------------------------------------------

static Type *check_expr(Checker *c, Expr *e, Type *expected);

// Every error of `set` must be one that ret, an error union, can return.
// Unknown when either set is inferred: stage0 does not infer sets.
static void require_errors_fit(const TypeDecl *set, const Type *ret, Loc loc) {
    if (!set || !ret->decl) {
        return;
    }
    for (size_t i = 0; i < set->errors.len; i++) {
        ErrorName *e = set->errors.items[i];
        if (!find_error(ret->decl, e->name)) {
            fatal_at(loc, "the error '%s' cannot be returned: %s does not contain it", e->name, type_name(ret));
        }
    }
}
static void check_const(Checker *c, ConstDecl *k);

static void expect_type(const Expr *e, const Type *got, const Type *want) {
    if (!assignable(got, want)) {
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

// The arguments of a call to a declared function, after the first `skip`
// parameters (1 for a method's self).
static void check_fn_args(Checker *c, Expr *call, FnDecl *fn, size_t skip) {
    size_t n = fn->params.len - skip;
    ParamInfo *params = arena_alloc((n + 1) * sizeof *params);
    for (size_t i = 0; i < n; i++) {
        Param *p = VEC_AT(fn->params, Param, i + skip);
        params[i] = (ParamInfo){p->name, p->binding->type};
    }
    const char *name = fn->owner ? arena_printf("'%s.%s'", fn->owner->name, fn->name) : arena_printf("'%s'", fn->name);
    check_args(c, call, name, params, n);
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

static void require_mutable_receiver(const Expr *receiver, const Type *rt, const char *display) {
    if (rt->kind == TY_REF ? !rt->mut : !(expr_is_place(receiver) && place_is_mutable(receiver))) {
        fatal_at(receiver->loc, "%s needs a mutable receiver: a 'var', or a place reached through '&mut'", display);
    }
}

static Type *check_method_call(Checker *c, Expr *call) {
    Expr *callee = call->callee;
    Expr *receiver = callee->base;
    Type *rt = check_expr(c, receiver, NULL);
    Type *t = rt->kind == TY_REF ? rt->elem : rt;
    if (t->kind == TY_STRUCT || t->kind == TY_ENUM) {
        FnDecl *fn = find_method(t->decl, callee->name);
        if (fn && fn->has_self) {
            const char *display = arena_printf("'%s.%s'", t->decl->name, fn->name);
            require_visible(c, callee->loc, fn->module, fn->pub, arena_printf("method %s", display));
            if (fn->self_mode == SELF_MUT_REF) {
                require_mutable_receiver(receiver, rt, display);
            }
            check_fn_args(c, call, fn, 1);
            call->call = CALL_FN;
            call->fn = fn;
            call->receiver = receiver;
            call->self_mode = fn->self_mode;
            return fn->ret;
        }
        if (fn) {
            fatal_at(callee->loc, "'%s.%s' has no 'self' parameter; call it as %s.%s(...)", t->decl->name, fn->name,
                     t->decl->name, fn->name);
        }
    }
    const Method *m = method_lookup(t, callee->name);
    if (!m) {
        fatal_at(callee->loc, "%s has no method '%s'", type_name(t), callee->name);
    }
    const char *display = arena_printf("%s.%s", type_name(t), m->name);
    if (m->self == SELF_MUT_REF) {
        require_mutable_receiver(receiver, rt, display);
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
        PathRef r = resolve_path(c, callee->base);
        const char *name = callee->name;
        if (r.kind == PATH_TYPE) {
            Type *t = r.type;
            bool has_init = t->kind == TY_LIST || t->kind == TY_MAP || (t->kind == TY_BUILTIN && t->builtin->has_init);
            if (has_init && strcmp(name, "init") == 0) {
                ParamInfo params[] = {{"a", library_type("alloc", "Allocator")}};
                check_args(c, call, arena_printf("%s.init", type_name(t)), params, 1);
                call->call = CALL_INIT;
                return t;
            }
            FnDecl *fn = t->kind == TY_STRUCT || t->kind == TY_ENUM ? find_method(t->decl, name) : NULL;
            if (!fn) {
                fatal_at(callee->loc, "%s has no function '%s'", type_name(t), name);
            }
            require_visible(c, callee->loc, fn->module, fn->pub, arena_printf("'%s.%s'", t->decl->name, name));
            check_fn_args(c, call, fn, 0);
            call->call = CALL_FN;
            call->fn = fn;
            return fn->ret;
        }
        if (r.kind == PATH_MODULE) {
            Module *m = r.alias->module;
            FnDecl *fn = find_fn(m, name);
            if (!fn) {
                fatal_at(callee->loc, "module '%s' has no function '%s'", m->name, name);
            }
            require_visible(c, callee->loc, m, fn->pub, arena_printf("function '%s'", name));
            check_fn_args(c, call, fn, 0);
            call->call = CALL_FN;
            call->fn = fn;
            return fn->ret;
        }
        if (r.kind == PATH_LIBRARY) {
            fatal_at(callee->loc, "'%s.%s' is not a function", r.alias->name, name);
        }
        return check_method_call(c, call);
    }
    if (callee->kind != E_IDENT || find_binding(c, callee->name)) {
        fatal_at(callee->loc, "this expression cannot be called");
    }
    FnDecl *fn = find_fn(c->module, callee->name);
    if (fn) {
        check_fn_args(c, call, fn, 0);
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
    if (find_type(c->module, e->name) || primitive_type(e->name)) {
        fatal_at(e->loc, "'%s' is a type, not a value", e->name);
    }
    if (find_alias(c->module, e->name)) {
        fatal_at(e->loc, "'%s' is a module, not a value", e->name);
    }
    fatal_at(e->loc, "unknown name '%s'", e->name);
}

static Type *check_field(Checker *c, Expr *e) {
    PathRef r = resolve_path(c, e->base);
    if (r.kind == PATH_TYPE) {
        Type *t = r.type;
        if (t->kind == TY_ENUM) {
            VariantDecl *v = find_variant(t->decl, e->name);
            if (v) {
                if (v->has_fields) {
                    fatal_at(e->loc, "'%s.%s' has fields; construct it as %s.%s { ... }", t->decl->name, v->name,
                             t->decl->name, v->name);
                }
                e->field_use = FIELD_VARIANT;
                e->variant = v;
                return t;
            }
        }
        if (t->kind == TY_ERROR_SET) {
            ErrorName *en = find_error(t->decl, e->name);
            if (en) {
                e->field_use = FIELD_ERROR;
                e->error = en;
                return t;
            }
        }
        if ((t->kind == TY_STRUCT || t->kind == TY_ENUM) && find_method(t->decl, e->name)) {
            fatal_at(e->loc, "'%s.%s' is a function; call it", t->decl->name, e->name);
        }
        fatal_at(e->loc, "%s has no variant or error named '%s'", type_name(t), e->name);
    }
    if (r.kind == PATH_MODULE) {
        Module *m = r.alias->module;
        ConstDecl *k = find_const(m, e->name);
        if (k) {
            require_visible(c, e->loc, m, k->pub, arena_printf("constant '%s'", k->name));
            check_const(c, k);
            e->field_use = FIELD_CONST;
            e->konst = k;
            return k->type;
        }
        if (find_fn(m, e->name)) {
            fatal_at(e->loc, "'%s.%s' is a function; in Arche-0 functions can only be called", r.alias->name, e->name);
        }
        fatal_at(e->loc, "module '%s' has no value named '%s'", m->name, e->name);
    }
    if (r.kind == PATH_LIBRARY) {
        fatal_at(e->loc, "'%s.%s' is not a value", r.alias->name, e->name);
    }
    Type *bt = check_expr(c, e->base, NULL);
    Type *t = bt->kind == TY_REF ? bt->elem : bt;
    const Field *f = field_lookup(t, e->name);
    if (!f) {
        fatal_at(e->loc, "%s has no field '%s'", type_name(t), e->name);
    }
    if (t->kind == TY_STRUCT) {
        require_visible(c, e->loc, t->decl->module, f->pub, arena_printf("field '%s' of %s", f->name, t->decl->name));
    }
    e->field_use = FIELD_VALUE;
    e->field = f;
    return f->type;
}

static Type *check_struct_lit(Checker *c, Expr *e) {
    Expr *path = e->type_path;
    PathRef r = resolve_path(c, path);
    Type *t;
    Vec *decls = NULL; // FieldDecl *, for a variant
    if (r.kind == PATH_TYPE && r.type->kind == TY_STRUCT) {
        t = r.type;
    } else {
        PathRef owner = path->kind == E_FIELD ? resolve_path(c, path->base) : r;
        VariantDecl *v = NULL;
        if (path->kind == E_FIELD && owner.kind == PATH_TYPE && owner.type->kind == TY_ENUM) {
            v = find_variant(owner.type->decl, path->name);
        }
        if (!v) {
            fatal_at(path->loc, "'{' here needs a struct type or an enum variant with fields");
        }
        if (!v->has_fields) {
            fatal_at(path->loc, "'%s.%s' has no fields; write it without '{ }'", owner.type->decl->name, v->name);
        }
        t = owner.type;
        e->variant = v;
        decls = &v->fields;
    }
    size_t n = decls ? decls->len : (size_t)t->nfields;
    for (size_t i = 0; i < e->fields.len || i < n; i++) {
        const char *want = i < n ? (decls ? VEC_AT(*decls, FieldDecl, i)->name : t->fields[i].name) : NULL;
        if (i >= e->fields.len) {
            fatal_at(e->loc, "this literal does not set field '%s'; every field is written", want);
        }
        FieldInit *fi = e->fields.items[i];
        if (!want) {
            fatal_at(fi->loc, "%s has no field '%s' here", type_name(t), fi->name);
        }
        if (strcmp(fi->name, want) != 0) {
            fatal_at(fi->loc, "fields are written in declaration order: expected '%s', found '%s'", want, fi->name);
        }
        Type *ft = decls ? VEC_AT(*decls, FieldDecl, i)->type : t->fields[i].type;
        if (!decls) {
            require_visible(c, fi->loc, t->decl->module, t->fields[i].pub,
                            arena_printf("field '%s' of %s", want, t->decl->name));
        }
        check_value(c, fi->value, ft);
    }
    return t;
}

static Type *check_index(Checker *c, Expr *e) {
    Type *bt = check_expr(c, e->base, NULL);
    check_value(c, e->index, type_int(USIZE));
    switch (bt->kind) {
    case TY_ARRAY:
    case TY_SLICE:
    case TY_LIST:
        return bt->elem;
    case TY_STR:
        return type_int(U8);
    default:
        fatal_at(e->loc, "%s cannot be indexed", type_name(bt));
    }
}

static Type *check_slice(Checker *c, Expr *e) {
    Type *bt = check_expr(c, e->base, NULL);
    check_value(c, e->index, type_int(USIZE));
    check_value(c, e->end, type_int(USIZE));
    switch (bt->kind) {
    case TY_STR:
        return type_str();
    case TY_SLICE:
        return type_slice(bt->mut, bt->elem);
    case TY_ARRAY:
    case TY_LIST:
        if (!expr_is_place(e->base)) {
            fatal_at(e->loc, "only a place can be sliced: store this %s in a variable first", type_name(bt));
        }
        return type_slice(place_is_mutable(e->base), bt->elem);
    default:
        fatal_at(e->loc, "%s cannot be sliced", type_name(bt));
    }
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

// check_operands when the left side is already checked.
static Type *check_operands_rhs(Checker *c, Expr *e) {
    Type *lt = e->base->type;
    Type *rt = check_expr(c, e->rhs, lt);
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
        // Error names are global (§5.2), so values of two error sets compare.
        if (!untyped(e->base) && !untyped(e->rhs)) {
            Type *lt = check_expr(c, e->base, NULL);
            if (lt->kind == TY_ERROR_SET) {
                Type *rt = check_expr(c, e->rhs, NULL);
                if (rt->kind != TY_ERROR_SET) {
                    fatal_at(e->loc, "the operands of '%s' have different types: %s and %s", op, type_name(lt),
                             type_name(rt));
                }
                return type_bool();
            }
            e->base->type = lt;
        }
        Type *t = e->base->type ? check_operands_rhs(c, e) : check_operands(c, e, NULL);
        if (!type_comparable(t)) {
            fatal_at(e->loc,
                     "'%s' works on integers, bools, errors, enums without payloads and structs of these, not %s; "
                     "compare text with .eq()",
                     op, type_name(t));
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
        case E_FIELD:
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
        Type *t = check_field(c, e);
        if (!c->fn && e->field_use != FIELD_CONST) {
            fatal_at(e->loc, "this is not a constant expression");
        }
        return t;
    }
    case E_CALL:
        return check_call(c, e);
    case E_INDEX:
        return check_index(c, e);
    case E_SLICE:
        return check_slice(c, e);
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
            fatal_at(e->loc, "'&' needs a place: a variable, a field, an element or '.*'");
        }
        if (e->mut && !place_is_mutable(e->base)) {
            fatal_at(e->loc, "'&mut' needs a mutable place: a 'var', or a place reached through '&mut'");
        }
        return type_ref(e->mut, t);
    }
    case E_STRUCT_LIT:
        return check_struct_lit(c, e);
    case E_ARRAY_LIT:
        if (!expected || expected->kind != TY_ARRAY) {
            fatal_at(e->loc, "an array literal takes its type from context, e.g. 'let a: [3]u32 = [1, 2, 3];'");
        }
        if (e->elems.len != expected->len) {
            fatal_at(e->loc, "this literal has %zu elements, but %s has %llu", e->elems.len, type_name(expected),
                     (unsigned long long)expected->len);
        }
        for (size_t i = 0; i < e->elems.len; i++) {
            check_value(c, e->elems.items[i], expected->elem);
        }
        return expected;
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
        if (!expected || expected->kind != TY_OPTIONAL) {
            fatal_at(e->loc, "'none' takes its type from context, e.g. 'let x: ?u32 = none;'");
        }
        return expected;
    case E_SOME: {
        Type *inner = expected && expected->kind == TY_OPTIONAL ? expected->elem : NULL;
        Type *t;
        if (inner) {
            check_value(c, e->base, inner);
            t = inner;
        } else {
            t = check_expr(c, e->base, NULL);
        }
        if (t->kind == TY_VOID || t->kind == TY_NORETURN) {
            fatal_at(e->loc, "'some' needs a value");
        }
        return type_optional(t);
    }
    case E_ORELSE: {
        Type *t = check_expr(c, e->base, expected ? type_optional(expected) : NULL);
        if (t->kind != TY_OPTIONAL) {
            fatal_at(e->loc, "'orelse' needs an optional on its left, not %s", type_name(t));
        }
        check_value(c, e->rhs, t->elem);
        return t->elem;
    }
    case E_CATCH: {
        Type *t = check_expr(c, e->base, NULL);
        if (t->kind != TY_ERROR_UNION) {
            fatal_at(e->loc, "'catch' needs an error union on its left, not %s", type_name(t));
        }
        check_value(c, e->rhs, t->elem);
        return t->elem;
    }
    case E_TRY: {
        if (c->defer_loops >= 0) {
            fatal_at(e->loc, "deferred code cannot use 'try'");
        }
        Type *ret = c->fn->ret;
        if (ret->kind != TY_ERROR_UNION) {
            fatal_at(e->loc, "'try' returns the error from '%s', which returns %s, not an error union", c->fn->name,
                     type_name(ret));
        }
        Type *t = check_expr(c, e->base, NULL);
        if (t->kind != TY_ERROR_UNION) {
            fatal_at(e->loc, "'try' needs an error union, not %s", type_name(t));
        }
        require_errors_fit(t->decl, ret, e->loc);
        return t->elem;
    }
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
    Checker sub = in_module(c, k->module);
    Type *t = resolve_type(&sub, k->type_expr);
    if (!is_int(t) && t->kind != TY_BOOL && t->kind != TY_STR) {
        fatal_at(k->type_expr->loc, "a constant's type is an integer type, bool or str");
    }
    k->type = t;
    check_value(&sub, k->value, t);
    k->val = const_eval(&sub, k->value);
    k->state = 2;
}

// An array length: a constant expression of type usize.
static uint64_t const_usize(Checker *c, Expr *e) {
    Checker sub = in_module(c, c->module);
    check_value(&sub, e, type_int(USIZE));
    return const_eval(&sub, e).bits;
}

// ---- statements -------------------------------------------------------------

static void check_block(Checker *c, Block *b);
static void check_stmt(Checker *c, Stmt *s);

static void check_assign(Checker *c, Stmt *s) {
    Type *t = check_expr(c, s->target, NULL);
    if (!expr_is_place(s->target)) {
        fatal_at(s->target->loc, "only a place can be assigned: a variable, a field, an element or '.*'");
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

static void check_pat_lit(const PatLit *lit, const Type *t, Loc loc) {
    if (lit->is_char && t->int_kind != U8) {
        fatal_at(loc, "a character pattern matches a u8, not %s", type_name(t));
    }
    if (lit->negative && !int_signed(t->int_kind)) {
        fatal_at(loc, "a negative pattern needs a signed type, and %s is unsigned", type_name(t));
    }
    uint64_t limit = lit->negative ? (uint64_t)(-(int_min(t->int_kind) + 1)) + 1 : int_max(t->int_kind);
    if (lit->magnitude > limit) {
        fatal_at(loc, "%s%llu does not fit in %s", lit->negative ? "-" : "", (unsigned long long)lit->magnitude,
                 type_name(t));
    }
}

static bool pat_lit_le(const PatLit *a, const PatLit *b) {
    if (a->negative != b->negative) {
        return a->negative;
    }
    return a->negative ? a->magnitude >= b->magnitude : a->magnitude <= b->magnitude;
}

// st is the scrutinee's type; t is st, or what st refers to when it is a
// reference: then the pattern binds views of the fields (§9).
static void check_pattern(Checker *c, Pattern *p, Type *st, Type *t, bool alternatives) {
    switch (p->kind) {
    case P_WILDCARD:
        return;
    case P_BIND:
        if (alternatives) {
            fatal_at(p->loc, "patterns joined with '|' cannot bind names");
        }
        p->binding = declare(c, p->name, p->loc, st, false);
        return;
    case P_INT:
    case P_RANGE:
        if (!is_int(t)) {
            fatal_at(p->loc, "an integer pattern needs an integer to match, not %s", type_name(t));
        }
        check_pat_lit(&p->lo, t, p->loc);
        if (p->kind == P_RANGE) {
            check_pat_lit(&p->hi, t, p->loc);
            if (!pat_lit_le(&p->lo, &p->hi)) {
                fatal_at(p->loc, "this range is empty");
            }
        }
        return;
    case P_BOOL:
        if (t->kind != TY_BOOL) {
            fatal_at(p->loc, "'%s' matches a bool, not %s", p->value ? "true" : "false", type_name(t));
        }
        return;
    case P_VARIANT:
        if (t->kind == TY_ERROR_SET) {
            p->error = t->decl ? find_error(t->decl, p->name) : global_error(p->name);
            if (!p->error) {
                fatal_at(p->loc, "%s has no error named '%s'", type_name(t), p->name);
            }
            if (p->has_fields) {
                fatal_at(p->loc, "an error has no fields");
            }
            return;
        }
        if (t->kind != TY_ENUM) {
            fatal_at(p->loc, "'.%s' matches an enum or an error, not %s", p->name, type_name(t));
        }
        p->variant = find_variant(t->decl, p->name);
        if (!p->variant) {
            fatal_at(p->loc, "%s has no variant named '%s'", type_name(t), p->name);
        }
        if (p->variant->has_fields && !p->has_fields) {
            fatal_at(p->loc, "'.%s' has fields: write '.%s { ... }', or '.%s { .. }' to ignore them", p->name, p->name,
                     p->name);
        }
        if (!p->variant->has_fields && p->has_fields) {
            fatal_at(p->loc, "'.%s' has no fields; write '.%s'", p->name, p->name);
        }
        for (size_t i = 0; i < p->field_pats.len; i++) {
            FieldPat *fp = p->field_pats.items[i];
            for (size_t j = 0; j < p->variant->fields.len && !fp->decl; j++) {
                FieldDecl *fd = p->variant->fields.items[j];
                if (strcmp(fd->name, fp->field) == 0) {
                    fp->decl = fd;
                }
            }
            if (!fp->decl) {
                fatal_at(fp->loc, "'.%s' has no field '%s'", p->name, fp->field);
            }
            for (size_t j = 0; j < i; j++) {
                if (strcmp(((FieldPat *)p->field_pats.items[j])->field, fp->field) == 0) {
                    fatal_at(fp->loc, "field '%s' is named twice", fp->field);
                }
            }
            if (fp->bind) {
                if (alternatives) {
                    fatal_at(fp->loc, "patterns joined with '|' cannot bind names");
                }
                Type *ft = st->kind == TY_REF ? type_ref(st->mut, fp->decl->type) : fp->decl->type;
                fp->binding = declare(c, fp->bind, fp->loc, ft, false);
            }
        }
        if (!p->rest && p->field_pats.len < p->variant->fields.len) {
            for (size_t j = 0; j < p->variant->fields.len; j++) {
                FieldDecl *fd = p->variant->fields.items[j];
                bool named = false;
                for (size_t i = 0; i < p->field_pats.len; i++) {
                    named = named || ((FieldPat *)p->field_pats.items[i])->decl == fd;
                }
                if (!named) {
                    fatal_at(p->loc, "this pattern does not name field '%s'; name it, or end with '..'", fd->name);
                }
            }
        }
        return;
    case P_NONE:
        if (t->kind != TY_OPTIONAL) {
            fatal_at(p->loc, "'none' matches an optional, not %s", type_name(t));
        }
        return;
    case P_SOME:
    case P_OK:
    case P_ERR: {
        const char *word = p->kind == P_SOME ? "some" : p->kind == P_OK ? "ok" : "err";
        TypeKind want = p->kind == P_SOME ? TY_OPTIONAL : TY_ERROR_UNION;
        if (t->kind != want) {
            fatal_at(p->loc, "'%s(...)' matches %s, not %s", word, want == TY_OPTIONAL ? "an optional" : "an error union",
                     type_name(t));
        }
        Type *inner = p->kind == P_ERR ? (t->decl ? t->decl->type : type_any_error()) : t->elem;
        if (inner->kind == TY_VOID && p->inner->kind != P_WILDCARD) {
            fatal_at(p->inner->loc, "a void result has no value: write 'ok(_)'");
        }
        Type *bind = st->kind == TY_REF ? type_ref(st->mut, inner) : inner;
        check_pattern(c, p->inner, bind, inner, alternatives);
        return;
    }
    }
}

static void check_match(Checker *c, Stmt *s) {
    Type *st = check_expr(c, s->expr, NULL);
    Type *t = st->kind == TY_REF ? st->elem : st;
    for (size_t i = 0; i < s->arms.len; i++) {
        MatchArm *arm = s->arms.items[i];
        size_t depth = c->scope.len;
        for (size_t j = 0; j < arm->patterns.len; j++) {
            check_pattern(c, arm->patterns.items[j], st, t, arm->patterns.len > 1);
        }
        check_block(c, arm->body);
        c->scope.len = depth;
    }
}

// Return lifting (§7.8): in a function returning ?T, `return v` with v: T
// returns some(v); returning E!T, `return v` returns the value and
// `return e` with an error returns the error.
static void check_return_value(Checker *c, Stmt *s, Type *ret) {
    Expr *e = s->expr;
    if (ret->kind == TY_OPTIONAL) {
        if (e->kind == E_NONE || e->kind == E_SOME) {
            check_value(c, e, ret);
            return;
        }
        Type *t = check_expr(c, e, ret->elem);
        if (assignable(t, ret->elem)) {
            s->ret_kind = RET_VALUE;
            return;
        }
        expect_type(e, t, ret);
        return;
    }
    if (ret->kind == TY_ERROR_UNION) {
        Type *t = check_expr(c, e, ret->elem->kind == TY_VOID ? NULL : ret->elem);
        if (t->kind == TY_ERROR_SET && ret->elem->kind != TY_ERROR_SET) {
            if (e->kind == E_FIELD && e->field_use == FIELD_ERROR) {
                if (ret->decl && !find_error(ret->decl, e->error->name)) {
                    fatal_at(e->loc, "the error '%s' cannot be returned: %s does not contain it", e->error->name,
                             type_name(ret));
                }
            } else {
                require_errors_fit(t->decl, ret, e->loc);
            }
            s->ret_kind = RET_ERROR;
            return;
        }
        if (ret->elem->kind != TY_VOID && assignable(t, ret->elem)) {
            s->ret_kind = RET_VALUE;
            return;
        }
        expect_type(e, t, ret);
        return;
    }
    check_value(c, e, ret);
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
        check_match(c, s);
        return;
    case S_RETURN: {
        if (c->defer_loops >= 0) {
            fatal_at(s->loc, "deferred code cannot 'return'");
        }
        Type *ret = c->fn->ret;
        s->ret_kind = RET_PLAIN;
        if (!s->expr) {
            if (!is_void_result(ret)) {
                fatal_at(s->loc, "'%s' returns %s, so 'return' needs a value", c->fn->name, type_name(ret));
            }
            return;
        }
        if (ret->kind == TY_VOID) {
            fatal_at(s->expr->loc, "'%s' returns nothing, so 'return' takes no value", c->fn->name);
        }
        check_return_value(c, s, ret);
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

// C names: "a0", then each segment of the path as _<length><text>, so that no
// two Arche names share a C name. The root module whose file name is not a
// module path has the empty name, which gives the segment "_0".
static const char *mangle(const char *module, const char *a, const char *b) {
    StrBuf s = {0};
    sb_puts(&s, "a0");
    const char *seg = module;
    for (;;) {
        const char *dot = strchr(seg, '.');
        size_t n = dot ? (size_t)(dot - seg) : strlen(seg);
        sb_printf(&s, "_%zu%.*s", n, (int)n, seg);
        if (!dot) {
            break;
        }
        seg = dot + 1;
    }
    sb_printf(&s, "_%zu%s", strlen(a), a);
    if (b) {
        sb_printf(&s, "_%zu%s", strlen(b), b);
    }
    return s.data;
}

static Module *find_module(const Program *p, const char *name) {
    for (size_t i = 0; i < p->modules.len; i++) {
        Module *m = p->modules.items[i];
        if (strcmp(m->name, name) == 0) {
            return m;
        }
    }
    return NULL;
}

static void check_imports(Checker *c, Module *m) {
    for (size_t i = 0; i < m->imports.len; i++) {
        Import *imp = m->imports.items[i];
        const char *first = imp->path.items[0];
        const char *last = imp->path.items[imp->path.len - 1];
        ModuleAlias *a = NEW(ModuleAlias);
        a->name = imp->alias ? imp->alias : last;
        a->loc = imp->loc;
        if (strcmp(first, "std") == 0 || strcmp(first, "alloc") == 0 || strcmp(first, "core") == 0) {
            if (imp->path.len > 1 || strcmp(first, "core") == 0) {
                fatal_at(imp->loc, "Arche-0 provides only the modules 'std' and 'alloc'");
            }
            a->library = first;
        } else {
            StrBuf name = {0};
            for (size_t j = 0; j < imp->path.len; j++) {
                sb_printf(&name, "%s%s", j > 0 ? "." : "", (const char *)imp->path.items[j]);
            }
            a->module = find_module(c->program, name.data);
        }
        if (find_alias(m, a->name)) {
            fatal_at(imp->loc, "'%s' is already imported", a->name);
        }
        vec_push(&m->aliases, a);
    }
}

typedef struct {
    const char *name;
    Loc loc;
} NamedItem;

static void check_item_names(Module *m) {
    Vec items = {0}; // NamedItem *
    for (size_t i = 0; i < m->fns.len; i++) {
        FnDecl *fn = m->fns.items[i];
        NamedItem *it = NEW(NamedItem);
        *it = (NamedItem){fn->name, fn->loc};
        vec_push(&items, it);
    }
    for (size_t i = 0; i < m->consts.len; i++) {
        ConstDecl *k = m->consts.items[i];
        NamedItem *it = NEW(NamedItem);
        *it = (NamedItem){k->name, k->loc};
        vec_push(&items, it);
    }
    for (size_t i = 0; i < m->types.len; i++) {
        TypeDecl *td = m->types.items[i];
        NamedItem *it = NEW(NamedItem);
        *it = (NamedItem){td->name, td->loc};
        vec_push(&items, it);
    }
    for (size_t i = 0; i < items.len; i++) {
        NamedItem *it = items.items[i];
        for (size_t j = 0; j < i; j++) {
            if (strcmp(((NamedItem *)items.items[j])->name, it->name) == 0) {
                fatal_at(it->loc, "'%s' is already declared in this module", it->name);
            }
        }
        if (find_alias(m, it->name) || is_predeclared(it->name)) {
            fatal_at(it->loc, "'%s' already names something; Arche has no shadowing", it->name);
        }
    }
}

static void register_types(Module *m) {
    for (size_t i = 0; i < m->types.len; i++) {
        TypeDecl *td = m->types.items[i];
        switch (td->kind) {
        case TD_STRUCT:
            td->type = type_declared(TY_STRUCT, td, mangle(m->name, td->name, NULL));
            break;
        case TD_ENUM:
            td->type = type_declared(TY_ENUM, td, mangle(m->name, td->name, NULL));
            break;
        case TD_ERROR:
            td->type = type_declared(TY_ERROR_SET, td, "a0_err");
            break;
        case TD_ALIAS:
            break;
        }
    }
}

static void check_field_names(const Vec *fields) {
    for (size_t i = 0; i < fields->len; i++) {
        FieldDecl *f = fields->items[i];
        for (size_t j = 0; j < i; j++) {
            if (strcmp(((FieldDecl *)fields->items[j])->name, f->name) == 0) {
                fatal_at(f->loc, "field '%s' is declared twice", f->name);
            }
        }
    }
}

static void resolve_decl(Checker *c, TypeDecl *td) {
    Checker sub = in_module(c, td->module);
    switch (td->kind) {
    case TD_STRUCT: {
        check_field_names(&td->fields);
        Type *t = td->type;
        t->nfields = (int)td->fields.len;
        t->fields = arena_alloc((td->fields.len + 1) * sizeof *t->fields);
        for (size_t i = 0; i < td->fields.len; i++) {
            FieldDecl *f = td->fields.items[i];
            f->type = resolve_type(&sub, f->type_expr);
            t->fields[i] = (Field){f->name, f->type, arena_printf("f_%s", f->name), f->pub};
        }
        break;
    }
    case TD_ENUM:
        for (size_t i = 0; i < td->variants.len; i++) {
            VariantDecl *v = td->variants.items[i];
            v->tag = (int)i;
            for (size_t j = 0; j < i; j++) {
                if (strcmp(((VariantDecl *)td->variants.items[j])->name, v->name) == 0) {
                    fatal_at(v->loc, "variant '%s' is declared twice", v->name);
                }
            }
            check_field_names(&v->fields);
            for (size_t j = 0; j < v->fields.len; j++) {
                FieldDecl *f = v->fields.items[j];
                f->type = resolve_type(&sub, f->type_expr);
            }
        }
        break;
    case TD_ERROR:
        for (size_t i = 0; i < td->errors.len; i++) {
            ErrorName *e = td->errors.items[i];
            for (size_t j = 0; j < i; j++) {
                if (strcmp(((ErrorName *)td->errors.items[j])->name, e->name) == 0) {
                    fatal_at(e->loc, "error '%s' is listed twice", e->name);
                }
            }
            e->id = error_id(e->name);
        }
        break;
    case TD_ALIAS:
        decl_type(c, td);
        break;
    }
    for (size_t i = 0; i < td->methods.len; i++) {
        FnDecl *fn = td->methods.items[i];
        for (size_t j = 0; j < i; j++) {
            if (strcmp(((FnDecl *)td->methods.items[j])->name, fn->name) == 0) {
                fatal_at(fn->loc, "'%s.%s' is already declared", td->name, fn->name);
            }
        }
        bool clash = (td->kind == TD_STRUCT && field_lookup(td->type, fn->name)) ||
                     (td->kind == TD_ENUM && find_variant(td, fn->name));
        if (clash) {
            fatal_at(fn->loc, "'%s' is already a %s of %s", fn->name, td->kind == TD_STRUCT ? "field" : "variant",
                     td->name);
        }
    }
}

// A struct or enum cannot contain itself by value: it would have no size.
static void check_contains(const Type *t, Vec *path, Vec *done) {
    while (t->kind == TY_ARRAY) {
        t = t->elem;
    }
    if (t->kind != TY_STRUCT && t->kind != TY_ENUM) {
        return;
    }
    for (size_t i = 0; i < done->len; i++) {
        if (done->items[i] == t) {
            return;
        }
    }
    for (size_t i = 0; i < path->len; i++) {
        if (path->items[i] == t) {
            fatal_at(t->decl->loc, "type '%s' contains itself; link values with a handle, such as an index into a list",
                     t->decl->name);
        }
    }
    vec_push(path, (void *)t);
    if (t->kind == TY_STRUCT) {
        for (int i = 0; i < t->nfields; i++) {
            check_contains(t->fields[i].type, path, done);
        }
    } else {
        for (size_t i = 0; i < t->decl->variants.len; i++) {
            VariantDecl *v = t->decl->variants.items[i];
            for (size_t j = 0; j < v->fields.len; j++) {
                check_contains(((FieldDecl *)v->fields.items[j])->type, path, done);
            }
        }
    }
    path->len--;
    vec_push(done, (void *)t);
}

static void check_signature(Checker *c, FnDecl *fn) {
    Checker sub = in_module(c, fn->module);
    for (size_t i = 0; i < fn->params.len; i++) {
        Param *p = VEC_AT(fn->params, Param, i);
        for (size_t j = 0; j < i; j++) {
            if (strcmp(VEC_AT(fn->params, Param, j)->name, p->name) == 0) {
                fatal_at(p->loc, "parameter '%s' is declared twice", p->name);
            }
        }
        p->binding = NEW(Binding);
        p->binding->name = p->name;
        p->binding->loc = p->loc;
        p->binding->type = resolve_type(&sub, p->type_expr);
    }
    fn->ret = fn->ret_expr ? resolve_type(&sub, fn->ret_expr) : type_void();
    if (!fn->owner) {
        fn->c_name = mangle(fn->module->name, fn->name, NULL);
        return;
    }
    fn->c_name = mangle(fn->module->name, fn->owner->name, fn->name);
    Param *first = fn->params.len > 0 ? fn->params.items[0] : NULL;
    if (first && strcmp(first->name, "self") == 0) {
        Type *owner = fn->owner->type;
        Type *t = first->binding->type;
        fn->has_self = true;
        if (t == owner) {
            fn->self_mode = SELF_VALUE;
        } else if (t->kind == TY_REF && t->elem == owner) {
            fn->self_mode = t->mut ? SELF_MUT_REF : SELF_REF;
        } else {
            fatal_at(first->loc, "'self' is %s, &%s or &mut %s", fn->owner->name, fn->owner->name, fn->owner->name);
        }
    }
}

static void check_main(Program *p) {
    FnDecl *main = find_fn(p->root, "main");
    if (!main) {
        fatal_at((Loc){p->root->file, 1, 1}, "the root module has no 'fn main(sys: &mut std.System) -> !void'");
    }
    const BuiltinType *system = builtin_type_lookup("std", "System");
    bool ok = main->params.len == 1 && VEC_AT(main->params, Param, 0)->binding->type == type_ref(true, system->type) &&
              main->ret->kind == TY_ERROR_UNION && main->ret->elem->kind == TY_VOID;
    if (!ok) {
        fatal_at(main->loc, "main must be declared as 'fn main(sys: &mut std.System) -> !void'");
    }
}

static void check_fn(Checker *c, FnDecl *fn) {
    c->module = fn->module;
    c->fn = fn;
    c->scope.len = 0;
    c->loops = 0;
    c->defer_loops = -1;
    for (size_t i = 0; i < fn->params.len; i++) {
        Param *p = VEC_AT(fn->params, Param, i);
        p->binding = declare(c, p->name, p->loc, p->binding->type, false);
    }
    check_block(c, fn->body);
    if (!is_void_result(fn->ret) && !block_ends(fn->body)) {
        fatal_at(fn->loc, "'%s' can reach its end without returning %s", fn->name, type_name(fn->ret));
    }
}

// Calls f for every function of the program: module functions and methods.
static void each_fn(Checker *c, void (*f)(Checker *, FnDecl *)) {
    for (size_t i = 0; i < c->program->modules.len; i++) {
        Module *m = c->program->modules.items[i];
        for (size_t j = 0; j < m->fns.len; j++) {
            f(c, m->fns.items[j]);
        }
        for (size_t j = 0; j < m->types.len; j++) {
            TypeDecl *td = m->types.items[j];
            for (size_t k = 0; k < td->methods.len; k++) {
                f(c, td->methods.items[k]);
            }
        }
    }
}

const char *error_name(int id) {
    return error_names.items[id - 1];
}

int error_count(void) {
    return (int)error_names.len;
}

void check_program(Program *p) {
    types_init();
    TypeDecl *io_error = library_type("std", "IoError")->decl;
    for (size_t i = 0; i < io_error->errors.len; i++) {
        // std.IoError's errors are numbered first, 1 to 5: the runtime returns them.
        error_id(((ErrorName *)io_error->errors.items[i])->name);
    }
    Checker c = {.program = p, .module = p->root, .defer_loops = -1};
    Vec *modules = &p->modules;
    for (size_t i = 0; i < modules->len; i++) {
        check_imports(&c, modules->items[i]);
    }
    for (size_t i = 0; i < modules->len; i++) {
        Module *m = modules->items[i];
        check_item_names(m);
        register_types(m);
    }
    for (size_t i = 0; i < modules->len; i++) {
        Module *m = modules->items[i];
        for (size_t j = 0; j < m->types.len; j++) {
            resolve_decl(&c, m->types.items[j]);
        }
    }
    Vec path = {0};
    Vec done = {0};
    Vec *types = composite_types();
    for (size_t i = 0; i < types->len; i++) {
        check_contains(types->items[i], &path, &done);
    }
    for (size_t i = 0; i < modules->len; i++) {
        Module *m = modules->items[i];
        for (size_t j = 0; j < m->consts.len; j++) {
            check_const(&c, m->consts.items[j]);
        }
    }
    each_fn(&c, check_signature);
    check_main(p);
    each_fn(&c, check_fn);
}
