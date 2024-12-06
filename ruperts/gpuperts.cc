
#include <CL/cl.h>
#include <ctime>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"

#include "opencl/clutil.h"
#include "polyhedra.h"
#include "util.h"

static CL *cl = nullptr;

struct RupertGPU {
  RupertGPU(const Polyhedron &poly,
            int width) : rc(StringPrintf("gpu.%lld", time(nullptr))),
                         poly(poly),
                         num_vertices(poly.vertices.size()),
                         width(width) {
    CHECK(width > 0);

    std::string defines =
      StringPrintf("#define NUM_VERTICES %d\n",
                   num_vertices);

    std::string kernel_src =
      defines +
      Util::ReadFile("gpuperts.cl");
    const auto &[program, kernels] = cl->BuildKernels(
        kernel_src,
        {"RotateAndProject"},
        1);

    auto RequireKernel = [&kernels](const char *name) -> cl_kernel {
        auto it = kernels.find(name);
        CHECK(it != kernels.end()) << name;
        return it->second;
      };

    rot_kernel = RequireKernel("RotateAndProject");
  }

  void InitializeQuats(cl_mem quats_gpu) {
    std::vector<double> quats(width * 4);
    for (int i = 0; i < width; i++) {
      quat4 initial_rot = RandomQuaternion(&rc);
      quats[i * 4 + 0] = initial_rot.x;
      quats[i * 4 + 1] = initial_rot.y;
      quats[i * 4 + 2] = initial_rot.z;
      quats[i * 4 + 3] = initial_rot.w;
    }

    CopyBufferToGPU(cl->queue, quats, quats_gpu);
  }

  void Run() {
    // The slow part here is the optimization, and it is inherently serial.
    // So we just do a lot of them at once. Generate random initial orientations:

    InitializeQuats(outer_quats);
    InitializeQuats(inner_quats);

    // Now, repeatedly:

    for (int iter = 0; iter < 1000; iter++) {
    // Transform vertices using each quat to make the outer projection and
    // inner projection. This gives us the 2D coordinates.
      {
        CHECK_SUCCESS(clSetKernelArg(rot_kernel, 0, sizeof (cl_mem),
                                     (void *)&outer_quats));

        // apply to both outer and inner.
      }

      //
      // Apply the translation to the inner projection.
      //
      // Compute the loss, which is the distance from each vertex on the inner
      // projection to the outer projection's convex hull (or zero if inside).
      // This can also be computed as the minimum distance to any triangle
      // in the outer projection (or zero if inside), which is more
      // parallelizable and possibly easier to implement. (We do have to
      // worry about consistently classifying points very close to a shared
      // edge, however.)
      //
      // Do the same for a few nearby points, to estimate the gradient wrt
      // each parameter.
      //
      // (Of course, if any are successful, succeed!)
      //
      // Perform gradient descent, updating the parameters.

    }
  }

  ~RupertGPU() {
    CHECK_SUCCESS(clReleaseKernel(rot_kernel));
    CHECK_SUCCESS(clReleaseProgram(program));

    CHECK_SUCCESS(clReleaseMemObject(outer_quats));
    CHECK_SUCCESS(clReleaseMemObject(inner_quats));
    // XXX...
  }

  ArcFour rc;
  const Polyhedron &poly;
  const int num_vertices = 0;
  const int width = 0;

  cl_program program;
  cl_kernel rot_kernel;

  // size num_vertices * 3. The polyhedron in its original orientation.
  cl_mem vertices;

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

};


int main(int argc, char **argv) {
  ANSI::Init();

  cl = new CL;


  delete cl;
  return 0;
}
