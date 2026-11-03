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
    Vec aliases; // ModuleAlias *
    FnDecl *fn;  // the function being checked
    Vec scope;   // Binding *, innermost last
} Checker;

typedef struct {
    const char *name;
    Type *type;
} ParamInfo;

static FnDecl *find_fn(const Module *m, const char *name) {
    for (size_t i = 0; i < m->fns.len; i++) {
        FnDecl *fn = VEC_AT(m->fns, FnDecl, i);
        if (strcmp(fn->name, name) == 0) {
            return fn;
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

// ---- types ----------------------------------------------------------------

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

static bool int_fits(IntKind k, uint64_t v) {
    switch (k) {
    case I8:
        return v <= 0x7F;
    case I16:
        return v <= 0x7FFF;
    case I32:
        return v <= 0x7FFFFFFF;
    case I64:
    case ISIZE:
        return v <= 0x7FFFFFFFFFFFFFFF;
    case U8:
        return v <= 0xFF;
    case U16:
        return v <= 0xFFFF;
    case U32:
        return v <= 0xFFFFFFFF;
    case U64:
    case USIZE:
    case INT_KIND_COUNT:
        break;
    }
    return true;
}

// ---- expressions ------------------------------------------------------------

bool expr_is_place(const Expr *e) {
    switch (e->kind) {
    case E_IDENT:
        return e->name_kind == NAME_BINDING;
    case E_FIELD:
        return e->base->type->kind == TY_REF || expr_is_place(e->base);
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

static void check_args(Checker *c, Expr *call, const char *callee, const ParamInfo *params, size_t n) {
    if (call->args.len != n) {
        fatal_at(call->loc, "%s takes %zu argument%s, but %zu %s given", callee, n, n == 1 ? "" : "s", call->args.len,
                 call->args.len == 1 ? "is" : "are");
    }
    for (size_t i = 0; i < n; i++) {
        Arg *a = VEC_AT(call->args, Arg, i);
        if (a->label && strcmp(a->label, params[i].name) != 0) {
            fatal_at(a->loc, "this argument is labeled '%s', but parameter %zu of %s is '%s'", a->label, i + 1, callee,
                     params[i].name);
        }
        check_value(c, a->value, params[i].type);
    }
}

static Type *check_builtin_call(Checker *c, Expr *call, BuiltinFn fn) {
    static ParamInfo panic_params[] = {{"message", NULL}};
    static ParamInfo assert_params[] = {{"condition", NULL}};
    call->call = CALL_BUILTIN_FN;
    call->builtin_fn = fn;
    switch (fn) {
    case BUILTIN_PANIC:
        panic_params[0].type = type_str();
        check_args(c, call, "panic", panic_params, 1);
        return type_noreturn();
    case BUILTIN_ASSERT:
        assert_params[0].type = type_bool();
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

static Type *check_expr_kind(Checker *c, Expr *e, Type *expected) {
    switch (e->kind) {
    case E_INT:
        if (!expected || expected->kind != TY_INT) {
            // Erasable (§5.6), but stage0 needs the type to emit the literal.
            fatal_at(e->loc, "an integer literal takes its type from context, e.g. 'let x: u32 = 5;'");
        }
        if (!int_fits(expected->int_kind, e->value)) {
            fatal_at(e->loc, "%llu does not fit in %s", (unsigned long long)e->value, type_name(expected));
        }
        return expected;
    case E_CHAR:
        return type_int(U8);
    case E_STRING:
        return type_str();
    case E_BOOL:
        return type_bool();
    case E_IDENT: {
        Binding *b = find_binding(c, e->name);
        if (b) {
            e->name_kind = NAME_BINDING;
            e->binding = b;
            return b->type;
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
    case E_REF: {
        Type *t = check_expr(c, e->base, NULL);
        if (!expr_is_place(e->base)) {
            fatal_at(e->loc, "'&' needs a place: a variable or a field");
        }
        if (e->mut && !place_is_mutable(e->base)) {
            fatal_at(e->loc, "'&mut' needs a mutable place: a 'var', or a place reached through '&mut'");
        }
        return type_ref(e->mut, t);
    }
    }
    fatal_at(e->loc, "stage0 does not support this expression yet");
}

static Type *check_expr(Checker *c, Expr *e, Type *expected) {
    e->type = check_expr_kind(c, e, expected);
    return e->type;
}

// ---- statements -------------------------------------------------------------

static void check_block(Checker *c, Block *b);

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
    case S_RETURN: {
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

static bool block_ends_in_exit(const Block *b) {
    if (b->stmts.len == 0) {
        return false;
    }
    const Stmt *last = VEC_AT(b->stmts, Stmt, b->stmts.len - 1);
    switch (last->kind) {
    case S_RETURN:
        return true;
    case S_EXPR:
        return last->expr->type->kind == TY_NORETURN;
    case S_BLOCK:
        return block_ends_in_exit(last->block);
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
            if (strcmp(first, "std") == 0 || strcmp(first, "alloc") == 0 || strcmp(first, "core") == 0) {
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

static void check_signature(Checker *c, FnDecl *fn) {
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
    for (size_t i = 0; i < fn->params.len; i++) {
        vec_push(&c->scope, VEC_AT(fn->params, Param, i)->binding);
    }
    check_block(c, fn->body);
    if (!is_void_result(fn->ret) && !block_ends_in_exit(fn->body)) {
        fatal_at(fn->loc, "'%s' can reach its end without returning %s", fn->name, type_name(fn->ret));
    }
}

void check_program(Module *root) {
    Checker c = {.module = root};
    check_imports(&c);
    for (size_t i = 0; i < root->fns.len; i++) {
        FnDecl *fn = VEC_AT(root->fns, FnDecl, i);
        for (size_t j = 0; j < i; j++) {
            if (strcmp(VEC_AT(root->fns, FnDecl, j)->name, fn->name) == 0) {
                fatal_at(fn->loc, "'%s' is already declared", fn->name);
            }
        }
        check_signature(&c, fn);
    }
    check_main(&c);
    for (size_t i = 0; i < root->fns.len; i++) {
        check_fn(&c, VEC_AT(root->fns, FnDecl, i));
    }
}
