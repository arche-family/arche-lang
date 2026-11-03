// Lexer: spec/arche-0.md §2.

#include "arche0.h"

#include <stdlib.h>
#include <string.h>

static const struct {
    const char *text;
    TokKind kind;
} keywords[] = {
    {"and", T_AND},       {"as", T_AS},         {"break", T_BREAK},   {"catch", T_CATCH},   {"const", T_CONST},
    {"continue", T_CONTINUE}, {"defer", T_DEFER}, {"else", T_ELSE},   {"enum", T_ENUM},     {"err", T_ERR},
    {"error", T_ERROR},   {"false", T_FALSE},   {"fn", T_FN},         {"if", T_IF},         {"import", T_IMPORT},
    {"let", T_LET},       {"match", T_MATCH},   {"mut", T_MUT},       {"none", T_NONE},     {"not", T_NOT},
    {"ok", T_OK},         {"or", T_OR},         {"orelse", T_ORELSE}, {"pub", T_PUB},       {"return", T_RETURN},
    {"some", T_SOME},     {"struct", T_STRUCT}, {"test", T_TEST},     {"true", T_TRUE},     {"try", T_TRY},
    {"type", T_TYPE},     {"var", T_VAR},       {"while", T_WHILE},
};

// Reserved for full Arche (§2.1).
static const char *const reserved[] = {
    "asm", "comptime", "dyn", "ensures", "for", "in", "interface", "naked", "nopanic", "region", "requires", "unsafe", "where",
};

// Longest match first: three-character operators, then two, then one.
static const struct {
    const char *text;
    TokKind kind;
} operators[] = {
    {"+%=", T_PLUS_WRAP_ASSIGN}, {"-%=", T_MINUS_WRAP_ASSIGN}, {"*%=", T_STAR_WRAP_ASSIGN}, {"<<=", T_SHL_ASSIGN},
    {">>=", T_SHR_ASSIGN},      {"..=", T_DOT_DOT_EQ},

    {"+%", T_PLUS_WRAP},  {"-%", T_MINUS_WRAP}, {"*%", T_STAR_WRAP},     {"<<", T_SHL},          {">>", T_SHR},
    {"==", T_EQ_EQ},      {"!=", T_NOT_EQ},     {"<=", T_LE},            {">=", T_GE},           {"+=", T_PLUS_ASSIGN},
    {"-=", T_MINUS_ASSIGN}, {"*=", T_STAR_ASSIGN}, {"/=", T_SLASH_ASSIGN}, {"%=", T_PERCENT_ASSIGN}, {"&=", T_AMP_ASSIGN},
    {"|=", T_PIPE_ASSIGN}, {"^=", T_CARET_ASSIGN}, {"->", T_ARROW},       {"=>", T_FAT_ARROW},    {"..", T_DOT_DOT},
    {".*", T_DOT_STAR},

    {"+", T_PLUS},   {"-", T_MINUS},   {"*", T_STAR},     {"/", T_SLASH},    {"%", T_PERCENT},  {"&", T_AMP},
    {"|", T_PIPE},   {"^", T_CARET},   {"~", T_TILDE},    {"<", T_LT},       {">", T_GT},       {"=", T_ASSIGN},
    {".", T_DOT},    {",", T_COMMA},   {":", T_COLON},    {";", T_SEMI},     {"?", T_QUESTION}, {"!", T_BANG},
    {"(", T_LPAREN}, {")", T_RPAREN},  {"[", T_LBRACKET}, {"]", T_RBRACKET}, {"{", T_LBRACE},   {"}", T_RBRACE},
};

const char *tok_kind_name(TokKind k) {
    switch (k) {
    case T_EOF:
        return "end of file";
    case T_IDENT:
        return "identifier";
    case T_INT:
        return "integer literal";
    case T_CHAR:
        return "character literal";
    case T_STRING:
        return "string literal";
    case T_AS_WRAP:
        return "'as%'";
    case T_UNDERSCORE:
        return "'_'";
    default:
        break;
    }
    for (size_t i = 0; i < sizeof keywords / sizeof *keywords; i++) {
        if (keywords[i].kind == k) {
            return arena_printf("'%s'", keywords[i].text);
        }
    }
    for (size_t i = 0; i < sizeof operators / sizeof *operators; i++) {
        if (operators[i].kind == k) {
            return arena_printf("'%s'", operators[i].text);
        }
    }
    return "token";
}

