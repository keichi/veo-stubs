#ifndef __STUB_HPP__
#define __STUB_HPP__

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "ve_offload.h"

using json = nlohmann::json;

enum veo_stubs_cmd {
    VS_CMD_LOAD_LIBRARY,
    VS_CMD_UNLOAD_LIBRARY,
    VS_CMD_GET_SYM,
    VS_CMD_ALLOC_MEM,
    VS_CMD_FREE_MEM,
    VS_CMD_READ_MEM,
    VS_CMD_WRITE_MEM,
    VS_CMD_CALL_ASYNC,
    VS_CMD_CALL_ASYNC_BY_NAME,
    VS_CMD_ASYNC_READ_MEM,
    VS_CMD_ASYNC_WRITE_MEM,
    VS_CMD_OPEN_CONTEXT,
    VS_CMD_CLOSE_CONTEXT,
    VS_CMD_QUIT,
};

enum veo_stubs_arg_type {
    VS_ARG_TYPE_I64,
    VS_ARG_TYPE_U64,
    VS_ARG_TYPE_I32,
    VS_ARG_TYPE_U32,
    VS_ARG_TYPE_I16,
    VS_ARG_TYPE_U16,
    VS_ARG_TYPE_I8,
    VS_ARG_TYPE_U8,
    VS_ARG_TYPE_DOUBLE,
    VS_ARG_TYPE_FLOAT,
    VS_ARG_TYPE_STACK,
};

struct veo_proc_handle {
    int32_t venode;
    pid_t pid;

    struct veo_thr_ctxt *default_context;
    std::vector<veo_thr_ctxt *> contexts;

    veo_proc_handle(int32_t venode, pid_t pid) : venode(venode), pid(pid) {}
};

template <typename T> class blocking_queue
{
    std::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cv;

public:
    void push(const T &elem)
    {
        std::lock_guard<std::mutex> lock(mtx);

        queue.push(elem);
        cv.notify_one();
    }

    void wait_pop(T &elem)
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return !queue.empty(); });

        elem = queue.front();
        queue.pop();
    }
};

struct veo_thr_ctxt {
    struct veo_proc_handle *proc;

    int sock;
    std::thread comm_thread;
    std::atomic<bool> is_running;

    blocking_queue<json> requests;
    uint64_t num_reqs;

    std::unordered_map<uint64_t, json> results;
    std::mutex results_mtx;
    std::condition_variable results_cv;

    veo_thr_ctxt(struct veo_proc_handle *proc, int sock)
        : proc(proc), sock(sock), num_reqs(0), is_running(true)
    {
    }

    uint64_t issue_reqid() { return num_reqs++; }

    void submit_request(json request) { this->requests.push(request); }

    bool wait_result(uint64_t reqid, json &result)
    {
        std::unique_lock<std::mutex> lock(this->results_mtx);

        this->results_cv.wait(lock, [=] {
            return this->results.find(reqid) != this->results.end() ||
                   !this->is_running;
        });

        // this means comm_thread exited
        if (this->results.find(reqid) == this->results.end()) {
            return false;
        }

        result = this->results.at(reqid);
        this->results.erase(reqid);

        return true;
    }

    bool peek_result(uint64_t reqid, json &result)
    {
        std::lock_guard<std::mutex> lock(this->results_mtx);

        if (this->results.find(reqid) == this->results.end()) {
            return false;
        }

        result = this->results.at(reqid);
        this->results.erase(reqid);

        return true;
    }
};

struct copy_descriptor {
    uint8_t *ve_ptr;
    uint8_t *vh_ptr;
    size_t len;
    std::vector<uint8_t> data;
};

void to_json(json &j, const copy_descriptor &arg)
{
    j["ve_ptr"] = reinterpret_cast<uint64_t>(arg.ve_ptr);
    j["vh_ptr"] = reinterpret_cast<uint64_t>(arg.vh_ptr);
    j["len"] = arg.len;
    j["data"] = arg.data;
}

void from_json(const json &j, copy_descriptor &arg)
{
    arg.ve_ptr = reinterpret_cast<uint8_t *>(j["ve_ptr"].get<uint64_t>());
    arg.vh_ptr = reinterpret_cast<uint8_t *>(j["vh_ptr"].get<uint64_t>());
    arg.len = j["len"].get<uint64_t>();
    arg.data = j["data"].get<std::vector<uint8_t>>();
}

struct stack_arg {
    veo_args_intent inout;
    char *buff;
    size_t len;
};

void to_json(json &j, const stack_arg &arg)
{
    j["inout"] = arg.inout;
    j["buff"] = reinterpret_cast<uint64_t>(arg.buff);
    j["len"] = arg.len;
}

void from_json(const json &j, stack_arg &arg)
{
    arg.inout = static_cast<veo_args_intent>(j["inout"].get<int32_t>());
    arg.buff = reinterpret_cast<char *>(j["buff"].get<uint64_t>());
    arg.len = j["len"].get<size_t>();
}

