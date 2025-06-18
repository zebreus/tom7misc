
// All deprecated.
// Just use the one in cc-lib/opencl directly.

#ifndef _LOWERCASE_CLUTIL_ADAPTOR_H
#define _LOWERCASE_CLUTIL_ADAPTOR_H

#include <CL/cl.h>
#include <CL/cl_platform.h>

#include <cstdint>
#include <cstdio>
#include <stdlib.h>
#include <string>
#include <utility>
#include <vector>

#include "opencl/clutil.h"

using uint8 = uint8_t;
// Better compatibility with CL.
using uchar = uint8_t;
using uint64 = uint64_t;

// Creates a new buffer on the GPU and copies the memory there. They
// do not alias. Note that the command queue is not flushed, so you
// should not touch the source memory until it is.
template<class T>
inline cl_mem MoveMemoryToGPU(cl_context context, cl_command_queue cmd,
                              bool readonly, std::vector<T> *v) {
  return CopyMemoryToGPU(context, cmd, *v, readonly);
}

template<class T>
inline cl_mem MoveMemoryToGPUConst(cl_context context, cl_command_queue cmd,
                                   const std::vector<T> &v) {
  return CopyMemoryToGPU(context, cmd, v, true);
}

#if 0
// Shares with the host memory and we don't control when it gets copied. This is
// quite inefficient.
template<class T>
static cl_mem BufferFromVector(cl_context context, bool readonly,
                               std::vector<T> *v) {
  return clCreateBuffer(context,
                        (readonly ? CL_MEM_READ_ONLY : 0) |
                        CL_MEM_USE_HOST_PTR,
                        sizeof (T) * v->size(),
                        (void *) v->data(),
                        nullptr);
}

// PERF: These are blocking copies; applications may be able to get
// much better throughput by batching...

// Creates a new buffer on the GPU and copies the memory there. They
// do not alias. Note that the command queue is not flushed, so you
// should not touch the source memory until it is.
template<class T>
static cl_mem MoveMemoryToGPU(cl_context context, cl_command_queue cmd,
                              bool readonly, std::vector<T> *v) {
  cl_mem buf = clCreateBuffer(context,
                              (readonly ? CL_MEM_READ_ONLY : 0),
                              sizeof (T) * v->size(),
                              nullptr,
                              nullptr);
  CHECK_SUCCESS(clEnqueueWriteBuffer(cmd, buf, CL_TRUE, 0,
                                     sizeof (T) * v->size(), v->data(), 0,
                                     nullptr, nullptr));
  return buf;
}

// Same, but with a constant vector. Implies read-only.
template<class T>
static cl_mem MoveMemoryToGPUConst(cl_context context, cl_command_queue cmd,
                                   const std::vector<T> &v) {
  cl_mem buf = clCreateBuffer(context,
                              CL_MEM_READ_ONLY,
                              sizeof (T) * v.size(),
                              nullptr,
                              nullptr);
  CHECK_SUCCESS(clEnqueueWriteBuffer(cmd, buf, CL_TRUE, 0,
                                     sizeof (T) * v.size(), v.data(), 0,
                                     nullptr, nullptr));
  return buf;
}

template<class T>
static cl_mem CreateUninitializedGPUMemory(cl_context context, int n_items) {
  return clCreateBuffer(context, 0, sizeof (T) * n_items, nullptr, nullptr);
}

template<class T>
static std::vector<T> CopyBufferFromGPU(cl_command_queue cmd,
                                        cl_mem buf, int n) {
  std::vector<T> vec;
  vec.resize(n);
  CHECK_SUCCESS(clEnqueueReadBuffer(cmd, buf, CL_TRUE, 0, sizeof (T) * n,
                                    vec.data(),
                                    // No wait-list or event.
                                    0, nullptr,
                                    nullptr));
  clFinish(cmd);
  return vec;
}

// Assumes the vector already has the correct size.
template<class T>
static void CopyBufferFromGPUTo(cl_command_queue cmd,
                                cl_mem buf, std::vector<T> *vec) {
  // This would yield an error. We could support empty buffers
  // by just succeeding, I guess?
  CHECK(!vec->empty());
  CHECK_SUCCESS(
      clEnqueueReadBuffer(cmd, buf, CL_TRUE, 0, sizeof (T) * vec->size(),
                          vec->data(),
                          // No wait-list or event.
                          0, nullptr,
                          nullptr));
  clFinish(cmd);
}

template<class T>
static void CopyBufferToGPU(cl_command_queue cmd,
                            const std::vector<T> &vec, cl_mem buf) {
  CHECK_SUCCESS(clEnqueueWriteBuffer(cmd, buf, CL_TRUE, 0,
                                     sizeof (T) * vec.size(), vec.data(), 0,
                                     nullptr, nullptr));
  clFinish(cmd);
}
#endif

#endif
