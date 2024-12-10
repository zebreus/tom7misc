
#include <CL/cl.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/stringprintf.h"
#include "arcfour.h"

#include "opencl/clutil.h"
#include "polyhedra.h"
#include "rendering.h"
#include "util.h"

static CL *cl = nullptr;

struct RupertGPU {
  // Step size when estimating derivative numerically.
  static constexpr double PARAM_H = 1.0e-7;

  // Width is the number of problem instances. Note that each instance
  // is a candidate rotation (quaternion) for the inner and outer
  // polyhedra, each combined with four differential quaternions (one
  // parameter tweaked). So the number of quaternions is 2 + 8 = 10
  // times the number of problem instances.
  RupertGPU(const Polyhedron &poly,
            int width) : rc(StringPrintf("gpu.%lld", time(nullptr))),
                         poly(poly),
                         num_vertices(poly.vertices.size()),
                         num_triangles(poly.faces->triangulation.size()),
                         width(width),
                         num_quaternions(width * 5) {
    CHECK(width > 0);

    std::string defines =
      StringPrintf("#define NUM_VERTICES %d\n"
                   "#define NUM_TRIANGLES %d\n"
                   "#define PARAM_H %.17g\n",
                   num_vertices,
                   num_triangles,
                   PARAM_H);

    std::string kernel_src =
      defines +
      Util::ReadFile("gpuperts.cl");
    const auto &[prg, kernels] = cl->BuildKernels(
        kernel_src,
        {"RotateAndProject",
         "TweakQuaternions",
         "ComputeError",
         "CheckForSolution",
         "GradientDescent",
        },
        1);
    program = prg;

    auto RequireKernel = [&kernels](const char *name) -> cl_kernel {
        auto it = kernels.find(name);
        CHECK(it != kernels.end()) << name;
        return it->second;
      };

    rot_kernel = RequireKernel("RotateAndProject");
    tweak_kernel = RequireKernel("TweakQuaternions");
    err_kernel = RequireKernel("ComputeError");
    check_kernel = RequireKernel("CheckForSolution");
    grad_kernel = RequireKernel("GradientDescent");

    std::vector<double> vd;
    for (const vec3 &v : poly.vertices) {
      vd.push_back(v.x);
      vd.push_back(v.y);
      vd.push_back(v.z);
    }
    vertices = CopyMemoryToGPU<double>(cl->context, cl->queue, vd,
                                       // readonly
                                       true);

    std::vector<int> td;
    for (const auto &[a, b, c] : poly.faces->triangulation) {
      td.push_back(a);
      td.push_back(b);
      td.push_back(c);
    }
    triangles = CopyMemoryToGPU<int>(cl->context, cl->queue, td,
                                     // readonly
                                     true);

    inner_quats = CreateUninitializedGPUMemory<double>(
        cl->context, num_quaternions * 4);
    outer_quats = CreateUninitializedGPUMemory<double>(
        cl->context, num_quaternions * 4);
    translate = CreateUninitializedGPUMemory<double>(cl->context, width * 2);

    // XXX implement translation
    ZeroGPUMemory<double>(cl->queue, translate, width);

    outer_vertices = CreateUninitializedGPUMemory<double>(
        cl->context, num_vertices * num_quaternions * 2);
    inner_vertices = CreateUninitializedGPUMemory<double>(
        cl->context, num_vertices * num_quaternions * 2);

    error_values = CreateUninitializedGPUMemory<double>(
        cl->context, width * 9);

    std::vector<int32_t> sol;
    sol.push_back(std::numeric_limits<int32_t>::max());
    solutions = CopyMemoryToGPU<int32_t>(cl->context, cl->queue, sol, false);

    clFinish(cl->queue);
    printf("Finished initialization.\n");
  }

  void InitializeQuats(cl_mem quats_gpu) {
    std::vector<double> quats(width * 4 * 5);
    for (int i = 0; i < width; i++) {
      // The main quaternion.
      // The four variants are left uninitialized!
      quat4 initial_rot = RandomQuaternion(&rc);
      quats[i * 4 * 5 + 0] = initial_rot.x;
      quats[i * 4 * 5 + 1] = initial_rot.y;
      quats[i * 4 * 5 + 2] = initial_rot.z;
      quats[i * 4 * 5 + 3] = initial_rot.w;
    }

    CopyBufferToGPU(cl->queue, quats, quats_gpu);
    clFinish(cl->queue);
  }

