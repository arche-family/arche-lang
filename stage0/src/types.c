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

Type *type_error_union(Type *elem) {
    if (!elem->error_union_cache) {
        Type *t = NEW(Type);
        t->kind = TY_ERROR_UNION;
        t->elem = elem;
        elem->error_union_cache = t;
    }
    return elem->error_union_cache;
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
    case TY_ERROR_UNION:
        return arena_printf("!%s", type_name(t->elem));
    case TY_BUILTIN:
        return arena_printf("%s.%s", t->builtin->module, t->builtin->name);
    }
    return "?";
}

// ---- the built-in library ---------------------------------------------------

static BuiltinType output_bt;
static BuiltinType args_bt;
static BuiltinType fs_bt;
static BuiltinType allocator_bt;
static BuiltinType system_bt;

static Type output_type = {.kind = TY_BUILTIN, .builtin = &output_bt};
static Type args_type = {.kind = TY_BUILTIN, .builtin = &args_bt};
static Type fs_type = {.kind = TY_BUILTIN, .builtin = &fs_bt};
static Type allocator_type = {.kind = TY_BUILTIN, .builtin = &allocator_bt};
static Type system_type = {.kind = TY_BUILTIN, .builtin = &system_bt};

static BuiltinType output_bt = {
    .module = "std",
    .name = "Output",
    .c_name = "a0_std_Output",
    .type = &output_type,
    .nmethods = 4,
    .methods =
        {
            {"write", SELF_MUT_REF, 1, {{"text", &str_type}}, &void_type, "a0_std_Output_write", false},
            {"write_byte", SELF_MUT_REF, 1, {{"byte", &int_types[U8]}}, &void_type, "a0_std_Output_write_byte", false},
            {"write_u64", SELF_MUT_REF, 1, {{"n", &int_types[U64]}}, &void_type, "a0_std_Output_write_u64", false},
            {"write_i64", SELF_MUT_REF, 1, {{"n", &int_types[I64]}}, &void_type, "a0_std_Output_write_i64", false},
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
            {"len", SELF_REF, 0, {{0}}, &int_types[USIZE], "a0_std_Args_len", false},
            {"get", SELF_REF, 1, {{"i", &int_types[USIZE]}}, &str_type, "a0_std_Args_get", true},
        },
};

static BuiltinType fs_bt = {.module = "std", .name = "Fs", .c_name = "a0_std_Fs", .type = &fs_type};

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
            {"fs", &fs_type, "fs"},
            {"stdout", &output_type, "out"},
            {"stderr", &output_type, "err"},
            {"args", &args_type, "args"},
            {"alloc", &allocator_type, "alloc"},
        },
};

static const BuiltinType *const builtin_types[] = {&output_bt, &args_bt, &fs_bt, &allocator_bt, &system_bt};

const BuiltinType *builtin_type_lookup(const char *module, const char *name) {
    for (size_t i = 0; i < sizeof builtin_types / sizeof *builtin_types; i++) {
        const BuiltinType *b = builtin_types[i];
        if (strcmp(b->module, module) == 0 && strcmp(b->name, name) == 0) {
            return b;
        }
    }
    return NULL;
}

const Method *method_lookup(const Type *t, const char *name) {
    if (t->kind != TY_BUILTIN) {
        return NULL;
    }
    for (int i = 0; i < t->builtin->nmethods; i++) {
        if (strcmp(t->builtin->methods[i].name, name) == 0) {
            return &t->builtin->methods[i];
        }
    }
    return NULL;
}

const Field *field_lookup(const Type *t, const char *name) {
    if (t->kind != TY_BUILTIN) {
        return NULL;
    }
    for (int i = 0; i < t->builtin->nfields; i++) {
        if (strcmp(t->builtin->fields[i].name, name) == 0) {
            return &t->builtin->fields[i];
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
