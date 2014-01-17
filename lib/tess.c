
/*---------------------------------------------------------------------------
 *
 * parallel voronoi and delaunay tesselation
 *
 * Tom Peterka
 * Argonne National Laboratory
 * 9700 S. Cass Ave.
 * Argonne, IL 60439
 * tpeterka@mcs.anl.gov
 *
 * (C) 2013 by Argonne National Laboratory.
 * See COPYRIGHT in top-level directory.
 *
--------------------------------------------------------------------------*/
#include "mpi.h"
#include "diy.h"
#include "tess.h"
#include "io.h"

#include <stddef.h>
#include <stdio.h>
#include <math.h>

static int dim = 3; /* everything 3D */
static float data_mins[3], data_maxs[3]; /* extents of overall domain */
MPI_Comm comm; /* MPI communicator */
static float min_vol, max_vol; /* cell volume range */
static int nblocks; /* number of blocks per process */
static double *times; /* timing info */
static int wrap_neighbors; /* whether wraparound neighbors are used */

/* pnetcdf output */
#define PNETCDF_IO

/*------------------------------------------------------------------------*/
/*
  initialize parallel voronoi and delaunay tessellation including
  initializing DIY with given info on local blocks and their neighbors

  num_blocks: local number of blocks in my process
  gids: global ids of my local blocks
  bounds: block bounds (extents) of my local blocks
  neighbors: neighbor lists for each of my local blocks, in lid order
  neighbor bounds need not be known, will be discovered automatically
  num_neighbors: number of neighbors for each of my local blocks, in lid order
  global_mins, global_maxs: overall data extents
  wrap: whether wraparound neighbors are used
  minvol, maxvol: filter range for which cells to keep
  pass -1.0 to skip either or both bounds
  mpi_comm: MPI communicator
  all_times: times for particle exchange, voronoi cells, convex hulls, and output
*/
void tess_init(int num_blocks, int *gids, 
	       struct bb_t *bounds, struct gb_t **neighbors, 
	       int *num_neighbors, float *global_mins, float *global_maxs, 
	       int wrap, float minvol, float maxvol, MPI_Comm mpi_comm,
	       double *all_times) {

  int i;

  /* save globals */
  comm = mpi_comm;
  nblocks = num_blocks;
  min_vol = minvol;
  max_vol = maxvol;
  times = all_times;
  wrap_neighbors = wrap;

  /* data extents */
  for(i = 0; i < 3; i++) {
    data_mins[i] = global_mins[i];
    data_maxs[i] = global_maxs[i];
  }

  /* init times */
  for (i = 0; i < MAX_TIMES; i++)
    times[i] = 0.0;

  /* init DIY */
  DIY_Init(dim, 1, comm);
  DIY_Decomposed(num_blocks, gids, bounds, NULL, NULL, NULL, NULL, neighbors, 
		 num_neighbors, wrap);

}
/*------------------------------------------------------------------------*/
/*
  initialize parallel voronoi and delaunay tessellation with an existing
  diy domain, assumes DIY_Init and DIY_Decompose done already

  num_blocks: local number of blocks in my process
  global_mins, global_maxs: overall data extents
  minvol, maxvol: filter range for which cells to keep
  pass -1.0 to skip either or both bounds
  mpi_comm: MPI communicator
  all_times: times for particle exchange, voronoi cells, convex hulls, and output
*/
void tess_init_diy_exist(int num_blocks, float *global_mins, 
			 float *global_maxs, float minvol, float maxvol, 
			 MPI_Comm mpi_comm, double *all_times) {

  int i;

  /* save globals */
  comm = mpi_comm;
  nblocks = num_blocks;
  min_vol = minvol;
  max_vol = maxvol;
  times = all_times;

  /* data extents */
  for(i = 0; i < 3; i++) {
    data_mins[i] = global_mins[i];
    data_maxs[i] = global_maxs[i];
  }

  /* init times */
  for (i = 0; i < MAX_TIMES; i++)
    times[i] = 0.0;

}
/*------------------------------------------------------------------------*/
/*
finalize tesselation
*/
void tess_finalize() {

  DIY_Finalize();

}
/*------------------------------------------------------------------------*/
/*
  parallel tessellation

  particles: particles[block_num][particle] 
  where each particle is 3 values, px, py, pz
  num_particles; number of particles in each block
  out_file: output file name
*/
void tess(float **particles, int *num_particles, char *out_file) {

  voronoi_delaunay(nblocks, particles, num_particles, times, out_file);

}
/*------------------------------------------------------------------------*/
/*
  test of parallel tesselation

  tot_blocks: total number of blocks in the domain
  data_size: domain grid size (x, y, z)
  jitter: maximum amount to randomly move each particle
  minvol, maxvol: filter range for which cells to keep
  pass -1.0 to skip either or both bounds
  wrap: whether wraparound neighbors are used
  times: times for particle exchange, voronoi cells, convex hulls, and output
*/
void tess_test(int tot_blocks, int *data_size, float jitter, 
	       float minvol, float maxvol, int wrap, double *times) {

  float **particles; /* particles[block_num][particle] 
			 where each particle is 3 values, px, py, pz */
  int *num_particles; /* number of particles in each block */
  int dim = 3; /* 3D */
  int given[3] = {0, 0, 0}; /* no constraints on decomposition in {x, y, z} */
  int ghost[6] = {0, 0, 0, 0, 0, 0}; /* ghost in {-x, +x, -y, +y, -z, +z} */
  int nblocks; /* my local number of blocks */
  int i;

  comm = MPI_COMM_WORLD;
  min_vol = minvol;
  max_vol = maxvol;
  wrap_neighbors = wrap;

  /* data extents */
  for(i = 0; i < 3; i++) {
    data_mins[i] = 0.0;
    data_maxs[i] = data_size[i] - 1.0;
  }

  /* have DIY do the decomposition */
  DIY_Init(dim, 1, comm);
  DIY_Decompose(ROUND_ROBIN_ORDER, data_size, tot_blocks, &nblocks, 1, 
		ghost, given, wrap);

  /* generate test points in each block */
  particles = (float **)malloc(nblocks * sizeof(float *));
  num_particles = (int *)malloc(nblocks * sizeof(int));
  for (i = 0; i < nblocks; i++)
    num_particles[i] = gen_particles(i, &particles[i], jitter);

  voronoi_delaunay(nblocks, particles, num_particles, times, "vor.out");

  /* cleanup */
  for (i = 0; i < nblocks; i++)
    free(particles[i]);
  free(particles);
  free(num_particles);

  DIY_Finalize();

}
/*--------------------------------------------------------------------------*/
/*
  parallel voronoi and delaunay tesselation

  nblocks: local number of blocks
  particles: particles[block_num][particle] 
  where each particle is 3 values, px, py, pz
  num_particles; number of particles in each block
  times: times for particle exchange, voronoi cells, convex hulls, and output
  out_file: output file name
*/
void voronoi_delaunay(int nblocks, float **particles, int *num_particles, 
		      double *times, char *out_file) {

  int *num_orig_particles; /* number of original particles, before any
			      neighbor exchange */
  int dim = 3; /* 3D */
  int rank; /* MPI rank */
  int i;

  MPI_Comm_rank(comm, &rank);

  /* init timing */
  for (i = 0; i < MAX_TIMES; i++)
    times[i] = 0.0;

  int **hdrs; /* headers */
  struct vblock_t *vblocks; /* voronoi blocks */
  struct vblock_t *tblocks; /* remporary voronoi blocks */

  num_orig_particles = (int *)malloc(nblocks * sizeof(int));
  for (i = 0; i < nblocks; i++)
    num_orig_particles[i] = num_particles[i];

  /* allocate and initialize blocks */
  create_blocks(nblocks, &vblocks, &hdrs); /* final */
  create_blocks(nblocks, &tblocks, NULL); /* temporary */

#ifdef TIMING
  MPI_Barrier(comm);
  times[LOCAL_TIME] = MPI_Wtime();
#endif

  /* create local voronoi cells */
  local_cells(nblocks, tblocks, vblocks, dim, num_particles, particles);

  /* cleanup local temporary blocks */
  destroy_blocks(nblocks, tblocks, NULL);

#ifdef TIMING
  MPI_Barrier(comm);
  times[LOCAL_TIME] = MPI_Wtime() - times[LOCAL_TIME];
  times[EXCH_TIME] = MPI_Wtime();
#endif

  /* exchange particles with neighbors */
  int **gids; /* owner global block ids of received particles */
  int **nids; /* owner native particle ids of received particles */
  unsigned char **dirs; /* wrapping directions of received articles */
  gids = (int **)malloc(nblocks * sizeof(int *));
  nids = (int **)malloc(nblocks * sizeof(int *));
  dirs = (unsigned char **)malloc(nblocks * sizeof(unsigned char *));
  neighbor_particles(nblocks, particles, num_particles, 
		     gids, nids, dirs);

#ifdef TIMING
  MPI_Barrier(comm);
  times[EXCH_TIME] = MPI_Wtime() - times[EXCH_TIME];
#endif

#ifdef TIMING
  MPI_Barrier(comm);
  times[CELL_TIME] = MPI_Wtime();
#endif

  /* create original voronoi cells */
  orig_cells(nblocks, vblocks, dim, num_particles, num_orig_particles,
	     particles, gids, nids, dirs, times);

  /* cleanup */
  for (i = 0; i < nblocks; i++) {
    free(gids[i]);
    free(nids[i]);
  }
  free(gids);
  free(nids);

#ifdef TIMING
  /* no barrier here; want min and max time */
  times[CELL_TIME] = MPI_Wtime() - times[CELL_TIME];
  MPI_Barrier(comm);
#endif

#ifdef TIMING
  MPI_Barrier(comm);
  times[VOL_TIME] = MPI_Wtime();
#endif

  /* compute volume and surface area manually (not using convex hulls) */
  cell_vols(nblocks, vblocks, particles);

#ifdef TIMING
  /* no barrier here; want min and max time */
  times[VOL_TIME] = MPI_Wtime() - times[VOL_TIME];
#endif

  /* prepare for output */
  prep_out(nblocks, vblocks);

#ifdef TIMING
  MPI_Barrier(comm);
  times[OUT_TIME] = MPI_Wtime();
#endif

  /* save headers */
  save_headers(nblocks, vblocks, hdrs);

  /* write output */
#ifdef PNETCDF_IO
  char out_ncfile[256];
  strncpy(out_ncfile, out_file, sizeof(out_ncfile));
  strncat(out_ncfile, ".nc", sizeof(out_file));
  pnetcdf_write(nblocks, vblocks, out_ncfile, comm);
#else
  diy_write(nblocks, vblocks, hdrs, out_file);
#endif

#ifdef TIMING
  MPI_Barrier(comm);
  times[OUT_TIME] = MPI_Wtime() - times[OUT_TIME];
#endif

  /* collect stats */
  collect_stats(nblocks, vblocks, times);

  /* cleanup */
  destroy_blocks(nblocks, vblocks, hdrs);
  free(num_orig_particles);

}
/*--------------------------------------------------------------------------*/
/*
  computes volume and surface area for completed cells

  nblocks: number of blocks
  vblocks: pointer to array of vblocks
  particles: particles in each block, particles[block_num][particle] include
   particles received from neighbors

*/
void cell_vols(int nblocks, struct vblock_t *vblocks, float **particles) {

  int b, j, f;

  /* compute areas of all faces */
  face_areas(nblocks, vblocks);

  /* for all blocks */
  for (b = 0; b < nblocks; b++) {

    vblocks[b].areas = (float *)malloc(vblocks[b].temp_num_complete_cells *
					   sizeof(float));
    vblocks[b].vols = (float *)malloc(vblocks[b].temp_num_complete_cells *
					   sizeof(float));

    /* for all complete cells */
    for (j = 0; j < vblocks[b].temp_num_complete_cells; j++) {

      int cell = vblocks[b].temp_complete_cells[j]; /* current cell */
      int num_faces; /* number of faces in the current cell */
      vblocks[b].areas[j] = 0.0;
      vblocks[b].vols[j] = 0.0;
      float temp_vol = 0.0; /* temporaries */
      float temp_area = 0.0;

      if (cell < vblocks[b].num_orig_particles - 1)
	num_faces = vblocks[b].cell_faces_start[cell + 1] -
	  vblocks[b].cell_faces_start[cell];
      else
	num_faces = vblocks[b].tot_num_cell_faces -
	  vblocks[b].cell_faces_start[cell];

      /* for all faces */
      for (f = 0; f < num_faces; f++) {

	/* current face */
	int fid = vblocks[b].cell_faces[vblocks[b].cell_faces_start[cell] + f];

	/* input particles of cells sharing the face */
	float p0[3], p1[3]; 
	int p; /* index of particle (could be neighbor's) */

	p = vblocks[b].faces[fid].cells[0];
	p0[0] = particles[b][3 * p];
	p0[1] = particles[b][3 * p + 1];
	p0[2] = particles[b][3 * p + 2];
	p = vblocks[b].faces[fid].cells[1];
	p1[0] = particles[b][3 * p];
	p1[1] = particles[b][3 * p + 1];
	p1[2] = particles[b][3 * p + 2];

	/* height of pyramid from site to face = 
	   distance between sites sharing the face / 2 */
	float height = sqrt((p0[0] - p1[0]) * (p0[0] - p1[0]) +
			    (p0[1] - p1[1]) * (p0[1] - p1[1]) +
			    (p0[2] - p1[2]) * (p0[2] - p1[2])) / 2.0;

	/* add the volume of the pyramid formed by site and current face
	   to the volume of the cell and add the face area to the surface
	   area of the cell */
	temp_vol += vblocks[b].face_areas[fid] * height / 3.0;
	temp_area += vblocks[b].face_areas[fid];

      } /* for all faces */

      /* store the cell permanently if it passes the volume thresholds */
      if ((min_vol < 0 || temp_vol >= min_vol) &&
	  (max_vol < 0 || temp_vol <= max_vol)) {

	vblocks[b].vols[j] = temp_vol;
	vblocks[b].areas[j] = temp_area;
	vblocks[b].complete_cells[vblocks[b].num_complete_cells] =
	  vblocks[b].temp_complete_cells[j];
	vblocks[b].num_complete_cells++;

      }

    } /* for all complete cells */

  } /* for all blocks */

}
/*--------------------------------------------------------------------------*/
/*
  computes areas of all faces

  nblocks: number of blocks
  vblocks: pointer to array of vblocks

*/
void face_areas(int nblocks, struct vblock_t *vblocks) {

  int b, f, v;

  /* for all blocks */
  for (b = 0; b < nblocks; b++) {

    vblocks[b].face_areas = (float *)malloc(vblocks[b].num_faces *
					    sizeof(float));

    /* for all faces */
    for (f = 0; f < vblocks[b].num_faces; f++) {

      vblocks[b].face_areas[f] = 0.0;

      /* all triangles fan out from same vertex */
      int v0 = vblocks[b].faces[f].verts[0];

      /* for all vertices in a face */
      for (v = 2; v <vblocks[b].faces[f].num_verts; v++) {

	/* remaining 2 vertices of one triangle in the polygon */
	int v1 = vblocks[b].faces[f].verts[v - 1];
	int v2 = vblocks[b].faces[f].verts[v];

	/* vectors for two sides of triangle v1v1 and v0v2 */
	float s1[3], s2[3]; 
	s1[0] = vblocks[b].verts[3 * v1] - 
	  vblocks[b].verts[3 * v0];
	s1[1] = vblocks[b].verts[3 * v1 + 1] - 
	  vblocks[b].verts[3 * v0 + 1];
	s1[2] = vblocks[b].verts[3 * v1 + 2] - 
	  vblocks[b].verts[3 * v0 + 2];
	s2[0] = vblocks[b].verts[3 * v2] - 
	  vblocks[b].verts[3 * v0];
	s2[1] = vblocks[b].verts[3 * v2 + 1] - 
	  vblocks[b].verts[3 * v0 + 1];
	s2[2] = vblocks[b].verts[3 * v2 + 2] - 
	  vblocks[b].verts[3 * v0 + 2];

	/* cross product of s1 and s2 */
	float c[3];
	c[0] = s1[1] * s2[2] - s1[2] * s2[1];
	c[1] = s1[2] * s2[0] - s1[0] * s2[2];
	c[2] = s1[0] * s2[1] - s1[1] * s2[0];

	/* area of triangle is |c| / 2 */
	float a = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) / 2.0;
	vblocks[b].face_areas[f] += a;

      } /* for all vertices */

    } /* for all faces */

  } /* for all blocks */

}
/*--------------------------------------------------------------------------*/
/*
  exchanges particles with neighbors

  nblocks: local number of blocks
  particles: particles before and after neighbor exchange (input / output)
  num_particles: number of new particles in each block (input / output)
  gids: global block ids of owners of received particles in each of my blocks
  nids: native particle ids of received particles in each of my blocks
   (allocated by this function, user's responsibility to free)
  dirs: wrapping direction of received particles in each of my blocks

  to send the site to the neighbor
*/
void neighbor_particles(int nblocks, float **particles, int *num_particles,
			int **gids, int **nids, unsigned char **dirs) {

  void ***recv_particles; /* pointers to particles in ecah block 
			     that are received from neighbors */
  int *num_recv_particles; /* number of received particles for each block */
  int i, j;

  recv_particles = (void ***)malloc(nblocks * sizeof(void **));
  num_recv_particles = (int *)malloc(nblocks * sizeof(int));

  /* particles were previously enqueued by local_cells(), ready to
     be exchanged */
  DIY_Exchange_neighbors(0, recv_particles, num_recv_particles, 1.0, 
			 &item_type);

  /* copy received particles to particles */
  for (i = 0; i < nblocks; i++) {

    /* debug */
/*     fprintf(stderr, "num_particles in gid %d before exchange is %d\n", */
/* 	    DIY_Gid(0, i), num_particles[i]); */

    gids[i] = (int *)malloc(num_recv_particles[i] * sizeof(int));
    nids[i] = (int *)malloc(num_recv_particles[i] * sizeof(int));
    dirs[i] = (unsigned char *)malloc(num_recv_particles[i]);

    int n = 0;
 
    if (num_recv_particles[i]) {

      /* grow space */
      particles[i] = 
	(float *)realloc(particles[i], 
			 (num_particles[i] + num_recv_particles[i]) *
			 3 * sizeof(float));

      /* copy received particles */
      for (j = 0; j < num_recv_particles[i]; j++) { 

	particles[i][3 * num_particles[i]] =
	  DIY_Exchd_item(struct remote_particle_t, recv_particles, i, j)->x;
	particles[i][3 * num_particles[i] + 1] =
	  DIY_Exchd_item(struct remote_particle_t, recv_particles, i, j)->y;
	particles[i][3 * num_particles[i] + 2] =
	  DIY_Exchd_item(struct remote_particle_t, recv_particles, i, j)->z;
	gids[i][n] = 
	  DIY_Exchd_item(struct remote_particle_t, recv_particles, i, j)->gid;
	nids[i][n] = 
	  DIY_Exchd_item(struct remote_particle_t, recv_particles, i, j)->nid;
	dirs[i][n] = 
	  DIY_Exchd_item(struct remote_particle_t, recv_particles, i, j)->dir;

	num_particles[i]++;
	n++;

      } /* copy received particles */

    } /* if num_recv_particles */

    /* debug */
/*     fprintf(stderr, "num_particles in gid %d after exchange is %d\n", */
/* 	    DIY_Gid(0, i), num_particles[i]); */

  } /* for all blocks */

  /* clean up */
  DIY_Flush_neighbors(0, recv_particles, num_recv_particles, &item_type);
  free(num_recv_particles);
  free(recv_particles);

}
/*--------------------------------------------------------------------------*/
/*
  exchanges is_complete list for exchanged particles with neighbors

  nblocks: number of blocks
  vblocks: local blocks
  rics: completion satus of received particles in each of my blocks
   (allocated by this function, user's responsibility to free)

*/
void neighbor_is_complete(int nblocks, struct vblock_t *vblocks,
			  struct remote_ic_t **rics) {

  void ***recv_ics; /* pointers to is_complete entries in ecah block 
			     that are received from neighbors */
  int *num_recv_ics; /* number of received is_completes for each block */
  int i, j;
  struct remote_ic_t ic; /* completion status being sent or received */

  recv_ics = (void ***)malloc(nblocks * sizeof(void **));
  num_recv_ics = (int *)malloc(nblocks * sizeof(int));

  /* for all blocks */
  for (i = 0; i < nblocks; i++) {

    /* for all particles in the current block */
    for (j = 0; j < vblocks[i].num_sent_particles; j++) {
      int p = vblocks[i].sent_particles[j].particle;
      ic.is_complete = vblocks[i].is_complete[p];
      ic.gid = DIY_Gid(0, i);
      ic.nid = p;
      /* DEPRECATED */
/*       DIY_Enqueue_item_all_near(0, i, (void *)&ic, */
/* 				NULL, sizeof(struct remote_ic_t), */
/* 				&(vblocks[i].sites[3 * p]), */
/* 				vblocks[i].sent_particles[j].ghost, NULL); */
      DIY_Enqueue_item_gbs(0, i, (void *)&ic,
			   NULL, sizeof(struct remote_ic_t),
			   vblocks[i].sent_particles[j].neigh_gbs,
			   vblocks[i].sent_particles[j].num_gbs, NULL);
    }

  } /* for all blocks */

  /* exchange neighbors */
  DIY_Exchange_neighbors(0, recv_ics, num_recv_ics, 1.0, &ic_type);

  /* copy received is_completed entries */
  for (i = 0; i < nblocks; i++) {

    rics[i] = (struct remote_ic_t *)malloc(num_recv_ics[i] * 
					   sizeof(struct remote_ic_t));

    for (j = 0; j < num_recv_ics[i]; j++) {
      rics[i][j].is_complete = 
	DIY_Exchd_item(struct remote_ic_t, recv_ics, i, j)->is_complete;
      rics[i][j].gid = 
	DIY_Exchd_item(struct remote_ic_t, recv_ics, i, j)->gid;
      rics[i][j].nid = 
	DIY_Exchd_item(struct remote_ic_t, recv_ics, i, j)->nid;
    }

  }

  /* clean up */
  DIY_Flush_neighbors(0, recv_ics, num_recv_ics, &ic_type);
  free(num_recv_ics);
  free(recv_ics);

}
/*--------------------------------------------------------------------------*/
/*
 makes DIY datatype for sending / receiving one item
*/
void item_type(DIY_Datatype *dtype) {

  struct map_block_t map[] = {
    {DIY_FLOAT, OFST, 1, offsetof(struct remote_particle_t, x)         },
    {DIY_FLOAT, OFST, 1, offsetof(struct remote_particle_t, y)         },
    {DIY_FLOAT, OFST, 1, offsetof(struct remote_particle_t, z)         },
    {DIY_INT,   OFST, 1, offsetof(struct remote_particle_t, gid)       },
    {DIY_INT,   OFST, 1, offsetof(struct remote_particle_t, nid)       },
    {DIY_BYTE,  OFST, 1, offsetof(struct remote_particle_t, dir)       },
  };
  DIY_Create_struct_datatype(0, 6, map, dtype);

}
/*--------------------------------------------------------------------------*/
/*
 makes DIY datatype for sending / receiving one is_complete entry
*/
void ic_type(DIY_Datatype *dtype) {

  struct map_block_t map[] = {
    {DIY_INT, OFST, 1, offsetof(struct remote_ic_t, is_complete) },
    {DIY_INT, OFST, 1, offsetof(struct remote_ic_t, gid)         },
    {DIY_INT, OFST, 1, offsetof(struct remote_ic_t, nid)         },
  };
  DIY_Create_struct_datatype(0, 3, map, dtype);

}
/*--------------------------------------------------------------------------*/
/*
  collects statistics

  nblocks: number of blocks
  vblocks: pointer to array of vblocks
  times: timing info
*/
void collect_stats(int nblocks, struct vblock_t *vblocks, double *times) {

  int i, j, k, m, n, v, f;
  int tot_num_cell_verts = 0; /* number of cell verts in all local blocks */
  int tot_num_face_verts = 0; /* number of face verts in all local blocks */
  int *unique_verts = NULL; /* unique vertices in one cell */
  int num_unique_verts; /* number of unique vertices in one cell*/
  static int max_unique_verts; /* allocated number of unique vertices */
  int chunk_size = 1024; /* allocation chunk size for unique_verts */
  float vol_bin_width; /* width of a volume histogram bin */
  float dense_bin_width; /* width of a density histogram bin */
  float tot_cell_vol = 0.0; /* sum of cell volumes */
  float tot_cell_dense = 0.0; /* sum of cell densities */
  struct stats_t stats; /* local stats */
  static int first_dense = 1; /* first density value saved */
  int rank;

  MPI_Comm_rank(comm, &rank);

  /* timing range */
  stats.min_cell_time = times[CELL_TIME];
  stats.max_cell_time = times[CELL_TIME];
  stats.min_vol_time = times[VOL_TIME];
  stats.max_vol_time = times[VOL_TIME];

  /* --- first pass: find average number of vertices per cell and 
     volume range --- */

  stats.tot_tets  = 0;
  stats.tot_cells = 0;
  stats.tot_faces = 0;
  stats.tot_verts = 0;
  stats.avg_cell_verts = 0.0;
  stats.avg_cell_faces = 0.0;
  stats.avg_face_verts = 0.0;
  stats.avg_cell_vol   = 0.0;
  stats.avg_cell_dense = 0.0;

  for (i = 0; i < nblocks; i++) { /* for all blocks */

    stats.tot_tets += (vblocks[i].num_loc_tets + vblocks[i].num_rem_tets);
    stats.tot_cells += vblocks[i].num_complete_cells;
    stats.tot_verts += vblocks[i].num_verts;

    /* for all complete cells in the current block */
    f = 0;
    v = 0;
    for (j = 0; j < vblocks[i].num_complete_cells; j++) {

      if (vblocks[i].vols[j] == 0.0)
	fprintf(stderr, "found cell with 0.0 volume--this should not happen\n");

      tot_cell_vol += vblocks[i].vols[j];
      float dense = 0.0;
      if (vblocks[i].vols[j] > 0.0) {
	dense = 1.0 / vblocks[i].vols[j];
	tot_cell_dense += dense;
      }

      int cell = vblocks[i].complete_cells[j]; /* current cell */
      int num_faces; /* number of face in the current cell */
      int num_verts; /* number of vertices in current face */

      if (cell < vblocks[i].num_orig_particles - 1)
	num_faces = vblocks[i].cell_faces_start[cell + 1] -
	  vblocks[i].cell_faces_start[cell];
      else
	num_faces = vblocks[i].tot_num_cell_faces -
	  vblocks[i].cell_faces_start[cell];

      stats.tot_faces += num_faces; /* not unique, but total for all cells */
      num_unique_verts = 0;

      /* volume range */
      if (i == 0 && j == 0) {
	stats.min_cell_vol = vblocks[i].vols[j];
	stats.max_cell_vol = vblocks[i].vols[j];
      }
      else {
	if (vblocks[i].vols[j] < stats.min_cell_vol)
	  stats.min_cell_vol = vblocks[i].vols[j];
	if (vblocks[i].vols[j] > stats.max_cell_vol)
	  stats.max_cell_vol = vblocks[i].vols[j];
      }

      /* density range */
      if (first_dense && vblocks[i].vols[j] > 0.0) {
	stats.min_cell_dense = dense;
	stats.max_cell_dense = stats.min_cell_dense;
	first_dense = 0;
      }
      else if (vblocks[i].vols[j] > 0.0) {
	if (dense < stats.min_cell_dense)
	  stats.min_cell_dense = dense;
	if (dense > stats.max_cell_dense)
	  stats.max_cell_dense = dense;
      }

      /* for all faces in the current cell */
      for (k = 0; k < num_faces; k++) {

	int start = vblocks[i].cell_faces_start[cell];
	int face = vblocks[i].cell_faces[start + k];
	num_verts = vblocks[i].faces[face].num_verts;

	tot_num_face_verts += num_verts;

	/* for all verts in the current face */
	for (m = 0; m < num_verts; m++) {

	  /* check if we already counted it */
	  for (n = 0; n < num_unique_verts; n++) {
	    if (vblocks[i].faces[face].verts[m] == unique_verts[n])
	      break;
	  }
	  if (n == num_unique_verts)
	    add_int(vblocks[i].faces[face].verts[m], &unique_verts, 
		    &num_unique_verts, &max_unique_verts, chunk_size);
	  v++;

	} /* for all verts */

	f++;

      } /* for all faces */

      tot_num_cell_verts += num_unique_verts;

    } /* for all complete cells */


  } /* for all blocks */

  free(unique_verts);

  /* compute local averages */
  if (stats.tot_cells) { /* don't divide by 0 */
    stats.avg_cell_verts = tot_num_cell_verts / stats.tot_cells;
    stats.avg_cell_faces = stats.tot_faces / stats.tot_cells;
    stats.avg_face_verts = tot_num_face_verts / stats.tot_faces;
    stats.avg_cell_vol = tot_cell_vol / stats.tot_cells;
    stats.avg_cell_dense = tot_cell_dense / stats.tot_cells;
  }

  /* aggregate totals across all procs and compute average on that */
  aggregate_stats(nblocks, vblocks, &stats);

  /* --- print output --- */

  /* global stats */
  vol_bin_width = (stats.max_cell_vol - stats.min_cell_vol) / 
    stats.num_vol_bins;
  dense_bin_width = (stats.max_cell_dense - stats.min_cell_dense) / 
    stats.num_dense_bins;
  if (rank == 0) {
    fprintf(stderr, "----------------- global stats ------------------\n");
    fprintf(stderr, "local voronoi / delaunay time = %.3lf s\n",
	    times[LOCAL_TIME]);
    fprintf(stderr, "particle exchange time = %.3lf s\n", times[EXCH_TIME]);
    fprintf(stderr, "[min, max] voronoi / delaunay time = [%.3lf, %.3lf] s\n",
	    stats.min_cell_time, stats.max_cell_time);
    fprintf(stderr, "[min, max] cell volume / area time = [%.3lf, %.3lf] s\n",
	    stats.min_vol_time, stats.max_vol_time);
    fprintf(stderr, "output time = %.3lf s\n", times[OUT_TIME]);
    fprintf(stderr, "-----\n");
    fprintf(stderr, "total tets found = %d\n", stats.tot_tets);
    fprintf(stderr, "total cells found = %d\n", stats.tot_cells);
    fprintf(stderr, "total cell vertices found = %d\n", stats.tot_verts);
    fprintf(stderr, "average number of vertices per cell = %.0lf\n",
	    stats.avg_cell_verts);
    fprintf(stderr, "average number of faces per cell = %.0lf\n",
	    stats.avg_cell_faces);
    fprintf(stderr, "average number of vertices per face = %.0lf\n",
	    stats.avg_face_verts);
    fprintf(stderr, "-----\n");
    fprintf(stderr, "min cell volume = %.3lf max cell volume = %.3lf "
	    "avg cell volume = %.3lf units^3\n",
	    stats.min_cell_vol, stats.max_cell_vol, stats.avg_cell_vol);
    fprintf(stderr, "number of cell volume histogram bins = %d\n",
	    stats.num_vol_bins);
    fprintf(stderr, "-----\n");
    fprintf(stderr, "cell volume histogram:\n");
    fprintf(stderr, "min value\tcount\t\tmax value\n");
    for (k = 0; k < stats.num_vol_bins; k++)
      fprintf(stderr, "%.3lf\t\t%d\t\t%.3lf\n", 
	      stats.min_cell_vol + k * vol_bin_width, stats.vol_hist[k], 
	      stats.min_cell_vol + (k + 1) * vol_bin_width);
    fprintf(stderr, "-----\n");
    fprintf(stderr, "min cell density = %.3lf max cell density = %.3lf "
	    "avg cell density = %.3lf units^3\n",
	    stats.min_cell_dense, stats.max_cell_dense, stats.avg_cell_dense);
    fprintf(stderr, "-----\n");
    fprintf(stderr, "cell density histogram:\n");
    fprintf(stderr, "min value\tcount\t\tmax value\n");
    for (k = 0; k < stats.num_dense_bins; k++)
      fprintf(stderr, "%.3lf\t\t%d\t\t%.3lf\n", 
	      stats.min_cell_dense + k * dense_bin_width, stats.dense_hist[k], 
	      stats.min_cell_dense + (k + 1) * dense_bin_width);
    fprintf(stderr, "-------------------------------------------------\n");
  }

}
/*--------------------------------------------------------------------------*/
/*
  aggregates local statistics into global statistics

  nblocks: number of blocks
  vblocks: pointer to array of vblocks
  loc_stats: local statistics
*/
void aggregate_stats(int nblocks, struct vblock_t *vblocks, 
		     struct stats_t *loc_stats) {

  float vol_bin_width; /* width of a volume histogram bin */
  float dense_bin_width; /* width of a density histogram bin */
  struct stats_t glo_stats; /* global stats */
  struct stats_t fin_stats; /* final (global) stats */
  int groupsize; /* MPI usual */
  DIY_Datatype dtype; /* custom datatype */
  MPI_Op op1, op2; /* custom operators */
  int i, j, k;

  MPI_Comm_size(comm, &groupsize);

  if (groupsize > 1) {

    /* create datatype */
    struct map_block_t map[] = {

      { DIY_INT,   OFST, 1, 
	offsetof(struct stats_t, tot_tets)      },
      { DIY_INT,   OFST, 1, 
	offsetof(struct stats_t, tot_cells)      },
      { DIY_INT,   OFST, 1, 
	offsetof(struct stats_t, tot_faces)      },
      { DIY_INT,   OFST, 1, 
	offsetof(struct stats_t, tot_verts)      },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, avg_cell_verts) },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, avg_cell_faces) },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, avg_face_verts) },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, min_cell_vol)   },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, max_cell_vol)   },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, avg_cell_vol)   },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, min_cell_dense) },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, max_cell_dense) },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, avg_cell_dense) },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, min_cell_time)  },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, max_cell_time)  },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, min_vol_time)  },
      { DIY_FLOAT, OFST, 1, 
	offsetof(struct stats_t, max_vol_time)  },
      { DIY_INT,   OFST, 1, 
	offsetof(struct stats_t, num_vol_bins)   },
      { DIY_INT,   OFST, 1, 
	offsetof(struct stats_t, num_dense_bins) },
      { DIY_INT,   OFST, MAX_HIST_BINS, 
	offsetof(struct stats_t, vol_hist)       },
      { DIY_INT,   OFST, MAX_HIST_BINS, 
	offsetof(struct stats_t, dense_hist)     },

    };

    DIY_Create_struct_datatype(0, 21, map, &dtype);

    MPI_Op_create(&average, 1, &op1);
    MPI_Op_create(&histogram, 1, &op2);

    /* first reduction computes averages and ranges */
    MPI_Reduce(loc_stats, &glo_stats, 1, dtype, op1, 0, comm);
    /* broadcast global stats to all process */
    MPI_Bcast(&glo_stats, 1, dtype, 0, comm);

  }

  else {

    glo_stats.tot_tets       = loc_stats->tot_tets;
    glo_stats.tot_cells      = loc_stats->tot_cells;
    glo_stats.tot_faces      = loc_stats->tot_faces;
    glo_stats.tot_verts      = loc_stats->tot_verts;
    glo_stats.avg_cell_verts = loc_stats->avg_cell_verts;
    glo_stats.avg_cell_faces = loc_stats->avg_cell_faces;
    glo_stats.avg_face_verts = loc_stats->avg_face_verts;
    glo_stats.min_cell_vol   = loc_stats->min_cell_vol;
    glo_stats.max_cell_vol   = loc_stats->max_cell_vol;
    glo_stats.avg_cell_vol   = loc_stats->avg_cell_vol;
    glo_stats.min_cell_dense = loc_stats->min_cell_dense;
    glo_stats.max_cell_dense = loc_stats->max_cell_dense;
    glo_stats.avg_cell_dense = loc_stats->avg_cell_dense;
    glo_stats.min_cell_time  = loc_stats->min_cell_time;
    glo_stats.max_cell_time  = loc_stats->max_cell_time;
    glo_stats.min_vol_time  = loc_stats->min_vol_time;
    glo_stats.max_vol_time  = loc_stats->max_vol_time;
    glo_stats.num_vol_bins   = 50;
    glo_stats.num_dense_bins = 100;

  }

  /* find local cell volume and density histograms */
  vol_bin_width = (glo_stats.max_cell_vol - glo_stats.min_cell_vol) / 
    glo_stats.num_vol_bins; /* volume */
  dense_bin_width = (glo_stats.max_cell_dense - glo_stats.min_cell_dense) / 
    glo_stats.num_dense_bins; /* density */
  for (k = 0; k < glo_stats.num_vol_bins; k++) /* volume */
    glo_stats.vol_hist[k] = 0;
  for (k = 0; k < glo_stats.num_dense_bins; k++) /* density */
    glo_stats.dense_hist[k] = 0;
  for (i = 0; i < nblocks; i++) { /* for all blocks */

    for (j = 0; j < vblocks[i].num_complete_cells; j++) { /* for all cells */

      /* volume */
      for (k = 0; k < glo_stats.num_vol_bins; k++) { /* for all bins */
	if (vblocks[i].vols[j] >= glo_stats.min_cell_vol + k * vol_bin_width && 
	    vblocks[i].vols[j] < 
	    glo_stats.min_cell_vol + (k + 1) * vol_bin_width) {
	  glo_stats.vol_hist[k]++;
	  break;
	}
      } /* for all bins */
      if (k == glo_stats.num_vol_bins)
	glo_stats.vol_hist[k - 1]++; /* catch roundoff error and open
					interval on right side of bin */

      /* density */
      for (k = 0; k < glo_stats.num_dense_bins; k++) { /* for all bins */
	if (vblocks[i].vols[j] > 0.0) {
	  float dense = 1.0 /vblocks[i].vols[j];
	  if (dense >= glo_stats.min_cell_dense + k * dense_bin_width && 
	      dense <  glo_stats.min_cell_dense + (k + 1) * dense_bin_width) {
	    glo_stats.dense_hist[k]++;
	    break;
	  }
	}
      } /* for all bins */
      if (k == glo_stats.num_dense_bins)
	glo_stats.dense_hist[k - 1]++; /* catch roundoff error and open
					interval on right side of bin */

    } /* for all cells */

  } /* for all blocks */

  if (groupsize > 1) {

    /* second reduction computes global histogram */
    MPI_Reduce(&glo_stats, &fin_stats, 1, dtype, op2, 0, comm);

    /* copy global stats back to local stats */
    loc_stats->tot_tets      = fin_stats.tot_tets;
    loc_stats->tot_cells      = fin_stats.tot_cells;
    loc_stats->tot_faces      = fin_stats.tot_faces;
    loc_stats->tot_verts      = fin_stats.tot_verts;
    loc_stats->avg_cell_verts = fin_stats.avg_cell_verts;
    loc_stats->avg_cell_faces = fin_stats.avg_cell_faces;
    loc_stats->avg_face_verts = fin_stats.avg_face_verts;
    loc_stats->min_cell_vol   = fin_stats.min_cell_vol;
    loc_stats->max_cell_vol   = fin_stats.max_cell_vol;
    loc_stats->avg_cell_vol   = fin_stats.avg_cell_vol;
    loc_stats->min_cell_dense = fin_stats.min_cell_dense;
    loc_stats->max_cell_dense = fin_stats.max_cell_dense;
    loc_stats->avg_cell_dense = fin_stats.avg_cell_dense;
    loc_stats->min_cell_time  = fin_stats.min_cell_time;
    loc_stats->max_cell_time  = fin_stats.max_cell_time;
    loc_stats->min_vol_time  = fin_stats.min_vol_time;
    loc_stats->max_vol_time  = fin_stats.max_vol_time;
    loc_stats->num_vol_bins   = fin_stats.num_vol_bins;
    loc_stats->num_dense_bins   = fin_stats.num_dense_bins;
    for (i = 0; i < MAX_HIST_BINS; i++) {
      loc_stats->vol_hist[i] = fin_stats.vol_hist[i];
      loc_stats->dense_hist[i] = fin_stats.dense_hist[i];
    }

    DIY_Destroy_datatype(&dtype);
    MPI_Op_free(&op1);
    MPI_Op_free(&op2);

  }
  else {

    loc_stats->num_vol_bins   = glo_stats.num_vol_bins;
    loc_stats->num_dense_bins = glo_stats.num_dense_bins;
    for (i = 0; i < MAX_HIST_BINS; i++) {
      loc_stats->vol_hist[i] = glo_stats.vol_hist[i];
      loc_stats->dense_hist[i] = glo_stats.dense_hist[i];
    }

  }

}
/*--------------------------------------------------------------------------*/
/*
  reduces averages and ranges

  in: input 1
  inout: input 2 and output
  len: 1
  datatype: unused
*/
void average(void *in, void *inout, int *len, MPI_Datatype *type) {

  /* quiet compiler warnings about unused variables */
  type = type;
  len = len;

  struct stats_t *stats1 = (struct stats_t *)in;
  struct stats_t *stats2 = (struct stats_t *)inout;

  /* weights for weighted averages based on cell counts */
  float w1 = (float)stats1->tot_cells / 
    (stats1->tot_cells + stats2->tot_cells);
  float w2 = (float)stats2->tot_cells / 
    (stats1->tot_cells + stats2->tot_cells);

  /* weighted average of two averages */
  stats2->avg_cell_verts = w1 * stats1->avg_cell_verts +
    w2 * stats2->avg_cell_verts;
  stats2->avg_cell_faces = w1 * stats1->avg_cell_faces +
    w2 * stats2->avg_cell_faces;
  stats2->avg_cell_vol = w1 * stats1->avg_cell_vol +
    w2 * stats2->avg_cell_vol;
  stats2->avg_cell_dense = w1 * stats1->avg_cell_dense +
    w2 * stats2->avg_cell_dense;

  /* new weights for weighted averages based on face counts */
  w1 = (float)stats1->tot_faces / 
    (stats1->tot_faces + stats2->tot_faces);
  w2 = (float)stats2->tot_faces / 
    (stats1->tot_faces + stats2->tot_faces);

  /* weighted average of two averages */
  stats2->avg_face_verts = w1 * stats1->avg_face_verts + 
    w2 * stats2->avg_face_verts;

  stats2->tot_tets += stats1->tot_tets;
  stats2->tot_cells += stats1->tot_cells;
  stats2->tot_verts += stats1->tot_verts;

  if (stats1->min_cell_vol < stats2->min_cell_vol)
    stats2->min_cell_vol = stats1->min_cell_vol;
  if (stats1->max_cell_vol > stats2->max_cell_vol)
    stats2->max_cell_vol = stats1->max_cell_vol;
  if (stats1->min_cell_dense < stats2->min_cell_dense)
    stats2->min_cell_dense = stats1->min_cell_dense;
  if (stats1->max_cell_dense > stats2->max_cell_dense)
    stats2->max_cell_dense = stats1->max_cell_dense;
  if (stats1->min_cell_time < stats2->min_cell_time)
    stats2->min_cell_time = stats1->min_cell_time;
  if (stats1->max_cell_time > stats2->max_cell_time)
    stats2->max_cell_time = stats1->max_cell_time;
  if (stats1->min_vol_time < stats2->min_vol_time)
    stats2->min_vol_time = stats1->min_vol_time;
  if (stats1->max_vol_time > stats2->max_vol_time)
    stats2->max_vol_time = stats1->max_vol_time;

  /* ought to do a cross-validation to find correct number of bins
     for now just pick a number */
  stats2->num_vol_bins = 50;
  stats2->num_dense_bins = 100;

}
/*--------------------------------------------------------------------------*/
/*
  reduces histograms

  in: input 1
  inout: input 2 and output
  len: 1
  datatype: unused
*/
void histogram(void *in, void *inout, int *len, MPI_Datatype *type) {

  /* quiet compiler warnings about unused variables */
  type = type;
  len = len;

  struct stats_t *stats1 = (struct stats_t *)in;
  struct stats_t *stats2 = (struct stats_t *)inout;
  int i;

  stats2->tot_tets      = stats1->tot_tets;
  stats2->tot_cells      = stats1->tot_cells;
  stats2->tot_faces      = stats1->tot_faces;
  stats2->tot_verts      = stats1->tot_verts;
  stats2->avg_cell_verts = stats1->avg_cell_verts;
  stats2->avg_cell_faces = stats1->avg_cell_faces;
  stats2->avg_face_verts = stats1->avg_face_verts;
  stats2->min_cell_vol   = stats1->min_cell_vol;
  stats2->max_cell_vol   = stats1->max_cell_vol;
  stats2->avg_cell_vol   = stats1->avg_cell_vol;
  stats2->min_cell_dense = stats1->min_cell_dense;
  stats2->max_cell_dense = stats1->max_cell_dense;
  stats2->avg_cell_dense = stats1->avg_cell_dense;
  stats2->min_cell_time  = stats1->min_cell_time;
  stats2->max_cell_time  = stats1->max_cell_time;
  stats2->min_vol_time  = stats1->min_vol_time;
  stats2->max_vol_time  = stats1->max_vol_time;
  stats2->num_vol_bins   = stats1->num_vol_bins;
  stats2->num_dense_bins = stats1->num_dense_bins;
  for (i = 0; i < stats2->num_vol_bins; i++)
    stats2->vol_hist[i] += stats1->vol_hist[i];
  for (i = 0; i < stats2->num_dense_bins; i++)
    stats2->dense_hist[i] += stats1->dense_hist[i];

}
/*--------------------------------------------------------------------------*/
/*
  prepare for output

  nblocks: number of blocks
  vblocks: pointer to array of vblocks
*/
void prep_out(int nblocks, struct vblock_t *vblocks) {

  struct bb_t bounds; /* block bounds */
  int i, j;

  /* save extents */
  for (i = 0; i < nblocks; i++) {
    DIY_Block_bounds(0, i, &bounds);
    vblocks[i].mins[0] = bounds.min[0];
    vblocks[i].mins[1] = bounds.min[1];
    vblocks[i].mins[2] = bounds.min[2];
    vblocks[i].maxs[0] = bounds.max[0];
    vblocks[i].maxs[1] = bounds.max[1];
    vblocks[i].maxs[2] = bounds.max[2];
  }

  /* save vertices (float version) */
  for (i = 0; i < nblocks; i++) {
    vblocks[i].save_verts = (float *)malloc(vblocks[i].num_verts * 3 * 
					       sizeof(float));
    for (j = 0; j < vblocks[i].num_verts; j++) {

        vblocks[i].save_verts[3 * j]     = vblocks[i].verts[3 * j];
        vblocks[i].save_verts[3 * j + 1] = vblocks[i].verts[3 * j + 1];
        vblocks[i].save_verts[3 * j + 2] = vblocks[i].verts[3 * j + 2];

    }
    free (vblocks[i].verts);
    vblocks[i].verts = NULL;
  }

}
/*--------------------------------------------------------------------------*/
/*
  save headers

  nblocks: number of blocks
  vblocks: pointer to array of vblocks
  hdrs: block headers
*/
void save_headers(int nblocks, struct vblock_t *vblocks, int **hdrs) {

  int i;

  for (i = 0; i < nblocks; i++) {

    hdrs[i][NUM_VERTS] = vblocks[i].num_verts;
    hdrs[i][TOT_NUM_CELL_VERTS] = vblocks[i].tot_num_cell_verts;
    hdrs[i][NUM_COMPLETE_CELLS] = vblocks[i].num_complete_cells;
/*     hdrs[i][TOT_NUM_CELL_FACES] = vblocks[i].tot_num_cell_faces; */
/*     hdrs[i][TOT_NUM_FACE_VERTS] = vblocks[i].tot_num_face_verts; */
    hdrs[i][NUM_ORIG_PARTICLES] = vblocks[i].num_orig_particles;
    hdrs[i][NUM_LOC_TETS] = vblocks[i].num_loc_tets;
    hdrs[i][NUM_REM_TETS] = vblocks[i].num_rem_tets;
    hdrs[i][NUM_FACES] = vblocks[i].num_faces;
    hdrs[i][TOT_NUM_CELL_FACES] = vblocks[i].tot_num_cell_faces;

  }

}
/*--------------------------------------------------------------------------*/
/*
  creates and initializes blocks and headers

  num_blocks: number of blocks
  vblocks: pointer to array of vblocks
  hdrs: pointer to array of headers, pass NULL if not used

  side effects: allocates memory for blocks and headers
*/
void create_blocks(int num_blocks, struct vblock_t **vblocks, int ***hdrs) {

  int i, j;

  /* allocate blocks and headers */
  *vblocks = (struct vblock_t*)malloc(sizeof(struct vblock_t) * 
				      num_blocks);
  if (hdrs)
    *hdrs = (int **)malloc(sizeof(int*) * num_blocks);

  for (i = 0; i < num_blocks; i++) {

    (*vblocks)[i].num_verts = 0;
    (*vblocks)[i].verts = NULL;
    (*vblocks)[i].save_verts = NULL;
    (*vblocks)[i].num_cell_verts = NULL;
    (*vblocks)[i].tot_num_cell_verts = 0;
    (*vblocks)[i].cells = NULL;
    (*vblocks)[i].sites = NULL;
    (*vblocks)[i].temp_num_complete_cells = 0;
    (*vblocks)[i].temp_complete_cells = NULL;
    (*vblocks)[i].num_complete_cells = 0;
    (*vblocks)[i].complete_cells = NULL;
    (*vblocks)[i].is_complete = NULL;
    (*vblocks)[i].areas = NULL;
    (*vblocks)[i].vols = NULL;
    (*vblocks)[i].face_areas = NULL;
    (*vblocks)[i].loc_tets = NULL;
    (*vblocks)[i].num_loc_tets = 0;
    (*vblocks)[i].rem_tet_gids = NULL;
    (*vblocks)[i].rem_tet_nids = NULL;
    (*vblocks)[i].rem_tet_wrap_dirs = NULL;
    (*vblocks)[i].num_rem_tets = 0;
    (*vblocks)[i].num_sent_particles = 0;
    (*vblocks)[i].alloc_sent_particles = 0;
    (*vblocks)[i].sent_particles = NULL;
    (*vblocks)[i].num_faces = 0;
    (*vblocks)[i].tot_num_cell_faces = 0;
    (*vblocks)[i].faces = NULL;
    (*vblocks)[i].cell_faces_start = NULL;
    (*vblocks)[i].cell_faces = NULL;

    if (hdrs) {
      (*hdrs)[i] = (int *)malloc(sizeof(int) * DIY_MAX_HDR_ELEMENTS);
      for (j = 0; j < DIY_MAX_HDR_ELEMENTS; j++)
	((*hdrs)[i])[j] = 0;
    }

  }

}
/*---------------------------------------------------------------------------*/
/*
  frees blocks and headers

  num_blocks: number of blocks
  vblocks: pointer to array of vblocks
  hdrs: pointer to array of headers, pass NULL if not used
*/
void destroy_blocks(int num_blocks, struct vblock_t *vblocks, int **hdrs) {

  int i;

  for (i = 0; i < num_blocks; i++) {
    if (hdrs && hdrs[i])
      free(hdrs[i]);
    if (vblocks[i].verts)
      free(vblocks[i].verts);
    if (vblocks[i].save_verts)
      free(vblocks[i].save_verts);
    if (vblocks[i].num_cell_verts)
      free(vblocks[i].num_cell_verts);
    if (vblocks[i].cells)
      free(vblocks[i].cells);
    if (vblocks[i].sites)
      free(vblocks[i].sites);
    if (vblocks[i].temp_complete_cells)
      free(vblocks[i].temp_complete_cells);
    if (vblocks[i].complete_cells)
      free(vblocks[i].complete_cells);
    if (vblocks[i].is_complete)
      free(vblocks[i].is_complete);
    if (vblocks[i].areas)
      free(vblocks[i].areas);
    if (vblocks[i].vols)
      free(vblocks[i].vols);
    if (vblocks[i].face_areas)
      free(vblocks[i].face_areas);
    if (vblocks[i].loc_tets)
      free(vblocks[i].loc_tets);
    if (vblocks[i].rem_tet_gids)
      free(vblocks[i].rem_tet_gids);
    if (vblocks[i].rem_tet_nids)
      free(vblocks[i].rem_tet_nids);
    if (vblocks[i].rem_tet_wrap_dirs)
      free(vblocks[i].rem_tet_wrap_dirs);
    if (vblocks[i].sent_particles)
      free(vblocks[i].sent_particles);
    if (vblocks[i].faces)
      free(vblocks[i].faces);
    if (vblocks[i].cell_faces_start)
      free(vblocks[i].cell_faces_start);
    if (vblocks[i].cell_faces)
      free(vblocks[i].cell_faces);
  }

  free(hdrs);
  free(vblocks);

}
/*---------------------------------------------------------------------------*/
/*
  determines cells that are incomplete or too close to neighbor such that
  they might change after neighbor exchange. The particles corresponding
  to sites of these cells are enqueued for exchange with neighors

  tblock: one temporary voronoi block
  vblock: one voronoi block
  lid: local id of block
*/
void incomplete_cells(struct vblock_t *tblock, struct vblock_t *vblock,
		      int lid) {

  struct bb_t bounds; /* block bounds */
  int vid; /* vertex id */
  int i, j, k, n;
  int chunk_size = 1024; /* allocation chunk size for sent particles */
  struct remote_particle_t rp; /* particle being sent or received */
  struct sent_t sent; /* info about sent particle saved for later */
  int complete; /* no vertices in the cell are the infinite vertex */

  DIY_Block_bounds(0, lid, &bounds);

  /* get gids of all neighbors, in case a particle needs to be
     sent to all neighbors
     (enumerating all gids manually (not via DIY_Enqueue_Item_all)
     to be consisent with enumerating particular neighbors) */
  int num_all_neigh_gbs = DIY_Num_neighbors(0, lid);
  struct gb_t all_neigh_gbs[MAX_NEIGHBORS];
  DIY_Get_neighbors(0, lid, all_neigh_gbs);

  n = 0; /* index into tblock->cells */

  /* for all cells */
  for (j = 0; j < tblock->num_orig_particles; j++) {

    complete = 1; /* assume complete cell unless found otherwise */
    sent.num_gbs = 0;

    /* for all vertex indices in the current cell */
    for (k = 0; k < tblock->num_cell_verts[j]; k++) {

      vid = tblock->cells[n];

      /* radius of delaunay circumshpere is the distance from
	 voronoi vertex to voronoi site */
      float sph_rad =
	sqrt((tblock->verts[3 * vid] - tblock->sites[3 * j]) *
	     (tblock->verts[3 * vid] - tblock->sites[3 * j]) +
	     (tblock->verts[3 * vid + 1] - tblock->sites[3 * j + 1]) *
	     (tblock->verts[3 * vid + 1] - tblock->sites[3 * j + 1]) +
	     (tblock->verts[3 * vid + 2] - tblock->sites[3 * j + 2]) *
	     (tblock->verts[3 * vid + 2] - tblock->sites[3 * j + 2]));

      /* if a vertex is not the infinite vertex, add any neighbors
	 within the delaunay circumsphere radius of block bounds */
      if (vid) {
	float pt[3]; /* target point as a float (verts are double) */
	pt[0] = tblock->verts[3 * vid];
	pt[1] = tblock->verts[3 * vid + 1];
	pt[2] = tblock->verts[3 * vid + 2];
	DIY_Add_gbs_all_near(0, lid, sent.neigh_gbs, &(sent.num_gbs),
			     MAX_NEIGHBORS, pt, sph_rad);
      }
      else
	complete = 0;

      n++;

    } /* for all vertex indices in this cell */

    rp.x = tblock->sites[3 * j];
    rp.y = tblock->sites[3 * j + 1];
    rp.z = tblock->sites[3 * j + 2];
    rp.gid = DIY_Gid(0, lid);
    rp.nid = j;
    rp.dir = 0x00;

    /* particle needs to be sent either to particular neighbors or to all */
    if (!complete || sent.num_gbs) {

      /* incomplete cell goes to all neighbors except self */
      if (!complete) {
	sent.num_gbs = 0;
	int my_gid = DIY_Gid(0, lid);
	for (i = 0; i < num_all_neigh_gbs; i++) {
	  if (all_neigh_gbs[i].gid != my_gid || 
	      all_neigh_gbs[i].neigh_dir != 0x00) {
	    sent.neigh_gbs[sent.num_gbs].gid = all_neigh_gbs[i].gid;
	    sent.neigh_gbs[sent.num_gbs].neigh_dir = all_neigh_gbs[i].neigh_dir;
	    sent.num_gbs++;
	  }
	}
      }

      DIY_Enqueue_item_gbs(0, lid, (void *)&rp,
			   NULL, sizeof(struct remote_particle_t),
			   sent.neigh_gbs, sent.num_gbs,
			   &transform_particle);

      /* save the details of the sent particle for later sending
	 completion status of sent particles to same neighbors */
      sent.particle = j;
      add_sent(sent, &(vblock->sent_particles),
	       &(vblock->num_sent_particles),
	       &(vblock->alloc_sent_particles), chunk_size);

    } /* if !complete || sent.num_gbs) */

  } /* for all cells */

}
/*--------------------------------------------------------------------------*/
/*
  determines connectivity of faces in complete cells

  vblock: one voronoi block

  side effects: allocates memory for cell_faces and cell_faces_start 
  in voronoi block
*/
void cell_faces(struct vblock_t *vblock) {

  int cell; /* current cell */
  int i;

  /* todo: allocate to size of complete cells and don't store faces for
     incomplete cells */

  /* temporary count of number of faces in each of my original cells */
  int *counts = (int *)malloc(vblock->num_orig_particles *
			      sizeof(int));
  /* starting offset of faces in each of my original cells */
  vblock->cell_faces_start = (int *)malloc(vblock->num_orig_particles *
					   sizeof(int));
  memset(counts, 0, vblock->num_orig_particles * sizeof(int));

  /* pass 1: traverse faces array and get number of faces in each cell
     use face starting offsets array temporarily to hold face counts,
     will convert to starting offsets (prefix sum of counts) later */
  vblock->tot_num_cell_faces = 0;
  for (i = 0; i < vblock->num_faces; i++) {
    cell = vblock->faces[i].cells[0];
    /* each block retains only those cells and their faces whose particles 
       it originally had */
    if (cell < vblock->num_orig_particles) {
      counts[cell]++;
      vblock->tot_num_cell_faces++;
    }
    cell = vblock->faces[i].cells[1];
    if (cell < vblock->num_orig_particles) {
      counts[cell]++;
      vblock->tot_num_cell_faces++;
    }
  }

  /* convert face counts to starting offsets and offset of end (used to
     compute face count of last cell */
  vblock->cell_faces_start[0] = 0;
  for (i = 1; i < vblock->num_orig_particles; i++)
    vblock->cell_faces_start[i] = vblock->cell_faces_start[i - 1] + 
      counts[i - 1];

  /* allocate cell_faces */
  vblock->cell_faces = (int *)malloc(vblock->tot_num_cell_faces * sizeof(int));

  /* pass 2: traverse faces array and save face ids for each cell */
  memset(counts, 0, vblock->num_orig_particles * sizeof(int));
  for (i = 0; i < vblock->num_faces; i++) {
    cell = vblock->faces[i].cells[0];
    /* again, each block retains only those cells and their faces 
       whose particles it originally had */
    if (cell < vblock->num_orig_particles) {
      int id = vblock->cell_faces_start[cell] + counts[cell];
      vblock->cell_faces[id] = i;
      counts[cell]++;
    }
    cell = vblock->faces[i].cells[1];
    if (cell < vblock->num_orig_particles) {
      int id = vblock->cell_faces_start[cell] + counts[cell];
      vblock->cell_faces[id] = i;
      counts[cell]++;
    }
  }

  /* cleanup */
  free(counts);

}
/*--------------------------------------------------------------------------*/
/*
  determines complete cells: cells that don't contain qhull'sinfinite vertex or
  any other vertices outside of the block bounds

  vblock: one voronoi block
  lid: local id of block

  side effects: allocates memory for complete cells in voronoi block
*/
void complete_cells(struct vblock_t *vblock, int lid) {

  struct bb_t bounds; /* block bounds */
  int vid, vid1; /* vertex id */
  int j, k, n, m;
  double d2_min = 0.0; /* dia^2 of circumscribing sphere of volume min_vol */
  int start_n; /* index into cells at start of new cell */
  int too_small; /* whether cell volume is definitely below threshold */

  /* allocate memory based on number of cells and average number of
     faces and vertices in a cell
     this is wasteful if filtering on volume because we will probably
     only need a fraction of this memory
     todo: fix this with my own memory manager */
  vblock->temp_complete_cells =
	  (int *)malloc(vblock->num_orig_particles * sizeof(int));
  vblock->complete_cells =
	  (int *)malloc(vblock->num_orig_particles * sizeof(int));

  DIY_Block_bounds(0, lid, &bounds);

  /* minimum cell diameter squared */
  if (min_vol > 0.0)
    /* d^2 = 4 * (3/4 * min_vol / pi)^ (2/3) */
    d2_min = 1.539339 * pow(min_vol, 0.66667);

  /* find complete cells */

  vblock->temp_num_complete_cells = 0;
  n = 0; /* index into vblock->cells */

  /* for all cells up to original number of input particles (each block 
     retains only those cells whose particles it originally had) */
  for (j = 0; j < vblock->num_orig_particles; j++) {

    /* init */
    if (!vblock->num_cell_verts[j])
      continue;

    int complete = 1;
    too_small = (min_vol > 0.0 ? 1 : 0);

    /* debug */
/*     fprintf(stderr, "cell %d has %d verts: ", j, vblock->num_cell_verts[j]); */

    /* for all vertex indices in the current cell */
    for (k = 0; k < vblock->num_cell_verts[j]; k++) {

      if (k == 0)
	start_n = n;

      vid = vblock->cells[n];

      /* debug */
/*       fprintf(stderr, "%d ", vid); */

      float eps = 1.0e-6;
      if ( /* vertex can fail for the following reasons */

	  /* qhull's "infinite vertex" */
	  (fabs(vblock->verts[3 * vid] - vblock->verts[0]) < eps &&
	   fabs(vblock->verts[3 * vid + 1] - vblock->verts[1]) < eps &&
	   fabs(vblock->verts[3 * vid + 2] - vblock->verts[2]) < eps) ||

	  /* out of overall data bounds when wrapping is off */
	  (!wrap_neighbors &&
	   (vblock->verts[3 * vid]     < data_mins[0] ||
	    vblock->verts[3 * vid]     > data_maxs[0] ||
	    vblock->verts[3 * vid + 1] < data_mins[1] ||
	    vblock->verts[3 * vid + 1] > data_maxs[1] ||
	    vblock->verts[3 * vid + 2] < data_mins[2] ||
	    vblock->verts[3 * vid + 2] > data_maxs[2]) ) ) {

	complete = 0;
	n += (vblock->num_cell_verts[j] - k); /* skip rest of this cell */
	break;

      } /* if */

      /* check minimum volume if enabled and it has not been excceded yet */
      if (too_small) {

	/* for all vertices in this cell */
	for (m = start_n; m < start_n + vblock->num_cell_verts[j]; m++) {
	  vid1 = vblock->cells[m];
	  double d2 =
	    (vblock->verts[3 * vid] - vblock->verts[3 * vid1]) *
	    (vblock->verts[3 * vid] - vblock->verts[3 * vid1]) +
	    (vblock->verts[3 * vid + 1] - vblock->verts[3 * vid1 + 1]) *
	    (vblock->verts[3 * vid + 1] - vblock->verts[3 * vid1 + 1]) +
	    (vblock->verts[3 * vid + 2] - vblock->verts[3 * vid1 + 2]) *
	    (vblock->verts[3 * vid + 2] - vblock->verts[3 * vid1 + 2]);
	  if (d2 > d2_min) {
	    too_small = 0;
	    break;
	  }

	} /* all vertices in this cell */

      } /* small volume threshold */

      /* check if volume is too small at the end of the cell */
      if (k == vblock->num_cell_verts[j] - 1 && too_small)
	complete = 0;
      n++;

    } /* for all vertex indices in this cell */

    /* one last check that site is within cell bounds. if so, save the cell */
    if (complete && cell_bounds(vblock, j, start_n)) {
      (vblock->temp_complete_cells)[vblock->temp_num_complete_cells++] = j;
      vblock->is_complete[j] = 1;
    }
    else
      vblock->is_complete[j] = 0;

  } /* for all cells */

}
/*--------------------------------------------------------------------------*/
/*
  generates test particles for a  block

  lid: local id of block
  particles: pointer to particle vector in this order: 
  particle0x, particle0y, particle0z, particle1x, particle1y, particle1z, ...
  jitter: maximum amount to randomly move particles

  returns: number of particles in this block

  side effects: allocates memory for particles, caller's responsibility to free
*/
int gen_particles(int lid, float **particles, float jitter) {

  int sizes[3]; /* number of grid points */
  int i, j, k;
  int n = 0;
  int num_particles; /* throreticl num particles with duplicates at 
			block boundaries */
  int act_num_particles; /* actual number of particles unique across blocks */
  float jit; /* random jitter amount, 0 - MAX_JITTER */

  /* allocate particles */
  struct bb_t bounds;
  DIY_Block_bounds(0, lid, &bounds);
  sizes[0] = (int)(bounds.max[0] - bounds.min[0] + 1);
  sizes[1] = (int)(bounds.max[1] - bounds.min[1] + 1);
  sizes[2] = (int)(bounds.max[2] - bounds.min[2] + 1);

  num_particles = sizes[0] * sizes[1] * sizes[2];

  *particles = (float *)malloc(num_particles * 3 * sizeof(float));

  /* assign particles */

  n = 0;
  for (i = 0; i < sizes[0]; i++) {
    if (bounds.min[0] > 0 && i == 0) /* dedup block doundary points */
      continue;
    for (j = 0; j < sizes[1]; j++) {
      if (bounds.min[1] > 0 && j == 0) /* dedup block doundary points */
	continue;
      for (k = 0; k < sizes[2]; k++) {
	if (bounds.min[2] > 0 && k == 0) /* dedup block doundary points */
	  continue;

	/* start with particles on a grid */
	(*particles)[3 * n] = bounds.min[0] + i;
	(*particles)[3 * n + 1] = bounds.min[1] + j;
	(*particles)[3 * n + 2] = bounds.min[2] + k;

	/* and now jitter them */
	jit = rand() / (float)RAND_MAX * 2 * jitter - jitter;
	if ((*particles)[3 * n] - jit >= bounds.min[0] &&
	    (*particles)[3 * n] - jit <= bounds.max[0])
	  (*particles)[3 * n] -= jit;
	else if ((*particles)[3 * n] + jit >= bounds.min[0] &&
		 (*particles)[3 * n] + jit <= bounds.max[0])
	  (*particles)[3 * n] += jit;

	jit = rand() / (float)RAND_MAX * 2 * jitter - jitter;
	if ((*particles)[3 * n + 1] - jit >= bounds.min[1] &&
	    (*particles)[3 * n + 1] - jit <= bounds.max[1])
	  (*particles)[3 * n + 1] -= jit;
	else if ((*particles)[3 * n + 1] + jit >= bounds.min[1] &&
		 (*particles)[3 * n + 1] + jit <= bounds.max[1])
	  (*particles)[3 * n + 1] += jit;

	jit = rand() / (float)RAND_MAX * 2 * jitter - jitter;
	if ((*particles)[3 * n + 2] - jit >= bounds.min[2] &&
	    (*particles)[3 * n + 2] - jit <= bounds.max[2])
	  (*particles)[3 * n + 2] -= jit;
	else if ((*particles)[3 * n + 2] + jit >= bounds.min[2] &&
		 (*particles)[3 * n + 2] + jit <= bounds.max[2])
	  (*particles)[3 * n + 2] += jit;


	n++;

      }

    }

  }

  act_num_particles = n;

  return act_num_particles;

}
/*--------------------------------------------------------------------------*/
/*
  prints a block

  vblock: current voronoi block
  gid: global block id
*/
void print_block(struct vblock_t *vblock, int gid) {

  int i;

  fprintf(stderr, "block gid = %d, %d complete cells: ", 
	  gid, vblock->num_complete_cells);
  for (i = 0; i < vblock->num_complete_cells; i++)
    fprintf(stderr, "%d ", vblock->complete_cells[i]);
  fprintf(stderr, "\n");

}
/*--------------------------------------------------------------------------*/
/*
  prints particles

  prticles: particle array
  num_particles: number of particles
  gid: block global id
*/
void print_particles(float *particles, int num_particles, int gid) {

  int n;

  for (n = 0; n < num_particles; n++)
    fprintf(stderr, "block = %d particle[%d] = [%.1lf %.1lf %.1lf]\n",
	    gid, n, particles[3 * n], particles[3 * n + 1],
	    particles[3 * n + 2]);

}
/*--------------------------------------------------------------------------*/
/*
  transforms particles for enqueueing to wraparound neighbors
  p: pointer to particle
  wrap_dir: wrapping direcion
*/
void transform_particle(char *p, unsigned char wrap_dir) {

  /* debug */
  float particle[3]; /* original particle */
  particle[0] = ((struct remote_particle_t*)p)->x;
  particle[1] = ((struct remote_particle_t*)p)->y;
  particle[2] = ((struct remote_particle_t*)p)->z;

  /* wrapping toward the left transforms to the right */
  if ((wrap_dir & DIY_X0) == DIY_X0) {
    ((struct remote_particle_t*)p)->x += (data_maxs[0] - data_mins[0]);
    ((struct remote_particle_t*)p)->dir |= DIY_X0;
  }

  /* and vice versa */
  if ((wrap_dir & DIY_X1) == DIY_X1) {
    ((struct remote_particle_t*)p)->x -= (data_maxs[0] - data_mins[0]);
    ((struct remote_particle_t*)p)->dir |= DIY_X1;
  }

  /* similar for y, z */
  if ((wrap_dir & DIY_Y0) == DIY_Y0) {
    ((struct remote_particle_t*)p)->y += (data_maxs[1] - data_mins[1]);
    ((struct remote_particle_t*)p)->dir |= DIY_Y0;
  }

  if ((wrap_dir & DIY_Y1) == DIY_Y1) {
    ((struct remote_particle_t*)p)->y -= (data_maxs[1] - data_mins[1]);
    ((struct remote_particle_t*)p)->dir |= DIY_Y1;
  }

  if ((wrap_dir & DIY_Z0) == DIY_Z0) {
    ((struct remote_particle_t*)p)->z += (data_maxs[2] - data_mins[2]);
    ((struct remote_particle_t*)p)->dir |= DIY_Z0;
  }

  if ((wrap_dir & DIY_Z1) == DIY_Z1) {
    ((struct remote_particle_t*)p)->z -= (data_maxs[2] - data_mins[2]);
    ((struct remote_particle_t*)p)->dir |= DIY_Z1;
  }

}
/*--------------------------------------------------------------------------*/
/* 
   comparison function for qsort
*/
int compare(const void *a, const void *b) {

  if (*((int*)a) < *((int*)b))
    return -1;
  if (*((int*)a) == *((int*)b))
    return 0;
  return 1;

}
/*--------------------------------------------------------------------------*/
/*
  adds an int to a c-style vector of ints

  val: value to be added
  vals: pointer to dynamic array of values
  numvals: pointer to number of values currently stored, updated by add_int
  maxvals: pointer to number of values currently allocated
  chunk_size: number of values to allocate at a time

*/
void add_int(int val, int **vals, int *numvals, int *maxvals, int chunk_size) {

  /* first time */
  if (*maxvals == 0) {
    *vals = (int *)malloc(chunk_size * sizeof(int));
    *numvals = 0;
    *maxvals = chunk_size;
  }

  /* grow memory */
  else if (*numvals >= *maxvals) {
    *vals = (int *)realloc(*vals, 
			       (chunk_size + *maxvals) * sizeof(int));
    *maxvals += chunk_size;
  }

  /* add the element */
  (*vals)[*numvals] = val;
  (*numvals)++;

}
/*--------------------------------------------------------------------------*/
/*
  adds a sent particle to a c-style vector of sent particles

  val: sent particle to be added
  vals: pointer to dynamic array of sent particles
  numvals: pointer to number of values currently stored, updated by add_int
  maxvals: pointer to number of values currently allocated
  chunk_size: number of values to allocate at a time

*/
void add_sent(struct sent_t val, struct sent_t **vals, int *numvals, 
	      int *maxvals, int chunk_size) {

  int i;

  /* first time */
  if (*maxvals == 0) {
    *vals = (struct sent_t *)malloc(chunk_size * sizeof(struct sent_t));
    *numvals = 0;
    *maxvals = chunk_size;
  }

  /* grow memory */
  else if (*numvals >= *maxvals) {
    *vals = 
      (struct sent_t *)realloc(*vals, 
			       (chunk_size + *maxvals) * 
			       sizeof(struct sent_t));
    *maxvals += chunk_size;
  }

  /* add the element */
  (*vals)[*numvals].particle = val.particle;
  (*vals)[*numvals].num_gbs = val.num_gbs;
  for (i = 0; i < MAX_NEIGHBORS; i++) {
    (*vals)[*numvals].neigh_gbs[i].gid = val.neigh_gbs[i].gid;
    (*vals)[*numvals].neigh_gbs[i].neigh_dir = val.neigh_gbs[i].neigh_dir;
  }
  (*numvals)++;

}
/*--------------------------------------------------------------------------*/
/*
  checks if an array of ints has been allocated large enough to access
  a given index. If not, grows the array and initializes the empty ints

  vals: pointer to dynamic array of ints
  index: desired index to be accessed
  numitems: pointer to number of items currently stored, ie, 
    last subscript accessed + 1, updated by this function
  maxitems: pointer to number of items currently allocated
  chunk_size: minimum number of items to allocate at a time
  init_val: initalization value for newly allocated items

*/
void add_empty_int(int **vals, int index, int *numitems, int *maxitems, 
		   int chunk_size, int init_val) {

  int i;
  int alloc_chunk; /* max of chunk_size and chunk needed to get to the index */

  /* first time */
  if (*maxitems == 0) {

    /* allocate */
    alloc_chunk = (index < chunk_size ? chunk_size : index + 1);
    *vals = (int *)malloc(alloc_chunk * sizeof(int));
    /* init empty vals */
    for (i = 0; i < alloc_chunk; i++)
      (*vals)[i] = init_val;

    *numitems = 0;
    *maxitems = alloc_chunk;

  }

  /* grow memory */
  else if (index >= *maxitems) {

    /* realloc */
    alloc_chunk = 
      (index < *maxitems + chunk_size ? chunk_size : index + 1 - *maxitems);
    *vals = (int *)realloc(*vals, (alloc_chunk + *maxitems) * sizeof(int));

    /* init empty buckets */
    for (i = *maxitems; i < *maxitems + alloc_chunk; i++)
      (*vals)[i] = init_val;

    *maxitems += alloc_chunk;

  }

  (*numitems)++;

}
/*--------------------------------------------------------------------------*/
/* 
   checks whether the site of the cell is inside the bounds of
   temporary cell (uses the original cells, not the complete cells, and
   the double version of verts, not save_verts)

   vblock: one voronoi block
   cell: current cell counter
   vert: current vertex counter

   returns: 1 = site is inside cell bounds, 0 = site is outside cell bounds
 */
 int cell_bounds(struct vblock_t *vblock, int cell, int vert) {

  float cell_min[3], cell_max[3];
  int k;

  /* get cell bounds */
  for (k = 0; k < vblock->num_cell_verts[cell]; k++) { /* vertices */

    int v = vblock->cells[vert];
	  
    if ((k == 0) || vblock->verts[3 * v] < cell_min[0])
      cell_min[0] = vblock->verts[3 * v];
    if ((k == 0) || vblock->verts[3 * v] > cell_max[0])
      cell_max[0] = vblock->verts[3 * v];

    if ((k == 0) || vblock->verts[3 * v + 1] < cell_min[1])
      cell_min[1] = vblock->verts[3 * v + 1];
    if ((k == 0) || vblock->verts[3 * v + 1] > cell_max[1])
      cell_max[1] = vblock->verts[3 * v + 1];

    if ((k == 0) || vblock->verts[3 * v + 2] < cell_min[2])
      cell_min[2] = vblock->verts[3 * v + 2];
    if ((k == 0) || vblock->verts[3 * v + 2] > cell_max[2])
      cell_max[2] = vblock->verts[3 * v + 2];

    vert++;

  } /* vertices */

  /* check that site of cell is in the interior of the bounds (sanity) */
  if (vblock->sites[3 * cell] < cell_min[0] ||
      vblock->sites[3 * cell] > cell_max[0] ||
      vblock->sites[3 * cell + 1] < cell_min[1] ||
      vblock->sites[3 * cell + 1] > cell_max[1] ||
      vblock->sites[3 * cell + 2] < cell_min[2] ||
      vblock->sites[3 * cell + 2] > cell_max[2]) {
    fprintf(stderr, "warning: the site for cell %d "
	    "[%.3f %.3f %.3f] is not "
	    "inside the cell bounds min [%.3f %.3f %.3f] "
	    "max [%.3f %.3f %.3f]; skipping this cell\n",
	    cell, vblock->sites[3 * cell], vblock->sites[3 * cell + 1], 
	    vblock->sites[3 * cell + 2],
	    cell_min[0], cell_min[1], cell_min[2],
	    cell_max[0], cell_max[1], cell_max[2]);

    return 0;
  }

  else
    return 1;

} 
/*--------------------------------------------------------------------------*/
/* 
   determines if the tetrahedron is local, and records the necessary information in vblock
  
   tet_verts: vertices of the tetrahedron
   n: number of vertices in strictly local final tets
   m: number of vertices in non strictly local final tets
 */
