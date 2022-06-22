# veo-stubs

[![build](https://github.com/keichi/veo-stubs/actions/workflows/build.yml/badge.svg)](https://github.com/keichi/veo-stubs/actions/workflows/build.yml)

veo-stubs is a stub of NEC's [VEO API](https://www.hpc.nec/documents/veos/en/aveo/).
It is designed to work without a VE card or VE SDK, and eases the development
and testing of applications that use the VEO API.

Although being a stub, veo-stubs tries to imitate the behavior of VEO as much
as possible to help users in finding bugs. When an application creates a
process handle, a `stub-veorun` process is started. A new thread context tied
to a process handle launches a worker in the corresponding stub-veorun
process. Kernel invocations submitted to a thread context are asynchronously
executed by its corresponding worker thread.

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

To install the files to the same location as the original VEO does,
add `-DCMAKE_INSTALL_PREFIX=/opt/nec/ve/veos` when invoking cmake.

## Usage

Link `libveo.so` to your application and invoke VEO API functions. Most VEO
applications should work without any source code modification (see the list of
supported functions). The VE library has to be rebuilt for VH.

By default, `libveo.so` assumes that the `stub-veorun` executable is installed
under `/opt/nec/ve/veos/libexec`. This can be overridden using the environment
variable `VEORUN_BIN=/path/to/stub-veorun`.

To enable verbose logging, set the environment variable `SPDLOG_LEVEL=debug`.
This will dump every message exchanged between the application and
`stub-veorun`.

## Limitations

- veo-stubs is not an emulator. The VE library must be built for VH.
- veo-stubs is not designed for performance. It should be used for functional
  tests only.
- veo-stubs is not thread-safe. VEO API functions should be not simultaneously
  called from multiple threads.

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
