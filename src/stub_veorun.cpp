#include <dlfcn.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <ffi.h>
#include <spdlog/spdlog.h>

#include "stub.hpp"

static void handle_load_library(int sock, const json &req)
{
    std::string libname = req["libname"];

    void *libhdl = dlopen(libname.c_str(), RTLD_LAZY);

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
    // TODO print dlerror() if fn is NULL

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

static uint64_t _call_func(void *fn, struct veo_args *args)
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
        }
    }

    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, arg_types.size(),
                                     &ffi_type_uint64, arg_types.data());

    uint64_t res;
    ffi_call(&cif, FFI_FN(fn), &res, arg_values.data());

    return res;
}

static void handle_call_async(int sock, const json &req)
{
    void *fn = reinterpret_cast<void *>(req["addr"].get<uint64_t>());
    struct veo_args args = req["args"];

    uint64_t res = _call_func(fn, &args);

    send_msg(sock, {{"result", res}, {"reqid", req["reqid"]}});
}

static void handle_call_async_by_name(int sock, const json &req)
{
    void *libhdl = reinterpret_cast<void *>((req["libhdl"].get<uint64_t>()));
    void *fn = dlsym(libhdl, req["symname"].get<std::string>().c_str());
    // TODO print dlerror() if fn is NULL
    struct veo_args args = req["args"];

    uint64_t res = _call_func(fn, &args);

    send_msg(sock, {{"result", res}, {"reqid", req["reqid"]}});
}

static void handle_quit(int sock, json req) {}

static void worker(int server_sock, int worker_sock)
{
    bool active = true;

    spdlog::debug("[VE] starting up worker thread");

    while (active) {
        json req;

        recv_msg(worker_sock, req);

        spdlog::debug("[VE] received {}", req.dump());

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
        case VS_CMD_CLOSE_CONTEXT:
            active = false;
            break;
        case VS_CMD_QUIT:
            handle_quit(worker_sock, req);
            active = false;
            shutdown(server_sock, SHUT_RDWR);
            close(server_sock);
            break;
        default:
            break;
        }
    }

    close(worker_sock);

    spdlog::debug("[VE] shutting down worker thread");
}

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);

    const std::string sock_path =
        "/tmp/stub-veorun." + std::to_string(getpid()) + ".sock";

    struct sockaddr_un server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_LOCAL;
    std::strcpy(server_addr.sun_path, sock_path.c_str());

    int server_sock = socket(AF_LOCAL, SOCK_STREAM, 0);

    if (bind(server_sock, reinterpret_cast<struct sockaddr *>(&server_addr),
             SUN_LEN(&server_addr)) == -1) {
        spdlog::error("[VE] bind() failed");
    }

    if (listen(server_sock, 32) == -1) {
        spdlog::error("[VE] listen() failed");
    }

    std::vector<std::thread> worker_threads;

    while (true) {
        int worker_sock = accept(server_sock, NULL, NULL);

        if (worker_sock == -1) {
            spdlog::debug("[VE] shutting down server");
            break;
        }

        worker_threads.emplace_back(worker, server_sock, worker_sock);
    }

    for (auto &thread : worker_threads) {
        thread.join();
    }

    unlink(sock_path.c_str());

    spdlog::debug("[VE] exiting");

    return 0;
}
