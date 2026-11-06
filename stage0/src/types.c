// Types and the built-in library surface of spec/arche-0.md §11.

#include "arche0.h"

#include <string.h>

static Type void_type = {.kind = TY_VOID};
static Type noreturn_type = {.kind = TY_NORETURN};
static Type bool_type = {.kind = TY_BOOL};
static Type str_type = {.kind = TY_STR};
static Type int_types[INT_KIND_COUNT] = {
    [I8] = {.kind = TY_INT, .int_kind = I8},       [I16] = {.kind = TY_INT, .int_kind = I16},
    [I32] = {.kind = TY_INT, .int_kind = I32},     [I64] = {.kind = TY_INT, .int_kind = I64},
    [ISIZE] = {.kind = TY_INT, .int_kind = ISIZE}, [U8] = {.kind = TY_INT, .int_kind = U8},
    [U16] = {.kind = TY_INT, .int_kind = U16},     [U32] = {.kind = TY_INT, .int_kind = U32},
    [U64] = {.kind = TY_INT, .int_kind = U64},     [USIZE] = {.kind = TY_INT, .int_kind = USIZE},
};

static const char *const int_names[INT_KIND_COUNT] = {
    [I8] = "i8", [I16] = "i16", [I32] = "i32", [I64] = "i64", [ISIZE] = "isize",
    [U8] = "u8", [U16] = "u16", [U32] = "u32", [U64] = "u64", [USIZE] = "usize",
};

static Vec composites; // Type *

Vec *composite_types(void) {
    return &composites;
}

static Type *new_composite(TypeKind kind, const char *c_prefix) {
    Type *t = NEW(Type);
    t->kind = kind;
    t->c_name = arena_printf("a0_%s_%zu", c_prefix, composites.len);
    vec_push(&composites, t);
    return t;
}

Type *type_void(void) {
    return &void_type;
}

Type *type_noreturn(void) {
    return &noreturn_type;
}

Type *type_bool(void) {
    return &bool_type;
}

Type *type_str(void) {
    return &str_type;
}

Type *type_int(IntKind k) {
    return &int_types[k];
}

Type *type_ref(bool mut, Type *elem) {
    Type **slot = &elem->ref_cache[mut];
    if (!*slot) {
        Type *t = NEW(Type);
        t->kind = TY_REF;
        t->mut = mut;
        t->elem = elem;
        *slot = t;
    }
    return *slot;
}

// []T and []mut T share one C type: they differ only in what the checker allows.
Type *type_slice(bool mut, Type *elem) {
    Type **slot = &elem->slice_cache[mut];
    if (!*slot) {
        Type *other = elem->slice_cache[!mut];
        Type *t;
        if (other) {
            t = NEW(Type);
            t->kind = TY_SLICE;
            t->c_name = other->c_name;
        } else {
            t = new_composite(TY_SLICE, "slice");
        }
        t->mut = mut;
        t->elem = elem;
        *slot = t;
    }
    return *slot;
}

Type *type_array(Type *elem, uint64_t len) {
    for (size_t i = 0; i < elem->array_cache.len; i++) {
        Type *t = elem->array_cache.items[i];
        if (t->len == len) {
            return t;
        }
    }
    Type *t = new_composite(TY_ARRAY, "array");
    t->elem = elem;
    t->len = len;
    vec_push(&elem->array_cache, t);
    return t;
}

Type *type_list(Type *elem) {
    if (!elem->list_cache) {
        Type *t = new_composite(TY_LIST, "list");
        if (elem == &int_types[U8]) {
            t->c_name = "a0_bytes"; // the runtime's, for std.Fs.read_bytes
        }
        t->elem = elem;
        elem->list_cache = t;
        type_slice(false, elem);
        type_slice(true, elem);
    }
    return elem->list_cache;
}

Type *type_map(Type *key, Type *value) {
    for (size_t i = 0; i < value->map_cache.len; i++) {
        Type *t = value->map_cache.items[i];
        if (t->key == key) {
            return t;
        }
    }
    Type *t = new_composite(TY_MAP, "map");
    t->key = key;
    t->elem = value;
    vec_push(&value->map_cache, t);
    type_optional(value); // get and remove return ?V
    return t;
}

Type *type_optional(Type *elem) {
    if (!elem->optional_cache) {
        Type *t = new_composite(TY_OPTIONAL, "opt");
        t->elem = elem;
        elem->optional_cache = t;
    }
    return elem->optional_cache;
}

static BuiltinType string_bt;