void gen_delaunay_tet(int tet_verts[4], struct vblock_t *vblock,
		      int *gids, int *nids, unsigned char *dirs,
		      struct remote_ic_t *rics, int lid, int num_recvd,
		      int *n, int *m) {

  int v; /* vertex in current tet (0, 1, 2, 3) */
  int i;

  /* test whether tet is strictly local (all vertices are local) or not */
  for (v = 0; v < 4; v++) {
    if (tet_verts[v] >= vblock->num_orig_particles)
        break;
  }
  if (v == 4) { /* local, store it */
   /* filter out tets that touch local incomplete voronoi cells */
    int v1;
    for (v1 = 0; v1 < 4; v1++) {
      if (!vblock->is_complete[tet_verts[v1]])
        break;
    }
    if (v1 == 4) {
      int v2;
      for (v2 = 0; v2 < 4; v2++)
        vblock->loc_tets[(*n)++] = tet_verts[v2];
    }
  }
  /* not strictly local, at least one vertex is remote, and at least one
     vertex is local */
  else if (tet_verts[0] < vblock->num_orig_particles ||
           tet_verts[1] < vblock->num_orig_particles ||
           tet_verts[2] < vblock->num_orig_particles ||
           tet_verts[3] < vblock->num_orig_particles) {

    /* decide whether I should own this tet, owner will be minimum
       block gid of all contributors to this tet */
    int sort_gids[4]; /* gids of 4 vertices */
    for (v = 0; v < 4; v++) {
      if (tet_verts[v] < vblock->num_orig_particles)
        sort_gids[v] = DIY_Gid(0, lid);
      else
        sort_gids[v] = gids[tet_verts[v] - vblock->num_orig_particles];
    }
    qsort(sort_gids, 4, sizeof(int), &compare);

    /* I will own the tet */
    if (sort_gids[0] == DIY_Gid(0, lid)) {

      /* filter out tets that touch local incomplete voronoi cells */
      for (v = 0; v < 4; v++) {

        /* if this vertex is local, check its completion status */
        if (tet_verts[v] < vblock->num_orig_particles &&
            !vblock->is_complete[tet_verts[v]])
          break;

        /* if this vertex is remote, check its completion status */
        if (tet_verts[v] >= vblock->num_orig_particles) {

          /* find the correct entry in the completion status
             todo: linear search for now, accelerate later */
          for (i = 0; i < num_recvd; i++) {
            if (rics[i].gid == 
      	  gids[tet_verts[v] - vblock->num_orig_particles] &&
      	  rics[i].nid == 
      	  nids[tet_verts[v] - vblock->num_orig_particles])
      	break;
          }
          assert(i < num_recvd); /* sanity */
          if (!rics[i].is_complete)
            break;
        } /* if vertex is remote */

      } /* for four vertices */

      if (v == 4) { /* complete */

        int v1;
        /* save four remote verts */
        for (v1 = 0; v1 < 4; v1++) {

          /* this vertex is local */
          if (tet_verts[v1] < vblock->num_orig_particles) {
            vblock->rem_tet_gids[*m] = DIY_Gid(0, lid);
            vblock->rem_tet_nids[*m] = tet_verts[v1];
            vblock->rem_tet_wrap_dirs[*m] = 0x00;
          }
          /* this vertex is remote */
          else {
            /* need to subtract number of original (local) particles 
      	 from vertex to index into gids and nids; 
      	 they are only for remote particles but tet verts
      	 are for all particles, local + remote */
            vblock->rem_tet_gids[*m] =
      	gids[tet_verts[v1] - vblock->num_orig_particles];
            vblock->rem_tet_nids[*m] = 
      	nids[tet_verts[v1] - vblock->num_orig_particles];
            vblock->rem_tet_wrap_dirs[*m] = 
      	dirs[tet_verts[v1] - vblock->num_orig_particles];
          }

          (*m)++;

        } /* same four remote verts */

      } /* complete */

    } /* I will own this tet */

  } /* not strictly local */
}
/*--------------------------------------------------------------------------*/
