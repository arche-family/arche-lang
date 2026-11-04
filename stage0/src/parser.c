// Parser: the whole grammar of spec/arche-0.md §14. Builds the AST directly;
// stage0 keeps no CST. Doc comments never reach the parser (lexer.c).

#include "arche0.h"

#include <string.h>

typedef struct {
    Token *t;
    size_t pos;
    // A struct literal cannot be an if/while condition or a match scrutinee
    // unless it is in parentheses (§7.4).
    bool no_struct_lit;
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
        bool saved = p->no_struct_lit;
        p->no_struct_lit = false;
        t->len = parse_expr(p);
        p->no_struct_lit = saved;
        expect(p, T_RBRACKET, "']'");
        t->elem = parse_type(p);
        return t;
    }
    if (!at(p, T_IDENT)) {
        unexpected(p, "a type");
    }
    TypeExpr *t = new_type_expr(TE_PATH, start->loc);
    vec_push(&t->segments, (void *)expect_ident(p, NULL));
    while (accept(p, T_DOT)) {
        vec_push(&t->segments, (void *)expect_ident(p, "a name"));
    }
    if (accept(p, T_LPAREN)) {
        while (!at(p, T_RPAREN)) {
            vec_push(&t->args, parse_type(p));
            if (!accept(p, T_COMMA)) {
                break;
            }
        }
        expect(p, T_RPAREN, "',' or ')'");
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

// Parses with struct literals allowed again: inside (), [], {} and arguments.
static Expr *parse_nested_expr(Parser *p) {
    bool saved = p->no_struct_lit;
    p->no_struct_lit = false;
    Expr *e = parse_expr(p);
    p->no_struct_lit = saved;
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
    case T_NONE:
        advance(p);
        return new_expr(E_NONE, t->loc);
    case T_SOME: {
        advance(p);
        Expr *e = new_expr(E_SOME, t->loc);
        expect(p, T_LPAREN, "'(' after 'some'");
        e->base = parse_nested_expr(p);
        expect(p, T_RPAREN, "')'");
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
        Expr *e = parse_nested_expr(p);
        expect(p, T_RPAREN, "')'");
        e->parens = true;
        return e;
    }
    case T_LBRACKET: {
        advance(p);
        Expr *e = new_expr(E_ARRAY_LIT, t->loc);
        while (!at(p, T_RBRACKET)) {
            vec_push(&e->elems, parse_nested_expr(p));
            if (!accept(p, T_COMMA)) {
                break;
            }
        }
        expect(p, T_RBRACKET, "',' or ']'");
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
        a->value = parse_nested_expr(p);
        vec_push(args, a);
        if (!accept(p, T_COMMA)) {
            break;
        }
    }
    expect(p, T_RPAREN, "',' or ')'");
}

static bool is_path(const Expr *e) {
    if (e->parens) {
        return false;
    }
    return e->kind == E_IDENT || (e->kind == E_FIELD && is_path(e->base));
}

static Expr *parse_struct_lit(Parser *p, Expr *path) {
    Expr *e = new_expr(E_STRUCT_LIT, path->loc);
    e->type_path = path;
    expect(p, T_LBRACE, NULL);
    while (!at(p, T_RBRACE)) {
        FieldInit *f = NEW(FieldInit);
        f->loc = peek(p)->loc;
        f->name = expect_ident(p, "a field name");
        expect(p, T_COLON, "':'");
        f->value = parse_nested_expr(p);
        vec_push(&e->fields, f);
        if (!accept(p, T_COMMA)) {
            break;
        }
    }
    expect(p, T_RBRACE, "',' or '}'");
    return e;
}

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    for (;;) {
        Token *t = peek(p);
        if (t->kind == T_LPAREN) {
            Expr *call = new_expr(E_CALL, e->loc);
            call->callee = e;
            parse_args(p, &call->args);
            e = call;
        } else if (t->kind == T_DOT) {
            advance(p);
            Token *name = expect(p, T_IDENT, "a field or method name");
            Expr *field = new_expr(E_FIELD, e->loc);
            field->base = e;
            field->name = ident_text(name);
            e = field;
        } else if (t->kind == T_DOT_STAR) {
            advance(p);
            Expr *deref = new_expr(E_DEREF, e->loc);
            deref->base = e;
            e = deref;
        } else if (t->kind == T_LBRACKET) {
            advance(p);
            Expr *start = parse_nested_expr(p);
            Expr *ix;
            if (accept(p, T_DOT_DOT)) {
                ix = new_expr(E_SLICE, t->loc);
                ix->end = parse_nested_expr(p);
            } else {
                ix = new_expr(E_INDEX, t->loc);
            }
            ix->base = e;
            ix->index = start;
            expect(p, T_RBRACKET, "']'");
            e = ix;
        } else if (t->kind == T_LBRACE && !p->no_struct_lit && is_path(e)) {
            e = parse_struct_lit(p, e);
        } else {
            return e;
        }
    }
}

