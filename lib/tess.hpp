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

using namespace std;

void create_dblocks(int num_blocks, struct dblock_t* &dblocks, int** &hdrs,
		    float **particles, int *num_particles);
void reset_dblocks(int num_blocks, struct dblock_t* &dblocks);
void fill_vert_to_tet(dblock_t* dblock);
#if 0 // old version
void incomplete_dcells_initial(struct dblock_t *tblock, int lid,
			       vector <sent_t> &sent_particles,
			       vector <int> &convex_hull_particles);
void incomplete_dcells_final(struct dblock_t *dblock,
			    int lid,
			    vector <sent_t> &sent_particles,
			    vector <int> &convex_hull_particles);
#else // new version
void incomplete_dcells_initial(struct dblock_t *tblock, int lid,
			       vector <set <gb_t> > &sent_particles,
			       vector <int> &convex_hull_particles);
void incomplete_dcells_final(struct dblock_t *dblock, int lid,
			     vector <set <gb_t> > &sent_particles,
			     vector <int> &convex_hull_particles);
#endif
void neighbor_d_is_complete(int nblocks, struct dblock_t *dblocks,
			    struct remote_ic_t **rics,
			    vector <struct sent_t> *sent_particles);
void sample_particles(float *particles, int &num_particles, int sample_rate);
