# veo-stubs

[![CMake](https://github.com/keichi/veo-stubs/actions/workflows/cmake.yml/badge.svg)](https://github.com/keichi/veo-stubs/actions/workflows/cmake.yml)

This is a stub of the [VEO API](https://www.hpc.nec/documents/veos/en/aveo/).
It works without a VE or VE SDK installed, and eases the development and
testing of applications that use the VEO API.

## Requirements

- A C++ compiler that supports C++17
- CMake 3.10 or higher
- libffi development files

## Installation

```
git clone --recursive https://github.com/keichi/veo-stubs.git
mkdir build && cd build
cmake ..
make
sudo make install
```

## Limitations

- veo-stubs is not an emulator. The VE library must be built for VH.
- veo-stubs is not designed for performance. It should be used for functional
  tests only.

## Supported functions

- [ ] `veo_proc_create_static`
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
- [ ] `veo_context_sync`
- [x] `veo_num_contexts`
- [x] `veo_get_context`
- [x] `veo_call_sync`
- [x] `veo_call_async`
- [x] `veo_call_async_by_name`
- [x] `veo_call_wait_result`
- [x] `veo_call_peek_result`
- [x] `veo_async_read_mem`
- [x] `veo_async_write_mem`
- [x] `veo_args_alloc`
- [x] `veo_args_free`
- [x] `veo_args_clear`
- [x] `veo_args_set_*`
- [x] `veo_api_version`
- [x] `veo_version_string`
- [ ] `veo_access_pcircvsyc_register`
- [ ] thread context attribute objects
- [ ] heterogeneous memory
