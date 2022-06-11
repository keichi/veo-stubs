#include <algorithm>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "stub.hpp"

extern "C" {

static void worker(struct veo_thr_ctxt *ctx)
{
    json cmd, res;

    while (true) {
        ctx->cmd_queue.wait_dequeue(cmd);

        std::cout << "[VH] sending command " << cmd << std::endl;

        send_msg(ctx->sock, cmd);

        if (cmd["cmd"] == VEO_STUBS_CMD_CLOSE_CONTEXT ||
            cmd["cmd"] == VEO_STUBS_CMD_QUIT) {
            break;
        }

        res = recv_msg(ctx->sock);

        std::cout << "[VH] received result " << res << std::endl;

        ctx->comp_queue.enqueue(res);
    }
}

static veo_thr_ctxt *_veo_context_open(struct veo_proc_handle *proc)
{
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_LOCAL;
    strcpy(server_addr.sun_path, "/tmp/stub-veorun.sock");

    int sock = socket(AF_LOCAL, SOCK_STREAM, 0);

    while (connect(sock, reinterpret_cast<struct sockaddr *>(&server_addr),
                   SUN_LEN(&server_addr)) < 0) {
        // TODO insert sleep?
    }

    std::cout << "[VH] connected to worker on VE" << std::endl;

    struct veo_thr_ctxt *ctx = new veo_thr_ctxt(proc, sock);
    ctx->comm_thread = std::thread(worker, ctx);

    return ctx;
}

struct veo_proc_handle *veo_proc_create(int venode)
{
    pid_t child_pid = fork();

    if (child_pid) {
        struct veo_proc_handle *proc = new veo_proc_handle(child_pid);
        struct veo_thr_ctxt *ctx = _veo_context_open(proc);

        proc->default_context = ctx;

        return proc;
    } else {
        execl("./stub-veorun", "./stub-veorun", NULL);

        return NULL;
    }
}

int veo_proc_destroy(struct veo_proc_handle *proc)
{
    proc->default_context->cmd_queue.enqueue({{"cmd", VEO_STUBS_CMD_QUIT}});

    std::cout << "[VH] Waiting for VE to quit" << std::endl;

    wait(NULL);

    delete proc;
    return 0;
}

uint64_t veo_load_library(struct veo_proc_handle *proc, const char *libname)
{
    proc->default_context->cmd_queue.enqueue(
        {{"cmd", VEO_STUBS_CMD_LOAD_LIBRARY},
         {"reqid", proc->default_context->reqid++},
         {"libname", libname}});

    json res;
    proc->default_context->comp_queue.wait_dequeue(res);

    return res["handle"];
}

int veo_unload_library(veo_proc_handle *proc, const uint64_t libhdl)
{
    proc->default_context->cmd_queue.enqueue(
        {{"cmd", VEO_STUBS_CMD_UNLOAD_LIBRARY},
         {"reqid", proc->default_context->reqid++},
         {"libhdl", libhdl}});

    json res;
    proc->default_context->comp_queue.wait_dequeue(res);

    return res["result"];
}

uint64_t veo_get_sym(struct veo_proc_handle *proc, uint64_t libhdl,
                     const char *symname)
{
    proc->default_context->cmd_queue.enqueue(
        {{"cmd", VEO_STUBS_CMD_GET_SYM},
         {"reqid", proc->default_context->reqid++},
         {"libhdl", libhdl},
         {"symname", symname}});

    json res;
    proc->default_context->comp_queue.wait_dequeue(res);

    return res["address"];
}

struct veo_thr_ctxt *veo_context_open(struct veo_proc_handle *proc)
{
    if (proc->contexts.empty()) {
        proc->contexts.push_back(proc->default_context);
        return proc->default_context;
    }

    static veo_thr_ctxt *ctx = _veo_context_open(proc);
    proc->contexts.push_back(ctx);

    return ctx;
}

int veo_context_close(struct veo_thr_ctxt *ctx)
{
    std::vector<veo_thr_ctxt *>::iterator it =
        std::find(ctx->proc->contexts.begin(), ctx->proc->contexts.end(), ctx);

    if (it != ctx->proc->contexts.end()) {
        ctx->proc->contexts.erase(it);
    }

    // We do not close the default context
    if (ctx == ctx->proc->default_context) {
        return 0;
    }

    ctx->cmd_queue.enqueue(
        {{"cmd", VEO_STUBS_CMD_CLOSE_CONTEXT}, {"reqid", ctx->reqid++}});

    delete ctx;
    return 0;
}

uint64_t veo_call_async_by_name(struct veo_thr_ctxt *ctx, uint64_t libhdl,
                                const char *symname, struct veo_args *args)
{
    uint64_t reqid = ctx->reqid++;

    ctx->cmd_queue.enqueue({{"cmd", VEO_STUBS_CMD_CALL_ASYNC},
                            {"reqid", reqid},
                            {"libhdl", libhdl},
                            {"symname", symname},
                            {"args", *args}});

    return reqid;
}

int veo_call_wait_result(struct veo_thr_ctxt *ctx, uint64_t reqid,
                         uint64_t *retp)
{
    return 0;
}

int veo_num_contexts(struct veo_proc_handle *proc)
{
    return proc->contexts.size();
}

struct veo_thr_ctxt *veo_get_context(struct veo_proc_handle *proc, int idx)
{
    if (idx >= proc->contexts.size()) {
        return NULL;
    }

    return proc->contexts[idx];
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
}
