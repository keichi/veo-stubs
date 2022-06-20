#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include "stub.hpp"
#include "ve_offload.h"

extern "C" {

static std::vector<veo_proc_handle *> procs;

static void worker(struct veo_thr_ctxt *ctx)
{
    json req, res;
    bool aborted = false;

    while (true) {
        ctx->requests.wait_pop(req);

        // Perform copy-in
        if (req.contains("copy_in")) {
            for (auto &j : req["copy_in"]) {
                copy_descriptor desc = j;
                j["data"] =
                    std::vector<uint8_t>(desc.vh_ptr, desc.vh_ptr + desc.len);
            }
        }

        if (!send_msg(ctx->sock, req)) {
            spdlog::error("[VH] failed to send command to VE");
            aborted = true;
            break;
        }

        if (req["cmd"] == VS_CMD_CLOSE_CONTEXT || req["cmd"] == VS_CMD_QUIT) {
            break;
        }

        if (!recv_msg(ctx->sock, res)) {
            spdlog::error("[VH] failed to receive result from VE");
            aborted = true;
            break;
        }

        spdlog::debug("[VH] received result {}", res.dump());

        // Perform copy-out
        if (res.contains("copy_out")) {
            for (const copy_descriptor &desc : res["copy_out"]) {
                std::copy(desc.data.begin(), desc.data.end(), desc.vh_ptr);
            }
        }

        {
            std::lock_guard<std::mutex> lock(ctx->results_mtx);

            ctx->results.insert({res["reqid"].get<uint64_t>(), res});
            ctx->results_cv.notify_one();
        }
    }

    ctx->is_running = false;

    if (aborted) {
        // Notify main thread in case it's waiting for results
        ctx->results_cv.notify_one();
    }
}

static veo_thr_ctxt *_veo_context_open(struct veo_proc_handle *proc)
{
    // We intentionally do not check if proc (or any pointer given by the user)
    // is valid to match the behavior with libveo
    const std::string sock_path =
        "/tmp/stub-veorun." + std::to_string(proc->pid) + ".sock";

    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_LOCAL;
    strcpy(server_addr.sun_path, sock_path.c_str());

    int sock = socket(AF_LOCAL, SOCK_STREAM, 0);

    // TODO We need to properly check if stub-veorun has started otherwise
    // stub-veorun may become an orphan process. Maybe send a message if
    // execvp fails?
    int retry_count = 0;
    const int MAX_RETRIES = 1000;

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
    spdlog::cfg::load_env_levels();

    pid_t child_pid = fork();

    if (child_pid) {
        // TODO use VE_NODE_NUMBER if venode == -1
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
    proc->default_context->submit_request({{"cmd", VS_CMD_QUIT}});

    spdlog::debug("[VH] Waiting for VE to quit");

    wait(NULL);

    // TODO make sure all cotexts are closed?
    proc->default_context->comm_thread.join();
    delete proc->default_context;

    const auto it = std::find(procs.begin(), procs.end(), proc);

    if (it != procs.end()) {
        procs.erase(it);
    }

    const std::string sock_path =
        "/tmp/stub-veorun." + std::to_string(proc->pid) + ".sock";
    unlink(sock_path.c_str());

    delete proc;
    return 0;
}

int veo_proc_identifier(veo_proc_handle *proc)
{
    const auto it = std::find(procs.begin(), procs.end(), proc);

    if (it == procs.end()) {
        return -1;
    }

    return it - procs.begin();
}

uint64_t veo_load_library(struct veo_proc_handle *proc, const char *libname)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request(
        {{"cmd", VS_CMD_LOAD_LIBRARY}, {"reqid", reqid}, {"libname", libname}});

    json result;
    if (!ctx->wait_result(reqid, result)) {
        return -1;
    }

    return result["result"];
}

int veo_unload_library(veo_proc_handle *proc, const uint64_t libhdl)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request(
        {{"cmd", VS_CMD_UNLOAD_LIBRARY}, {"reqid", reqid}, {"libhdl", libhdl}});

    json result;
    if (!ctx->wait_result(reqid, result)) {
        return -1;
    }

    return result["result"];
}

uint64_t veo_get_sym(struct veo_proc_handle *proc, uint64_t libhdl,
                     const char *symname)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request({{"cmd", VS_CMD_GET_SYM},
                         {"reqid", reqid},
                         {"libhdl", libhdl},
                         {"symname", symname}});

    json result;
    if (!ctx->wait_result(reqid, result)) {
        return 0;
    }

    return result["result"];
}

