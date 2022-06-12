#include <ve_offload.h>

int main()
{
    struct veo_proc_handle *proc = veo_proc_create(0);
    uint64_t handle = veo_load_library(proc, "./libvehello.dylib");
    struct veo_thr_ctxt *ctx = veo_context_open(proc);

    uint64_t ve_buf;
    uint8_t vh_buf[1024];

    veo_alloc_mem(proc, &ve_buf, 1024);
    veo_read_mem(proc, vh_buf, ve_buf, 1024);
    veo_write_mem(proc, ve_buf, vh_buf, 1024);
    veo_free_mem(proc, ve_buf);

    struct veo_args *argp = veo_args_alloc();
    veo_args_set_i32(argp, 0, 123);

    uint64_t id = veo_call_async_by_name(ctx, handle, "hello", argp);
    uint64_t retval;
    veo_call_wait_result(ctx, id, &retval);

    veo_args_free(argp);

    veo_context_close(ctx);
    veo_unload_library(proc, handle);
    veo_proc_destroy(proc);

    return 0;
}
