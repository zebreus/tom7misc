// OpenCL kernel for Nopert #229 proof search.
// Batch-evaluates 5D boxes (Cayley chart + projective view triangle) against
// candidate Farkas contact triples to certify that inner polyhedron cannot be
// contained within outer polyhedron.
//
// Evaluates exact 27-point Bernstein control bounds for the 10-coefficient
// quadratic displacement polynomial across each box.

#define MAX_CONTACTS 64
#define MAX_TRIPLES 128

typedef struct {
  // Cayley box center
  double cx, cy, cz;
  // Cayley box radii
  double rx, ry, rz;
  // Projective view triangle corners
  double tri[3][3];
  // Cayley chart index (0, 1, 2)
  int chart;
  // Number of candidate triples to test
  int num_triples;
  // Offset in global triples array
  int triple_offset;
} GpuBox;

typedef struct {
  int vertex;
  double edge[3];
  double defect;
} GpuContact;

typedef struct {
  // Contact indices
  int c0, c1, c2;
  // Precomputed cross-product coefficients
  double w_coeff[3][3];
} GpuTriple;

typedef struct {
  // 1 if box is proved non-Rupert, 0 otherwise
  int certified;
  // Winning candidate triple index
  int winning_triple;
  // Winning inner contact vertices
  int inner[3];
  // Certified positive margin
  double margin;
} GpuResult;

inline double3 Cross(double3 a, double3 b) {
  return (double3)(
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x
  );
}

