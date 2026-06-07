#ifndef GC_GC_IMPL_H
#define GC_GC_IMPL_H
/**
 * @author     Ruby developers <ruby-core@ruby-lang.org>
 * @copyright  This  file  is   a  part  of  the   programming  language  Ruby.
 *             Permission  is hereby  granted,  to  either redistribute  and/or
 *             modify this file, provided that  the conditions mentioned in the
 *             file COPYING are met.  Consult the file for details.
 * @brief      Header for GC implementations introduced in [Feature #20470].
 */
#include "ruby/ruby.h"

#include <stddef.h>
#include <stdint.h>

/* GC-agnostic bump-pointer allocation interface shared with the JIT.
 *
 * These small, GC-implementation-independent data structures let a JIT compiler
 * inline the object-allocation fast path without being coupled to any particular
 * GC's internals.
 *
 * A GC that supports inline bump allocation publishes, per ractor and per size
 * class, a `struct gc_bump_pointer_heap`: a thread-local cursor that the JIT (or
 * the interpreter) advances by `slot_size` bytes per allocation until it reaches
 * `cursor_end`, at which point it must call back into the GC's "miss" function.
 *
 * GCs that cannot expose a compatible bump pointer (or builds that must not
 * inline, such as ASan / debug builds) simply publish an array consisting of a
 * single sentinel entry (`slot_size == 0`); the JIT then detects that no heap is
 * available and falls back to a normal call into the GC.
 */

/* A single thread-local bump-pointer region for one size class.
 *
 * Allocation is: if (cursor + slot_size <= cursor_end) { obj = cursor; cursor += slot_size; }
 * otherwise the GC's miss function must be called to refill the cursor.
 *
 * The JIT only ever reads/writes `cursor`, reads `jit_cursor_end`, and bakes
 * `slot_size` as a compile-time constant. The interpreter allocates against
 * `cursor_end`; `jit_cursor_end` is normally identical to it, but is held at
 * `cursor` (an empty window) while RUBY_INTERNAL_EVENT_NEWOBJ allocation tracing
 * is enabled, so the JIT's inlined fast path -- which cannot fire that hook --
 * always misses into the GC's slow path while the interpreter window (and hence
 * its GC scheduling) is left undisturbed. `region_start` is reserved for the
 * GC's own bookkeeping (e.g. deriving allocation counts from cursor movement)
 * and is ignored by the JIT.
 */
struct gc_bump_pointer_heap {
    uintptr_t cursor;          /* next free slot */
    uintptr_t cursor_end;      /* one past the end of the interpreter's usable window */
    uintptr_t jit_cursor_end;  /* the JIT's view of cursor_end (see above) */
    uintptr_t region_start;    /* GC-private: cursor value at last allocation-count flush */
    size_t    slot_size;       /* bytes per slot; 0 marks the sentinel / "no bump heap here" */
};

/* Per-ractor allocation cache.
 *
 * `gc_private` is owned by the GC implementation (e.g. the default GC's per-size
 * private region/page bookkeeping). `bump_heaps` is a flexible array of
 * `gc_bump_pointer_heap` in ascending `slot_size` order, terminated by a
 * sentinel entry whose `slot_size` is 0.
 */
struct rb_ractor_gc_cache {
    void *gc_private;
    struct gc_bump_pointer_heap bump_heaps[];
};

#ifndef RB_GC_OBJECT_METADATA_ENTRY_DEFINED
# define RB_GC_OBJECT_METADATA_ENTRY_DEFINED
struct rb_gc_object_metadata_entry {
    ID name;
    VALUE val;
};
#endif

#ifdef BUILDING_MODULAR_GC
# define GC_IMPL_FN
#else
// `GC_IMPL_FN` is an implementation detail of `!USE_MODULAR_GC` builds
// to have the default GC in the same translation unit as gc.c for
// the sake of optimizer visibility. It expands to nothing unless
// you're the default GC.
//
// For the default GC, do not copy-paste this when implementing
// these functions. This takes advantage of internal linkage winning
// when appearing first. See C99 6.2.2p4.
# define GC_IMPL_FN static
#endif

