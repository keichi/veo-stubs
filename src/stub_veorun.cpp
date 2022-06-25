#include <dlfcn.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <ffi.h>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include "stub.hpp"
#include "ve_offload.h"

static void handle_load_library(int sock, const json &req)
{
    std::string libname = req["libname"];

    void *libhdl = dlopen(libname.c_str(), RTLD_LAZY);

    if (libhdl == NULL) {
        spdlog::error("{}", dlerror());
    }

    send_msg(sock, {{"result", reinterpret_cast<uint64_t>(libhdl)},
                    {"reqid", req["reqid"]}});
}

static void handle_unload_library(int sock, const json &req)
{
    void *libhdl = reinterpret_cast<void *>((req["libhdl"].get<uint64_t>()));

    int32_t result = dlclose(libhdl);

    send_msg(sock, {{"result", result}, {"reqid", req["reqid"]}});
}

static void handle_get_sym(int sock, const json &req)
{
    void *libhdl = reinterpret_cast<void *>((req["libhdl"].get<uint64_t>()));
    std::string symname = req["symname"];

    void *fn = dlsym(libhdl, symname.c_str());

    if (fn == NULL) {
        spdlog::error("{}", dlerror());
    }

    send_msg(sock, {{"result", reinterpret_cast<uint64_t>(fn)},
                    {"reqid", req["reqid"]}});
}

static void handle_alloc_mem(int sock, const json &req)
{
    uint64_t size = req["size"];
    const void *ptr = malloc(size);

    send_msg(sock, {{"result", reinterpret_cast<uint64_t>(ptr)},
                    {"reqid", req["reqid"]}});
}

static void handle_free_mem(int sock, json req)
{
    uint64_t addr = req["addr"];
    free(reinterpret_cast<void *>(addr));

    send_msg(sock, {{"result", 0}, {"reqid", req["reqid"]}});
}

static void handle_read_mem(int sock, const json &req)
{
    const uint8_t *src =
        reinterpret_cast<uint8_t *>(req["src"].get<uint64_t>());
    uint64_t size = req["size"];

    std::vector<uint8_t> data(src, src + size);

    send_msg(sock, {{"result", 0}, {"reqid", req["reqid"]}, {"data", data}});
}

static void handle_write_mem(int sock, const json &req)
{
    uint8_t *dst = reinterpret_cast<uint8_t *>(req["dst"].get<uint64_t>());
    uint64_t size = req["size"];
    std::vector<uint8_t> data(req["data"]);

    std::copy(data.begin(), data.end(), dst);

    send_msg(sock, {{"result", 0}, {"reqid", req["reqid"]}});
}

static uint64_t _call_func(const void *fn, struct veo_args *args)
{
    ffi_cif cif;
    std::vector<ffi_type *> arg_types;
    std::vector<void *> arg_values;

    for (auto &arg : args->args) {
        switch (arg.val.index()) {
        case VS_ARG_TYPE_I64:
            arg_types.push_back(&ffi_type_sint64);
            arg_values.push_back(std::get_if<VS_ARG_TYPE_I64>(&arg.val));
            break;
        case VS_ARG_TYPE_U64:
            arg_types.push_back(&ffi_type_uint64);
            arg_values.push_back(std::get_if<VS_ARG_TYPE_U64>(&arg.val));
            break;
        case VS_ARG_TYPE_I32:
            arg_types.push_back(&ffi_type_sint32);
            arg_values.push_back(std::get_if<VS_ARG_TYPE_I32>(&arg.val));
            break;
        case VS_ARG_TYPE_U32:
            arg_types.push_back(&ffi_type_uint32);
            arg_values.push_back(std::get_if<VS_ARG_TYPE_U32>(&arg.val));
            break;
        case VS_ARG_TYPE_I16:
            arg_types.push_back(&ffi_type_sint16);
            arg_values.push_back(std::get_if<VS_ARG_TYPE_I16>(&arg.val));
            break;
        case VS_ARG_TYPE_U16:
            arg_types.push_back(&ffi_type_uint16);
            arg_values.push_back(std::get_if<VS_ARG_TYPE_U16>(&arg.val));
            break;
        case VS_ARG_TYPE_I8:
            arg_types.push_back(&ffi_type_sint8);
            arg_values.push_back(std::get_if<VS_ARG_TYPE_I8>(&arg.val));
            break;
        case VS_ARG_TYPE_U8:
            arg_types.push_back(&ffi_type_uint8);
            arg_values.push_back(std::get_if<VS_ARG_TYPE_U8>(&arg.val));
            break;
        case VS_ARG_TYPE_DOUBLE:
            arg_types.push_back(&ffi_type_double);
            arg_values.push_back(std::get_if<VS_ARG_TYPE_DOUBLE>(&arg.val));
            break;
        case VS_ARG_TYPE_FLOAT:
            arg_types.push_back(&ffi_type_float);
            arg_values.push_back(std::get_if<VS_ARG_TYPE_FLOAT>(&arg.val));
            break;
        case VS_ARG_TYPE_STACK:
            arg_types.push_back(&ffi_type_uint64);
            arg_values.push_back(&std::get<VS_ARG_TYPE_STACK>(arg.val).buff);
            break;
        }
    }

    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, arg_types.size(),
                                     &ffi_type_uint64, arg_types.data());

    uint64_t res;
    ffi_call(&cif, FFI_FN(fn), &res, arg_values.data());

    return res;
}