static Expr *parse_prefix(Parser *p) {
    Token *t = peek(p);
    switch (t->kind) {
    case T_MINUS:
    case T_TILDE:
    case T_NOT: {
        advance(p);
        Expr *e = new_expr(E_UNARY, t->loc);
        e->unary = t->kind == T_MINUS ? U_NEG : t->kind == T_TILDE ? U_BITNOT : U_NOT;
        e->base = parse_prefix(p);
        return e;
    }
    case T_TRY: {
        advance(p);
        Expr *e = new_expr(E_TRY, t->loc);
        e->base = parse_prefix(p);
        return e;
    }
    case T_AMP: {
        advance(p);
        Expr *e = new_expr(E_REF, t->loc);
        e->mut = accept(p, T_MUT);
        e->base = parse_prefix(p);
        return e;
    }
    default:
        return parse_postfix(p);
    }
}

static Expr *binary(BinaryOp op, Loc loc, Expr *lhs, Expr *rhs) {
    Expr *e = new_expr(E_BINARY, loc);
    e->binary = op;
    e->base = lhs;
    e->rhs = rhs;
    return e;
}

static Expr *parse_cast(Parser *p) {
    Expr *e = parse_prefix(p);
    while (at(p, T_AS) || at(p, T_AS_WRAP)) {
        Token *op = advance(p);
        Expr *c = new_expr(E_CAST, op->loc);
        c->base = e;
        c->wrapping = op->kind == T_AS_WRAP;
        c->cast_to = parse_type(p);
        e = c;
    }
    return e;
}

static Expr *parse_mul(Parser *p) {
    Expr *e = parse_cast(p);
    for (;;) {
        Token *t = peek(p);
        BinaryOp op;
        switch (t->kind) {
        case T_STAR:
            op = B_MUL;
            break;
        case T_SLASH:
            op = B_DIV;
            break;
        case T_PERCENT:
            op = B_REM;
            break;
        case T_STAR_WRAP:
            op = B_WMUL;
            break;
        default:
            return e;
        }
        advance(p);
        e = binary(op, t->loc, e, parse_cast(p));
    }
}

static Expr *parse_add(Parser *p) {
    Expr *e = parse_mul(p);
    for (;;) {
        Token *t = peek(p);
        BinaryOp op;
        switch (t->kind) {
        case T_PLUS:
            op = B_ADD;
            break;
        case T_MINUS:
            op = B_SUB;
            break;
        case T_PLUS_WRAP:
            op = B_WADD;
            break;
        case T_MINUS_WRAP:
            op = B_WSUB;
            break;
        default:
            return e;
        }
        advance(p);
        e = binary(op, t->loc, e, parse_mul(p));
    }
}

static Expr *parse_shift(Parser *p) {
    Expr *e = parse_add(p);
    while (at(p, T_SHL) || at(p, T_SHR)) {
        Token *t = advance(p);
        e = binary(t->kind == T_SHL ? B_SHL : B_SHR, t->loc, e, parse_add(p));
    }
    return e;
}

// `&`, `^` and `|` cannot be mixed without parentheses (§7.1).
static Expr *parse_bit(Parser *p) {
    Expr *e = parse_shift(p);
    TokKind first = T_EOF;
    while (at(p, T_AMP) || at(p, T_CARET) || at(p, T_PIPE)) {
        Token *t = advance(p);
        if (first != T_EOF && t->kind != first) {
            fatal_at(t->loc, "'&', '^' and '|' cannot be mixed without parentheses");
        }
        first = t->kind;
        BinaryOp op = t->kind == T_AMP ? B_BITAND : t->kind == T_CARET ? B_BITXOR : B_BITOR;
        e = binary(op, t->loc, e, parse_shift(p));
    }
    return e;
}

