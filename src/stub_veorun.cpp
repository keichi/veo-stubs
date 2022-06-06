#include <dlfcn.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include <ffi.h>
#include <readerwriterqueue.h>

#include "stub.hpp"

static void handle_load_library(int sock, json msg)
{
    std::string libname = msg["libname"];

    void *handle = dlopen(libname.c_str(), RTLD_LAZY);

    send_msg(sock, {{"handle", reinterpret_cast<uint64_t>(handle)}});
}

static void handle_unload_library(int sock, json msg)
{
    void *handle = reinterpret_cast<void *>((msg["libhdl"].get<uint64_t>()));

    int32_t result = dlclose(handle);

    send_msg(sock, {{"result", result}});
}

static void handle_call_async(int sock, json msg)
{
    void *libhdl = reinterpret_cast<void *>((msg["libhdl"].get<uint64_t>()));
    std::string symname = msg["symname"];
    struct veo_args args = msg["args"];

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

    uint64_t reqid = 0;
    send_msg(sock, {{"reqid", reqid}});
}

static void handle_quit(int sock, json msg) {}

moodycamel::BlockingReaderWriterQueue<int> cmd_queue;

int main(int argc, char *argv[])
{
    struct sockaddr_un server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_LOCAL;
    strcpy(server_addr.sun_path, "/tmp/stub-veorun.sock");

    int sock = socket(AF_LOCAL, SOCK_STREAM, 0);

    unlink("/tmp/stub-veorun.sock");

    if (bind(sock, reinterpret_cast<struct sockaddr *>(&server_addr),
             SUN_LEN(&server_addr)) == -1) {
        std::cout << "[VE] bind() failed" << std::endl;
    }

    if (listen(sock, 32) == -1) {
        std::cout << "[VE] listen() failed" << std::endl;
    }

    int sock2 = accept(sock, NULL, NULL);

    bool active = true;

    while (active) {
        json msg = recv_msg(sock2);

        std::cout << msg << std::endl;

        switch (msg["cmd"].get<int32_t>()) {
        case VEO_STUBS_CMD_LOAD_LIBRARY:
            handle_load_library(sock2, msg);
            break;
        case VEO_STUBS_CMD_UNLOAD_LIBRARY:
            handle_unload_library(sock2, msg);
            break;
        case VEO_STUBS_CMD_CALL_ASYNC:
            handle_call_async(sock2, msg);
            break;
        case VEO_STUBS_CMD_QUIT:
            handle_quit(sock2, msg);
            active = false;
            break;
        default:
            break;
        }
    }

    close(sock);
    close(sock2);

    return 0;
}
