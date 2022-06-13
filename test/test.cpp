#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ve_offload.h"

int factorial(int number)
{
    return number <= 1 ? number : factorial(number - 1) * number;
}

TEST_CASE("Create and destroy a VE proc handle")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    veo_proc_destroy(proc);
}

TEST_CASE("Create and destroy multiple VE proc handles")
{
    struct veo_proc_handle *proc1 = veo_proc_create(0);
    struct veo_proc_handle *proc2 = veo_proc_create(1);
    veo_proc_destroy(proc1);
    veo_proc_destroy(proc2);
}

TEST_CASE("Create and destroy a VE thread context")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    struct veo_thr_ctxt *ctx = veo_context_open(proc);
    veo_context_close(ctx);
    veo_proc_destroy(proc);
}

TEST_CASE("Create and destroy mutliple VE thread contexts")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    struct veo_thr_ctxt *ctxts[10];

    for (size_t i = 0; i < 10; i++) {
        CHECK(veo_num_contexts(proc) == i);

        ctxts[i] = veo_context_open(proc);

        CHECK(veo_num_contexts(proc) == i + 1);
    }

    for (size_t i = 0; i < 10; i++) {
        CHECK(veo_num_contexts(proc) == 10 - i);

        veo_context_close(ctxts[i]);

        CHECK(veo_num_contexts(proc) == 10 - i - 1);
    }

    veo_proc_destroy(proc);
}

TEST_CASE("Allocate and free VE memory")
{
    struct veo_proc_handle *proc = veo_proc_create(0);

    uint64_t ve_buf;
    veo_alloc_mem(proc, &ve_buf, 256);

    CHECK(ve_buf > 0);

    veo_free_mem(proc, ve_buf);

    veo_proc_destroy(proc);
}

TEST_CASE("Write and read VE memory")
{
    struct veo_proc_handle *proc = veo_proc_create(0);

    uint64_t ve_buf;
    uint8_t vh_buf1[256], vh_buf2[256];

    for (size_t i = 0; i < 256; i++) {
        vh_buf1[i] = i;
        vh_buf2[i] = 0;
    }

    veo_alloc_mem(proc, &ve_buf, 256);

    CHECK(ve_buf > 0);

    veo_write_mem(proc, ve_buf, vh_buf1, 256);
    veo_read_mem(proc, vh_buf2, ve_buf, 256);

    for (size_t i = 0; i < 256; i++) {
        CHECK(vh_buf1[i] == i);
        CHECK(vh_buf2[i] == i);
    }

    veo_free_mem(proc, ve_buf);

    veo_proc_destroy(proc);
}

TEST_CASE("Load and unload library on VE")
{
    struct veo_proc_handle *proc = veo_proc_create(0);

    uint64_t handle = veo_load_library(proc, "./libvehello.so");

    CHECK(handle > 0);

    veo_unload_library(proc, handle);

    veo_proc_destroy(proc);
}

TEST_CASE("Call a VE function and wait for result")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    struct veo_thr_ctxt *ctx = veo_context_open(proc);
    uint64_t handle = veo_load_library(proc, "./libvehello.so");

    CHECK(handle > 0);

    struct veo_args *argp = veo_args_alloc();
    veo_args_set_i32(argp, 0, 123);

    uint64_t reqid = veo_call_async_by_name(ctx, handle, "hello", argp);
    uint64_t retval;

    CHECK(reqid > 0);

    veo_call_wait_result(ctx, reqid, &retval);

    CHECK(retval == 124);

    veo_unload_library(proc, handle);
    veo_context_close(ctx);
    veo_proc_destroy(proc);
}

TEST_CASE("Bulk call a VE function and wait for results")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    struct veo_thr_ctxt *ctx = veo_context_open(proc);
    uint64_t handle = veo_load_library(proc, "./libvehello.so");

    CHECK(handle > 0);

    struct veo_args *argp = veo_args_alloc();
    veo_args_set_i32(argp, 0, 123);

    uint64_t reqids[64];

    for (size_t i = 0; i < 64; i++) {
        reqids[i] = veo_call_async_by_name(ctx, handle, "hello", argp);

        CHECK(reqids[i] > 0);
    }

    uint64_t retval;

    for (size_t i = 0; i < 64; i++) {
        veo_call_wait_result(ctx, reqids[i], &retval);

        CHECK(retval == 124);
    }

    veo_unload_library(proc, handle);
    veo_context_close(ctx);
    veo_proc_destroy(proc);
}