static bool comparison_op(TokKind k, BinaryOp *op) {
    switch (k) {
    case T_EQ_EQ:
        *op = B_EQ;
        return true;
    case T_NOT_EQ:
        *op = B_NE;
        return true;
    case T_LT:
        *op = B_LT;
        return true;
    case T_LE:
        *op = B_LE;
        return true;
    case T_GT:
        *op = B_GT;
        return true;
    case T_GE:
        *op = B_GE;
        return true;
    default:
        return false;
    }
}

static Expr *parse_cmp(Parser *p) {
    Expr *e = parse_bit(p);
    BinaryOp op;
    if (comparison_op(peek(p)->kind, &op)) {
        Token *t = advance(p);
        e = binary(op, t->loc, e, parse_bit(p));
        if (comparison_op(peek(p)->kind, &op)) {
            fatal_at(peek(p)->loc, "comparisons do not chain; combine them with 'and'");
        }
    }
    return e;
}

static Expr *parse_and(Parser *p) {
    Expr *e = parse_cmp(p);
    while (at(p, T_AND)) {
        Token *t = advance(p);
        e = binary(B_AND, t->loc, e, parse_cmp(p));
    }
    return e;
}

// `and` and `or` cannot be mixed without parentheses (§7.1).
static Expr *parse_or(Parser *p) {
    Expr *e = parse_and(p);
    while (at(p, T_OR)) {
        Token *t = advance(p);
        Expr *rhs = parse_and(p);
        bool mixed = (e->kind == E_BINARY && e->binary == B_AND && !e->parens) ||
                     (rhs->kind == E_BINARY && rhs->binary == B_AND && !rhs->parens);
        if (mixed) {
            fatal_at(t->loc, "'and' and 'or' cannot be mixed without parentheses");
        }
        e = binary(B_OR, t->loc, e, rhs);
    }
    return e;
}

static Expr *parse_expr(Parser *p) {
    Expr *e = parse_or(p);
    if (at(p, T_ORELSE) || at(p, T_CATCH)) {
        Token *t = advance(p);
        Expr *f = new_expr(t->kind == T_ORELSE ? E_ORELSE : E_CATCH, t->loc);
        f->base = e;
        f->rhs = parse_expr(p); // right-associative
        return f;
    }
    return e;
}

// A condition or scrutinee: a struct literal there needs parentheses.
static Expr *parse_condition(Parser *p) {
    bool saved = p->no_struct_lit;
    p->no_struct_lit = true;
    Expr *e = parse_expr(p);
    p->no_struct_lit = saved;
    return e;
}

// ---- patterns ---------------------------------------------------------------

static Pattern *new_pattern(PatternKind kind, Loc loc) {
    Pattern *pat = NEW(Pattern);
    pat->kind = kind;
    pat->loc = loc;
    return pat;
}

static bool parse_pat_lit(Parser *p, PatLit *lit) {
    bool negative = accept(p, T_MINUS);
    Token *t = peek(p);
    if (t->kind == T_INT || (t->kind == T_CHAR && !negative)) {
        advance(p);
        lit->negative = negative;
        lit->magnitude = t->value;
        lit->is_char = t->kind == T_CHAR;
        return true;
    }
    if (negative) {
        unexpected(p, "an integer after '-'");
    }
    return false;
}

