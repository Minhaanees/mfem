//            MFEM Distance Function Solver - Parallel Version
//
// Compile with: make distance
//
// Sample runs: mpirun -np 4 distance -m ../data/inline-segment.mesh -rs 3 -t 0.5
//              mpirun -np 4 distance -m ../data/inline-quad.mesh -rs 1 -t 0.1
//              mpirun -np 4 distance -m ./cir.msh -t 0.01
//              mpirun -np 4 distance -m ../data/star.mesh
//

// Sample runs:  mpirun -np 4 ex1p -m ../data/square-disc.mesh
//               mpirun -np 4 ex1p -m ../data/star.mesh
//               mpirun -np 4 ex1p -m ../data/star-mixed.mesh
//               mpirun -np 4 ex1p -m ../data/escher.mesh
//               mpirun -np 4 ex1p -m ../data/fichera.mesh
//               mpirun -np 4 ex1p -m ../data/fichera-mixed.mesh
//               mpirun -np 4 ex1p -m ../data/toroid-wedge.mesh
//               mpirun -np 4 ex1p -m ../data/periodic-annulus-sector.msh
//               mpirun -np 4 ex1p -m ../data/periodic-torus-sector.msh
//               mpirun -np 4 ex1p -m ../data/square-disc-p2.vtk -o 2
//               mpirun -np 4 ex1p -m ../data/square-disc-p3.mesh -o 3
//               mpirun -np 4 ex1p -m ../data/square-disc-nurbs.mesh -o -1
//               mpirun -np 4 ex1p -m ../data/star-mixed-p2.mesh -o 2
//               mpirun -np 4 ex1p -m ../data/disc-nurbs.mesh -o -1
//               mpirun -np 4 ex1p -m ../data/pipe-nurbs.mesh -o -1
//               mpirun -np 4 ex1p -m ../data/ball-nurbs.mesh -o 2
//               mpirun -np 4 ex1p -m ../data/fichera-mixed-p2.mesh -o 2
//               mpirun -np 4 ex1p -m ../data/star-surf.mesh
//               mpirun -np 4 ex1p -m ../data/square-disc-surf.mesh
//               mpirun -np 4 ex1p -m ../data/inline-segment.mesh
//               mpirun -np 4 ex1p -m ../data/amr-quad.mesh
//               mpirun -np 4 ex1p -m ../data/amr-hex.mesh
//               mpirun -np 4 ex1p -m ../data/mobius-strip.mesh
//               mpirun -np 4 ex1p -m ../data/mobius-strip.mesh -o -1 -sc
//
// Device sample runs:
//               mpirun -np 4 ex1p -pa -d cuda
//               mpirun -np 4 ex1p -pa -d occa-cuda
//               mpirun -np 4 ex1p -pa -d raja-omp
//               mpirun -np 4 ex1p -pa -d ceed-cpu
//             * mpirun -np 4 ex1p -pa -d ceed-cuda
//             * mpirun -np 4 ex1p -pa -d ceed-hip
//               mpirun -np 4 ex1p -pa -d ceed-cuda:/gpu/cuda/shared
//               mpirun -np 4 ex1p -m ../data/beam-tet.mesh -pa -d ceed-cpu
//
// Description:  This example code demonstrates the use of MFEM to define a
//               simple finite element discretization of the Laplace problem
//               -Delta u = 1 with homogeneous Dirichlet boundary conditions.
//               Specifically, we discretize using a FE space of the specified
//               order, or if order < 1 using an isoparametric/isogeometric
//               space (i.e. quadratic for quadratic curvilinear mesh, NURBS for
//               NURBS mesh, etc.)

#include "mfem.hpp"
#include <fstream>
#include <iostream>

using namespace std;
using namespace mfem;

class ExactSegmentDistCoeff : public Coefficient
{
   virtual double Eval(ElementTransformation &T, const IntegrationPoint &ip)
   {
      Vector x(3);
      T.Transform(ip, x);
      return min(x(0), 1.0 - x(0));
   }
};

class ExactQuadDistCoeff : public Coefficient
{
   virtual double Eval(ElementTransformation &T, const IntegrationPoint &ip)
   {
      Vector x(3);
      T.Transform(ip, x);
      return min( min(x(0), 1.0 - x(0)), min(x(1), 1.0 - x(1)) );
   }
};

class ExactCircleDistCoeff : public Coefficient
{
   virtual double Eval(ElementTransformation &T, const IntegrationPoint &ip)
   {
      Vector x(3);
      T.Transform(ip, x);
      const double rad = sqrt( x(0) * x(0) + x(1) * x(1) );
      return 1.0 - rad;
   }
};