// Bootup
GC_IMPL_FN void *rb_gc_impl_objspace_alloc(void);
GC_IMPL_FN void rb_gc_impl_objspace_init(void *objspace_ptr);
GC_IMPL_FN struct rb_ractor_gc_cache *rb_gc_impl_ractor_gc_cache_init(void *objspace_ptr, void *ractor);
GC_IMPL_FN void rb_gc_impl_set_params(void *objspace_ptr);
GC_IMPL_FN void rb_gc_impl_init(void);
GC_IMPL_FN size_t *rb_gc_impl_heap_sizes(void *objspace_ptr);
// Shutdown
GC_IMPL_FN void rb_gc_impl_shutdown_free_objects(void *objspace_ptr);
GC_IMPL_FN void rb_gc_impl_objspace_free(void *objspace_ptr);
GC_IMPL_FN void rb_gc_impl_ractor_gc_cache_free(void *objspace_ptr, struct rb_ractor_gc_cache *gc_cache);
// GC
GC_IMPL_FN void rb_gc_impl_start(void *objspace_ptr, bool full_mark, bool immediate_mark, bool immediate_sweep, bool compact);
GC_IMPL_FN bool rb_gc_impl_during_gc_p(void *objspace_ptr);
GC_IMPL_FN void rb_gc_impl_prepare_heap(void *objspace_ptr);
GC_IMPL_FN void rb_gc_impl_gc_enable(void *objspace_ptr);
GC_IMPL_FN void rb_gc_impl_gc_disable(void *objspace_ptr, bool finish_current_gc);
GC_IMPL_FN bool rb_gc_impl_gc_enabled_p(void *objspace_ptr);
GC_IMPL_FN void rb_gc_impl_stress_set(void *objspace_ptr, VALUE flag);
GC_IMPL_FN VALUE rb_gc_impl_stress_get(void *objspace_ptr);
GC_IMPL_FN VALUE rb_gc_impl_config_get(void *objspace_ptr);
GC_IMPL_FN void rb_gc_impl_config_set(void *objspace_ptr, VALUE hash);
GC_IMPL_FN struct rb_gc_vm_context *rb_gc_impl_get_vm_context(void *objspace_ptr);
// Object allocation
GC_IMPL_FN VALUE rb_gc_impl_new_obj(void *objspace_ptr, void *cache_ptr, VALUE klass, VALUE flags, bool wb_protected, size_t alloc_size);
/* Slow path for the inline bump-pointer fast path: the bump heap for `heap_idx`
 * is exhausted. Refill that heap's cursor (possibly moving to the next region or
 * page, running an incremental mark step, or triggering a GC) and return a raw,
 * uninitialized T_NONE cell of the appropriate size. The caller is responsible
 * for writing the object header. */
GC_IMPL_FN VALUE rb_gc_impl_new_obj_bump_pointer_miss(void *objspace_ptr, struct rb_ractor_gc_cache *gc_cache, size_t heap_idx);
/* The bump-heap slot-size strides (bytes per slot), as a GC-global, 0-terminated
 * array in ascending order, indexed identically to a ractor's `bump_heaps`.
 * Returns NULL when the GC does not support the JIT's inline bump-pointer fast
 * path at all (no bump heaps, or a check/sanitizer build whose interpreter fast
 * path does extra per-object work the JIT cannot replicate).
 *
 * These strides are an invariant of the GC, not of any one ractor, so the JIT may
 * read them at compile time to bake the size-class index and stride even though
 * the compiling ractor and the running ractor may differ. */
GC_IMPL_FN const size_t *rb_gc_impl_zjit_bump_slot_sizes(void *objspace_ptr);
GC_IMPL_FN size_t rb_gc_impl_obj_slot_size(VALUE obj);
GC_IMPL_FN size_t rb_gc_impl_heap_id_for_size(void *objspace_ptr, size_t size);
GC_IMPL_FN bool rb_gc_impl_size_allocatable_p(size_t size);
// Malloc
/*
 * BEWARE: These functions may or may not run under GVL.
 *
 * You might want to make them thread-safe.
 * Garbage collecting inside is possible if and only if you
 * already have GVL.  Also raising exceptions without one is a
 * total disaster.
 *
 * When you absolutely cannot allocate the requested amount of
 * memory just return NULL (with appropriate errno set).
 * The caller side takes care of that situation.
 */