  // Each quaternion comes with four additional copies that tweak one of the
  // parameters. We use these to estimate the derivative numerically. This
  // copies the first of the group of five to the other four, and adds the
  // difference H to each parameter.
  void TweakQuaternions(cl_mem quats) {
    CHECK_SUCCESS(
        clSetKernelArg(tweak_kernel, 0, sizeof(cl_mem), (void *)&quats));

    size_t global_work_offset[] = {(size_t)0};
    // One per configuration.
    size_t global_work_size[] = {(size_t)width};

    CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, tweak_kernel,
                                         // 1D
                                         1,
                                         // It does its own indexing
                                         global_work_offset, global_work_size,
                                         // No local work
                                         nullptr,
                                         // No wait list
                                         0, nullptr,
                                         // no event
                                         nullptr));

    clFinish(cl->queue);
  };

  // Transform the polyhedron's vertices (shared) using each quat to make
  // the outer projection and inner projection. This gives us the 2D
  // coordinates.
  void RotateQuat(cl_mem quats, cl_mem vertices_out) {
    CHECK_SUCCESS(
        clSetKernelArg(rot_kernel, 0, sizeof(cl_mem), (void *)&quats));
    CHECK_SUCCESS(
        clSetKernelArg(rot_kernel, 1, sizeof(cl_mem), (void *)&vertices));
    CHECK_SUCCESS(
        clSetKernelArg(rot_kernel, 2, sizeof(cl_mem), (void *)&vertices_out));

    // Simple 1D Kernel
    size_t global_work_offset[] = {(size_t)0};
    size_t global_work_size[] = {(size_t)num_quaternions};

    CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, rot_kernel,
                                         // 1D
                                         1,
                                         // It does its own indexing
                                         global_work_offset, global_work_size,
                                         // No local work
                                         nullptr,
                                         // No wait list
                                         0, nullptr,
                                         // no event
                                         nullptr));

    clFinish(cl->queue);
  }

  void GetError() {
    CHECK_SUCCESS(
        clSetKernelArg(err_kernel, 0, sizeof(cl_mem), (void *)&triangles));
    CHECK_SUCCESS(
        clSetKernelArg(err_kernel, 1, sizeof(cl_mem), (void *)&outer_vertices));
    CHECK_SUCCESS(
        clSetKernelArg(err_kernel, 2, sizeof(cl_mem), (void *)&inner_vertices));
    CHECK_SUCCESS(
        clSetKernelArg(err_kernel, 3, sizeof(cl_mem), (void *)&error_values));

    size_t global_work_offset[] = {(size_t)0};
    // Compute the error for a perturbed configuration (or the main one).
    size_t global_work_size[] = {(size_t)width * 9};

    CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, err_kernel,
                                         // 1D
                                         1,
                                         // It does its own indexing
                                         global_work_offset, global_work_size,
                                         // No local work
                                         nullptr,
                                         // No wait list
                                         0, nullptr,
                                         // no event
                                         nullptr));
    clFinish(cl->queue);
  }

  std::optional<std::pair<quat4, quat4>> GetSolution() {
    CHECK_SUCCESS(
        clSetKernelArg(check_kernel, 0, sizeof(cl_mem), (void *)&error_values));
    CHECK_SUCCESS(
        clSetKernelArg(check_kernel, 1, sizeof(cl_mem), (void *)&solutions));

    size_t global_work_offset[] = {(size_t)0};
    // We could actually check the perturbed versions for zero error too,
    // but this adds a lot of complexity here and it would only be dumb
    // luck if one of them worked (and we weren't able to subsequently
    // find it).
    size_t global_work_size[] = {(size_t)width};

    CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, check_kernel,
                                         // 1D
                                         1,
                                         // It does its own indexing
                                         global_work_offset, global_work_size,
                                         // No local work
                                         nullptr,
                                         // No wait list
                                         0, nullptr,
                                         // no event
                                         nullptr));
    clFinish(cl->queue);

    std::vector<int32_t> sols = CopyBufferFromGPU<int32_t>(cl->queue,
                                                           solutions,
                                                           1);
    if (sols[0] < std::numeric_limits<int32_t>::max()) {
      int idx = sols[0];
      printf("Success at index %d! Reading solution...\n", idx);
      std::vector<double> oq = CopyBufferFromGPU<double>(cl->queue,
                                                         outer_quats,
                                                         num_quaternions);
      std::vector<double> iq = CopyBufferFromGPU<double>(cl->queue,
                                                         inner_quats,
                                                         num_quaternions);

      auto GetQuat = [idx](const std::vector<double> &qs) -> quat4 {
          CHECK(idx >= 0 && idx * 5 + 4 < qs.size());
          return quat4(qs[idx * 5 + 0],
                       qs[idx * 5 + 1],
                       qs[idx * 5 + 2],
                       qs[idx * 5 + 3]);
        };

      return std::make_pair(GetQuat(oq), GetQuat(iq));

    } else {
      return std::nullopt;
    }
  }

  void GradientDescent() {
    CHECK_SUCCESS(
        clSetKernelArg(grad_kernel, 0, sizeof(cl_mem), (void *)&outer_quats));
    CHECK_SUCCESS(
        clSetKernelArg(grad_kernel, 1, sizeof(cl_mem), (void *)&inner_quats));
    CHECK_SUCCESS(
        clSetKernelArg(grad_kernel, 2, sizeof(cl_mem), (void *)&error_values));

    size_t global_work_offset[] = {(size_t)0};
    size_t global_work_size[] = {(size_t)width};

    CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, grad_kernel,
                                         // 1D
                                         1,
                                         // It does its own indexing
                                         global_work_offset, global_work_size,
                                         // No local work
                                         nullptr,
                                         // No wait list
                                         0, nullptr,
                                         // no event
                                         nullptr));
    clFinish(cl->queue);
  }

  void Run() {
    // The slow part here is the optimization, and it is inherently
    // serial. So we just do a lot of them at once. Generate random
    // initial orientations:

    for (;;) {
      InitializeQuats(outer_quats);
      InitializeQuats(inner_quats);

      // Now, repeatedly:

      for (int iter = 0; iter < 1000; iter++) {
        printf("Start iter %d.\n", iter);
        // Entering the loop, we have the current configuration in the
        // first of the five quaternions for each configuration; the
        // rest are garbage. Fill the next four with the samples to
        // estimate the derivative for each parameter.
        TweakQuaternions(outer_quats);
        TweakQuaternions(inner_quats);

        RotateQuat(outer_quats, outer_vertices);
        RotateQuat(inner_quats, inner_vertices);


        // TODO: Apply the translation to the inner projection. (Or make it part
        // of the previous.)

        // Get the error for each vertex in the inner mesh. This is the
        // min over all triangles in the triangulation, interpreted in
        // the outer vertices. We are performing numeric differentiation
        // so we only need the error value, but note that we get 9 error
        // values per configuration (the "main" one and one for each
        // perturbed parameter)!
        GetError();

        if (auto so = GetSolution()) {
          const auto &[oq, iq] = so.value();

          printf("Solution found!\n"
                 "outer:\n"
                 "%s\n"
                 "inner:\n"
                 "%s\n",
                 QuatString(oq).c_str(),
                 QuatString(iq).c_str());

          Rendering rendering(poly, 1920, 1080);
          Polyhedron outer = Rotate(poly, oq);
          Polyhedron inner = Rotate(poly, iq);
          Mesh2D souter = Shadow(outer);
          Mesh2D sinner = Shadow(inner);
          rendering.RenderMesh(souter);
          rendering.DarkenBG();
          rendering.RenderMesh(sinner);
          rendering.RenderBadPoints(sinner, souter);
          rendering.Save(StringPrintf("%s-gpu-solved.png", poly.name));
          return;
        }

        // Perform gradient descent, updating the parameters.
        GradientDescent();
      }
    }
  }

  ~RupertGPU() {
    CHECK_SUCCESS(clReleaseKernel(rot_kernel));
    CHECK_SUCCESS(clReleaseKernel(tweak_kernel));
    CHECK_SUCCESS(clReleaseKernel(err_kernel));
    CHECK_SUCCESS(clReleaseKernel(check_kernel));
    CHECK_SUCCESS(clReleaseKernel(grad_kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(vertices));
    CHECK_SUCCESS(clReleaseMemObject(triangles));

    CHECK_SUCCESS(clReleaseMemObject(outer_quats));
    CHECK_SUCCESS(clReleaseMemObject(inner_quats));
    CHECK_SUCCESS(clReleaseMemObject(translate));

    CHECK_SUCCESS(clReleaseMemObject(outer_vertices));
    CHECK_SUCCESS(clReleaseMemObject(inner_vertices));
    CHECK_SUCCESS(clReleaseMemObject(error_values));
    CHECK_SUCCESS(clReleaseMemObject(solutions));
  }

  ArcFour rc;
  const Polyhedron &poly;
  const int num_vertices = 0;
  const int num_triangles = 0;
  const int width = 0;
  // number of quaternions in each of the outer_quats and inner_quats arrays.
  const int num_quaternions = 0;

  cl_program program;
  cl_kernel rot_kernel;
  cl_kernel tweak_kernel;
  cl_kernel err_kernel;
  cl_kernel check_kernel;
  cl_kernel grad_kernel;

  // size num_vertices * 3. The polyhedron in its original orientation.
  cl_mem vertices;
  // The triangulation of its faces, as triples of vertex indices.
  // size num_triangles * 3.
  cl_mem triangles;

  // These are the optimization parameters. They should be normalized
  // quaternions. size width * 4.
  cl_mem outer_quats;
  cl_mem inner_quats;
  // Translation in xy plane. size width * 2.
  cl_mem translate;

  // TODO: derivatives

  // Temporary storage. Projected to 2D. Size width * num_vertices * 2.
  cl_mem outer_vertices;
  cl_mem inner_vertices;

  // Error values. There are 9 error values per configuration.
  cl_mem error_values;

  cl_mem solutions;
};

static void Run() {
  Polyhedron target = SnubCube();
  RupertGPU gpupert(target, 10000);

  gpupert.Run();
  printf("Run done\n");

  delete target.faces;
  target.faces = nullptr;
}

int main(int argc, char **argv) {
  ANSI::Init();

  cl = new CL;
  Run();

  printf("Delete CL..\n");
  delete cl;
  return 0;
}
