// Parser: spec/arche-0.md §14. Builds the AST directly; stage0 keeps no CST.
//
// S1 covers imports, functions, `return`, blocks, calls, method calls, field
// access, `&`/`&mut` and literals. Later sprints add the rest of §14.

#include "arche0.h"

#include <string.h>

typedef struct {
    Token *t;
    size_t pos;
    const SourceFile *f;
} Parser;

static Token *peek(Parser *p) {
    return &p->t[p->pos];
}

static Token *peek_next(Parser *p) {
    return p->t[p->pos].kind == T_EOF ? &p->t[p->pos] : &p->t[p->pos + 1];
}

static bool at(Parser *p, TokKind k) {
    return peek(p)->kind == k;
}

static Token *advance(Parser *p) {
    Token *t = &p->t[p->pos];
    if (t->kind != T_EOF) {
        p->pos++;
    }
    return t;
}

static bool accept(Parser *p, TokKind k) {
    if (at(p, k)) {
        advance(p);
        return true;
    }
    return false;
}

static const char *describe(const Token *t) {
    if (t->kind == T_IDENT) {
        return arena_printf("identifier '%.*s'", (int)t->text_len, t->text);
    }
    return tok_kind_name(t->kind);
}

static _Noreturn void unexpected(Parser *p, const char *what) {
    fatal_at(peek(p)->loc, "expected %s, found %s", what, describe(peek(p)));
}

static Token *expect(Parser *p, TokKind k, const char *what) {
    if (!at(p, k)) {
        unexpected(p, what ? what : tok_kind_name(k));
    }
    return advance(p);
}

static const char *ident_text(const Token *t) {
    return arena_strndup(t->text, t->text_len);
}

static const char *expect_ident(Parser *p, const char *what) {
    return ident_text(expect(p, T_IDENT, what));
}

// ---- types ----------------------------------------------------------------

static Expr *parse_expr(Parser *p);

static TypeExpr *new_type_expr(TypeExprKind kind, Loc loc) {
    TypeExpr *t = NEW(TypeExpr);
    t->kind = kind;
    t->loc = loc;
    return t;
}

static TypeExpr *parse_type(Parser *p) {
    Token *start = peek(p);
    if (accept(p, T_QUESTION)) {
        TypeExpr *t = new_type_expr(TE_OPTIONAL, start->loc);
        t->elem = parse_type(p);
        return t;
    }
    if (accept(p, T_BANG)) {
        TypeExpr *t = new_type_expr(TE_ERROR_UNION, start->loc);
        t->elem = parse_type(p);
        return t;
    }
    if (accept(p, T_AMP)) {
        TypeExpr *t = new_type_expr(TE_REF, start->loc);
        t->mut = accept(p, T_MUT);
        t->elem = parse_type(p);
        return t;
    }
    if (accept(p, T_LBRACKET)) {
        if (accept(p, T_RBRACKET)) {
            TypeExpr *t = new_type_expr(TE_SLICE, start->loc);
            t->mut = accept(p, T_MUT);
            t->elem = parse_type(p);
            return t;
        }
        TypeExpr *t = new_type_expr(TE_ARRAY, start->loc);
        t->len = parse_expr(p);
        expect(p, T_RBRACKET, NULL);
        t->elem = parse_type(p);
        return t;
    }
    if (!at(p, T_IDENT)) {
        unexpected(p, "a type");
    }
    TypeExpr *t = new_type_expr(TE_PATH, start->loc);
    vec_push(&t->segments, (void *)expect_ident(p, NULL));
    while (accept(p, T_DOT)) {
        vec_push(&t->segments, (void *)expect_ident(p, NULL));
    }
    if (accept(p, T_LPAREN)) {
        do {
            if (at(p, T_RPAREN)) {
                break;
            }
            vec_push(&t->args, parse_type(p));
        } while (accept(p, T_COMMA));
        expect(p, T_RPAREN, NULL);
    }
    if (at(p, T_BANG)) {
        TypeExpr *u = new_type_expr(TE_ERROR_UNION, advance(p)->loc);
        u->set = t;
        u->elem = parse_type(p);
        return u;
    }
    return t;
}

// ---- expressions ------------------------------------------------------------

static Expr *new_expr(ExprKind kind, Loc loc) {
    Expr *e = NEW(Expr);
    e->kind = kind;
    e->loc = loc;
    return e;
}

static Expr *parse_primary(Parser *p) {
    Token *t = peek(p);
    switch (t->kind) {
    case T_INT:
    case T_CHAR: {
        advance(p);
        Expr *e = new_expr(t->kind == T_INT ? E_INT : E_CHAR, t->loc);
        e->value = t->value;
        return e;
    }
    case T_STRING: {
        advance(p);
        Expr *e = new_expr(E_STRING, t->loc);
        e->bytes = t->bytes;
        e->bytes_len = t->bytes_len;
        return e;
    }
    case T_TRUE:
    case T_FALSE: {
        advance(p);
        Expr *e = new_expr(E_BOOL, t->loc);
        e->value = t->kind == T_TRUE;
        return e;
    }
    case T_IDENT: {
        advance(p);
        Expr *e = new_expr(E_IDENT, t->loc);
        e->name = ident_text(t);
        return e;
    }
    case T_LPAREN: {
        advance(p);
        Expr *e = parse_expr(p);
        expect(p, T_RPAREN, NULL);
        return e;
    }
    default:
        unexpected(p, "an expression");
    }
}