int veo_alloc_mem(struct veo_proc_handle *proc, uint64_t *addr,
                  const size_t size)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request(
        {{"cmd", VS_CMD_ALLOC_MEM}, {"reqid", reqid}, {"size", size}});

    json result;
    if (!ctx->wait_result(reqid, result)) {
        return -1;
    }

    *addr = result["result"];

    return *addr == 0 ? -1 : 0;
}

int veo_free_mem(struct veo_proc_handle *proc, uint64_t addr)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request(
        {{"cmd", VS_CMD_FREE_MEM}, {"reqid", reqid}, {"addr", addr}});

    json result;
    if (!ctx->wait_result(reqid, result)) {
        return -1;
    }

    return result["result"];
}

int veo_read_mem(struct veo_proc_handle *proc, void *dst, uint64_t src,
                 size_t size)
{
    struct veo_thr_ctxt *ctx = proc->default_context;
    uint64_t reqid = ctx->issue_reqid();

    ctx->submit_request({{"cmd", VS_CMD_READ_MEM},
                         {"reqid", reqid},
                         {"src", src},
                         {"size", size}});

    json result;
    if (!ctx->wait_result(reqid, result)) {
        return -1;
    }

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

    ctx->submit_request({{"cmd", VS_CMD_WRITE_MEM},
                         {"reqid", reqid},
                         {"dst", dst},
                         {"size", size},
                         {"data", data}});

    json result;
    if (!ctx->wait_result(reqid, result)) {
        return -1;
    }

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
    // Do nothing if the context has already exited
    if (!ctx->is_running) {
        return 0;
    }

    struct veo_proc_handle *proc = ctx->proc;

    const auto it =
        std::find(ctx->proc->contexts.begin(), ctx->proc->contexts.end(), ctx);

    if (it != ctx->proc->contexts.end()) {
        ctx->proc->contexts.erase(it);
    }

    // We do not close the default context
    if (ctx == ctx->proc->default_context) {
        return 0;
    }

    uint64_t reqid = ctx->issue_reqid();
    ctx->submit_request({{"cmd", VS_CMD_CLOSE_CONTEXT}, {"reqid", reqid}});

    ctx->comm_thread.join();

    delete ctx;
    return 0;
}

static json copy_in_for_stack_args(struct veo_args *argp)
{
    json copy = json::array();

    for (const auto &arg : argp->args) {
        if (arg.val.index() != VS_ARG_TYPE_STACK) continue;
        stack_arg sa = std::get<stack_arg>(arg.val);

        if (sa.inout == VEO_INTENT_IN || sa.inout == VEO_INTENT_INOUT) {
            copy.push_back(copy_descriptor{
                NULL, reinterpret_cast<uint8_t *>(sa.buff), sa.len});
        }
    }

    return copy;
}

static json copy_out_for_stack_args(struct veo_args *argp)
{
    json copy = json::array();

    for (const auto &arg : argp->args) {
        if (arg.val.index() != VS_ARG_TYPE_STACK) continue;
        stack_arg sa = std::get<stack_arg>(arg.val);

        if (sa.inout == VEO_INTENT_OUT || sa.inout == VEO_INTENT_INOUT) {
            copy.push_back(copy_descriptor{
                NULL, reinterpret_cast<uint8_t *>(sa.buff), sa.len});
        }
    }

    return copy;
}

uint64_t veo_call_async(struct veo_thr_ctxt *ctx, uint64_t addr,
                        struct veo_args *argp)
{
    uint64_t reqid = ctx->issue_reqid();

    json req = {{"cmd", VS_CMD_CALL_ASYNC},
                {"reqid", reqid},
                {"addr", addr},
                {"args", *argp},
                {"copy_in", copy_in_for_stack_args(argp)},
                {"copy_out", copy_out_for_stack_args(argp)}};

    ctx->submit_request(req);

    return reqid;
}

uint64_t veo_call_async_by_name(struct veo_thr_ctxt *ctx, uint64_t libhdl,
                                const char *symname, struct veo_args *argp)
{
    uint64_t reqid = ctx->issue_reqid();

    json req = {{"cmd", VS_CMD_CALL_ASYNC_BY_NAME},
                {"reqid", reqid},
                {"libhdl", libhdl},
                {"symname", symname},
                {"args", *argp},
                {"copy_in", copy_in_for_stack_args(argp)},
                {"copy_out", copy_out_for_stack_args(argp)}};

    ctx->submit_request(req);

    return reqid;
}

