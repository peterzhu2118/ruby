#include "id_table.h"

static void
id_table_bench_free(void *ptr)
{
    rb_id_table_free((struct rb_id_table *)ptr);
}

static size_t
id_table_bench_memsize(const void *ptr)
{
    return ptr ? rb_id_table_memsize((const struct rb_id_table *)ptr) : 0;
}

static const rb_data_type_t id_table_bench_type = {
    .wrap_struct_name = "IDTable",
    .function = {
        .dmark = NULL, /* only immediate values are stored */
        .dfree = id_table_bench_free,
        .dsize = id_table_bench_memsize,
    },
    .flags = RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED,
};

static VALUE
id_table_bench_alloc(VALUE klass)
{
    return TypedData_Wrap_Struct(klass, &id_table_bench_type, rb_id_table_create(0));
}

/* Process-wide pool of interned IDs used as keys. */
static VALUE bench_id_pool; /* Ruby array pinning the symbols */
static ID *bench_ids;       /* C array for fast access */
static size_t bench_ids_size;

static ID
id_table_bench_id(size_t i)
{
    if (i >= bench_ids_size) {
        size_t new_size = bench_ids_size ? bench_ids_size : 1024;
        while (new_size <= i) new_size *= 2;
        REALLOC_N(bench_ids, ID, new_size);
        for (size_t j = bench_ids_size; j < new_size; j++) {
            VALUE sym = rb_sprintf("id_table_bench_%lu", (unsigned long)j);
            rb_ary_push(bench_id_pool, sym);
            bench_ids[j] = rb_intern_str(sym);
        }
        bench_ids_size = new_size;
    }
    return bench_ids[i];
}

static struct rb_id_table *
id_table_bench_get(VALUE self)
{
    struct rb_id_table *tbl;
    TypedData_Get_Struct(self, struct rb_id_table, &id_table_bench_type, tbl);
    return tbl;
}

static size_t
id_table_bench_count(VALUE vcount)
{
    long count = NUM2LONG(vcount);
    if (count < 0) rb_raise(rb_eArgError, "count must not be negative");
    return (size_t)count;
}

/*
 * call-seq: IDTable#add(count) -> Integer
 *
 * Inserts +count+ new entries into the table and returns the new number
 * of entries.
 */
static VALUE
id_table_bench_add(VALUE self, VALUE vcount)
{
    struct rb_id_table *tbl = id_table_bench_get(self);
    size_t count = id_table_bench_count(vcount);
    size_t size = rb_id_table_size(tbl);

    for (size_t i = 0; i < count; i++) {
        size_t idx = size + i;
        rb_id_table_insert(tbl, id_table_bench_id(idx), INT2FIX((long)idx));
    }
    return SIZET2NUM(size + count);
}

/*
 * call-seq: IDTable#lookup(count) -> Integer
 *
 * Performs +count+ lookups of existing keys (round robin) and returns the
 * number of successful lookups.
 */
static VALUE
id_table_bench_lookup(VALUE self, VALUE vcount)
{
    struct rb_id_table *tbl = id_table_bench_get(self);
    size_t count = id_table_bench_count(vcount);
    size_t size = rb_id_table_size(tbl);

    if (size == 0 && count > 0) {
        rb_raise(rb_eRuntimeError, "table is empty");
    }

    size_t hits = 0;
    for (size_t i = 0; i < count; i++) {
        size_t idx = i % size;
        VALUE val;
        if (rb_id_table_lookup(tbl, id_table_bench_id(idx), &val)) {
            if (val != INT2FIX((long)idx)) {
                rb_raise(rb_eRuntimeError, "unexpected value for key %lu", (unsigned long)idx);
            }
            hits++;
        }
    }
    return SIZET2NUM(hits);
}

void
Init_id_table_bench(void)
{
    bench_id_pool = rb_ary_new();
    rb_global_variable(&bench_id_pool);

    VALUE klass = rb_define_class("IDTable", rb_cObject);
    rb_define_alloc_func(klass, id_table_bench_alloc);
    rb_define_method(klass, "add", id_table_bench_add, 1);
    rb_define_method(klass, "lookup", id_table_bench_lookup, 1);
}