// E!T has the same C type for every E: the error number and the value.
Type *type_error_union_of(TypeDecl *set, Type *elem) {
    for (size_t i = 0; i < elem->union_cache.len; i++) {
        Type *t = elem->union_cache.items[i];
        if (t->decl == set) {
            return t;
        }
    }
    Type *t;
    if (elem->union_cache.len > 0) {
        t = NEW(Type);
        t->kind = TY_ERROR_UNION;
        t->c_name = ((Type *)elem->union_cache.items[0])->c_name;
    } else if (elem->kind == TY_VOID) {
        t = NEW(Type);
        t->kind = TY_ERROR_UNION;
        t->c_name = "a0_err";
    } else if (elem->kind == TY_BUILTIN && elem->builtin == &string_bt) {
        t = NEW(Type);
        t->kind = TY_ERROR_UNION;
        t->c_name = "a0_res_String"; // the runtime's, for std.Fs.read_file
    } else if (elem->kind == TY_LIST && elem->elem == &int_types[U8]) {
        t = NEW(Type);
        t->kind = TY_ERROR_UNION;
        t->c_name = "a0_res_bytes"; // the runtime's, for std.Fs.read_bytes
    } else {
        t = new_composite(TY_ERROR_UNION, "res");
    }
    t->elem = elem;
    t->decl = set;
    vec_push(&elem->union_cache, t);
    return t;
}

Type *type_error_union(Type *elem) {
    return type_error_union_of(NULL, elem);
}

static Type any_error_type = {.kind = TY_ERROR_SET};

Type *type_any_error(void) {
    return &any_error_type;
}

Type *type_declared(TypeKind kind, TypeDecl *decl, const char *c_name) {
    Type *t = NEW(Type);
    t->kind = kind;
    t->decl = decl;
    t->c_name = c_name;
    if (kind == TY_STRUCT || kind == TY_ENUM) {
        vec_push(&composites, t);
    }
    return t;
}

static const char *decl_name(const TypeDecl *d) {
    return d->module->name[0] ? arena_printf("%s.%s", d->module->name, d->name) : d->name;
}

const char *type_name(const Type *t) {
    switch (t->kind) {
    case TY_VOID:
        return "void";
    case TY_NORETURN:
        return "noreturn";
    case TY_BOOL:
        return "bool";
    case TY_INT:
        return int_names[t->int_kind];
    case TY_STR:
        return "str";
    case TY_REF:
        return arena_printf("&%s%s", t->mut ? "mut " : "", type_name(t->elem));
    case TY_SLICE:
        return arena_printf("[]%s%s", t->mut ? "mut " : "", type_name(t->elem));
    case TY_ARRAY:
        return arena_printf("[%llu]%s", (unsigned long long)t->len, type_name(t->elem));
    case TY_LIST:
        return arena_printf("alloc.List(%s)", type_name(t->elem));
    case TY_MAP:
        return arena_printf("alloc.Map(%s, %s)", type_name(t->key), type_name(t->elem));
    case TY_OPTIONAL:
        return arena_printf("?%s", type_name(t->elem));
    case TY_ERROR_UNION:
        return t->decl ? arena_printf("%s!%s", decl_name(t->decl), type_name(t->elem))
                       : arena_printf("!%s", type_name(t->elem));
    case TY_ERROR_SET:
        if (!t->decl) {
            return "error";
        }
        return decl_name(t->decl);
    case TY_STRUCT:
    case TY_ENUM:
        return decl_name(t->decl);
    case TY_BUILTIN:
        return arena_printf("%s.%s", t->builtin->module, t->builtin->name);
    }
    return "?";
}

static BuiltinType symbol_bt;