static void handle_call_common(int sock, const json &req, const void *fn)
{
    struct veo_args argp = req["args"];
    std::vector<copy_descriptor> copy_in = req["copy_in"];
    std::vector<copy_descriptor> copy_out = req["copy_out"];

    int i = 0;
    for (auto &arg : argp.args) {
        if (arg.val.index() != VS_ARG_TYPE_STACK) continue;

        auto sa = std::get_if<VS_ARG_TYPE_STACK>(&arg.val);

        sa->buff = reinterpret_cast<char *>(alloca(sa->len));

        if (sa->inout == VEO_INTENT_IN || sa->inout == VEO_INTENT_INOUT) {
            auto &desc = copy_in[i++];
            std::copy(desc.data.begin(), desc.data.end(), sa->buff);
        }
    }

    uint64_t res = _call_func(fn, &argp);

    i = 0;
    for (auto &arg : argp.args) {
        if (arg.val.index() != VS_ARG_TYPE_STACK) continue;

        auto sa = std::get_if<VS_ARG_TYPE_STACK>(&arg.val);

        if (sa->inout == VEO_INTENT_OUT || sa->inout == VEO_INTENT_INOUT) {
            auto &desc = copy_out[i++];
            desc.data.resize(desc.len);
            std::copy(sa->buff, sa->buff + sa->len, desc.data.begin());
        }
    }

    send_msg(
        sock,
        {{"result", res}, {"reqid", req["reqid"]}, {"copy_out", copy_out}});
}

static void handle_call_async(int sock, const json &req)
{
    void *fn = reinterpret_cast<void *>(req["addr"].get<uint64_t>());

    handle_call_common(sock, req, fn);
}

static void handle_call_async_by_name(int sock, const json &req)
{
    void *libhdl = reinterpret_cast<void *>((req["libhdl"].get<uint64_t>()));
    void *fn = dlsym(libhdl, req["symname"].get<std::string>().c_str());

    if (fn == NULL) {
        spdlog::error("{}", dlerror());
    }

    handle_call_common(sock, req, fn);
}

static void handle_async_read_mem(int sock, const json &req)
{
    std::vector<copy_descriptor> descs = req["copy_out"];

    for (auto &desc : descs) {
        desc.data.resize(desc.len);
        std::copy(desc.ve_ptr, desc.ve_ptr + desc.len, desc.data.begin());
    }

    send_msg(sock,
             {{"result", 0}, {"reqid", req["reqid"]}, {"copy_out", descs}});
}