struct veo_arg {
    std::variant<int64_t, uint64_t, int32_t, uint32_t, int16_t, uint16_t,
                 int8_t, uint8_t, double, float, stack_arg>
        val;
};

struct veo_args {
    std::vector<veo_arg> args;
};

void to_json(json &j, const veo_args &argp)
{
    j = json::array();

    for (const auto &arg : argp.args) {
        json e = {{"type", arg.val.index()}};

        switch (arg.val.index()) {
        case VS_ARG_TYPE_I64:
            e["val"] = std::get<VS_ARG_TYPE_I64>(arg.val);
            break;
        case VS_ARG_TYPE_U64:
            e["val"] = std::get<VS_ARG_TYPE_U64>(arg.val);
            break;
        case VS_ARG_TYPE_I32:
            e["val"] = std::get<VS_ARG_TYPE_I32>(arg.val);
            break;
        case VS_ARG_TYPE_U32:
            e["val"] = std::get<VS_ARG_TYPE_U32>(arg.val);
            break;
        case VS_ARG_TYPE_I16:
            e["val"] = std::get<VS_ARG_TYPE_I16>(arg.val);
            break;
        case VS_ARG_TYPE_U16:
            e["val"] = std::get<VS_ARG_TYPE_U16>(arg.val);
            break;
        case VS_ARG_TYPE_I8:
            e["val"] = std::get<VS_ARG_TYPE_I8>(arg.val);
            break;
        case VS_ARG_TYPE_U8:
            e["val"] = std::get<VS_ARG_TYPE_I8>(arg.val);
            break;
        case VS_ARG_TYPE_DOUBLE:
            e["val"] = std::get<VS_ARG_TYPE_DOUBLE>(arg.val);
            break;
        case VS_ARG_TYPE_FLOAT:
            e["val"] = std::get<VS_ARG_TYPE_FLOAT>(arg.val);
            break;
        case VS_ARG_TYPE_STACK:
            e["val"] = std::get<VS_ARG_TYPE_STACK>(arg.val);
            break;
        }

        j.push_back(e);
    }
}

void from_json(const json &j, veo_args &argp)
{
    for (const auto &e : j) {
        veo_arg arg;

        switch (e["type"].get<int32_t>()) {
        case VS_ARG_TYPE_I64:
            arg.val = e["val"].get<int64_t>();
            break;
        case VS_ARG_TYPE_U64:
            arg.val = e["val"].get<uint64_t>();
            break;
        case VS_ARG_TYPE_I32:
            arg.val = e["val"].get<int32_t>();
            break;
        case VS_ARG_TYPE_U32:
            arg.val = e["val"].get<uint64_t>();
            break;
        case VS_ARG_TYPE_I16:
            arg.val = e["val"].get<int16_t>();
            break;
        case VS_ARG_TYPE_U16:
            arg.val = e["val"].get<uint64_t>();
            break;
        case VS_ARG_TYPE_I8:
            arg.val = e["val"].get<int8_t>();
            break;
        case VS_ARG_TYPE_U8:
            arg.val = e["val"].get<uint8_t>();
            break;
        case VS_ARG_TYPE_DOUBLE:
            arg.val = e["val"].get<double>();
            break;
        case VS_ARG_TYPE_FLOAT:
            arg.val = e["val"].get<float>();
            break;
        case VS_ARG_TYPE_STACK:
            arg.val = e["val"].get<stack_arg>();
            break;
        }

        argp.args.push_back(arg);
    }
}

template <typename T> int veo_args_set(struct veo_args *ca, int argnum, T val)
{
    ca->args.resize(std::max(ca->args.size(), static_cast<size_t>(argnum + 1)));
    ca->args[argnum].val = val;

    return 0;
}

bool do_write(int fd, const uint8_t *buf, size_t count)
{
    while (count > 0) {
        ssize_t written_bytes = write(fd, buf, count);
        if (written_bytes == 0 || written_bytes == -1) {
            return false;
        }
        buf += written_bytes;
        count -= written_bytes;
    }

    return true;
}

bool do_read(int fd, uint8_t *buf, size_t count)
{
    while (count > 0) {
        ssize_t read_bytes = read(fd, buf, count);
        if (read_bytes == 0 || read_bytes == -1) {
            return false;
        }
        buf += read_bytes;
        count -= read_bytes;
    }

    return true;
}

bool send_msg(int sock, const json &msg)
{
    std::vector<std::uint8_t> buffer = json::to_msgpack(msg);
    uint32_t size = buffer.size();

    if (!do_write(sock, reinterpret_cast<uint8_t *>(&size), sizeof(size))) {
        return false;
    }
    if (!do_write(sock, buffer.data(), size)) {
        return false;
    }

    return true;
}

bool recv_msg(int sock, json &msg)
{
    uint32_t size;
    if (!do_read(sock, reinterpret_cast<uint8_t *>(&size), sizeof(size))) {
        return false;
    }

    std::vector<std::uint8_t> buffer(size);
    if (!do_read(sock, buffer.data(), size)) {
        return false;
    }

    msg = json::from_msgpack(buffer);

    return true;
}

#endif