static Pattern *parse_pattern(Parser *p) {
    Token *t = peek(p);
    switch (t->kind) {
    case T_UNDERSCORE:
        advance(p);
        return new_pattern(P_WILDCARD, t->loc);
    case T_IDENT: {
        advance(p);
        Pattern *pat = new_pattern(P_BIND, t->loc);
        pat->name = ident_text(t);
        return pat;
    }
    case T_TRUE:
    case T_FALSE: {
        advance(p);
        Pattern *pat = new_pattern(P_BOOL, t->loc);
        pat->value = t->kind == T_TRUE;
        return pat;
    }
    case T_NONE:
        advance(p);
        return new_pattern(P_NONE, t->loc);
    case T_SOME:
    case T_OK:
    case T_ERR: {
        advance(p);
        Pattern *pat = new_pattern(t->kind == T_SOME ? P_SOME : t->kind == T_OK ? P_OK : P_ERR, t->loc);
        expect(p, T_LPAREN, "'('");
        pat->inner = parse_pattern(p);
        expect(p, T_RPAREN, "')'");
        return pat;
    }
    case T_DOT: {
        advance(p);
        Pattern *pat = new_pattern(P_VARIANT, t->loc);
        pat->name = expect_ident(p, "a variant or error name");
        if (accept(p, T_LBRACE)) {
            pat->has_fields = true;
            while (!at(p, T_RBRACE)) {
                if (accept(p, T_DOT_DOT)) {
                    pat->rest = true;
                    break;
                }
                FieldPat *fp = NEW(FieldPat);
                fp->loc = peek(p)->loc;
                fp->field = expect_ident(p, "a field name");
                fp->bind = fp->field;
                if (accept(p, T_COLON)) {
                    if (accept(p, T_UNDERSCORE)) {
                        fp->bind = NULL;
                    } else {
                        fp->bind = expect_ident(p, "a name or '_'");
                    }
                }
                vec_push(&pat->field_pats, fp);
                if (!accept(p, T_COMMA)) {
                    break;
                }
            }
            expect(p, T_RBRACE, "',' or '}'");
        }
        return pat;
    }
    default: {
        Pattern *pat = new_pattern(P_INT, t->loc);
        if (!parse_pat_lit(p, &pat->lo)) {
            unexpected(p, "a pattern");
        }
        if (accept(p, T_DOT_DOT_EQ)) {
            pat->kind = P_RANGE;
            if (!parse_pat_lit(p, &pat->hi)) {
                unexpected(p, "the end of the range");
            }
        }
        return pat;
    }
    }
}

// ---- statements -------------------------------------------------------------

static Block *parse_block(Parser *p);

static Stmt *new_stmt(StmtKind kind, Loc loc) {
    Stmt *s = NEW(Stmt);
    s->kind = kind;
    s->loc = loc;
    return s;
}

static bool assign_op(TokKind k, AssignOp *op) {
    static const struct {
        TokKind tok;
        AssignOp op;
    } ops[] = {
        {T_ASSIGN, A_SET},           {T_PLUS_ASSIGN, A_ADD},        {T_MINUS_ASSIGN, A_SUB},
        {T_STAR_ASSIGN, A_MUL},      {T_SLASH_ASSIGN, A_DIV},       {T_PERCENT_ASSIGN, A_REM},
        {T_PLUS_WRAP_ASSIGN, A_WADD}, {T_MINUS_WRAP_ASSIGN, A_WSUB}, {T_STAR_WRAP_ASSIGN, A_WMUL},
        {T_AMP_ASSIGN, A_BITAND},    {T_PIPE_ASSIGN, A_BITOR},      {T_CARET_ASSIGN, A_BITXOR},
        {T_SHL_ASSIGN, A_SHL},       {T_SHR_ASSIGN, A_SHR},
    };
    for (size_t i = 0; i < sizeof ops / sizeof *ops; i++) {
        if (ops[i].tok == k) {
            *op = ops[i].op;
            return true;
        }
    }
    return false;
}

// simple = "_" "=" expr | expr [ assign_op expr ]
static Stmt *parse_simple(Parser *p) {
    Token *t = peek(p);
    if (accept(p, T_UNDERSCORE)) {
        expect(p, T_ASSIGN, "'=' after '_'");
        Stmt *s = new_stmt(S_DISCARD, t->loc);
        s->expr = parse_expr(p);
        return s;
    }
    Expr *e = parse_expr(p);
    AssignOp op;
    if (assign_op(peek(p)->kind, &op)) {
        Stmt *s = new_stmt(S_ASSIGN, peek(p)->loc);
        advance(p);
        s->op = op;
        s->target = e;
        s->expr = parse_expr(p);
        return s;
    }
    Stmt *s = new_stmt(S_EXPR, t->loc);
    s->expr = e;
    return s;
}

static Stmt *parse_if(Parser *p, Loc loc) {
    Stmt *s = new_stmt(S_IF, loc);
    s->expr = parse_condition(p);
    s->block = parse_block(p);
    if (accept(p, T_ELSE)) {
        Token *t = peek(p);
        if (accept(p, T_IF)) {
            s->els = parse_if(p, t->loc);
        } else {
            s->els = new_stmt(S_BLOCK, t->loc);
            s->els->block = parse_block(p);
        }
    }
    return s;
}

