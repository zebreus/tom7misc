
#include "sos-gpu.h"

#include "clutil.h"

// Process the rectangular output of the GPU 'ways' algorithm
// into a CPU-friendly vector of pairs, and optionally check
// that they sum to the right value.
template<bool CHECK_OUTPUT, size_t MAX_WAYS>
inline static std::vector<std::vector<std::pair<uint64_t, uint64_t>>>
ProcessGPUOutput(int height,
                 const std::vector<std::pair<uint64_t, uint32_t>> &inputs,
                 const std::vector<uint64_t> &output_rect,
                 const std::vector<uint32_t> &output_sizes) {

  // XXX verbose flag
  if (false) {
    for (int y = 0; y < height; y++) {
      printf(ABLUE("%llu") " " ACYAN("%d") " size: " APURPLE("%d") "\n",
             inputs[y].first, (int)inputs[y].second, (int)output_sizes[y]);
      for (int x = 0; x < MAX_WAYS; x ++) {
        int rect_base = y * MAX_WAYS * 2;
        CHECK(rect_base < output_rect.size()) <<
          rect_base << " " << output_rect.size();
        uint64_t a = output_rect[rect_base + x * 2 + 0];
        uint64_t b = output_rect[rect_base + x * 2 + 1];
        printf("  %llu^2 + %llu^2 " AGREY("= %llu") "\n",
               a, b, a * a + b * b);
      }
    }
  }

  CHECK((int)output_sizes.size() == height);
  std::vector<std::vector<std::pair<uint64_t, uint64_t>>> ret;
  ret.reserve(height);
  for (int row = 0; row < height; row++) {
    const uint64_t sum = inputs[row].first;
    const int rect_base = row * MAX_WAYS * 2;
    std::vector<std::pair<uint64_t, uint64_t>> one_ret;
    const int size = output_sizes[row];
    CHECK(size % 2 == 0) << "Size is supposed to be incremented by 2 each "
      "time. " << size << " " << sum;
    one_ret.reserve(size / 2);
    for (int i = 0; i < size / 2; i++) {
      uint64_t a = output_rect[rect_base + i * 2 + 0];
      uint64_t b = output_rect[rect_base + i * 2 + 1];
      one_ret.emplace_back(a, b);
    }

    if constexpr (CHECK_OUTPUT) {
      for (const auto &[a, b] : one_ret) {
        CHECK(a * a + b * b == sum) << a << "^2 + " << b << "^2 != "
                                    << sum << "(out size " << size << ")"
                                    << " with height " << height
                                    << "\nWays: " << WaysString(one_ret);
      }
    }

    ret.push_back(std::move(one_ret));
  }
  return ret;
}


