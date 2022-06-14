# veo-stubs

[![CMake](https://github.com/keichi/veo-stubs/actions/workflows/cmake.yml/badge.svg)](https://github.com/keichi/veo-stubs/actions/workflows/cmake.yml)

This is a stub of the [VEO API](https://www.hpc.nec/documents/veos/en/aveo/).
It works without a VE or VE SDK installed, and eases the development and
testing of applications that use the VEO API.

## Supported functions

- [x] `veo_proc_create`
- [x] `veo_proc_destroy`
- [x] `veo_proc_identifier`
- [x] `veo_load_library`
- [x] `veo_unload_library`
- [x] `veo_get_sym`
- [x] `veo_free_mem`
- [x] `veo_read_mem`
- [x] `veo_write_mem`
- [x] `veo_context_open`
- [x] `veo_context_close`
- [x] `veo_num_contexts`
- [x] `veo_get_context`
- [x] `veo_call_async`
- [x] `veo_call_async_by_name`
- [x] `veo_call_wait_result`
- [x] `veo_call_peek_result`
- [x] `veo_args_alloc`
- [x] `veo_args_free`
- [x] `veo_args_clear`
- [x] `veo_args_set_*`