GC_IMPL_FN void *rb_gc_impl_malloc(void *objspace_ptr, size_t size, bool gc_allowed);
GC_IMPL_FN void *rb_gc_impl_calloc(void *objspace_ptr, size_t size, bool gc_allowed);
GC_IMPL_FN void *rb_gc_impl_realloc(void *objspace_ptr, void *ptr, size_t new_size, size_t old_size, bool gc_allowed);
GC_IMPL_FN void rb_gc_impl_free(void *objspace_ptr, void *ptr, size_t old_size);
GC_IMPL_FN void rb_gc_impl_adjust_memory_usage(void *objspace_ptr, ssize_t diff);
// Marking
GC_IMPL_FN void rb_gc_impl_mark(void *objspace_ptr, VALUE obj);
GC_IMPL_FN void rb_gc_impl_mark_and_move(void *objspace_ptr, VALUE *ptr);
GC_IMPL_FN void rb_gc_impl_mark_and_pin(void *objspace_ptr, VALUE obj);
GC_IMPL_FN void rb_gc_impl_mark_maybe(void *objspace_ptr, VALUE obj);
// Weak references
GC_IMPL_FN void rb_gc_impl_declare_weak_references(void *objspace_ptr, VALUE obj);
GC_IMPL_FN bool rb_gc_impl_handle_weak_references_alive_p(void *objspace_ptr, VALUE obj);
// Compaction
GC_IMPL_FN void rb_gc_impl_register_pinning_obj(void *objspace_ptr, VALUE obj);
GC_IMPL_FN bool rb_gc_impl_object_moved_p(void *objspace_ptr, VALUE obj);
GC_IMPL_FN VALUE rb_gc_impl_location(void *objspace_ptr, VALUE value);
// Write barriers
GC_IMPL_FN void rb_gc_impl_writebarrier(void *objspace_ptr, VALUE a, VALUE b);
GC_IMPL_FN void rb_gc_impl_writebarrier_unprotect(void *objspace_ptr, VALUE obj);
GC_IMPL_FN void rb_gc_impl_writebarrier_remember(void *objspace_ptr, VALUE obj);
// Heap walking
GC_IMPL_FN void rb_gc_impl_each_objects(void *objspace_ptr, int (*callback)(void *, void *, size_t, void *), void *data);
GC_IMPL_FN void rb_gc_impl_each_object(void *objspace_ptr, void (*func)(VALUE obj, void *data), void *data);
// Finalizers
GC_IMPL_FN void rb_gc_impl_make_zombie(void *objspace_ptr, VALUE obj, void (*dfree)(void *), void *data);
GC_IMPL_FN VALUE rb_gc_impl_define_finalizer(void *objspace_ptr, VALUE obj, VALUE block);
GC_IMPL_FN void rb_gc_impl_undefine_finalizer(void *objspace_ptr, VALUE obj);
GC_IMPL_FN void rb_gc_impl_copy_finalizer(void *objspace_ptr, VALUE dest, VALUE obj);
GC_IMPL_FN void rb_gc_impl_shutdown_call_finalizer(void *objspace_ptr);
// Forking
GC_IMPL_FN void rb_gc_impl_before_fork(void *objspace_ptr);
GC_IMPL_FN void rb_gc_impl_after_fork(void *objspace_ptr, rb_pid_t pid);
// Statistics
GC_IMPL_FN void rb_gc_impl_set_measure_total_time(void *objspace_ptr, VALUE flag);
GC_IMPL_FN bool rb_gc_impl_get_measure_total_time(void *objspace_ptr);
GC_IMPL_FN unsigned long long rb_gc_impl_get_total_time(void *objspace_ptr);
GC_IMPL_FN size_t rb_gc_impl_gc_count(void *objspace_ptr);
GC_IMPL_FN VALUE rb_gc_impl_latest_gc_info(void *objspace_ptr, VALUE key);
GC_IMPL_FN VALUE rb_gc_impl_stat(void *objspace_ptr, VALUE hash_or_sym);
GC_IMPL_FN VALUE rb_gc_impl_stat_heap(void *objspace_ptr, VALUE heap_name, VALUE hash_or_sym);
GC_IMPL_FN const char *rb_gc_impl_active_gc_name(void);
// Miscellaneous
GC_IMPL_FN struct rb_gc_object_metadata_entry *rb_gc_impl_object_metadata(void *objspace_ptr, VALUE obj);
GC_IMPL_FN bool rb_gc_impl_pointer_to_heap_p(void *objspace_ptr, const void *ptr);
GC_IMPL_FN bool rb_gc_impl_garbage_object_p(void *objspace_ptr, VALUE obj);
GC_IMPL_FN void rb_gc_impl_set_event_hook(void *objspace_ptr, const rb_event_flag_t event);
GC_IMPL_FN void rb_gc_impl_copy_attributes(void *objspace_ptr, VALUE dest, VALUE obj);

#undef GC_IMPL_FN

#endif