static Stmt *parse_stmt(Parser *p) {
    Token *t = peek(p);
    switch (t->kind) {
    case T_LET:
    case T_VAR: {
        advance(p);
        Stmt *s = new_stmt(S_LET, t->loc);
        s->mutable = t->kind == T_VAR;
        s->name = expect_ident(p, "a name");
        if (accept(p, T_COLON)) {
            s->type_expr = parse_type(p);
        }
        expect(p, T_ASSIGN, "'=': every binding is initialized");
        s->expr = parse_expr(p);
        expect(p, T_SEMI, "';'");
        return s;
    }
    case T_IF:
        advance(p);
        return parse_if(p, t->loc);
    case T_WHILE: {
        advance(p);
        Stmt *s = new_stmt(S_WHILE, t->loc);
        s->expr = parse_condition(p);
        if (accept(p, T_COLON)) {
            expect(p, T_LPAREN, "'(' around the step");
            s->step = parse_simple(p);
            if (s->step->kind == S_DISCARD) {
                fatal_at(s->step->loc, "a while step is an assignment or an expression");
            }
            expect(p, T_RPAREN, "')'");
        }
        s->block = parse_block(p);
        return s;
    }
    case T_MATCH: {
        advance(p);
        Stmt *s = new_stmt(S_MATCH, t->loc);
        s->expr = parse_condition(p);
        expect(p, T_LBRACE, "'{'");
        while (!at(p, T_RBRACE)) {
            MatchArm *arm = NEW(MatchArm);
            arm->loc = peek(p)->loc;
            do {
                vec_push(&arm->patterns, parse_pattern(p));
            } while (accept(p, T_PIPE));
            expect(p, T_FAT_ARROW, "'=>'");
            arm->body = parse_block(p);
            accept(p, T_COMMA);
            vec_push(&s->arms, arm);
        }
        advance(p);
        return s;
    }
    case T_RETURN: {
        advance(p);
        Stmt *s = new_stmt(S_RETURN, t->loc);
        if (!at(p, T_SEMI)) {
            s->expr = parse_expr(p);
        }
        expect(p, T_SEMI, "';'");
        return s;
    }
    case T_BREAK:
    case T_CONTINUE:
        advance(p);
        expect(p, T_SEMI, "';'");
        return new_stmt(t->kind == T_BREAK ? S_BREAK : S_CONTINUE, t->loc);
    case T_DEFER: {
        advance(p);
        Stmt *s = new_stmt(S_DEFER, t->loc);
        if (at(p, T_LBRACE)) {
            s->step = new_stmt(S_BLOCK, peek(p)->loc);
            s->step->block = parse_block(p);
        } else {
            s->step = parse_simple(p);
            expect(p, T_SEMI, "';'");
        }
        return s;
    }
    case T_LBRACE: {
        Stmt *s = new_stmt(S_BLOCK, t->loc);
        s->block = parse_block(p);
        return s;
    }
    default: {
        Stmt *s = parse_simple(p);
        expect(p, T_SEMI, "';'");
        return s;
    }
    }
}

