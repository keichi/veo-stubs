#include <algorithm>
#include <cstdint>
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include "stub.hpp"
#include "ve_offload.h"

extern "C" {

static std::vector<veo_proc_handle *> procs;

static void worker(struct veo_thr_ctxt *ctx)
{
    json cmd, res;

    while (true) {
        ctx->requests.wait_pop(cmd);

        spdlog::debug("[VH] sending command {}", cmd.dump());

        send_msg(ctx->sock, cmd);

        if (cmd["cmd"] == VEO_STUBS_CMD_CLOSE_CONTEXT ||
            cmd["cmd"] == VEO_STUBS_CMD_QUIT) {
            break;
        }

        res = recv_msg(ctx->sock);

        spdlog::debug("[VH] received result {}", res.dump());

        {
            std::lock_guard<std::mutex> lock(ctx->results_mtx);

            ctx->results.insert({res["reqid"].get<uint64_t>(), res});
            ctx->results_cv.notify_all();
        }
    }
}

static veo_thr_ctxt *_veo_context_open(struct veo_proc_handle *proc)
{
    const std::string sock_path =
        "/tmp/stub-veorun." + std::to_string(proc->pid) + ".sock";

    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_LOCAL;
    strcpy(server_addr.sun_path, sock_path.c_str());

    int sock = socket(AF_LOCAL, SOCK_STREAM, 0);

    int retry_count = 0;
    const int MAX_RETRIES = 100;
    while (connect(sock, reinterpret_cast<struct sockaddr *>(&server_addr),
                   SUN_LEN(&server_addr)) < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        if (++retry_count >= MAX_RETRIES) {
            spdlog::error("[VH] cannot connect to worker on VE");

            return NULL;
        }
    }

    spdlog::debug("[VH] connected to worker on VE");

    struct veo_thr_ctxt *ctx = new veo_thr_ctxt(proc, sock);
    ctx->comm_thread = std::thread(worker, ctx);

    return ctx;
}

struct veo_proc_handle *veo_proc_create(int venode)
{
    // TODO call this in an __attribute__((constructor)) function?
    spdlog::set_level(spdlog::level::debug);

    pid_t child_pid = fork();

    if (child_pid) {
        struct veo_proc_handle *proc = new veo_proc_handle(venode, child_pid);
        struct veo_thr_ctxt *ctx = _veo_context_open(proc);

        if (ctx == NULL) {
            return NULL;
        }

        proc->default_context = ctx;

        procs.push_back(proc);

        return proc;
    } else {
        char *VEORUN_BIN_ENV = strdup(getenv("VEORUN_BIN"));
        const char *VEORUN_BIN =
            VEORUN_BIN_ENV ? VEORUN_BIN_ENV : "stub-veorun";
        const char *argv[] = {VEORUN_BIN, NULL};

        execvp(VEORUN_BIN, const_cast<char *const *>(argv));

        spdlog::error("[VH] failed to launch stub-veorun");

        exit(-1);
    }
}

int veo_proc_destroy(struct veo_proc_handle *proc)
{
    proc->default_context->submit_request({{"cmd", VEO_STUBS_CMD_QUIT}});

    spdlog::debug("[VH] Waiting for VE to quit");

    wait(NULL);

    // TODO make sure all cotexts are closed?
    proc->default_context->comm_thread.join();
    delete proc->default_context;

    std::vector<veo_proc_handle *>::iterator it =
        std::find(procs.begin(), procs.end(), proc);

    if (it != procs.end()) {
        procs.erase(it);
    }

    delete proc;
    return 0;
}

int veo_proc_identifier(veo_proc_handle *proc)
{
    std::vector<veo_proc_handle *>::iterator it =
        std::find(procs.begin(), procs.end(), proc);

    if (it != procs.end()) {
        return it - procs.begin();
    }

    return -1;
}

uint64_t veo_load_library(struct veo_proc_handle *proc, const char *libname)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request({{"cmd", VEO_STUBS_CMD_LOAD_LIBRARY},
                         {"reqid", reqid},
                         {"libname", libname}});

    json result;
    ctx->wait_result(reqid, result);

    return result["result"];
}

int veo_unload_library(veo_proc_handle *proc, const uint64_t libhdl)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request({{"cmd", VEO_STUBS_CMD_UNLOAD_LIBRARY},
                         {"reqid", reqid},
                         {"libhdl", libhdl}});

    json result;
    ctx->wait_result(reqid, result);

    return result["result"];
}

uint64_t veo_get_sym(struct veo_proc_handle *proc, uint64_t libhdl,
                     const char *symname)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request({{"cmd", VEO_STUBS_CMD_GET_SYM},
                         {"reqid", reqid},
                         {"libhdl", libhdl},
                         {"symname", symname}});

    json result;
    ctx->wait_result(reqid, result);

    return result["result"];
}