int main(int argc, char *argv[])
{
   // 1. Initialize MPI.
   int num_procs, myid;
   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);

   // 2. Parse command-line options.
   const char *mesh_file = "../data/star.mesh";
   int rs_levels = 0;
   int order = 1;
   double t_param = 1.0;
   bool static_cond = false;
   bool pa = false;
   const char *device_config = "cpu";
   bool visualization = true;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&rs_levels, "-rs", "--refine-serial",
                  "Number of times to refine the mesh uniformly in serial.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree) or -1 for"
                  " isoparametric space.");
   args.AddOption(&t_param, "-t", "--t-param", "Varadhan's t constant");
   args.AddOption(&static_cond, "-sc", "--static-condensation", "-no-sc",
                  "--no-static-condensation", "Enable static condensation.");
   args.AddOption(&pa, "-pa", "--partial-assembly", "-no-pa",
                  "--no-partial-assembly", "Enable Partial Assembly.");
   args.AddOption(&device_config, "-d", "--device",
                  "Device configuration string, see Device::Configure().");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.Parse();
   if (!args.Good())
   {
      if (myid == 0)
      {
         args.PrintUsage(cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (myid == 0) { args.PrintOptions(cout); }

   // 3. Enable hardware devices such as GPUs, and programming models such as
   //    CUDA, OCCA, RAJA and OpenMP based on command line options.
   Device device(device_config);
   if (myid == 0) { device.Print(); }

   // Refine the mesh.
   Mesh mesh(mesh_file, 1, 1);
   const int dim = mesh.Dimension();
   for (int lev = 0; lev < rs_levels; lev++) { mesh.UniformRefinement(); }

   // 6. Define a parallel mesh by a partitioning of the serial mesh. Refine
   //    this mesh further in parallel to increase the resolution. Once the
   //    parallel mesh is defined, the serial mesh can be deleted.
   ParMesh pmesh(MPI_COMM_WORLD, mesh);
   mesh.Clear();
   {
      int par_ref_levels = 2;
      for (int l = 0; l < par_ref_levels; l++)
      {
         pmesh.UniformRefinement();
      }
   }

   // 7. Define a parallel finite element space on the parallel mesh. Here we
   //    use continuous Lagrange finite elements of the specified order. If
   //    order < 1, we instead use an isoparametric/isogeometric space.
   FiniteElementCollection *fec;
   bool delete_fec;
   if (order > 0)
   {
      fec = new H1_FECollection(order, dim);
      delete_fec = true;
   }
   else if (pmesh.GetNodes())
   {
      fec = pmesh.GetNodes()->OwnFEC();
      delete_fec = false;
      if (myid == 0)
      {
         cout << "Using isoparametric FEs: " << fec->Name() << endl;
      }
   }
   else
   {
      fec = new H1_FECollection(order = 1, dim);
      delete_fec = true;
   }
   ParFiniteElementSpace fespace(&pmesh, fec);
   HYPRE_Int size = fespace.GlobalTrueVSize();
   if (myid == 0) { cout << "Number of FE unknowns: " << size << endl; }

   // 8. Determine the list of true (i.e. parallel conforming) essential
   //    boundary dofs. In this example, the boundary conditions are defined
   //    by marking all the boundary attributes from the mesh as essential
   //    (Dirichlet) and converting them to a list of true dofs.
   Array<int> ess_tdof_list;
   if (pmesh.bdr_attributes.Size())
   {
      Array<int> ess_bdr(pmesh.bdr_attributes.Max());
      ess_bdr = 1;
      fespace.GetEssentialTrueDofs(ess_bdr, ess_tdof_list);
   }

   // Solution x with correct Dirichlet BC.
   ParGridFunction w(&fespace);
   w = 0.0;
   Array<int> bdr(pmesh.bdr_attributes.Max()); bdr = 1;
   ConstantCoefficient one(1.0);
   w.ProjectBdrCoefficient(one, bdr);

   // Set up RHS.
   ParLinearForm b(&fespace);
   b = 0.0;

   // Diffusion and mass terms in the LHS.
   ParBilinearForm a(&fespace);
   if (pa) { a.SetAssemblyLevel(AssemblyLevel::PARTIAL); }
   a.AddDomainIntegrator(new MassIntegrator(one));
   ConstantCoefficient t_coeff(t_param);
   a.AddDomainIntegrator(new DiffusionIntegrator(t_coeff));

   // 12. Assemble the parallel bilinear form and the corresponding linear
   //     system, applying any necessary transformations such as: parallel
   //     assembly, eliminating boundary conditions, applying conforming
   //     constraints for non-conforming AMR, static condensation, etc.
   if (static_cond) { a.EnableStaticCondensation(); }
   a.Assemble();

   OperatorPtr A;
   Vector B, X;
   a.FormLinearSystem(ess_tdof_list, w, b, A, X, B);

   // 13. Solve the linear system A X = B.
   //     * With full assembly, use the BoomerAMG preconditioner from hypre.
   //     * With partial assembly, use Jacobi smoothing, for now.
   Solver *prec = NULL;
   if (pa)
   {
      if (UsesTensorBasis(fespace))
      {
         prec = new OperatorJacobiSmoother(a, ess_tdof_list);
      }
   }
   else { prec = new HypreBoomerAMG; }

   CGSolver cg(MPI_COMM_WORLD);
   cg.SetRelTol(1e-12);
   cg.SetMaxIter(5000);
   cg.SetPrintLevel(1);
   if (prec) { cg.SetPreconditioner(*prec); }
   cg.SetOperator(*A);
   cg.Mult(B, X);
   delete prec;

   // 14. Recover the parallel grid function corresponding to X. This is the
   //     local finite element solution on each processor.
   a.RecoverFEMSolution(X, b, w);

   // Varadhan transformation.
   ParGridFunction u(&fespace);
   for (int i = 0; i < u.Size(); i++)
   {
      u(i) = - sqrt(t_param) * log(w(i));
   }

   Coefficient *exact_dist = NULL;
   ParGridFunction u_error(&fespace);
   if (strcmp(mesh_file, "../data/inline-segment.mesh") == 0)
   {
      exact_dist = new ExactSegmentDistCoeff;
   }
   if (strcmp(mesh_file, "../data/inline-quad.mesh") == 0)
   {
      exact_dist = new ExactQuadDistCoeff;
   }
   if (strcmp(mesh_file, "./cir.msh") == 0)
   {
      exact_dist = new ExactCircleDistCoeff;
   }
   if (exact_dist)
   {
      const double l1 = u.ComputeL1Error(*exact_dist),
                   linf = u.ComputeMaxError(*exact_dist);
      if (myid == 0)
      {
         std::cout << "L1   error: " << l1 << endl
                   << "Linf error: " << linf << endl;
      }
      // Visualize the error.
      u_error.ProjectCoefficient(*exact_dist);
      for (int i = 0; i < u.Size(); i++)
      {
         u_error(i) = fabs(u_error(i) - u(i));
      }
   }

   // 15. Save the refined mesh and the solution in parallel. This output can
   //     be viewed later using GLVis: "glvis -np <np> -m mesh -g sol".
   {
      ostringstream mesh_name, sol_name;
      mesh_name << "mesh." << setfill('0') << setw(6) << myid;
      sol_name << "sol." << setfill('0') << setw(6) << myid;

      ofstream mesh_ofs(mesh_name.str().c_str());
      mesh_ofs.precision(8);
      pmesh.Print(mesh_ofs);

      ofstream sol_ofs(sol_name.str().c_str());
      sol_ofs.precision(8);
      w.Save(sol_ofs);
   }

   // 16. Send the solution by socket to a GLVis server.
   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;

      socketstream sol_sock_w(vishost, visport);
      sol_sock_w << "parallel " << num_procs << " " << myid << "\n";
      sol_sock_w.precision(8);
      sol_sock_w << "solution\n" << pmesh << w;
      sol_sock_w << "window_geometry " << 0 << " " << 0 << " "
                                       << 600 << " " << 600 << "\n"
                 << "window_title '" << "w" << "'\n" << flush;

      socketstream sol_sock_u(vishost, visport);
      sol_sock_u << "parallel " << num_procs << " " << myid << "\n";
      sol_sock_u.precision(8);
      sol_sock_u << "solution\n" << pmesh << u;
      sol_sock_u << "window_geometry " << 600 << " " << 0 << " "
                                       << 600 << " " << 600 << "\n"
                 << "window_title '" << "u" << "'\n" << flush;

      if (exact_dist)
      {
         socketstream sol_sock_e(vishost, visport);
         sol_sock_e << "parallel " << num_procs << " " << myid << "\n";
         sol_sock_e.precision(8);
         sol_sock_e << "solution\n" << pmesh << u_error;
         sol_sock_e << "window_geometry " << 1200 << " " << 0 << " "
                                          << 600 << " " << 600 << "\n"
                    << "window_title '" << "|u - d|" << "'\n" << flush;
      }
   }


   ParaViewDataCollection paraview_dc("Dist", &pmesh);
   paraview_dc.SetPrefixPath("ParaView");
   paraview_dc.SetLevelsOfDetail(order);
   paraview_dc.SetDataFormat(VTKFormat::BINARY);
   paraview_dc.SetHighOrderOutput(true);
   paraview_dc.SetCycle(0);
   paraview_dc.SetTime(0.0);
   paraview_dc.RegisterField("w",&w);
   paraview_dc.RegisterField("u",&u);
   paraview_dc.Save();



   if (delete_fec) { delete fec; }

   MPI_Finalize();
   return 0;
}
