
#include "mod-gpu.h"

#include <vector>
#include <utility>
#include <cstdint>
#include <mutex>

#include "base/logging.h"
#include "threadutil.h"

std::vector<ModQuickPassGPU::FullRun>
ModQuickPassGPU::Run(const std::vector<uint64_t> &primes,
                     const std::vector<std::pair<int, int>> &mns) {

  CHECK(primes.size() == width) << primes.size() << " want " << width;
  CHECK(mns.size() == height) << mns.size() << " want " << height;

  // Only one GPU process at a time.
  MutexLock ml(&m);

  // Run to initialize atomics.
  {
    CHECK_SUCCESS(clSetKernelArg(kernel_init, 0, sizeof (cl_mem),
                                 (void *)&out_sizes_gpu));

    // Simple 1D Kernel
    size_t global_work_offset[] = { (size_t)0 };
    size_t global_work_size[] = { (size_t)height };

    CHECK_SUCCESS(
        clEnqueueNDRangeKernel(cl->queue, kernel_init,
                               // 1D
                               1,
                               // It does its own indexing
                               global_work_offset,
                               global_work_size,
                               // No local work
                               nullptr,
                               // No wait list
                               0, nullptr,
                               // no event
                               nullptr));
    clFinish(cl->queue);
  }

  // Load inputs.
  CopyBufferToGPU(cl->queue, primes, primes_gpu);

  // XXX
  std::vector<int16_t> mns16;
  mns16.reserve(mns.size() * 2);
  for (const auto &[m, n] : mns) {
    mns16.push_back(m);
    mns16.push_back(n);
  }
  CopyBufferToGPU(cl->queue, mns16, mns_gpu);

  // Now the actual work.
  {
    CHECK_SUCCESS(clSetKernelArg(kernel_quick, 0, sizeof (cl_mem),
                                 (void *)&primes_gpu));
    CHECK_SUCCESS(clSetKernelArg(kernel_quick, 1, sizeof (cl_mem),
                                 (void *)&mns_gpu));
    CHECK_SUCCESS(clSetKernelArg(kernel_quick, 2, sizeof (cl_mem),
                                 (void *)&out_gpu));
    CHECK_SUCCESS(clSetKernelArg(kernel_quick, 3, sizeof (cl_mem),
                                 (void *)&out_sizes_gpu));

    // 2D kernel, primes by mns.
    size_t global_work_offset[] = { (size_t)0, 0 };
    size_t global_work_size[] = { (size_t)width, height };

    CHECK_SUCCESS(
        clEnqueueNDRangeKernel(cl->queue, kernel_quick,
                               // 1D
                               2,
                               // It does its own indexing
                               global_work_offset,
                               global_work_size,
                               // No local work
                               nullptr,
                               // No wait list
                               0, nullptr,
                               // no event
                               nullptr));

    clFinish(cl->queue);
  }

  std::vector<uint64_t> out =
    CopyBufferFromGPU<uint64_t>(cl->queue, out_gpu, height * MAX_FULL_RUNS);
  std::vector<uint32_t> out_sizes =
    CopyBufferFromGPU<uint32_t>(cl->queue, out_sizes_gpu, height);

  std::vector<FullRun> full;
  for (int mn_idx = 0; mn_idx < height; mn_idx++) {
    const int num = out_sizes[mn_idx];
    const auto &[m, n] = mns[mn_idx];
    CHECK(num <= MAX_FULL_RUNS) << "This means we found an example that "
      "has more than " << MAX_FULL_RUNS << " primes in the batch for "
      "which it has no solution! We could handle this, or increase the "
      "parameter. (m,n) is " << m << "," << n << " and one prime is "
                                << out[mn_idx * MAX_FULL_RUNS + 0];

    for (int i = 0; i < num; i++) {
      full.push_back(FullRun(m, n, out[mn_idx * MAX_FULL_RUNS +i]));
    }
  }

  return full;
}