int veo_alloc_mem(struct veo_proc_handle *proc, uint64_t *addr,
                  const size_t size)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request(
        {{"cmd", VEO_STUBS_CMD_ALLOC_MEM}, {"reqid", reqid}, {"size", size}});

    json result;
    ctx->wait_result(reqid, result);

    *addr = result["result"];

    return *addr == 0 ? -1 : 0;
}

int veo_free_mem(struct veo_proc_handle *proc, uint64_t addr)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request(
        {{"cmd", VEO_STUBS_CMD_FREE_MEM}, {"reqid", reqid}, {"addr", addr}});

    json result;
    ctx->wait_result(reqid, result);

    return result["result"];
}

int veo_read_mem(struct veo_proc_handle *proc, void *dst, uint64_t src,
                 size_t size)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request({{"cmd", VEO_STUBS_CMD_READ_MEM},
                         {"reqid", reqid},
                         {"src", src},
                         {"size", size}});

    json result;
    ctx->wait_result(reqid, result);

    std::vector<uint8_t> data(result["data"]);

    std::copy(data.begin(), data.end(), reinterpret_cast<uint8_t *>(dst));

    return result["result"];
}

int veo_write_mem(struct veo_proc_handle *proc, uint64_t dst, const void *src,
                  size_t size)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    std::vector<uint8_t> data(reinterpret_cast<const uint8_t *>(src),
                              reinterpret_cast<const uint8_t *>(src) + size);

    ctx->submit_request({{"cmd", VEO_STUBS_CMD_WRITE_MEM},
                         {"reqid", reqid},
                         {"dst", dst},
                         {"size", size},
                         {"data", data}});

    json result;
    ctx->wait_result(reqid, result);

    return result["result"];
}

struct veo_thr_ctxt *veo_context_open(struct veo_proc_handle *proc)
{
    if (proc->contexts.empty()) {
        proc->contexts.push_back(proc->default_context);
        return proc->default_context;
    }

    struct veo_thr_ctxt *ctx = _veo_context_open(proc);
    proc->contexts.push_back(ctx);

    return ctx;
}

int veo_context_close(struct veo_thr_ctxt *ctx)
{
    struct veo_proc_handle *proc = ctx->proc;

    std::vector<veo_thr_ctxt *>::iterator it =
        std::find(ctx->proc->contexts.begin(), ctx->proc->contexts.end(), ctx);

    if (it != ctx->proc->contexts.end()) {
        ctx->proc->contexts.erase(it);
    }

    // We do not close the default context
    if (ctx == ctx->proc->default_context) {
        return 0;
    }

    uint64_t reqid = ctx->issue_reqid();
    ctx->submit_request(
        {{"cmd", VEO_STUBS_CMD_CLOSE_CONTEXT}, {"reqid", reqid}});

    ctx->comm_thread.join();

    delete ctx;
    return 0;
}

uint64_t veo_call_async(struct veo_thr_ctxt *ctx, uint64_t addr,
                        struct veo_args *args)
{
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request({{"cmd", VEO_STUBS_CMD_CALL_ASYNC},
                         {"reqid", reqid},
                         {"addr", addr},
                         {"args", *args}});

    return reqid;
}

uint64_t veo_call_async_by_name(struct veo_thr_ctxt *ctx, uint64_t libhdl,
                                const char *symname, struct veo_args *args)
{
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request({{"cmd", VEO_STUBS_CMD_CALL_ASYNC_BY_NAME},
                         {"reqid", reqid},
                         {"libhdl", libhdl},
                         {"symname", symname},
                         {"args", *args}});

    return reqid;
}

int veo_call_wait_result(struct veo_thr_ctxt *ctx, uint64_t reqid,
                         uint64_t *retp)
{
    spdlog::debug("[VH] waiting for request {}", reqid);

    json result;
    ctx->wait_result(reqid, result);

    *retp = result["result"];

    spdlog::debug("[VH] request {} completed", reqid);

    return VEO_COMMAND_OK;
}

int veo_call_peek_result(struct veo_thr_ctxt *ctx, uint64_t reqid,
                         uint64_t *retp)
{
    spdlog::debug("[VH] peeking request {}", reqid);

    json result;
    bool finished = ctx->peek_result(reqid, result);

    if (!finished) {
        spdlog::debug("[VH] request {} is pending", reqid);

        return VEO_COMMAND_UNFINISHED;
    }

    *retp = result["result"];

    return VEO_COMMAND_OK;
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

void veo_args_clear(struct veo_args *ca) { ca->args.clear(); }

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