std::vector<std::vector<std::pair<uint64_t, uint64_t>>>
// An input is a target sum, with its expected number of ways (use CWW).
// Ways should be > 0. Computation is proportional to the largest sum,
// so this is intended for use with batches of sums that are of similar
// magnitude.
WaysGPU::GetWays(const std::vector<std::pair<uint64_t, uint32_t>> &inputs) {
  TIMER_START(all);
  CHECK(inputs.size() == height) << inputs.size() << " " << height;

  TIMER_START(prep);
  // Rectangular calculation. For the batch, we compute the low and
  // high values, which are the lowest and highest that any sum needs.

  uint64_t minmin = (uint64_t)-1;
  uint64_t maxmax = (uint64_t)0;

  // Make the array of sums, min, and max.
  std::vector<uint64_t> sums;
  sums.reserve(height * 3);
  for (const auto &[num, e] : inputs) {
    uint64_t mx = Sqrt64(num - 2 + 1);
    uint64_t mxmx = mx * mx;
    if (mxmx > num - 2 + 1)
      mx--;

    uint64_t q = num / 2;
    uint64_t r = num % 2;
    uint64_t mn = Sqrt64(q + (r ? 1 : 0));
    if (2 * mn * mn < num) {
      mn++;
    }

    CHECK(e <= MAX_WAYS) << num << " " << e;
    sums.push_back(num);
    sums.push_back(mn);
    sums.push_back(mx);

    minmin = std::min(minmin, mn);
    maxmax = std::max(maxmax, mx);
  }

  // We perform a rectangular calculation. The "x" coordinate of the
  // input is the offset of the trial square, which ranges from
  // 0..width inclusive. The "y" coordinate is the target sum.
  CHECK(minmin <= maxmax) << " " << minmin << " " << maxmax;
  // Inclusive.
  uint64_t width = maxmax - minmin + 1;
  TIMER_END(prep);

  // PERF ideas: I think we spend all the time computing sqrts, for
  // which we need double precision because the numbers are large. (So
  // a faster integer square root would be best here!)
  //
  // But we end up using the same trial squares for all of them, since
  // the calculation is rectangular between minmin/maxmax. Is there
  // some way that we could perform the necessary square roots in a
  // pre-pass? What we actually call sqrt on is target = sum - trialsquare^2,
  // which differs for each sum, so it's not even clear that we redo
  // a lot of work, actually.
  //
  // Another possibility is to compute the minimum and maximum sqrt(target)
  // for the rectangle beforehand, and then just loop over them and square?
  // I think the thing about this is that we end up generating a lot of sums
  // that aren't interesting to us.

  /*
  CHECK(!inputs.empty());
  printf(ACYAN("%llu") " height %d, Width: %llu, minmin %llu maxmax %llu\n",
         inputs[0].first,
         height, width, minmin, maxmax);
  */

  std::vector<uint64_t> output_rect;
  std::vector<uint32_t> output_sizes;

  {
    // Only one GPU process at a time.
    TIMER_START(ml);
    MutexLock ml(&m);
    TIMER_END(ml);

    TIMER_START(input);
    // PERF no clFinish
    CopyBufferToGPU(cl->queue, sums, sums_gpu);
    TIMER_END(input);

    TIMER_START(clear);
    uint64_t SENTINEL = -1;
    CHECK_SUCCESS(
        clEnqueueFillBuffer(cl->queue,
                            output_gpu,
                            // pattern and its size in bytes
                            &SENTINEL, sizeof (uint64_t),
                            // offset and size to fill (in BYTES)
                            0, (size_t)(MAX_WAYS * 2 * height *
                                        sizeof (uint64_t)),
                            // no wait list or event
                            0, nullptr, nullptr));
    TIMER_END(clear);

    // Kernel 1.
    {
      TIMER_START(args1);
      CHECK_SUCCESS(clSetKernelArg(kernel1, 0, sizeof (cl_mem),
                                   (void *)&sums_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel1, 1, sizeof (cl_mem),
                                   (void *)&output_size_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel1, 2, sizeof (cl_mem),
                                   (void *)&output_gpu));
      TIMER_END(args1);

      // PerfectSquares is a 1D kernel.
      size_t global_work_offset[] = { (size_t)0 };
      // Height: Number of sums.
      size_t global_work_size[] = { (size_t)height };

      TIMER_START(kernel1);
      CHECK_SUCCESS(
          clEnqueueNDRangeKernel(cl->queue, kernel1,
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
      // PERF: No wait
      clFinish(cl->queue);
      TIMER_END(kernel1);
    }

    // Kernel 2.
    {
      TIMER_START(args2);
      CHECK_SUCCESS(clSetKernelArg(kernel2, 0, sizeof (uint64_t),
                                   (void *)&minmin));
      CHECK_SUCCESS(clSetKernelArg(kernel2, 1, sizeof (cl_mem),
                                   (void *)&sums_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel2, 2, sizeof (cl_mem),
                                   (void *)&output_size_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel2, 3, sizeof (cl_mem),
                                   (void *)&output_gpu));
      TIMER_END(args2);

      size_t global_work_offset[] = { (size_t)0, (size_t)0 };

      //   Width: Nonnegative offsets from minmin that cover all of
      //     the (min-max) spans (inclusive) for the rectangle.
      //   Height: Number of sums.
      // (Transpose seems much faster; not sure why.)

      #if TRANSPOSE
      size_t global_work_size[] = { (size_t)height, (size_t)width };
      #else
      size_t global_work_size[] = { (size_t)width, (size_t)height };
      #endif

      TIMER_START(kernel2);
      CHECK_SUCCESS(
          clEnqueueNDRangeKernel(cl->queue, kernel2,
                                 // 2D
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
      TIMER_END(kernel2);
    }

    TIMER_START(read);
    output_sizes =
      CopyBufferFromGPU<uint32_t>(cl->queue, output_size_gpu, height);
    output_rect =
      CopyBufferFromGPU<uint64_t>(cl->queue, output_gpu,
                                  height * MAX_WAYS * 2);
    TIMER_END(read);
  }
  // Done with GPU.

  TIMER_START(ret);
  auto ret = ProcessGPUOutput<CHECK_OUTPUT, MAX_WAYS>(
      height, inputs, output_rect, output_sizes);
  TIMER_END(ret);

  TIMER_END(all);
  return ret;
}

// An input is a target sum, with its expected number of ways (use CWW).
// Ways should be > 0. Computation is proportional to the largest sum,
// so this is intended for use with batches of sums that are of similar
// magnitude.
std::vector<std::vector<std::pair<uint64_t, uint64_t>>>
WaysGPUMerge::GetWays(
    const std::vector<std::pair<uint64_t, uint32_t>> &inputs) {
  TIMER_START(all);
  CHECK(inputs.size() == height) << inputs.size() << " " << height;

  TIMER_START(prep);

  // Make the array of sums.
  std::vector<uint64_t> sums;
  sums.reserve(height);
  for (const auto &[num, e] : inputs) {
    CHECK(e <= MAX_WAYS) << num << " " << e;
    sums.push_back(num);
  }
  TIMER_END(prep);

  std::vector<uint64_t> output_rect;
  std::vector<uint32_t> output_sizes;

  {
    // Only one GPU process at a time.
    TIMER_START(ml);
    MutexLock ml(&m);
    TIMER_END(ml);

    TIMER_START(input);
    // PERF no clFinish
    CopyBufferToGPU(cl->queue, sums, sums_gpu);
    TIMER_END(input);

    // Run kernel.
    {
      TIMER_START(args2);
      CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                   (void *)&sums_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                   (void *)&output_size_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                   (void *)&output_gpu));
      TIMER_END(args2);

      // Simple 1D Kernel
      size_t global_work_offset[] = { (size_t)0 };
      size_t global_work_size[] = { (size_t)height };

      TIMER_START(kernel2);
      CHECK_SUCCESS(
          clEnqueueNDRangeKernel(cl->queue, kernel,
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
      TIMER_END(kernel2);
    }

    TIMER_START(read);
    output_sizes =
      CopyBufferFromGPU<uint32_t>(cl->queue, output_size_gpu, height);
    output_rect =
      CopyBufferFromGPU<uint64_t>(cl->queue, output_gpu,
                                  height * MAX_WAYS * 2);
    TIMER_END(read);
  }
  // Done with GPU.

  TIMER_START(ret);
  auto ret = ProcessGPUOutput<CHECK_OUTPUT, MAX_WAYS>(
      height, inputs, output_rect, output_sizes);
  TIMER_END(ret);

  TIMER_END(all);
  return ret;
}

// Sets *rejected_f to the number of squares (not rows) that were
// filtered. Returns the rows that were kept.
std::vector<TryMe>
TryFilterGPU::FilterWays(std::vector<TryMe> &input,
                         uint64_t *rejected_f) {

  CHECK(input.size() == height) << input.size() << " " << height;

  // Make the input arrays.
  std::vector<uint64_t> ways;
  ways.reserve(height * MAX_WAYS * 2);
  std::vector<uint32_t> ways_size;
  ways_size.reserve(height);

  for (const TryMe &tryme : input) {
    CHECK(tryme.squareways.size() <= MAX_WAYS) << tryme.squareways.size();
    ways_size.push_back(tryme.squareways.size() * 2);
    for (int i = 0; i < MAX_WAYS; i++) {
      if (i < tryme.squareways.size()) {
        const auto &[a, b] = tryme.squareways[i];
        // PERF compare using kernel
        ways.push_back(a * a);
        ways.push_back(b * b);
      } else {
        ways.push_back(0);
        ways.push_back(0);
      }
    }
  }

  std::vector<uint32_t> rejected;

  {
    // Only one GPU process at a time.
    MutexLock ml(&m);

    CopyBufferToGPU(cl->queue, ways, ways_gpu);
    CopyBufferToGPU(cl->queue, ways_size, ways_size_gpu);

    // Run kernel.
    {
      CHECK_SUCCESS(clSetKernelArg(kernel2, 0, sizeof (cl_mem),
                                   (void *)&ways_size_gpu));

      CHECK_SUCCESS(clSetKernelArg(kernel2, 1, sizeof (cl_mem),
                                   (void *)&ways_gpu));
      CHECK_SUCCESS(clSetKernelArg(kernel2, 2, sizeof (cl_mem),
                                   (void *)&rejected_gpu));

      // Simple 1D Kernel
      // PERF: Might help to factor out one or more of the loops in
      // the kernel...
      size_t global_work_offset[] = { (size_t)0 };
      size_t global_work_size[] = { (size_t)height };

      CHECK_SUCCESS(
          clEnqueueNDRangeKernel(cl->queue, kernel2,
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

    rejected =
      CopyBufferFromGPU<uint32_t>(cl->queue, rejected_gpu, height);
  }
  // Done with GPU.

  uint64_t rejected_counter = 0;
  std::vector<TryMe> out;
  out.reserve(height);
  for (int row = 0; row < height; row++) {
    if (rejected[row] == 0) {
      // Keep it. Since the order is the same as the input, the
      // simplest thing is to just copy the input row.
      out.push_back(input[row]);
    } else {
      // Filter out, but accumulate counter.
      rejected_counter += rejected[row];
    }
  }

  *rejected_f = rejected_counter;
  return out;
}


// Processes a batch of numbers (size height).
// Returns a dense array of factors (MAX_FACTORS x height)
// with the count of factors per input.
std::pair<std::vector<uint64_t>,
          std::vector<uint8>>
FactorizeGPU::Factorize(const std::vector<uint64_t> &nums) {
  // Only one GPU process at a time.
  MutexLock ml(&m);

  CHECK(nums.size() == height);

  // Run kernel.
  {

    CopyBufferToGPU(cl->queue, nums, nums_gpu);

    CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                 (void *)&nums_gpu));

    CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                 (void *)&out_gpu));

    CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                 (void *)&out_size_gpu));


    // Simple 1D Kernel
    size_t global_work_offset[] = { (size_t)0 };
    size_t global_work_size[] = { (size_t)height };

    CHECK_SUCCESS(
        clEnqueueNDRangeKernel(cl->queue, kernel,
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

  return make_pair(
      CopyBufferFromGPU<uint64_t>(cl->queue, out_gpu, MAX_FACTORS * height),
      CopyBufferFromGPU<uint8_t>(cl->queue, out_size_gpu, height));
}
