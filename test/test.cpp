#include <random>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "crc32.h"
#include "ve_offload.h"

TEST_CASE("Create and destroy a VE proc handle")
{
    struct veo_proc_handle *proc = veo_proc_create(0);

    REQUIRE(proc != NULL);
    REQUIRE(veo_proc_identifier(proc) == 0);

    veo_proc_destroy(proc);
}

TEST_CASE("Create and destroy multiple VE proc handles")
{
    struct veo_proc_handle *proc1 = veo_proc_create(0);
    struct veo_proc_handle *proc2 = veo_proc_create(1);

    REQUIRE(proc1 != NULL);
    REQUIRE(proc2 != NULL);

    REQUIRE(veo_proc_identifier(proc1) == 0);
    REQUIRE(veo_proc_identifier(proc2) == 1);

    veo_proc_destroy(proc1);
    veo_proc_destroy(proc2);
}

TEST_CASE("Create and destroy a VE thread context")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    struct veo_thr_ctxt *ctx = veo_context_open(proc);

    veo_context_close(ctx);
    veo_proc_destroy(proc);
}

TEST_CASE("Create and destroy mutliple VE thread contexts")
{
    constexpr size_t REP = 10;

    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    struct veo_thr_ctxt *ctxts[REP];

    for (size_t i = 0; i < REP; i++) {
        REQUIRE(veo_num_contexts(proc) == i);

        ctxts[i] = veo_context_open(proc);
        REQUIRE(ctxts[i] != NULL);

        REQUIRE(veo_num_contexts(proc) == i + 1);
    }

    for (size_t i = 0; i < REP; i++) {
        REQUIRE(veo_num_contexts(proc) == REP - i);

        veo_context_close(ctxts[i]);

        REQUIRE(veo_num_contexts(proc) == REP - i - 1);
    }

    veo_proc_destroy(proc);
}

TEST_CASE("Allocate and free VE memory")
{
    constexpr size_t BUF_SIZE = 256;

    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    uint64_t ve_buf;
    veo_alloc_mem(proc, &ve_buf, BUF_SIZE);

    REQUIRE(ve_buf > 0);

    veo_free_mem(proc, ve_buf);

    veo_proc_destroy(proc);
}

TEST_CASE("Write VE memory")
{
    std::mt19937 engine(0xdeadbeef);
    std::uniform_int_distribution<uint8_t> dist;

    constexpr size_t BUF_SIZE = 1024;

    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    struct veo_thr_ctxt *ctx = veo_context_open(proc);
    REQUIRE(ctx != NULL);

    uint64_t handle = veo_load_library(proc, "./libvetest.so");
    REQUIRE(handle > 0);

    uint64_t ve_buf;
    uint8_t vh_buf[BUF_SIZE];

    for (size_t i = 0; i < BUF_SIZE; i++) {
        vh_buf[i] = dist(engine);
    }

    veo_alloc_mem(proc, &ve_buf, BUF_SIZE);

    REQUIRE(ve_buf > 0);

    veo_write_mem(proc, ve_buf, vh_buf, BUF_SIZE);

    struct veo_args *argp = veo_args_alloc();
    veo_args_set_u64(argp, 0, ve_buf);
    veo_args_set_u64(argp, 1, BUF_SIZE);

    uint64_t reqid = veo_call_async_by_name(ctx, handle, "checksum", argp);
    uint64_t retval;

    REQUIRE(reqid > 0);

    veo_call_wait_result(ctx, reqid, &retval);

    REQUIRE(retval == crc32(vh_buf, BUF_SIZE));

    veo_args_free(argp);

    veo_free_mem(proc, ve_buf);

    veo_unload_library(proc, handle);
    veo_context_close(ctx);
    veo_proc_destroy(proc);
}