bool type_comparable(const Type *t) {
    switch (t->kind) {
    case TY_INT:
    case TY_BOOL:
    case TY_ERROR_SET:
        return true;
    case TY_BUILTIN:
        return t->builtin == &symbol_bt;
    case TY_ENUM:
        for (size_t i = 0; i < t->decl->variants.len; i++) {
            if (VEC_AT(t->decl->variants, VariantDecl, i)->has_fields) {
                return false;
            }
        }
        return true;
    case TY_STRUCT:
        for (int i = 0; i < t->nfields; i++) {
            if (!type_comparable(t->fields[i].type)) {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

bool type_is_key(const Type *t) {
    if (t->kind == TY_ERROR_SET) {
        return false;
    }
    if (t->kind == TY_STRUCT) {
        for (int i = 0; i < t->nfields; i++) {
            if (!type_is_key(t->fields[i].type)) {
                return false;
            }
        }
        return true;
    }
    return type_comparable(t);
}

// ---- the built-in library ---------------------------------------------------

static BuiltinType output_bt;
static BuiltinType args_bt;
static BuiltinType interner_bt;
static BuiltinType fs_bt;
static BuiltinType allocator_bt;
static BuiltinType system_bt;

static Type output_type = {.kind = TY_BUILTIN, .builtin = &output_bt};
static Type args_type = {.kind = TY_BUILTIN, .builtin = &args_bt};
static Type fs_type = {.kind = TY_BUILTIN, .builtin = &fs_bt};
static Type allocator_type = {.kind = TY_BUILTIN, .builtin = &allocator_bt};
static Type system_type = {.kind = TY_BUILTIN, .builtin = &system_bt};
static Type string_type = {.kind = TY_BUILTIN, .builtin = &string_bt};
static Type interner_type = {.kind = TY_BUILTIN, .builtin = &interner_bt};
static Type symbol_type = {.kind = TY_BUILTIN, .builtin = &symbol_bt};

static BuiltinType output_bt = {
    .module = "std",
    .name = "Output",
    .c_name = "a0_std_Output",
    .type = &output_type,
    .nmethods = 4,
    .methods =
        {
            {"write", SELF_MUT_REF, 1, {{"text", &str_type}}, &void_type, "a0_std_Output_write", false, M_RUNTIME},
            {"write_byte", SELF_MUT_REF, 1, {{"byte", &int_types[U8]}}, &void_type, "a0_std_Output_write_byte", false,
             M_RUNTIME},
            {"write_u64", SELF_MUT_REF, 1, {{"n", &int_types[U64]}}, &void_type, "a0_std_Output_write_u64", false,
             M_RUNTIME},
            {"write_i64", SELF_MUT_REF, 1, {{"n", &int_types[I64]}}, &void_type, "a0_std_Output_write_i64", false,
             M_RUNTIME},
        },
};

static BuiltinType args_bt = {
    .module = "std",
    .name = "Args",
    .c_name = "a0_std_Args",
    .type = &args_type,
    .nmethods = 2,
    .methods =
        {
            {"len", SELF_REF, 0, {{0}}, &int_types[USIZE], "a0_std_Args_len", false, M_RUNTIME},
            {"get", SELF_REF, 1, {{"i", &int_types[USIZE]}}, &str_type, "a0_std_Args_get", true, M_RUNTIME},
        },
};

// Its methods return std.IoError unions, which types_init makes.
static BuiltinType fs_bt = {.module = "std", .name = "Fs", .c_name = "a0_std_Fs", .type = &fs_type};

static BuiltinType string_bt = {
    .module = "alloc",
    .name = "String",
    .c_name = "a0_alloc_String",
    .type = &string_type,
    .has_init = true,
    .nmethods = 4,
    .methods =
        {
            {"push_str", SELF_MUT_REF, 1, {{"text", &str_type}}, &void_type, "a0_alloc_String_push_str", false, M_RUNTIME},
            {"push_ascii", SELF_MUT_REF, 1, {{"byte", &int_types[U8]}}, &void_type, "a0_alloc_String_push_ascii", true,
             M_RUNTIME},
            {"view", SELF_REF, 0, {{0}}, &str_type, "a0_alloc_String_view", false, M_RUNTIME},
            {"len", SELF_REF, 0, {{0}}, &int_types[USIZE], "a0_alloc_String_len", false, M_RUNTIME},
        },
};

static BuiltinType symbol_bt = {.module = "alloc", .name = "Symbol", .c_name = "a0_alloc_Symbol", .type = &symbol_type};

static BuiltinType interner_bt = {
    .module = "alloc",
    .name = "Interner",
    .c_name = "a0_alloc_Interner",
    .type = &interner_type,
    .has_init = true,
    .nmethods = 2,
    .methods =
        {
            {"intern", SELF_MUT_REF, 1, {{"text", &str_type}}, &symbol_type, "a0_alloc_Interner_intern", false,
             M_RUNTIME},
            {"text", SELF_REF, 1, {{"sym", &symbol_type}}, &str_type, "a0_alloc_Interner_text", true, M_RUNTIME},
        },
};

static BuiltinType allocator_bt = {
    .module = "alloc", .name = "Allocator", .c_name = "a0_alloc_Allocator", .type = &allocator_type};

static BuiltinType system_bt = {
    .module = "std",
    .name = "System",
    .c_name = "a0_std_System",
    .type = &system_type,
    .nfields = 5,
    .fields =
        {
            {"fs", &fs_type, "fs", true},
            {"stdout", &output_type, "out", true},
            {"stderr", &output_type, "err", true},
            {"args", &args_type, "args", true},
            {"alloc", &allocator_type, "alloc", true},
        },
};

static const BuiltinType *const builtin_types[] = {
    &output_bt, &args_bt, &fs_bt, &allocator_bt, &system_bt, &string_bt, &symbol_bt, &interner_bt,
};

// std.IoError (§11). Its errors are numbered 1 to 5 before any other, so the
// runtime can return them: see A0_ERR_* in arche0_rt.h.
static Module std_module = {.name = "std"};
static TypeDecl io_error_decl = {.kind = TD_ERROR, .name = "IoError", .pub = true, .module = &std_module};
static const char *const io_error_names[] = {"NotFound", "AccessDenied", "IsDirectory", "InvalidUtf8", "IoFailed"};

void types_init(void) {
    for (size_t i = 0; i < sizeof io_error_names / sizeof *io_error_names; i++) {
        ErrorName *e = NEW(ErrorName);
        e->name = io_error_names[i];
        e->id = (int)i + 1;
        vec_push(&io_error_decl.errors, e);
    }
    io_error_decl.type = type_declared(TY_ERROR_SET, &io_error_decl, "a0_err");
    Method *m = fs_bt.methods;
    m[0] = (Method){"read_file", SELF_REF, 2, {{"path", &str_type}, {"a", &allocator_type}},
                    type_error_union_of(&io_error_decl, &string_type), "a0_std_Fs_read_file", false, M_RUNTIME};
    m[1] = (Method){"write_file", SELF_REF, 2, {{"path", &str_type}, {"data", &str_type}},
                    type_error_union_of(&io_error_decl, &void_type), "a0_std_Fs_write_file", false, M_RUNTIME};
    m[2] = (Method){"read_bytes", SELF_REF, 2, {{"path", &str_type}, {"a", &allocator_type}},
                    type_error_union_of(&io_error_decl, type_list(&int_types[U8])), "a0_std_Fs_read_bytes", false,
                    M_RUNTIME};
    m[3] = (Method){"write_bytes", SELF_REF, 2, {{"path", &str_type}, {"data", type_slice(false, &int_types[U8])}},
                    type_error_union_of(&io_error_decl, &void_type), "a0_std_Fs_write_bytes", false, M_RUNTIME};
    fs_bt.nmethods = 4;
}

Type *library_type(const char *library, const char *name) {
    if (strcmp(library, "std") == 0 && strcmp(name, "IoError") == 0) {
        return io_error_decl.type;
    }
    const BuiltinType *b = builtin_type_lookup(library, name);
    return b ? b->type : NULL;
}

const BuiltinType *builtin_type_lookup(const char *module, const char *name) {
    for (size_t i = 0; i < sizeof builtin_types / sizeof *builtin_types; i++) {
        const BuiltinType *b = builtin_types[i];
        if (strcmp(b->module, module) == 0 && strcmp(b->name, name) == 0) {
            return b;
        }
    }
    return NULL;
}

static Method method(const char *name, SelfMode self, Type *ret, MethodOp op) {
    Method m = {0};
    m.name = name;
    m.self = self;
    m.ret = ret;
    m.op = op;
    return m;
}

// The methods of str, slices, arrays and lists depend on the element type, so
// they are made the first time they are looked up.
static void build_methods(Type *t) {
    Method *ms = arena_alloc(8 * sizeof *ms);
    int n = 0;
    switch (t->kind) {
    case TY_STR:
        ms[n++] = method("len", SELF_VALUE, &int_types[USIZE], M_LEN);
        ms[n] = method("eq", SELF_VALUE, &bool_type, M_RUNTIME);
        ms[n].nparams = 1;
        ms[n].params[0] = (BuiltinParam){"other", &str_type};
        ms[n++].c_name = "a0_str_eq";
        ms[n] = method("starts_with", SELF_VALUE, &bool_type, M_RUNTIME);
        ms[n].nparams = 1;
        ms[n].params[0] = (BuiltinParam){"prefix", &str_type};
        ms[n++].c_name = "a0_str_starts_with";
        ms[n++] = method("bytes", SELF_VALUE, type_slice(false, &int_types[U8]), M_STR_BYTES);
        break;
    case TY_SLICE:
    case TY_ARRAY:
        ms[n++] = method("len", SELF_REF, &int_types[USIZE], M_LEN);
        break;
    case TY_LIST:
        ms[n] = method("push", SELF_MUT_REF, &void_type, M_LIST_PUSH);
        ms[n].nparams = 1;
        ms[n++].params[0] = (BuiltinParam){"item", t->elem};
        ms[n++] = method("pop", SELF_MUT_REF, type_optional(t->elem), M_LIST_POP);
        ms[n++] = method("len", SELF_REF, &int_types[USIZE], M_LEN);
        ms[n++] = method("clear", SELF_MUT_REF, &void_type, M_LIST_CLEAR);
        ms[n++] = method("items", SELF_REF, type_slice(false, t->elem), M_LIST_ITEMS);
        ms[n++] = method("items_mut", SELF_MUT_REF, type_slice(true, t->elem), M_LIST_ITEMS_MUT);
        break;
    case TY_MAP:
        ms[n] = method("put", SELF_MUT_REF, &void_type, M_MAP_PUT);
        ms[n].nparams = 2;
        ms[n].params[0] = (BuiltinParam){"key", t->key};
        ms[n++].params[1] = (BuiltinParam){"value", t->elem};
        ms[n] = method("get", SELF_REF, type_optional(t->elem), M_MAP_GET);
        ms[n].nparams = 1;
        ms[n++].params[0] = (BuiltinParam){"key", t->key};
        ms[n] = method("remove", SELF_MUT_REF, type_optional(t->elem), M_MAP_REMOVE);
        ms[n].nparams = 1;
        ms[n++].params[0] = (BuiltinParam){"key", t->key};
        ms[n++] = method("len", SELF_REF, &int_types[USIZE], M_LEN);
        break;
    default:
        break;
    }
    t->methods = ms;
    t->nmethods = n;
}

const Method *method_lookup(Type *t, const char *name) {
    const Method *ms;
    int n;
    if (t->kind == TY_BUILTIN) {
        ms = t->builtin->methods;
        n = t->builtin->nmethods;
    } else {
        if (!t->methods) {
            build_methods(t);
        }
        ms = t->methods;
        n = t->nmethods;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(ms[i].name, name) == 0) {
            return &ms[i];
        }
    }
    return NULL;
}

const Field *field_lookup(const Type *t, const char *name) {
    const Field *fs = NULL;
    int n = 0;
    if (t->kind == TY_BUILTIN) {
        fs = t->builtin->fields;
        n = t->builtin->nfields;
    } else if (t->kind == TY_STRUCT) {
        fs = t->fields;
        n = t->nfields;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(fs[i].name, name) == 0) {
            return &fs[i];
        }
    }
    return NULL;
}

bool builtin_fn_lookup(const char *name, BuiltinFn *out) {
    static const struct {
        const char *name;
        BuiltinFn fn;
    } fns[] = {
        {"panic", BUILTIN_PANIC},
        {"assert", BUILTIN_ASSERT},
        {"unreachable", BUILTIN_UNREACHABLE},
    };
    for (size_t i = 0; i < sizeof fns / sizeof *fns; i++) {
        if (strcmp(fns[i].name, name) == 0) {
            *out = fns[i].fn;
            return true;
        }
    }
    return false;
}

bool int_type_lookup(const char *name, IntKind *out) {
    for (int k = 0; k < INT_KIND_COUNT; k++) {
        if (strcmp(int_names[k], name) == 0) {
            *out = (IntKind)k;
            return true;
        }
    }
    return false;
}

bool int_signed(IntKind k) {
    return k <= ISIZE;
}

int int_bits(IntKind k) {
    switch (k) {
    case I8:
    case U8:
        return 8;
    case I16:
    case U16:
        return 16;
    case I32:
    case U32:
        return 32;
    default:
        return 64;
    }
}

int64_t int_min(IntKind k) {
    if (!int_signed(k)) {
        return 0;
    }
    int bits = int_bits(k);
    return bits == 64 ? INT64_MIN : -((int64_t)1 << (bits - 1));
}

uint64_t int_max(IntKind k) {
    int bits = int_bits(k);
    if (int_signed(k)) {
        return ((uint64_t)1 << (bits - 1)) - 1;
    }
    return bits == 64 ? UINT64_MAX : ((uint64_t)1 << bits) - 1;
}

// The suffix of the runtime's helpers for this type: isize and usize share
// i64's and u64's.
const char *int_suffix(IntKind k) {
    static const char *const names[INT_KIND_COUNT] = {
        [I8] = "i8", [I16] = "i16", [I32] = "i32", [I64] = "i64", [ISIZE] = "i64",
        [U8] = "u8", [U16] = "u16", [U32] = "u32", [U64] = "u64", [USIZE] = "u64",
    };
    return names[k];
}