typedef struct {
    const SourceFile *f;
    const char *s;
    size_t len;
    size_t pos;
    size_t line_start;
    uint32_t line;
    Vec tokens;
} Lexer;

static Loc loc_at(const Lexer *lx, size_t pos) {
    return (Loc){lx->f, lx->line, (uint32_t)(pos - lx->line_start + 1)};
}

static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static Token *push(Lexer *lx, TokKind kind, size_t start, size_t end) {
    Token *t = NEW(Token);
    t->kind = kind;
    t->loc = loc_at(lx, start);
    t->text = lx->s + start;
    t->text_len = end - start;
    vec_push(&lx->tokens, t);
    return t;
}

// The whole file must be UTF-8 before anything else is looked at.
static void check_utf8(const SourceFile *f) {
    const uint8_t *s = (const uint8_t *)f->text;
    size_t line_start = 0;
    uint32_t line = 1;
    size_t i = 0;
    while (i < f->len) {
        uint32_t cp;
        size_t n = utf8_decode(s + i, f->len - i, &cp);
        if (n == 0) {
            fatal_at((Loc){f, line, (uint32_t)(i - line_start + 1)}, "the file is not valid UTF-8");
        }
        if (cp == '\n') {
            line++;
            line_start = i + 1;
        }
        i += n;
    }
}

static void lex_ident(Lexer *lx) {
    size_t start = lx->pos;
    size_t end = start;
    while (end < lx->len && is_ident_char(lx->s[end])) {
        end++;
    }
    size_t n = end - start;
    const char *text = lx->s + start;
    lx->pos = end;
    if (n == 1 && text[0] == '_') {
        push(lx, T_UNDERSCORE, start, end);
        return;
    }
    for (size_t i = 0; i < sizeof reserved / sizeof *reserved; i++) {
        if (strlen(reserved[i]) == n && memcmp(reserved[i], text, n) == 0) {
            fatal_at(loc_at(lx, start), "'%s' is reserved for full Arche and cannot be used here", reserved[i]);
        }
    }
    for (size_t i = 0; i < sizeof keywords / sizeof *keywords; i++) {
        if (strlen(keywords[i].text) == n && memcmp(keywords[i].text, text, n) == 0) {
            if (keywords[i].kind == T_AS && end < lx->len && lx->s[end] == '%') {
                lx->pos = end + 1;
                push(lx, T_AS_WRAP, start, end + 1);
                return;
            }
            push(lx, keywords[i].kind, start, end);
            return;
        }
    }
    push(lx, T_IDENT, start, end);
}

static void lex_number(Lexer *lx) {
    size_t start = lx->pos;
    size_t end = start;
    while (end < lx->len && is_ident_char(lx->s[end])) {
        end++;
    }
    const char *s = lx->s;
    unsigned base = 10;
    const char *base_name = "decimal";
    size_t i = start;
    if (s[i] == '0' && i + 1 < end) {
        if (s[i + 1] == 'x') {
            base = 16;
            base_name = "hexadecimal";
            i += 2;
        } else if (s[i + 1] == 'o') {
            base = 8;
            base_name = "octal";
            i += 2;
        } else if (s[i + 1] == 'b') {
            base = 2;
            base_name = "binary";
            i += 2;
        }
    }
    if (i == end) {
        fatal_at(loc_at(lx, start), "integer literal has no digits");
    }
    uint64_t v = 0;
    bool after_digit = false;
    for (; i < end; i++) {
        char c = s[i];
        if (c == '_') {
            if (!after_digit || i + 1 == end || s[i + 1] == '_') {
                fatal_at(loc_at(lx, i), "'_' in an integer literal must separate two digits");
            }
            after_digit = false;
            continue;
        }
        int d = hex_value(c);
        if (d < 0 || (unsigned)d >= base) {
            fatal_at(loc_at(lx, i), "invalid digit '%c' in a %s literal", c, base_name);
        }
        if (v > (UINT64_MAX - (unsigned)d) / base) {
            fatal_at(loc_at(lx, start), "integer literal does not fit in 64 bits");
        }
        v = v * base + (unsigned)d;
        after_digit = true;
    }
    lx->pos = end;
    push(lx, T_INT, start, end)->value = v;
}