static void parse_args(Parser *p, Vec *args) {
    expect(p, T_LPAREN, NULL);
    while (!at(p, T_RPAREN)) {
        Arg *a = NEW(Arg);
        a->loc = peek(p)->loc;
        if (at(p, T_IDENT) && peek_next(p)->kind == T_COLON) {
            a->label = ident_text(advance(p));
            advance(p);
        }
        a->value = parse_expr(p);
        vec_push(args, a);
        if (!accept(p, T_COMMA)) {
            break;
        }
    }
    expect(p, T_RPAREN, "',' or ')'");
}

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    for (;;) {
        if (at(p, T_LPAREN)) {
            Expr *call = new_expr(E_CALL, e->loc);
            call->callee = e;
            parse_args(p, &call->args);
            e = call;
        } else if (at(p, T_DOT)) {
            advance(p);
            Token *name = expect(p, T_IDENT, "a field or method name");
            Expr *field = new_expr(E_FIELD, e->loc);
            field->base = e;
            field->name = ident_text(name);
            e = field;
        } else {
            return e;
        }
    }
}

static Expr *parse_prefix(Parser *p) {
    Token *t = peek(p);
    if (accept(p, T_AMP)) {
        Expr *e = new_expr(E_REF, t->loc);
        e->mut = accept(p, T_MUT);
        e->base = parse_prefix(p);
        return e;
    }
    return parse_postfix(p);
}

static Expr *parse_expr(Parser *p) {
    return parse_prefix(p);
}

// ---- statements -------------------------------------------------------------

static Block *parse_block(Parser *p);

static Stmt *new_stmt(StmtKind kind, Loc loc) {
    Stmt *s = NEW(Stmt);
    s->kind = kind;
    s->loc = loc;
    return s;
}

static Stmt *parse_stmt(Parser *p) {
    Token *t = peek(p);
    if (at(p, T_LBRACE)) {
        Stmt *s = new_stmt(S_BLOCK, t->loc);
        s->block = parse_block(p);
        return s;
    }
    if (accept(p, T_RETURN)) {
        Stmt *s = new_stmt(S_RETURN, t->loc);
        if (!at(p, T_SEMI)) {
            s->expr = parse_expr(p);
        }
        expect(p, T_SEMI, "';'");
        return s;
    }
    Stmt *s = new_stmt(S_EXPR, t->loc);
    s->expr = parse_expr(p);
    expect(p, T_SEMI, "';'");
    return s;
}

static Block *parse_block(Parser *p) {
    Block *b = NEW(Block);
    b->loc = expect(p, T_LBRACE, "'{'")->loc;
    while (!at(p, T_RBRACE)) {
        if (at(p, T_EOF)) {
            unexpected(p, "'}'");
        }
        vec_push(&b->stmts, parse_stmt(p));
    }
    advance(p);
    return b;
}

// ---- items ------------------------------------------------------------------

static FnDecl *parse_fn(Parser *p, Module *m, bool pub) {
    expect(p, T_FN, NULL);
    FnDecl *fn = NEW(FnDecl);
    fn->pub = pub;
    fn->module = m;
    fn->loc = peek(p)->loc;
    fn->name = expect_ident(p, "a function name");
    expect(p, T_LPAREN, "'('");
    while (!at(p, T_RPAREN)) {
        Param *param = NEW(Param);
        param->loc = peek(p)->loc;
        param->name = expect_ident(p, "a parameter name");
        expect(p, T_COLON, "':'");
        param->type_expr = parse_type(p);
        vec_push(&fn->params, param);
        if (!accept(p, T_COMMA)) {
            break;
        }
    }
    expect(p, T_RPAREN, "',' or ')'");
    if (accept(p, T_ARROW)) {
        fn->ret_expr = parse_type(p);
    }
    fn->body = parse_block(p);
    return fn;
}

static Import *parse_import(Parser *p) {
    Import *imp = NEW(Import);
    imp->loc = expect(p, T_IMPORT, NULL)->loc;
    vec_push(&imp->path, (void *)expect_ident(p, "a module path"));
    while (accept(p, T_DOT)) {
        vec_push(&imp->path, (void *)expect_ident(p, "a module name"));
    }
    if (accept(p, T_AS)) {
        imp->alias = expect_ident(p, "a name after 'as'");
    }
    expect(p, T_SEMI, "';'");
    return imp;
}

Module *parse_module(const SourceFile *f, const char *name) {
    Module *m = NEW(Module);
    m->name = name;
    m->file = f;
    Parser p = {lex_file(f), 0, f};

    while (at(&p, T_IMPORT)) {
        vec_push(&m->imports, parse_import(&p));
    }
    while (!at(&p, T_EOF)) {
        Token *t = peek(&p);
        if (t->kind == T_IMPORT) {
            fatal_at(t->loc, "imports must come before every other item");
        }
        if (t->kind == T_TEST) {
            // §12: stage0 may leave test blocks out; it still parses them.
            advance(&p);
            expect(&p, T_STRING, "a test name");
            parse_block(&p);
            continue;
        }
        bool pub = accept(&p, T_PUB);
        t = peek(&p);
        switch (t->kind) {
        case T_FN:
            vec_push(&m->fns, parse_fn(&p, m, pub));
            break;
        case T_CONST:
        case T_TYPE:
            fatal_at(t->loc, "stage0 does not support %s declarations yet", tok_kind_name(t->kind));
        default:
            unexpected(&p, "an item ('fn', 'const', 'type' or 'test')");
        }
    }
    return m;
}
