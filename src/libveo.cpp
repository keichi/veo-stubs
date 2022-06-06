#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "stub.hpp"

extern "C" {

struct veo_proc_handle *veo_proc_create(int venode)
{
    pid_t child_pid = fork();

    if (child_pid) {
        struct sockaddr_un server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sun_family = AF_LOCAL;
        strcpy(server_addr.sun_path, "/tmp/stub-veorun.sock");

        int sock = socket(AF_LOCAL, SOCK_STREAM, 0);

        while (connect(sock, reinterpret_cast<struct sockaddr *>(&server_addr),
                       SUN_LEN(&server_addr)) < 0) {
        }

        return new veo_proc_handle{child_pid, sock};
    } else {
        execl("./stub-veorun", "./stub-veorun", NULL);
    }
}

int veo_proc_destroy(struct veo_proc_handle *proc)
{
    send_msg(proc->sock, {{"cmd", VEO_STUBS_CMD_QUIT}});

    wait(NULL);

    delete proc;
    return 0;
}

uint64_t veo_load_library(struct veo_proc_handle *proc, const char *libname)
{
    send_msg(proc->sock,
             {{"cmd", VEO_STUBS_CMD_LOAD_LIBRARY}, {"libname", libname}});

    json msg = recv_msg(proc->sock);

    return msg["handle"];
}

int veo_unload_library(veo_proc_handle *proc, const uint64_t libhdl)
{
    send_msg(proc->sock,
             {{"cmd", VEO_STUBS_CMD_UNLOAD_LIBRARY}, {"libhdl", libhdl}});

    json msg = recv_msg(proc->sock);

    return msg["result"];
}

struct veo_thr_ctxt *veo_context_open(struct veo_proc_handle *proc)
{
    return new veo_thr_ctxt{proc};
}

int veo_context_close(struct veo_thr_ctxt *ctx)
{
    delete ctx;
    return 0;
}

struct veo_args *veo_args_alloc(void) { return new veo_args; }

void veo_args_free(struct veo_args *ca) { delete ca; }

int veo_args_set_i64(struct veo_args *ca, int argnum, int64_t val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].type = VEO_STUBS_ARG_TYPE_I64;
    ca->args[argnum].i64 = val;

    return 0;
}

int veo_args_set_u64(struct veo_args *ca, int argnum, uint64_t val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].type = VEO_STUBS_ARG_TYPE_U64;
    ca->args[argnum].u64 = val;

    return 0;
}

int veo_args_set_i32(struct veo_args *ca, int argnum, int32_t val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].type = VEO_STUBS_ARG_TYPE_I32;
    ca->args[argnum].i32 = val;

    return 0;
}

int veo_args_set_u32(struct veo_args *ca, int argnum, uint32_t val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].type = VEO_STUBS_ARG_TYPE_U32;
    ca->args[argnum].u32 = val;

    return 0;
}

int veo_args_set_i16(struct veo_args *ca, int argnum, int16_t val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].type = VEO_STUBS_ARG_TYPE_I16;
    ca->args[argnum].i16 = val;

    return 0;
}

int veo_args_set_u16(struct veo_args *ca, int argnum, uint16_t val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].type = VEO_STUBS_ARG_TYPE_U16;
    ca->args[argnum].u16 = val;

    return 0;
}

int veo_args_set_i8(struct veo_args *ca, int argnum, int8_t val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].type = VEO_STUBS_ARG_TYPE_I8;
    ca->args[argnum].i8 = val;

    return 0;
}

int veo_args_set_u8(struct veo_args *ca, int argnum, uint8_t val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].type = VEO_STUBS_ARG_TYPE_U8;
    ca->args[argnum].u8 = val;

    return 0;
}

int veo_args_set_double(struct veo_args *ca, int argnum, double val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].type = VEO_STUBS_ARG_TYPE_DOUBLE;
    ca->args[argnum].d = val;

    return 0;
}

int veo_args_set_float(struct veo_args *ca, int argnum, float val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].type = VEO_STUBS_ARG_TYPE_FLOAT;
    ca->args[argnum].f = val;

    return 0;
}

uint64_t veo_call_async_by_name(struct veo_thr_ctxt *ctx, uint64_t libhdl,
                                const char *symname, struct veo_args *args)
{
    send_msg(ctx->proc->sock, {{"cmd", VEO_STUBS_CMD_CALL_ASYNC},
                               {"libhdl", libhdl},
                               {"symname", symname},
                               {"args", *args}});

    json msg = recv_msg(ctx->proc->sock);

    return msg["reqid"];
}

int veo_call_wait_result(struct veo_thr_ctxt *ctx, uint64_t reqid,
                         uint64_t *retp)
{
    return 0;
}
}