int veo_call_sync(struct veo_proc_handle *proc, uint64_t addr,
                  struct veo_args *args, uint64_t *result)
{
    struct veo_thr_ctxt *ctx = proc->default_context;

    uint64_t reqid = veo_call_async(ctx, addr, args);

    return veo_call_wait_result(ctx, reqid, result);
}

// TODO how do we now this reqid is valid?
int veo_call_wait_result(struct veo_thr_ctxt *ctx, uint64_t reqid,
                         uint64_t *retp)
{
    spdlog::debug("[VH] waiting for request {}", reqid);

    json result;
    if (!ctx->wait_result(reqid, result)) {
        spdlog::error("[VH] context is not running");
        return VEO_COMMAND_ERROR;
    }

    *retp = result["result"];

    spdlog::debug("[VH] request {} completed", reqid);

    // TODO return VEO_COMMAND_ERROR if symbol cannot be found
    // TODO return VEO_COMMAND_ERROR if reqid is invalid
    // TODO what if the command has already finished?

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

uint64_t veo_async_read_mem(struct veo_thr_ctxt *ctx, void *dst, uint64_t src,
                            size_t size)
{
    uint64_t reqid = ctx->issue_reqid();

    copy_descriptor desc{reinterpret_cast<uint8_t *>(src),
                         reinterpret_cast<uint8_t *>(dst), size};

    json req = {{"cmd", VS_CMD_ASYNC_READ_MEM},
                {"reqid", reqid},
                {"copy_out", json::array({desc})}};

    ctx->submit_request(req);

    return reqid;
}

uint64_t veo_async_write_mem(struct veo_thr_ctxt *ctx, uint64_t dst,
                             const void *src, size_t size)
{
    uint64_t reqid = ctx->issue_reqid();

    copy_descriptor desc{reinterpret_cast<uint8_t *>(dst),
                         reinterpret_cast<uint8_t *>(const_cast<void *>(src)),
                         size};

    json req = {{"cmd", VS_CMD_ASYNC_WRITE_MEM},
                {"reqid", reqid},
                {"copy_in", json::array({desc})}};

    ctx->submit_request(req);

    return reqid;
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

int veo_get_context_state(struct veo_thr_ctxt *ctx)
{
    return ctx->is_running ? VEO_STATE_RUNNING : VEO_STATE_EXIT;
}

struct veo_args *veo_args_alloc(void) { return new veo_args; }

void veo_args_free(struct veo_args *ca) { delete ca; }

void veo_args_clear(struct veo_args *ca) { ca->args.clear(); }

int veo_args_set_i64(struct veo_args *ca, int argnum, int64_t val)
{
    return veo_args_set(ca, argnum, val);
}

int veo_args_set_u64(struct veo_args *ca, int argnum, uint64_t val)
{
    return veo_args_set(ca, argnum, val);
}

int veo_args_set_i32(struct veo_args *ca, int argnum, int32_t val)
{
    return veo_args_set(ca, argnum, val);
}

int veo_args_set_u32(struct veo_args *ca, int argnum, uint32_t val)
{
    return veo_args_set(ca, argnum, val);
}

int veo_args_set_i16(struct veo_args *ca, int argnum, int16_t val)
{
    return veo_args_set(ca, argnum, val);
}

int veo_args_set_u16(struct veo_args *ca, int argnum, uint16_t val)
{
    return veo_args_set(ca, argnum, val);
}

int veo_args_set_i8(struct veo_args *ca, int argnum, int8_t val)
{
    return veo_args_set(ca, argnum, val);
}

int veo_args_set_u8(struct veo_args *ca, int argnum, uint8_t val)
{
    return veo_args_set(ca, argnum, val);
}

int veo_args_set_double(struct veo_args *ca, int argnum, double val)
{
    return veo_args_set(ca, argnum, val);
}

int veo_args_set_float(struct veo_args *ca, int argnum, float val)
{
    return veo_args_set(ca, argnum, val);
}

int veo_args_set_stack(veo_args *ca, enum veo_args_intent inout, int argnum,
                       char *buff, size_t len)
{
    return veo_args_set(ca, argnum, stack_arg{inout, buff, len});
}

int veo_api_version(void) { return VEO_API_VERSION; }

const char *veo_version_string(void)
{
    static const char *VEO_VERSION_STRING = "2.10.0";

    return VEO_VERSION_STRING;
}
}