TEST_CASE("Read VE memory")
{
    constexpr size_t BUF_SIZE = 1024;

    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    struct veo_thr_ctxt *ctx = veo_context_open(proc);
    REQUIRE(ctx != NULL);

    uint64_t handle = veo_load_library(proc, "./libvetest.so");
    REQUIRE(handle > 0);

    uint64_t ve_buf;
    uint8_t vh_buf[BUF_SIZE];

    veo_alloc_mem(proc, &ve_buf, BUF_SIZE);

    REQUIRE(ve_buf > 0);

    struct veo_args *argp = veo_args_alloc();
    veo_args_set_u64(argp, 0, ve_buf);
    veo_args_set_u64(argp, 1, BUF_SIZE);

    uint64_t reqid = veo_call_async_by_name(ctx, handle, "iota", argp);
    uint64_t retval;

    REQUIRE(reqid > 0);

    veo_call_wait_result(ctx, reqid, &retval);

    REQUIRE(retval == 0);

    veo_args_free(argp);

    veo_read_mem(proc, vh_buf, ve_buf, BUF_SIZE);

    veo_free_mem(proc, ve_buf);

    uint8_t x = 0;
    for (size_t i = 0; i < BUF_SIZE; i++) {
        REQUIRE(vh_buf[i] == x++);
    }

    veo_unload_library(proc, handle);
    veo_context_close(ctx);
    veo_proc_destroy(proc);
}

TEST_CASE("Write and read back VE memory")
{
    std::mt19937 engine(0xdeadbeef);
    std::uniform_int_distribution<uint8_t> dist;

    constexpr size_t BUF_SIZE = 1024;

    struct veo_proc_handle *proc = veo_proc_create(0);

    uint64_t ve_buf;
    uint8_t vh_buf1[BUF_SIZE], vh_buf2[BUF_SIZE];

    for (size_t i = 0; i < BUF_SIZE; i++) {
        vh_buf1[i] = dist(engine);
        vh_buf2[i] = 0;
    }

    veo_alloc_mem(proc, &ve_buf, BUF_SIZE);

    REQUIRE(ve_buf > 0);

    veo_write_mem(proc, ve_buf, vh_buf1, BUF_SIZE);
    veo_read_mem(proc, vh_buf2, ve_buf, BUF_SIZE);

    for (size_t i = 0; i < BUF_SIZE; i++) {
        REQUIRE(vh_buf1[i] == vh_buf2[i]);
    }

    veo_free_mem(proc, ve_buf);

    veo_proc_destroy(proc);
}

TEST_CASE("Load and unload library on VE")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    uint64_t handle1 = veo_load_library(proc, "./libsomerandomname.so");
    REQUIRE(handle1 == 0);

    uint64_t handle2 = veo_load_library(proc, "./libvetest.so");
    REQUIRE(handle2 > 0);

    veo_unload_library(proc, handle2);

    veo_proc_destroy(proc);
}

TEST_CASE("Get address of a symbol on VE")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    struct veo_thr_ctxt *ctx = veo_context_open(proc);
    REQUIRE(ctx != NULL);

    uint64_t handle = veo_load_library(proc, "./libvetest.so");
    REQUIRE(handle > 0);

    REQUIRE(veo_get_sym(proc, handle, "increment") > 0);
    REQUIRE(veo_get_sym(proc, handle, "somerandomname") == 0);

    veo_unload_library(proc, handle);
    veo_context_close(ctx);
    veo_proc_destroy(proc);
}

TEST_CASE("Call a VE function by name and wait for result")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    struct veo_thr_ctxt *ctx = veo_context_open(proc);
    REQUIRE(ctx != NULL);

    uint64_t handle = veo_load_library(proc, "./libvetest.so");
    REQUIRE(handle > 0);

    struct veo_args *argp = veo_args_alloc();
    veo_args_set_i32(argp, 0, 123);

    uint64_t reqid = veo_call_async_by_name(ctx, handle, "increment", argp);
    uint64_t retval;

    REQUIRE(reqid > 0);

    veo_call_wait_result(ctx, reqid, &retval);

    REQUIRE(retval == 124);

    veo_args_free(argp);

    veo_unload_library(proc, handle);
    veo_context_close(ctx);
    veo_proc_destroy(proc);
}