static void handle_async_write_mem(int sock, const json &req)
{
    std::vector<copy_descriptor> descs = req["copy_in"];

    for (auto &desc : descs) {
        std::copy(desc.data.begin(), desc.data.end(), desc.ve_ptr);
    }

    send_msg(sock, {{"result", 0}, {"reqid", req["reqid"]}});
}

static void handle_sync_context(int sock, const json &req)
{
    send_msg(sock, {{"result", 0}, {"reqid", req["reqid"]}});
}

static void handle_quit(int sock, json req) {}

static void close_server_sock(int server_sock)
{
    shutdown(server_sock, SHUT_RDWR);
    close(server_sock);
}

static void worker(int server_sock, int worker_sock)
{
    bool active = true;

    spdlog::debug("Starting up worker thread");

    while (active) {
        json req;

        if (!recv_msg(worker_sock, req)) {
            // We reach here if the VH disconnects unexpectedly. This usually
            // means that the VH crashed, so we clean up and exit.
            spdlog::error("Failed to receive command from VH");
            close_server_sock(server_sock);
            break;
        }

        spdlog::debug("Received command {}", req.dump());

        switch (req["cmd"].get<int32_t>()) {
        case VS_CMD_LOAD_LIBRARY:
            handle_load_library(worker_sock, req);
            break;
        case VS_CMD_UNLOAD_LIBRARY:
            handle_unload_library(worker_sock, req);
            break;
        case VS_CMD_GET_SYM:
            handle_get_sym(worker_sock, req);
            break;
        case VS_CMD_ALLOC_MEM:
            handle_alloc_mem(worker_sock, req);
            break;
        case VS_CMD_FREE_MEM:
            handle_free_mem(worker_sock, req);
            break;
        case VS_CMD_READ_MEM:
            handle_read_mem(worker_sock, req);
            break;
        case VS_CMD_WRITE_MEM:
            handle_write_mem(worker_sock, req);
            break;
        case VS_CMD_CALL_ASYNC:
            handle_call_async(worker_sock, req);
            break;
        case VS_CMD_CALL_ASYNC_BY_NAME:
            handle_call_async_by_name(worker_sock, req);
            break;
        case VS_CMD_ASYNC_READ_MEM:
            handle_async_read_mem(worker_sock, req);
            break;
        case VS_CMD_ASYNC_WRITE_MEM:
            handle_async_write_mem(worker_sock, req);
            break;
        case VS_CMD_CLOSE_CONTEXT:
            active = false;
            break;
        case VS_CMD_SYNC_CONTEXT:
            handle_sync_context(worker_sock, req);
            break;
        case VS_CMD_QUIT:
            handle_quit(worker_sock, req);
            active = false;
            close_server_sock(server_sock);
            break;
        default:
            break;
        }
    }

    close(worker_sock);

    spdlog::debug("Shutting down worker thread");
}

int main(int argc, char *argv[])
{
    spdlog::cfg::load_env_levels();
    spdlog::set_pattern("[%^%l%$] [VE] [PID %P] [TID %t] %v");

    spdlog::debug("Starting server");

    const std::string sock_path =
        "/tmp/stub-veorun." + std::to_string(getpid()) + ".sock";

    struct sockaddr_un server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_LOCAL;
    std::strcpy(server_addr.sun_path, sock_path.c_str());

    int server_sock = socket(AF_LOCAL, SOCK_STREAM, 0);

    if (bind(server_sock, reinterpret_cast<struct sockaddr *>(&server_addr),
             SUN_LEN(&server_addr)) == -1) {
        spdlog::error("Bind() failed");
    }

    if (listen(server_sock, 32) == -1) {
        spdlog::error("Listen() failed");
    }

    spdlog::debug("Server is listening at {}", sock_path);

    std::vector<std::thread> worker_threads;

    while (true) {
        int worker_sock = accept(server_sock, NULL, NULL);

        if (worker_sock == -1) {
            break;
        }

        worker_threads.emplace_back(worker, server_sock, worker_sock);
    }

    for (auto &thread : worker_threads) {
        thread.join();
    }

    unlink(sock_path.c_str());

    spdlog::debug("Exiting server");

    return 0;
}