// Reads the escape at lx->s[*i] == '\\'. Returns the code point and advances *i.
static uint32_t lex_escape(Lexer *lx, size_t *i, bool in_string) {
    size_t at = *i;
    if (at + 1 >= lx->len) {
        fatal_at(loc_at(lx, at), "unfinished escape");
    }
    char c = lx->s[at + 1];
    *i = at + 2;
    switch (c) {
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case '0':
        return 0;
    case '\\':
    case '"':
    case '\'':
        return (uint8_t)c;
    case 'x': {
        int hi = at + 2 < lx->len ? hex_value(lx->s[at + 2]) : -1;
        int lo = at + 3 < lx->len ? hex_value(lx->s[at + 3]) : -1;
        if (hi < 0 || lo < 0) {
            fatal_at(loc_at(lx, at), "'\\x' needs two hexadecimal digits");
        }
        uint32_t v = (uint32_t)(hi * 16 + lo);
        if (v > 0x7F) {
            fatal_at(loc_at(lx, at), "'\\x' escapes go up to \\x7F; use \\u{...} for other characters");
        }
        *i = at + 4;
        return v;
    }
    case 'u': {
        if (!in_string) {
            fatal_at(loc_at(lx, at), "'\\u{...}' is allowed only in string literals");
        }
        size_t j = at + 2;
        if (j >= lx->len || lx->s[j] != '{') {
            fatal_at(loc_at(lx, at), "expected '{' after '\\u'");
        }
        j++;
        uint32_t v = 0;
        int digits = 0;
        while (j < lx->len && hex_value(lx->s[j]) >= 0) {
            if (++digits > 6) {
                fatal_at(loc_at(lx, at), "'\\u{...}' takes at most 6 hexadecimal digits");
            }
            v = v * 16 + (uint32_t)hex_value(lx->s[j]);
            j++;
        }
        if (digits == 0 || j >= lx->len || lx->s[j] != '}') {
            fatal_at(loc_at(lx, at), "'\\u{...}' needs hexadecimal digits and a closing '}'");
        }
        if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) {
            fatal_at(loc_at(lx, at), "'\\u{%X}' is not a Unicode scalar value", (unsigned)v);
        }
        *i = j + 1;
        return v;
    }
    default:
        fatal_at(loc_at(lx, at), "unknown escape '\\%c'", c);
    }
}

static void lex_char(Lexer *lx) {
    size_t start = lx->pos;
    size_t i = start + 1;
    if (i >= lx->len || lx->s[i] == '\n') {
        fatal_at(loc_at(lx, start), "character literal is not closed");
    }
    if (lx->s[i] == '\'') {
        fatal_at(loc_at(lx, start), "empty character literal");
    }
    uint32_t v;
    if (lx->s[i] == '\\') {
        v = lex_escape(lx, &i, false);
    } else {
        uint8_t c = (uint8_t)lx->s[i];
        if (c >= 0x80) {
            fatal_at(loc_at(lx, start), "a character literal must be ASCII");
        }
        if (c == '\r') {
            fatal_at(loc_at(lx, i), "'\\r' is allowed only in string literals");
        }
        v = c;
        i++;
    }
    if (i >= lx->len || lx->s[i] != '\'') {
        fatal_at(loc_at(lx, start), "a character literal holds exactly one character");
    }
    lx->pos = i + 1;
    push(lx, T_CHAR, start, i + 1)->value = v;
}