TEST_CASE("Call a VE function by address and wait for result")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    struct veo_thr_ctxt *ctx = veo_context_open(proc);
    REQUIRE(ctx != NULL);

    uint64_t handle = veo_load_library(proc, "./libvetest.so");
    REQUIRE(handle > 0);

    uint64_t addr = veo_get_sym(proc, handle, "increment");
    REQUIRE(addr > 0);

    struct veo_args *argp = veo_args_alloc();
    veo_args_set_i32(argp, 0, 123);

    uint64_t reqid = veo_call_async(ctx, addr, argp);
    uint64_t retval;

    REQUIRE(reqid > 0);

    veo_call_wait_result(ctx, reqid, &retval);

    REQUIRE(retval == 124);

    veo_args_free(argp);

    veo_unload_library(proc, handle);
    veo_context_close(ctx);
    veo_proc_destroy(proc);
}

TEST_CASE("Bulk call a VE function and wait for results")
{
    constexpr size_t REP = 100;

    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    struct veo_thr_ctxt *ctx = veo_context_open(proc);
    REQUIRE(ctx != NULL);

    uint64_t handle = veo_load_library(proc, "./libvetest.so");
    REQUIRE(handle > 0);

    struct veo_args *argps[REP];
    uint64_t reqids[REP];

    for (size_t i = 0; i < REP; i++) {
        argps[i] = veo_args_alloc();
        veo_args_set_i32(argps[i], 0, i);

        reqids[i] = veo_call_async_by_name(ctx, handle, "increment", argps[i]);

        REQUIRE(reqids[i] > 0);
    }

    uint64_t retval;

    for (size_t i = 0; i < REP; i++) {
        veo_call_wait_result(ctx, reqids[i], &retval);

        REQUIRE(retval == i + 1);

        veo_args_free(argps[i]);
    }

    veo_unload_library(proc, handle);
    veo_context_close(ctx);
    veo_proc_destroy(proc);
}

TEST_CASE("Bulk call a VE function and wait for results in reverse order")
{
    constexpr size_t REP = 100;

    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    struct veo_thr_ctxt *ctx = veo_context_open(proc);
    REQUIRE(ctx != NULL);

    uint64_t handle = veo_load_library(proc, "./libvetest.so");
    REQUIRE(handle > 0);

    struct veo_args *argps[REP];
    uint64_t reqids[REP];

    for (size_t i = 0; i < REP; i++) {
        argps[i] = veo_args_alloc();
        veo_args_set_i32(argps[i], 0, i);

        reqids[i] = veo_call_async_by_name(ctx, handle, "increment", argps[i]);

        REQUIRE(reqids[i] > 0);
    }

    uint64_t retval;

    for (size_t i = 0; i < REP; i++) {
        veo_call_wait_result(ctx, reqids[REP - i - 1], &retval);

        REQUIRE(retval == REP - i);

        veo_args_free(argps[REP - i - 1]);
    }

    veo_unload_library(proc, handle);
    veo_context_close(ctx);
    veo_proc_destroy(proc);
}

TEST_CASE("Synchronously call a VE function")
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    REQUIRE(proc != NULL);

    uint64_t handle = veo_load_library(proc, "./libvetest.so");
    REQUIRE(handle > 0);

    uint64_t addr = veo_get_sym(proc, handle, "increment");
    REQUIRE(addr > 0);

    struct veo_args *argp = veo_args_alloc();
    veo_args_set_i32(argp, 0, 123);

    uint64_t retval;
    REQUIRE(veo_call_sync(proc, addr, argp, &retval) == 0);

    REQUIRE(retval == 124);

    veo_args_free(argp);

    veo_unload_library(proc, handle);
    veo_proc_destroy(proc);
}
