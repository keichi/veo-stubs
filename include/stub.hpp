#ifndef __STUB_HPP__
#define __STUB_HPP__

#include <mutex>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <readerwriterqueue.h>

using json = nlohmann::json;

enum veo_stubs_cmd {
    VEO_STUBS_CMD_LOAD_LIBRARY,
    VEO_STUBS_CMD_UNLOAD_LIBRARY,
    VEO_STUBS_CMD_GET_SYM,
    VEO_STUBS_CMD_ALLOC_MEM,
    VEO_STUBS_CMD_FREE_MEM,
    VEO_STUBS_CMD_READ_MEM,
    VEO_STUBS_CMD_WRITE_MEM,
    VEO_STUBS_CMD_CALL_ASYNC,
    VEO_STUBS_CMD_OPEN_CONTEXT,
    VEO_STUBS_CMD_CLOSE_CONTEXT,
    VEO_STUBS_CMD_QUIT,
};

enum veo_stubs_arg_type {
    VEO_STUBS_ARG_TYPE_I64,
    VEO_STUBS_ARG_TYPE_U64,
    VEO_STUBS_ARG_TYPE_I32,
    VEO_STUBS_ARG_TYPE_U32,
    VEO_STUBS_ARG_TYPE_I16,
    VEO_STUBS_ARG_TYPE_U16,
    VEO_STUBS_ARG_TYPE_I8,
    VEO_STUBS_ARG_TYPE_U8,
    VEO_STUBS_ARG_TYPE_DOUBLE,
    VEO_STUBS_ARG_TYPE_FLOAT,
};

struct veo_proc_handle {
    int32_t venode;
    pid_t pid;

    struct veo_thr_ctxt *default_context;
    std::vector<veo_thr_ctxt *> contexts;

    veo_proc_handle(int32_t venode, pid_t pid) : venode(venode), pid(pid) {}
};

struct veo_thr_ctxt {
    struct veo_proc_handle *proc;
    int sock;
    moodycamel::BlockingReaderWriterQueue<json> requests;
    std::unordered_map<uint64_t, json> results;
    std::mutex results_mtx;
    std::condition_variable results_cv;
    uint64_t num_reqs;
    std::thread comm_thread;

    veo_thr_ctxt(struct veo_proc_handle *proc, int sock)
        : proc(proc), sock(sock), num_reqs(0)
    {
    }

    uint64_t issue_reqid() { return num_reqs++; }

    void submit_request(json request) { this->requests.enqueue(request); }

    json wait_for_result(uint64_t reqid)
    {
        std::unique_lock<std::mutex> lock(this->results_mtx);

        this->results_cv.wait(lock, [=] {
            return this->results.find(reqid) != this->results.end();
        });

        json result = this->results.at(reqid);
        this->results.erase(reqid);

        return result;
    }
};

struct veo_arg {
    veo_stubs_arg_type type;

    union {
        int64_t i64;
        uint64_t u64;
        int32_t i32;
        uint32_t u32;
        int16_t i16;
        uint16_t u16;
        int8_t i8;
        uint8_t u8;
        double d;
        float f;
    };
};

struct veo_args {
    std::vector<veo_arg> args;
};

void to_json(json &j, const veo_args &args)
{
    j = json::array();

    for (const auto &arg : args.args) {
        switch (arg.type) {
        case VEO_STUBS_ARG_TYPE_I64:
            j.push_back({{"type", arg.type}, {"val", arg.i64}});
            break;
        case VEO_STUBS_ARG_TYPE_U64:
            j.push_back({{"type", arg.type}, {"val", arg.u64}});
            break;
        case VEO_STUBS_ARG_TYPE_I32:
            j.push_back({{"type", arg.type}, {"val", arg.i32}});
            break;
        case VEO_STUBS_ARG_TYPE_U32:
            j.push_back({{"type", arg.type}, {"val", arg.u32}});
            break;
        case VEO_STUBS_ARG_TYPE_I16:
            j.push_back({{"type", arg.type}, {"val", arg.i16}});
            break;
        case VEO_STUBS_ARG_TYPE_U16:
            j.push_back({{"type", arg.type}, {"val", arg.u16}});
            break;
        case VEO_STUBS_ARG_TYPE_I8:
            j.push_back({{"type", arg.type}, {"val", arg.i8}});
            break;
        case VEO_STUBS_ARG_TYPE_U8:
            j.push_back({{"type", arg.type}, {"val", arg.u8}});
            break;
        case VEO_STUBS_ARG_TYPE_DOUBLE:
            j.push_back({{"type", arg.type}, {"val", arg.d}});
            break;
        case VEO_STUBS_ARG_TYPE_FLOAT:
            j.push_back({{"type", arg.type}, {"val", arg.f}});
            break;
        }
    }
}

void from_json(const json &j, veo_args &args)
{
    args.args.clear();

    for (const auto &e : j) {
        veo_arg arg = veo_arg{e["type"]};

        switch (arg.type) {
        case VEO_STUBS_ARG_TYPE_I64:
            arg.i64 = e["val"];
            break;
        case VEO_STUBS_ARG_TYPE_U64:
            arg.u64 = e["val"];
            break;
        case VEO_STUBS_ARG_TYPE_I32:
            arg.i32 = e["val"];
            break;
        case VEO_STUBS_ARG_TYPE_U32:
            arg.u32 = e["val"];
            break;
        case VEO_STUBS_ARG_TYPE_I16:
            arg.i16 = e["val"];
            break;
        case VEO_STUBS_ARG_TYPE_U16:
            arg.u16 = e["val"];
            break;
        case VEO_STUBS_ARG_TYPE_I8:
            arg.i8 = e["val"];
            break;
        case VEO_STUBS_ARG_TYPE_U8:
            arg.u8 = e["val"];
            break;
        case VEO_STUBS_ARG_TYPE_DOUBLE:
            arg.d = e["val"];
            break;
        case VEO_STUBS_ARG_TYPE_FLOAT:
            arg.f = e["val"];
            break;
        }

        args.args.push_back(arg);
    }
}

void write_all(int fd, const uint8_t *buf, size_t count)
{
    while (count > 0) {
        ssize_t written_bytes = write(fd, buf, count);
        if (written_bytes == -1) {
            throw std::runtime_error("write() returned error");
        }
        buf += written_bytes;
        count -= written_bytes;
    }
}

void read_all(int fd, uint8_t *buf, size_t count)
{
    while (count > 0) {
        ssize_t read_bytes = read(fd, buf, count);
        if (read_bytes == -1) {
            throw std::runtime_error("read() returned error");
        }
        buf += read_bytes;
        count -= read_bytes;
    }
}

void send_msg(int sock, const json &msg)
{
    std::vector<std::uint8_t> buffer = json::to_msgpack(msg);
    uint32_t size = buffer.size();

    write_all(sock, reinterpret_cast<uint8_t *>(&size), sizeof(size));
    write_all(sock, buffer.data(), size);
}

json recv_msg(int sock)
{
    uint32_t size;
    read_all(sock, reinterpret_cast<uint8_t *>(&size), sizeof(size));

    std::vector<std::uint8_t> buffer(size);
    read_all(sock, buffer.data(), size);

    return json::from_msgpack(buffer);
}

#endif