inline double Dot(double3 a, double3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Evaluate a batch of boxes against their candidate triples using
// Bernstein control points.
__kernel void EvaluateBoxes(
    __global const GpuBox *restrict boxes,
    __global const GpuContact *restrict contacts,
    __global const GpuTriple *restrict triples,
    __global GpuResult *restrict results,
    const int num_boxes) {

  const int gid = get_global_id(0);
  if (gid >= num_boxes) return;

  const GpuBox box = boxes[gid];

  double x0 = box.cx;
  double y0 = box.cy;
  double z0 = box.cz;

  // Chart signs
  double sx = (box.chart == 2 || box.chart == 3) ? -1.0 : 1.0;
  double sy = (box.chart == 1 || box.chart == 3) ? -1.0 : 1.0;
  double sz = (box.chart == 1 || box.chart == 2) ? -1.0 : 1.0;

  double3 view_center = (double3)(
    (box.tri[0][0] + box.tri[1][0] + box.tri[2][0]) / 3.0,
    (box.tri[0][1] + box.tri[1][1] + box.tri[2][1]) / 3.0,
    (box.tri[0][2] + box.tri[1][2] + box.tri[2][2]) / 3.0
  );

  double ex = fmax(fabs(box.cx - box.rx), fabs(box.cx + box.rx));
  double ey = fmax(fabs(box.cy - box.ry), fabs(box.cy + box.ry));
  double ez = fmax(fabs(box.cz - box.rz), fabs(box.cz + box.rz));
  double d_bound = 1.0 + ex*ex + ey*ey + ez*ez;

  GpuResult res;
  res.certified = 0;
  res.winning_triple = -1;
  res.inner[0] = 0;
  res.inner[1] = 0;
  res.inner[2] = 0;
  res.margin = -1e30;

  // Cayley rotation matrix numerator at center.
  double num[3][3] = {
    {1.0 + x0*x0 - y0*y0 - z0*z0, 2.0*(x0*y0 - z0), 2.0*(x0*z0 + y0)},
    {2.0*(x0*y0 + z0), 1.0 - x0*x0 + y0*y0 - z0*z0, 2.0*(y0*z0 - x0)},
    {2.0*(x0*z0 - y0), 2.0*(y0*z0 + x0), 1.0 - x0*x0 - y0*y0 + z0*z0}
  };
  double denom0 = 1.0 + x0*x0 + y0*y0 + z0*z0;

  // Test candidate triples assigned to this box.
  for (int t = 0; t < box.num_triples; t++) {
    const GpuTriple triple = triples[box.triple_offset + t];

    const GpuContact c0 = contacts[triple.c0];
    const GpuContact c1 = contacts[triple.c1];
    const GpuContact c2 = contacts[triple.c2];

    double3 edge0 = (double3)(c0.edge[0], c0.edge[1], c0.edge[2]);
    double3 edge1 = (double3)(c1.edge[0], c1.edge[1], c1.edge[2]);
    double3 edge2 = (double3)(c2.edge[0], c2.edge[1], c2.edge[2]);

    double w0 = Dot(view_center, Cross(edge1, edge2));
    double w1 = Dot(view_center, Cross(edge2, edge0));
    double w2 = Dot(view_center, Cross(edge0, edge1));

    // Must strictly contain the origin in dual projective view
    if (w0 <= 1e-9 || w1 <= 1e-9 || w2 <= 1e-9) continue;

    // Evaluate best inner vertex for each of the 3 contacts (at center)
    double best_val0 = -1e30, best_val1 = -1e30, best_val2 = -1e30;
    int best_in0 = 0, best_in1 = 0, best_in2 = 0;

    double3 out0 = (double3)(VERTICES[c0.vertex][0],
                             VERTICES[c0.vertex][1],
                             VERTICES[c0.vertex][2]);
    double3 out1 = (double3)(VERTICES[c1.vertex][0],
                             VERTICES[c1.vertex][1],
                             VERTICES[c1.vertex][2]);
    double3 out2 = (double3)(VERTICES[c2.vertex][0],
                             VERTICES[c2.vertex][1],
                             VERTICES[c2.vertex][2]);

    for (int k = 0; k < NUM_VERTICES; k++) {
      double3 vin = (double3)(VERTICES[k][0], VERTICES[k][1], VERTICES[k][2]);
      double3 rot_in = (double3)(
        sx * (num[0][0]*vin.x + num[0][1]*vin.y + num[0][2]*vin.z),
        sy * (num[1][0]*vin.x + num[1][1]*vin.y + num[1][2]*vin.z),
        sz * (num[2][0]*vin.x + num[2][1]*vin.y + num[2][2]*vin.z)
      );

      double3 disp0 = rot_in - denom0 * out0;
      double v0 = Dot(view_center, Cross(edge0, disp0));
      if (v0 > best_val0) { best_val0 = v0; best_in0 = k; }

      double3 disp1 = rot_in - denom0 * out1;
      double v1 = Dot(view_center, Cross(edge1, disp1));
      if (v1 > best_val1) { best_val1 = v1; best_in1 = k; }

      double3 disp2 = rot_in - denom0 * out2;
      double v2 = Dot(view_center, Cross(edge2, disp2));
      if (v2 > best_val2) { best_val2 = v2; best_in2 = k; }
    }

    // Now compute the 10 quadratic coefficients of the combined
    // displacement polynomial for this triple: C[m] = w0*P0[m] +
    // w1*P1[m] + w2*P2[m]
    double3 u0 = Cross(view_center, edge0);
    double3 u1 = Cross(view_center, edge1);
    double3 u2 = Cross(view_center, edge2);

    int in_idx[3] = {best_in0, best_in1, best_in2};
    int out_idx[3] = {c0.vertex, c1.vertex, c2.vertex};
    double3 u_vec[3] = {u0, u1, u2};
    double w_vec[3] = {w0, w1, w2};

    double C[10] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    for (int c_idx = 0; c_idx < 3; c_idx++) {
      int in_k = in_idx[c_idx];
      int out_k = out_idx[c_idx];
      double3 vin = (double3)(VERTICES[in_k][0],
                              VERTICES[in_k][1],
                              VERTICES[in_k][2]);
      double3 vout = (double3)(VERTICES[out_k][0],
                               VERTICES[out_k][1],
                               VERTICES[out_k][2]);
      double3 u = u_vec[c_idx];
      double weight = w_vec[c_idx];

      // D_c[m]: displacement vector polynomial components for
      // coordinate c in 0..2.
      // Monomial order: 1, x, y, z, x^2, xy, xz, y^2, yz, z^2
      double D0[10] = {
        sx*vin.x - vout.x,
        0.0,
        2.0*sx*vin.z,
        -2.0*sx*vin.y,
        sx*vin.x - vout.x,
        2.0*sx*vin.y,
        2.0*sx*vin.z,
        -sx*vin.x - vout.x,
        0.0,
        -sx*vin.x - vout.x
      };

      double D1[10] = {
        sy*vin.y - vout.y,
        -2.0*sy*vin.z,
        0.0,
        2.0*sy*vin.x,
        -sy*vin.y - vout.y,
        2.0*sy*vin.x,
        0.0,
        sy*vin.y - vout.y,
        2.0*sy*vin.z,
        -sy*vin.y - vout.y
      };

      double D2[10] = {
        sz*vin.z - vout.z,
        2.0*sz*vin.y,
        -2.0*sz*vin.x,
        0.0,
        -sz*vin.z - vout.z,
        0.0,
        2.0*sz*vin.x,
        -sz*vin.z - vout.z,
        2.0*sz*vin.y,
        sz*vin.z - vout.z
      };

      for (int m = 0; m < 10; m++) {
        double P_m = u.x * D0[m] + u.y * D1[m] + u.z * D2[m];
        C[m] += weight * P_m;
      }
    }

    // Bernstein 27-point control evaluation on the Cayley box
    // [cx-rx, cx+rx] x ...
    double lx = box.cx - box.rx, ly = box.cy - box.ry, lz = box.cz - box.rz;
    double wx = 2.0 * box.rx, wy = 2.0 * box.ry, wz = 2.0 * box.rz;

    double a0 =
      (C[0] + C[1]*lx + C[2]*ly + C[3]*lz + C[4]*lx*lx +
       C[5]*lx*ly + C[6]*lx*lz + C[7]*ly*ly + C[8]*ly*lz + C[9]*lz*lz);
    double ax = wx * (C[1] + 2.0*C[4]*lx + C[5]*ly + C[6]*lz);
    double ay = wy * (C[2] + C[5]*lx + 2.0*C[7]*ly + C[8]*lz);
    double az = wz * (C[3] + C[6]*lx + C[8]*ly + 2.0*C[9]*lz);
    double axx = C[4]*wx*wx, ayy = C[7]*wy*wy, azz = C[9]*wz*wz;
    double axy = C[5]*wx*wy, axz = C[6]*wx*wz, ayz = C[8]*wy*wz;

    double min_b = 1e30;
    for (int bi = 0; bi <= 2; bi++) {
      double ti = 0.5 * bi * ax + (bi == 2 ? axx : 0.0);
      for (int bj = 0; bj <= 2; bj++) {
        double tj = ti + 0.5 * bj * ay + (bj == 2 ? ayy : 0.0) +
          0.25 * bi * bj * axy;
        for (int bk = 0; bk <= 2; bk++) {
          double val = a0 + tj + 0.5 * bk * az + (bk == 2 ? azz : 0.0) +
            0.25 * bk * (bi * axz + bj * ayz);
          if (val < min_b) min_b = val;
        }
      }
    }

    double defect_penalty = d_bound *
      (w0 * c0.defect + w1 * c1.defect + w2 * c2.defect);
    double margin = min_b - defect_penalty - 1e-9 * d_bound;

    if (margin > 0.0) {
      // Certified!
      res.certified = 1;
      res.winning_triple = t;
      res.inner[0] = best_in0;
      res.inner[1] = best_in1;
      res.inner[2] = best_in2;
      res.margin = margin;
      break;
    }
  }

  results[gid] = res;
}
