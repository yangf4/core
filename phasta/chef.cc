#include <ph.h>
#include <phInput.h>
#include <phBC.h>
#include <phRestart.h>
#include <phAdapt.h>
#include <phOutput.h>
#include <phPartition.h>
#include <apfMDS.h>
#include <apfMesh2.h>
#include <apf.h>
#include <gmi_mesh.h>
#include <gmi_sim.h>
#include <PCU.h>
#include <SimUtil.h>
#include <cassert>

namespace {
  static FILE* openFileRead(ph::Input&, const char* path) {
    return fopen(path, "r");
  }

  static FILE* openFileWrite(ph::Output&, const char* path) {
    return fopen(path, "w");
  }
}

ph::Input* globalInput;
ph::BCs* globalBCs;
int globalPeers;

static void afterSplit(apf::Mesh2* m)
{
  ph::Input& in = *globalInput;
  ph::BCs& bcs = *globalBCs;
  std::string path = ph::setupOutputDir();
  ph::setupOutputSubdir(path);
  /* check if the mesh changed at all */
  if ((PCU_Comm_Peers()!=globalPeers) ||
      in.adaptFlag ||
      in.tetrahedronize) {
    if (in.parmaPtn && PCU_Comm_Peers() > 1)
      ph::balance(in,m);
    apf::reorderMdsMesh(m);
  }
  assert(!in.filterMatches);
  ph::Output o;
  o.openfile_write = openFileWrite;
  ph::generateOutput(in, bcs, m, o);
  ph::detachAndWriteSolution(in, o, m, path);
  ph::writeGeomBC(o, path);
  ph::writeAuxiliaryFiles(path, in.timeStepNumber);
  if ( ! in.outMeshFileName.empty() )
    m->writeNative(in.outMeshFileName.c_str());
  m->verify();
  m->destroyNative();
  apf::destroyMesh(m);
}

int main(int argc, char** argv)
{
  int provided;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  assert(provided == MPI_THREAD_MULTIPLE);
  PCU_Comm_Init();
  Sim_readLicenseFile(0);
  gmi_sim_start();
  gmi_register_sim();
  gmi_register_mesh();
  globalPeers = PCU_Comm_Peers();
  ph::Input in;
  in.openfile_read = openFileRead;
  in.load("adapt.inp");
  ph::BCs bcs;
  gmi_model* g;
  ph::loadModelAndBCs(in, g, bcs);
  apf::Mesh2* m = apf::loadMdsMesh(g, in.meshFileName.c_str());
  m->verify();
  if (in.solutionMigration)
    ph::readAndAttachSolution(in, m);
  else
    ph::attachZeroSolution(in, m);
  if (in.buildMapping)
    ph::buildMapping(m);
  apf::setMigrationLimit(in.elementsPerMigration);
  if (in.adaptFlag) {
    ph::adapt(in, m);
    ph::goToStepDir(in.timeStepNumber);
  }
  if (in.tetrahedronize)
    ph::tetrahedronize(in, m);
  globalInput = &in;
  globalBCs = &bcs;
  ph::split(in, m, afterSplit);
  gmi_sim_stop();
  Sim_unregisterAllKeys();
  PCU_Comm_Free();
  MPI_Finalize();
}