static Block *parse_block(Parser *p) {
    Block *b = NEW(Block);
    b->loc = expect(p, T_LBRACE, "'{'")->loc;
    bool saved = p->no_struct_lit;
    p->no_struct_lit = false;
    while (!at(p, T_RBRACE)) {
        if (at(p, T_EOF)) {
            unexpected(p, "'}'");
        }
        vec_push(&b->stmts, parse_stmt(p));
    }
    p->no_struct_lit = saved;
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

static FieldDecl *parse_field(Parser *p, bool pub) {
    FieldDecl *f = NEW(FieldDecl);
    f->pub = pub;
    f->loc = peek(p)->loc;
    f->name = expect_ident(p, "a field name");
    expect(p, T_COLON, "':'");
    f->type_expr = parse_type(p);
    return f;
}

// Methods of a struct or enum body; fields are allowed only in structs and
// come before every method (§4).
static void parse_members(Parser *p, TypeDecl *td, Module *m) {
    while (!at(p, T_RBRACE)) {
        bool pub = accept(p, T_PUB);
        if (at(p, T_FN)) {
            FnDecl *fn = parse_fn(p, m, pub);
            fn->owner = td;
            vec_push(&td->methods, fn);
            continue;
        }
        if (!at(p, T_IDENT)) {
            unexpected(p, td->kind == TD_STRUCT ? "a field or 'fn'" : "'fn' or '}'");
        }
        if (td->kind != TD_STRUCT) {
            fatal_at(peek(p)->loc, "an enum lists its variants first, separated by ',', then its methods");
        }
        if (td->methods.len > 0) {
            fatal_at(peek(p)->loc, "a struct lists its fields before its methods");
        }
        vec_push(&td->fields, parse_field(p, pub));
        if (!at(p, T_RBRACE)) {
            expect(p, T_COMMA, "',' after the field");
        }
    }
}

static TypeDecl *parse_type_decl(Parser *p, Module *m, bool pub) {
    expect(p, T_TYPE, NULL);
    TypeDecl *td = NEW(TypeDecl);
    td->pub = pub;
    td->module = m;
    td->loc = peek(p)->loc;
    td->name = expect_ident(p, "a type name");
    expect(p, T_ASSIGN, "'='");
    if (accept(p, T_STRUCT)) {
        td->kind = TD_STRUCT;
        expect(p, T_LBRACE, "'{'");
        parse_members(p, td, m);
        advance(p);
    } else if (accept(p, T_ENUM)) {
        td->kind = TD_ENUM;
        expect(p, T_LBRACE, "'{'");
        do {
            if (!at(p, T_IDENT)) {
                break;
            }
            VariantDecl *v = NEW(VariantDecl);
            v->loc = peek(p)->loc;
            v->name = expect_ident(p, NULL);
            if (accept(p, T_LBRACE)) {
                v->has_fields = true;
                do {
                    if (at(p, T_RBRACE)) {
                        break;
                    }
                    vec_push(&v->fields, parse_field(p, false));
                } while (accept(p, T_COMMA));
                expect(p, T_RBRACE, "',' or '}'");
                if (v->fields.len == 0) {
                    fatal_at(v->loc, "a variant written with '{ }' needs at least one field");
                }
            }
            vec_push(&td->variants, v);
        } while (accept(p, T_COMMA));
        if (td->variants.len == 0) {
            unexpected(p, "a variant");
        }
        parse_members(p, td, m);
        advance(p);
    } else if (accept(p, T_ERROR)) {
        td->kind = TD_ERROR;
        expect(p, T_LBRACE, "'{'");
        do {
            if (at(p, T_RBRACE) && td->errors.len > 0) {
                break;
            }
            ErrorName *e = NEW(ErrorName);
            e->loc = peek(p)->loc;
            e->name = expect_ident(p, "an error name");
            vec_push(&td->errors, e);
        } while (accept(p, T_COMMA));
        expect(p, T_RBRACE, "',' or '}'");
    } else {
        td->kind = TD_ALIAS;
        td->alias = parse_type(p);
        expect(p, T_SEMI, "';'");
    }
    return td;
}

static ConstDecl *parse_const(Parser *p, Module *m, bool pub) {
    expect(p, T_CONST, NULL);
    ConstDecl *c = NEW(ConstDecl);
    c->pub = pub;
    c->module = m;
    c->loc = peek(p)->loc;
    c->name = expect_ident(p, "a constant name");
    expect(p, T_COLON, "':' and the constant's type");
    c->type_expr = parse_type(p);
    expect(p, T_ASSIGN, "'='");
    c->value = parse_expr(p);
    expect(p, T_SEMI, "';'");
    return c;
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
    Parser p = {lex_file(f), 0, false};

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
            vec_push(&m->consts, parse_const(&p, m, pub));
            break;
        case T_TYPE:
            vec_push(&m->types, parse_type_decl(&p, m, pub));
            break;
        case T_VAR:
        case T_LET:
            fatal_at(t->loc, "a module has no '%s' bindings; use 'const' for a module-level value",
                     t->kind == T_VAR ? "var" : "let");
        default:
            unexpected(&p, pub ? "'fn', 'const' or 'type' after 'pub'" : "an item: 'fn', 'const', 'type' or 'test'");
        }
    }
    return m;
}
