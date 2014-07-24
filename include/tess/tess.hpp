// ---------------------------------------------------------------------------
//  
//   functions that have C++ arguments that C source files
//   should not see, hence they are in a separate header
//  
//   Tom Peterka
//   Argonne National Laboratory
//   9700 S. Cass Ave.
//   Argonne, IL 60439
//   tpeterka@mcs.anl.gov
//  
//   (C) 2013 by Argonne National Laboratory.
//   See COPYRIGHT in top-level directory.
//  
// --------------------------------------------------------------------------

#include <vector>
#include <set>

#include <diy/serialization.hpp>
#include <diy/master.hpp>

typedef  diy::ContinuousBounds       Bounds;
typedef  diy::RegularContinuousLink  RCLink;

using namespace std;

void* create_block();
void destroy_block(void* b);
void save_block(const void* b, diy::BinaryBuffer& bb);
void load_block(void* b, diy::BinaryBuffer& bb);
void create(int gid, const Bounds& core, const Bounds& bounds, const diy::Link& link);
void gen_particles(void* b_, const diy::Master::ProxyWithLink& cp, void*);
void delaunay1(void* b_, const diy::Master::ProxyWithLink& cp, void* ps);
void delaunay2(void* b_, const diy::Master::ProxyWithLink& cp, void* ps);
void delaunay3(void* b_, const diy::Master::ProxyWithLink& cp, void* ps);
void neighbor_particles(void* b_, const diy::Master::ProxyWithLink& cp, void*);
void incomplete_cells_initial(struct dblock_t *dblock, const diy::Master::ProxyWithLink& cp);
void incomplete_cells_final(struct dblock_t *dblock, const diy::Master::ProxyWithLink& cp);
void reset_block(struct dblock_t* &dblock);
void fill_vert_to_tet(dblock_t* dblock);
void wall_particles(struct dblock_t *dblock);
void sample_particles(float *particles, int &num_particles, int sample_rate);
diy::Direction nearest_neighbor(float* p, float* mins, float* maxs);

// add blocks to a master
struct AddBlock
{
  AddBlock(diy::Master& master_):
    master(master_)           {}

  void  operator()(int gid, const Bounds& core, const Bounds& bounds, const RCLink& link) const
  {
    dblock_t*      b = static_cast<dblock_t*>(create_block());
    RCLink*        l = new RCLink(link);
    diy::Master&   m = const_cast<diy::Master&>(master);

    int lid = m.add(gid, b, l);

    // init block fields
    b->gid = gid;
    b->mins[0] = core.min[0]; b->mins[1] = core.min[1]; b->mins[2] = core.min[2];
    b->maxs[0] = core.max[0]; b->maxs[1] = core.max[1]; b->maxs[2] = core.max[2];
    b->num_orig_particles = 0;
    b->num_particles = 0;
    b->particles = NULL;
    b->num_tets = 0;
    b->tets = NULL;
    b->vert_to_tet = NULL;

    // debug
    //     fprintf(stderr, "Done adding block gid %d\n", b->gid);
  }

  diy::Master&  master;
};

// serialize a block
namespace diy
{
  template<>
  struct Serialization<dblock_t>
  {
    static void save(BinaryBuffer& bb, const dblock_t& d)
    {
      // debug
      //             fprintf(stderr, "Saving block gid %d\n", d.gid);
      diy::save(bb, d.gid);
      diy::save(bb, d.mins);
      diy::save(bb, d.maxs);
      diy::save(bb, d.num_orig_particles);
      diy::save(bb, d.num_particles);
      diy::save(bb, d.particles, 3 * d.num_particles);
      // NB tets and vert_to_tet get recreated in each phase; not saved and reloaded
      vector <int> *convex_hull_particles = 
        static_cast<vector <int>*>(d.convex_hull_particles);
      diy::save(bb, *convex_hull_particles);
      vector <set <int> > *sent_particles = 
        static_cast<vector <set <int> >*>(d.sent_particles);
      diy::save(bb, *sent_particles);
      // TODO: not savint Dt for now, recomputing upon loading instead

      // debug
      //       fprintf(stderr, "Done saving block gid %d\n", d.gid);
    }

    static void load(BinaryBuffer& bb, dblock_t& d)
    {
      diy::load(bb, d.gid);
      // debug
      //             fprintf(stderr, "Loading block gid %d\n", d.gid);
      diy::load(bb, d.mins);
      diy::load(bb, d.maxs);
      diy::load(bb, d.num_orig_particles);
      diy::load(bb, d.num_particles);
      d.particles = NULL;
      if (d.num_particles)
        d.particles = (float*)malloc(d.num_particles * 3 * sizeof(float));
      diy::load(bb, d.particles, 3 * d.num_particles);
      // NB tets and vert_to_tet get recreated in each phase; not saved and reloaded
      d.num_tets = 0;
      d.tets = NULL;
      d.vert_to_tet = NULL;
      if (d.num_particles)
        d.vert_to_tet = (int*)malloc(d.num_particles * sizeof(int));
      diy::load(bb, *(static_cast<vector <int>*>(d.convex_hull_particles)));
      diy::load(bb, *(static_cast<vector <set <int> >*>(d.sent_particles)));
      // TODO: re-initializing Dt instead of loading it here;
      // allocated in create_block and recomputed here
      local_cells(&d);
      // debug
      //       fprintf(stderr, "Done loading block gid %d\n", d.gid);
    }
  };
}
