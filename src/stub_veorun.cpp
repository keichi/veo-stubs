#include <dlfcn.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <ffi.h>

#include "stub.hpp"

static void handle_load_library(int sock, json req)
{
    std::string libname = req["libname"];

    void *libhdl = dlopen(libname.c_str(), RTLD_LAZY);

    send_msg(sock, {{"result", reinterpret_cast<uint64_t>(libhdl)},
                    {"reqid", req["reqid"]}});
}

static void handle_unload_library(int sock, json req)
{
    void *libhdl = reinterpret_cast<void *>((req["libhdl"].get<uint64_t>()));

    int32_t result = dlclose(libhdl);

    send_msg(sock, {{"result", result}, {"reqid", req["reqid"]}});
}

static void handle_get_sym(int sock, json req)
{
    void *libhdl = reinterpret_cast<void *>((req["libhdl"].get<uint64_t>()));
    std::string symname = req["symname"];

    void *fn = dlsym(libhdl, symname.c_str());

    send_msg(sock, {{"result", reinterpret_cast<uint64_t>(fn)},
                    {"reqid", req["reqid"]}});
}

static void handle_call_async(int sock, json req)
{
    void *libhdl = reinterpret_cast<void *>((req["libhdl"].get<uint64_t>()));
    std::string symname = req["symname"];
    struct veo_args args = req["args"];

    ffi_cif cif;
    std::vector<ffi_type *> arg_types;
    std::vector<void *> arg_values;

    for (auto &arg : args.args) {
        switch (arg.type) {
        case VEO_STUBS_ARG_TYPE_I64:
            arg_types.push_back(&ffi_type_sint64);
            arg_values.push_back(&(arg.i64));
            break;
        case VEO_STUBS_ARG_TYPE_U64:
            arg_types.push_back(&ffi_type_uint64);
            arg_values.push_back(&(arg.u64));
            break;
        case VEO_STUBS_ARG_TYPE_I32:
            arg_types.push_back(&ffi_type_sint32);
            arg_values.push_back(&(arg.i32));
            break;
        case VEO_STUBS_ARG_TYPE_U32:
            arg_types.push_back(&ffi_type_uint32);
            arg_values.push_back(&(arg.u32));
            break;
        case VEO_STUBS_ARG_TYPE_I16:
            arg_types.push_back(&ffi_type_sint16);
            arg_values.push_back(&(arg.i16));
            break;
        case VEO_STUBS_ARG_TYPE_U16:
            arg_types.push_back(&ffi_type_uint16);
            arg_values.push_back(&(arg.u16));
            break;
        case VEO_STUBS_ARG_TYPE_I8:
            arg_types.push_back(&ffi_type_sint8);
            arg_values.push_back(&(arg.i8));
            break;
        case VEO_STUBS_ARG_TYPE_U8:
            arg_types.push_back(&ffi_type_uint8);
            arg_values.push_back(&(arg.u8));
            break;
        case VEO_STUBS_ARG_TYPE_DOUBLE:
            arg_types.push_back(&ffi_type_double);
            arg_values.push_back(&(arg.d));
            break;
        case VEO_STUBS_ARG_TYPE_FLOAT:
            arg_types.push_back(&ffi_type_float);
            arg_values.push_back(&(arg.f));
            break;
        }
    }

    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, arg_types.size(),
                                     &ffi_type_uint64, arg_types.data());

    void *fn = dlsym(libhdl, symname.c_str());
    uint64_t res;
    ffi_call(&cif, FFI_FN(fn), &res, arg_values.data());

    send_msg(sock, {{"result", res}, {"reqid", req["reqid"]}});
}

static void handle_quit(int sock, json req) {}

static void worker(int server_sock, int worker_sock)
{
    bool active = true;

    std::cout << "[VE] starting up worker thread " << std::this_thread::get_id()
              << std::endl;

    while (active) {
        json req = recv_msg(worker_sock);

        std::cout << "[VE] received " << req << std::endl;

        switch (req["cmd"].get<int32_t>()) {
        case VEO_STUBS_CMD_LOAD_LIBRARY:
            handle_load_library(worker_sock, req);
            break;
        case VEO_STUBS_CMD_UNLOAD_LIBRARY:
            handle_unload_library(worker_sock, req);
            break;
        case VEO_STUBS_CMD_GET_SYM:
            handle_get_sym(worker_sock, req);
            break;
        case VEO_STUBS_CMD_CALL_ASYNC:
            handle_call_async(worker_sock, req);
            break;
        case VEO_STUBS_CMD_CLOSE_CONTEXT:
            active = false;
            break;
        case VEO_STUBS_CMD_QUIT:
            handle_quit(worker_sock, req);
            active = false;
            close(server_sock);
            break;
        default:
            break;
        }
    }

    close(worker_sock);

    std::cout << "[VE] shutting down worker thread "
              << std::this_thread::get_id() << std::endl;
}

int main(int argc, char *argv[])
{
    struct sockaddr_un server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_LOCAL;
    strcpy(server_addr.sun_path, "/tmp/stub-veorun.sock");

    int server_sock = socket(AF_LOCAL, SOCK_STREAM, 0);

    unlink("/tmp/stub-veorun.sock");

    if (bind(server_sock, reinterpret_cast<struct sockaddr *>(&server_addr),
             SUN_LEN(&server_addr)) == -1) {
        std::cout << "[VE] bind() failed" << std::endl;
    }

    if (listen(server_sock, 32) == -1) {
        std::cout << "[VE] listen() failed" << std::endl;
    }

    std::vector<std::thread> worker_threads;

    while (true) {
        int worker_sock = accept(server_sock, NULL, NULL);

        if (worker_sock == -1) {
            std::cout << "[VE] shutting down server" << std::endl;
            break;
        }

        worker_threads.emplace_back(worker, server_sock, worker_sock);
    }

    for (auto &thread : worker_threads) {
        thread.join();
    }

    std::cout << "[VE] exiting" << std::endl;

    return 0;
}