static void lex_string(Lexer *lx) {
    size_t start = lx->pos;
    size_t i = start + 1;
    StrBuf b = {0};
    sb_add(&b, "", 0);
    for (;;) {
        if (i >= lx->len || lx->s[i] == '\n') {
            fatal_at(loc_at(lx, start), "string literal is not closed (it cannot span lines)");
        }
        char c = lx->s[i];
        if (c == '"') {
            break;
        }
        if (c != '\\') {
            sb_add(&b, &c, 1);
            i++;
            continue;
        }
        uint32_t v = lex_escape(lx, &i, true);
        char enc[4];
        size_t n;
        if (v < 0x80) {
            enc[0] = (char)v;
            n = 1;
        } else if (v < 0x800) {
            enc[0] = (char)(0xC0 | (v >> 6));
            enc[1] = (char)(0x80 | (v & 0x3F));
            n = 2;
        } else if (v < 0x10000) {
            enc[0] = (char)(0xE0 | (v >> 12));
            enc[1] = (char)(0x80 | ((v >> 6) & 0x3F));
            enc[2] = (char)(0x80 | (v & 0x3F));
            n = 3;
        } else {
            enc[0] = (char)(0xF0 | (v >> 18));
            enc[1] = (char)(0x80 | ((v >> 12) & 0x3F));
            enc[2] = (char)(0x80 | ((v >> 6) & 0x3F));
            enc[3] = (char)(0x80 | (v & 0x3F));
            n = 4;
        }
        sb_add(&b, enc, n);
    }
    lx->pos = i + 1;
    Token *t = push(lx, T_STRING, start, i + 1);
    t->bytes = arena_strndup(b.data, b.len);
    t->bytes_len = b.len;
    free(b.data);
}

static void lex_comment(Lexer *lx) {
    size_t i = lx->pos;
    while (i < lx->len && lx->s[i] != '\n') {
        if (lx->s[i] == '\r') {
            fatal_at(loc_at(lx, i), "'\\r' is allowed only in string literals; lines end with '\\n'");
        }
        i++;
    }
    lx->pos = i;
}

static void lex_operator(Lexer *lx) {
    for (size_t i = 0; i < sizeof operators / sizeof *operators; i++) {
        size_t n = strlen(operators[i].text);
        if (lx->pos + n <= lx->len && memcmp(lx->s + lx->pos, operators[i].text, n) == 0) {
            push(lx, operators[i].kind, lx->pos, lx->pos + n);
            lx->pos += n;
            return;
        }
    }
    uint8_t c = (uint8_t)lx->s[lx->pos];
    if (c >= 0x80) {
        uint32_t cp = 0;
        utf8_decode((const uint8_t *)lx->s + lx->pos, lx->len - lx->pos, &cp);
        fatal_at(loc_at(lx, lx->pos), "unexpected character U+%04X; identifiers are ASCII", (unsigned)cp);
    }
    if (c < 0x20 || c == 0x7F) {
        fatal_at(loc_at(lx, lx->pos), "unexpected control character 0x%02X", c);
    }
    fatal_at(loc_at(lx, lx->pos), "unexpected character '%c'", c);
}

Token *lex_file(const SourceFile *f) {
    check_utf8(f);
    Lexer lx = {.f = f, .s = f->text, .len = f->len, .line = 1};
    while (lx.pos < lx.len) {
        char c = lx.s[lx.pos];
        if (c == '\n') {
            lx.pos++;
            lx.line++;
            lx.line_start = lx.pos;
        } else if (c == ' ' || c == '\t') {
            lx.pos++;
        } else if (c == '\r') {
            fatal_at(loc_at(&lx, lx.pos), "'\\r' is allowed only in string literals; lines end with '\\n'");
        } else if (c == '/' && lx.pos + 1 < lx.len && lx.s[lx.pos + 1] == '/') {
            lex_comment(&lx);
        } else if (is_ident_start(c)) {
            lex_ident(&lx);
        } else if (c >= '0' && c <= '9') {
            lex_number(&lx);
        } else if (c == '\'') {
            lex_char(&lx);
        } else if (c == '"') {
            lex_string(&lx);
        } else {
            lex_operator(&lx);
        }
    }
    push(&lx, T_EOF, lx.pos, lx.pos);
    Token *tokens = arena_alloc(lx.tokens.len * sizeof *tokens);
    for (size_t i = 0; i < lx.tokens.len; i++) {
        tokens[i] = *VEC_AT(lx.tokens, Token, i);
    }
    free(lx.tokens.items);
    return tokens;
}
