//---------------------------------------------------------------------------
//
// voronoi rendering
//
// Tom Peterka
// Argonne National Laboratory
// 9700 S. Cass Ave.
// Argonne, IL 60439
// tpeterka@mcs.anl.gov
//
// (C) 2011 by Argonne National Laboratory.
// See COPYRIGHT in top-level directory.
//
//--------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "voronoi.h"
#include "ser_io.hpp"
#include <math.h>

#if defined(MAC_OSX)
#include <GLUT/glut.h> 
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/glut.h> 
#include <GL/gl.h>
#endif

#define SPHERE_RAD_FACTOR .005 // used to compute sphere radius

#define PNETCDF_IO

#ifndef SERIAL_IO
#include "mpi.h"
#include "io.h"
#endif

using namespace std;

// 3d point or vector
struct vec3d {
  float x, y, z;
};

// 2d point or vector
struct vec2d {
  float x, y;
};

// color
struct rgba {
  float r, g, b, a;
};

// mouse button state
int press_x, press_y; 
int release_x, release_y; 

// rotate
vec2d rot = {0.0, 0.0};
float rot_rate = 0.2;

// scale
float scale = 1.0; 
float scale_rate = 0.01;
vec2d aspect; // scaling due to window aspect ratio

// near clip plane
float near = 0.1;

// window size
// vec2d win_size = {1024, 512};
vec2d win_size = {1024, 1024};
// vec2d win_size = {512, 512};

// previous window size
vec2d old_win_size;

// translate
vec2d trans = {0.0, 0.0};
float trans_rate = 0.01;

// transform mode
int xform_mode = 0; 
bool block_mode = false;
#define XFORM_NONE    0 
#define XFORM_ROTATE  1
#define XFORM_SCALE   2 
#define XFORM_TRANS   3

// rendering mode
bool draw_fancy = false;
bool draw_particle = true;
bool color_density = false;
bool draw_tess = false;
bool draw_del = false;

// volume filtering
float min_vol = 0.0; // desired min vol threshold
float max_vol = 0.0; // desired max vol threshold
float min_vol_act = 0.0; // actual min vol we have
float max_vol_act = 0.0; // actual max vol we have
float min_vol_clamp = 1.0e-6; // clamp min_vol_act to this value
float vol_step = 0.001;

vec3d sizes; // individual data sizes in each dimension
float size; // one overall number for data size, max of individual sizes
float sphere_rad; // sphere radius
float clip = 0.0; // clipping faces at this fraction of size (0.0-1.0)
float z_clip; // z value of clipping (only in z for now)

// voronoi sites
vector<vec3d> sites;
vec3d site_min;
vec3d site_max;
vec3d site_center;

// voronoi vertices, faces, cells
vector<vec3d> verts;
vector<int> num_face_verts;

// delaunay tet vertics
vector<vec3d> tet_verts;

// volumes associated with faces
vector <float> face_vols;

// voronoi blocks
vblock_t **vblocks;
int nblocks;

// general prupose quadrics
GLUquadricObj *q;

// point sprite texture
static GLubyte sprite_intensity[5][5] = {
  {  50,    50,   50,   50,  50,  },
  {  50,   100,  100,  100,  50,  },
  {  50,   100,  255,  100,  50,  },
  {  50,   100,  100,  100,  50,  },
  {  50,    50,   50,   50,  50,  },
};
static GLubyte sprite_rgba[5][5][4];
static GLuint tex;

// function prototypes
void display();
void init_display();
void draw_cube(float *mins, float *maxs, float r, float g, float b) ;
void mouse(int button, int state, int x, int y);
void motion(int x, int y);
void key(unsigned char key, int x, int y);
void timer(int val);
void draw_sphere(rgba &color, vec3d &pos, float rad);
void draw_spheres(vector<vec3d> &sites, float rad);
void draw_sprites(vector<vec3d> &sites, float size);
void draw_axes();
void draw_tets();
void reshape(int w, int h);
void init_model();
void init_viewport(bool reset);
void headlight();
void ComputeNormal(vec3d *verts, int num_verts, vec3d &normal);
void filter_volume(float min_vol, float max_vol);
void clip_cells(float z_clip);
void CellBounds(vblock_t *vblock, int cell, int face, int vert,
		float *cell_min, float *cell_max, float *centroid);
int compare(const void *a, const void *b);

//--------------------------------------------------------------------------

int main(int argc, char** argv) {

  int num_vis_cells = 0; // numbe of visible cells
  // total number of local and remote tets
  int num_loc_tets = 0;
  int num_rem_tets = 0;
  int n, m;

  if (argc < 3) {
    fprintf(stderr, "Usage: draw <filename> <swap (0 or 1)>"
	    " [min. volume (optional)] [max volume (optional)]\n");
    exit(0);
  }

  int swap_bytes = atoi(argv[2]);

  if (argc > 3)
    min_vol = atof(argv[3]);

  if (argc > 4)
    max_vol = atof(argv[4]);

  // read the file

#ifdef PNETCDF_IO

  int tot_blocks; // total number of blocks
  int *gids; // block global ids (unused)
  int *num_neighbors; // number of neighbors for each local block (unused)
  int **neighbors; // neighbors of each local block (unused)
  int **neigh_procs; // procs of neighbors of each local block (unused)
  swap_bytes = swap_bytes; // quiet compiler warning, unused w/ pnetcdf
  MPI_Init(&argc, &argv);
  pnetcdf_read(&nblocks, &tot_blocks, &vblocks, argv[1], MPI_COMM_WORLD,
	       &gids, &num_neighbors, &neighbors, &neigh_procs);
  MPI_Finalize();

#else

  SER_IO *io = new SER_IO(swap_bytes); // io object
  nblocks = io->ReadAllBlocks(argv[1], vblocks, false);

#endif

  // get overall data extent
  vec3d data_min, data_max;
  for (int i = 0; i < nblocks; i++) {
    if (i == 0) {
      data_min.x = vblocks[i]->mins[0];
      data_min.y = vblocks[i]->mins[1];
      data_min.z = vblocks[i]->mins[2];
      data_max.x = vblocks[i]->maxs[0];
      data_max.y = vblocks[i]->maxs[1];
      data_max.z = vblocks[i]->maxs[2];
    }
    if (vblocks[i]->mins[0] < data_min.x)
      data_min.x = vblocks[i]->mins[0];
    if (vblocks[i]->mins[1] < data_min.y)
      data_min.y = vblocks[i]->mins[1];
    if (vblocks[i]->mins[2] < data_min.z)
      data_min.z = vblocks[i]->mins[2];
    if (vblocks[i]->maxs[0] > data_max.x)
      data_max.x = vblocks[i]->maxs[0];
    if (vblocks[i]->maxs[1] > data_max.y)
      data_max.y = vblocks[i]->maxs[1];
    if (vblocks[i]->maxs[2] > data_max.z)
      data_max.z = vblocks[i]->maxs[2];
  }

  // debug
  fprintf(stderr, "data sizes mins[%.3f %.3f %.3f] maxs[%.3f %.3f %.3f]\n",
	  data_min.x, data_min.y, data_min.z, 
	  data_max.x, data_max.y, data_max.z);

  // package rendering data
  for (int i = 0; i < nblocks; i++) { // blocks

    // sites
    n = 0;
    for (int j = 0; j < vblocks[i]->num_orig_particles; j++) {

	vec3d s;
	s.x = vblocks[i]->sites[n];
	s.y = vblocks[i]->sites[n + 1];
	s.z = vblocks[i]->sites[n + 2];
	n += 3;
	sites.push_back(s);

    }

    // --- old version of cell faces

//     n = 0; // index into num_face_verts
//     m = 0; // index into face_verts
//     for (int j = 0; j < vblocks[i]->num_complete_cells; j++) { // cells

//       for (int k = 0; k < vblocks[i]->num_cell_faces[j]; k++) { // faces

// 	if (vblocks[i]->vols[j] >= min_vol &&
// 	    (max_vol <= 0.0 || vblocks[i]->vols[j] <= max_vol)) {
// 	  num_face_verts.push_back(vblocks[i]->num_face_verts[n]);
// 	  face_vols.push_back(vblocks[i]->vols[j]);
// 	  if (i == 0 && j == 0)
// 	    min_vol_act = vblocks[i]->vols[j];
// 	  if (vblocks[i]->vols[j] < min_vol_act)
// 	    min_vol_act = vblocks[i]->vols[j];
// 	  if (vblocks[i]->vols[j] > max_vol_act)
// 	    max_vol_act = vblocks[i]->vols[j];
// 	}

// 	// debug
// // 	fprintf(stderr, "num_complete cells = %d num_faces = %d "
// // 		"num_verts[face %d] = %d\n",
// // 		vblocks[i]->num_complete_cells, vblocks[i]->num_cell_faces[j],
// // 		n, vblocks[i]->num_face_verts[n]);

// 	for (int l = 0; l < vblocks[i]->num_face_verts[n]; l++) { // vertices

// 	  int v = vblocks[i]->face_verts[m];
// 	  vec3d s;
// 	  s.x = vblocks[i]->save_verts[3 * v];
// 	  s.y = vblocks[i]->save_verts[3 * v + 1];
// 	  s.z = vblocks[i]->save_verts[3 * v + 2];
// 	  m++;
// 	  if (vblocks[i]->vols[j] >= min_vol &&
// 	      (max_vol <= 0.0 || vblocks[i]->vols[j] <= max_vol))
// 	    verts.push_back(s);

// 	  // debug
// // 	  fprintf(stderr, "%d ", v);

// 	} // vertices

// 	// debug
// // 	fprintf(stderr, "\n");

// 	n++;

//       } // faces

//       if (vblocks[i]->vols[j] >= min_vol &&
// 	  (max_vol <= 0.0 || vblocks[i]->vols[j] <= max_vol))
// 	num_vis_cells++;

//     } // cells

    // ---- end of old version of cell faces

    // ---- new version of cell faces

    for (int j = 0; j < vblocks[i]->num_complete_cells; j++) { // cells

      int cell = vblocks[i]->complete_cells[j]; // current cell
      int num_faces; // number of face in the current cell
      int num_verts; // number of vertices in the current face

      if (cell < vblocks[i]->num_orig_particles - 1)
	num_faces = vblocks[i]->cell_faces_start[cell + 1] -
	  vblocks[i]->cell_faces_start[cell];
      else
	num_faces = vblocks[i]->new_tot_num_cell_faces -
	  vblocks[i]->cell_faces_start[cell];

      for (int k = 0; k < num_faces; k++) { // faces

	int start = vblocks[i]->cell_faces_start[cell];
	int face = vblocks[i]->cell_faces[start + k];
	num_verts = vblocks[i]->faces[face].num_verts;

	if (vblocks[i]->vols[j] >= min_vol &&
	    (max_vol <= 0.0 || vblocks[i]->vols[j] <= max_vol)) {
	  num_face_verts.push_back(num_verts);
	  face_vols.push_back(vblocks[i]->vols[j]);
	  if (i == 0 && j == 0)
	    min_vol_act = vblocks[i]->vols[j];
	  if (vblocks[i]->vols[j] < min_vol_act)
	    min_vol_act = vblocks[i]->vols[j];
	  if (vblocks[i]->vols[j] > max_vol_act)
	    max_vol_act = vblocks[i]->vols[j];
	}

	// debug
// 	fprintf(stderr, "cell = %d num_faces = %d start = %d "
// 		"num_verts[face %d] = %d\n",
// 		cell, num_faces, start, face, num_verts);

	for (int l = 0; l < num_verts; l++) { // vertices

	  int v = vblocks[i]->faces[face].verts[l];
	  vec3d s;
	  s.x = vblocks[i]->save_verts[3 * v];
	  s.y = vblocks[i]->save_verts[3 * v + 1];
	  s.z = vblocks[i]->save_verts[3 * v + 2];
	  if (vblocks[i]->vols[j] >= min_vol &&
	      (max_vol <= 0.0 || vblocks[i]->vols[j] <= max_vol))
	    verts.push_back(s);

	} // vertices

      } // faces

      if (vblocks[i]->vols[j] >= min_vol &&
	  (max_vol <= 0.0 || vblocks[i]->vols[j] <= max_vol))
	num_vis_cells++;

    } // cells

    // ---- end of new version of cell faces

    // local tets
    for (int j = 0; j < vblocks[i]->num_loc_tets; j++) {

      // site indices for tet vertices
      int s0 = vblocks[i]->loc_tets[4 * j];
      int s1 = vblocks[i]->loc_tets[4 * j + 1];
      int s2 = vblocks[i]->loc_tets[4 * j + 2];
      int s3 = vblocks[i]->loc_tets[4 * j + 3];

      // debug
//       int sort[] = {s0, s1, s2, s3};; // sorted version
//       qsort(sort, 4, sizeof(int), &compare);
//       fprintf(stderr, "%d %d %d %d\n", sort[0], sort[1], sort[2], sort[3]);

      // coordinates for tet vertices
      vec3d p0, p1, p2, p3;
      p0.x = vblocks[i]->sites[3 * s0];
      p0.y = vblocks[i]->sites[3 * s0 + 1];
      p0.z = vblocks[i]->sites[3 * s0 + 2];
      p1.x = vblocks[i]->sites[3 * s1];
      p1.y = vblocks[i]->sites[3 * s1 + 1];
      p1.z = vblocks[i]->sites[3 * s1 + 2];
      p2.x = vblocks[i]->sites[3 * s2];
      p2.y = vblocks[i]->sites[3 * s2 + 1];
      p2.z = vblocks[i]->sites[3 * s2 + 2];
      p3.x = vblocks[i]->sites[3 * s3];
      p3.y = vblocks[i]->sites[3 * s3 + 1];
      p3.z = vblocks[i]->sites[3 * s3 + 2];

      // add the vertices
      tet_verts.push_back(p0);
      tet_verts.push_back(p1);
      tet_verts.push_back(p2);
      tet_verts.push_back(p3);

      // debug
//       fprintf(stderr, "site %d [%.3f %.3f %.3f] "
// 	      "site %d [%.3f %.3f %.3f] "
// 	      "site %d [%.3f %.3f %.3f] "
// 	      "site %d [%.3f %.3f %.3f]\n", 
// 	      vblocks[i]->tets[4 * j], p0.x, p0.y, p0.z,
// 	      vblocks[i]->tets[4 * j + 1], p1.x, p1.y, p1.z,
// 	      vblocks[i]->tets[4 * j + 2], p2.x, p2.y, p2.z,
// 	      vblocks[i]->tets[4 * j + 3], p3.x, p3.y, p3.z);

      num_loc_tets++;

    } // local tets

    // remote tets
    for (int j = 0; j < vblocks[i]->num_rem_tets; j++) {

      // gids for tet vertices
      int g0 = vblocks[i]->rem_tet_gids[4 * j];
      int g1 = vblocks[i]->rem_tet_gids[4 * j + 1];
      int g2 = vblocks[i]->rem_tet_gids[4 * j + 2];
      int g3 = vblocks[i]->rem_tet_gids[4 * j + 3];

      // site indices for tet vertices
      int s0 = vblocks[i]->rem_tet_nids[4 * j];
      int s1 = vblocks[i]->rem_tet_nids[4 * j + 1];
      int s2 = vblocks[i]->rem_tet_nids[4 * j + 2];
      int s3 = vblocks[i]->rem_tet_nids[4 * j + 3];

      // coordinates for tet vertices
      // assuming that gid = lid, ie, blocks were written and read in gid order
      vec3d p0, p1, p2, p3;

      // p0
      p0.x = vblocks[g0]->sites[3 * s0];
      p0.y = vblocks[g0]->sites[3 * s0 + 1];
      p0.z = vblocks[g0]->sites[3 * s0 + 2];

      // wraparound transform
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j] & DIY_X0) == DIY_X0)
	p0.x += (data_max.x - data_min.x);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j] & DIY_X1) == DIY_X1)
	p0.x -= (data_max.x - data_min.x);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j] & DIY_Y0) == DIY_Y0)
	p0.y += (data_max.y - data_min.y);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j] & DIY_Y1) == DIY_Y1)
	p0.y -= (data_max.y - data_min.y);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j] & DIY_Z0) == DIY_Z0)
	p0.z += (data_max.z - data_min.z);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j] & DIY_Z1) == DIY_Z1)
	p0.z -= (data_max.z - data_min.z);

      // p1
      p1.x = vblocks[g1]->sites[3 * s1];
      p1.y = vblocks[g1]->sites[3 * s1 + 1];
      p1.z = vblocks[g1]->sites[3 * s1 + 2];

      // wraparound transform
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 1] & DIY_X0) == DIY_X0)
	p1.x += (data_max.x - data_min.x);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 1] & DIY_X1) == DIY_X1)
	p1.x -= (data_max.x - data_min.x);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 1] & DIY_Y0) == DIY_Y0)
	p1.y += (data_max.y - data_min.y);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 1] & DIY_Y1) == DIY_Y1)
	p1.y -= (data_max.y - data_min.y);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 1] & DIY_Z0) == DIY_Z0)
	p1.z += (data_max.z - data_min.z);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 1] & DIY_Z1) == DIY_Z1)
	p1.z -= (data_max.z - data_min.z);

      // p2
      p2.x = vblocks[g2]->sites[3 * s2];
      p2.y = vblocks[g2]->sites[3 * s2 + 1];
      p2.z = vblocks[g2]->sites[3 * s2 + 2];

      // wraparound transform
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 2] & DIY_X0) == DIY_X0)
	p2.x += (data_max.x - data_min.x);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 2] & DIY_X1) == DIY_X1)
	p2.x -= (data_max.x - data_min.x);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 2] & DIY_Y0) == DIY_Y0)
	p2.y += (data_max.y - data_min.y);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 2] & DIY_Y1) == DIY_Y1)
	p2.y -= (data_max.y - data_min.y);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 2] & DIY_Z0) == DIY_Z0)
	p2.z += (data_max.z - data_min.z);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 2] & DIY_Z1) == DIY_Z1)
	p2.z -= (data_max.z - data_min.z);

      // p3
      p3.x = vblocks[g3]->sites[3 * s3];
      p3.y = vblocks[g3]->sites[3 * s3 + 1];
      p3.z = vblocks[g3]->sites[3 * s3 + 2];

      // wraparaound transform
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 3] & DIY_X0) == DIY_X0)
	p3.x += (data_max.x - data_min.x);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 3] & DIY_X1) == DIY_X1)
	p3.x -= (data_max.x - data_min.x);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 3] & DIY_Y0) == DIY_Y0)
	p3.y += (data_max.y - data_min.y);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 3] & DIY_Y1) == DIY_Y1)
	p3.y -= (data_max.y - data_min.y);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 3] & DIY_Z0) == DIY_Z0)
	p3.z += (data_max.z - data_min.z);
      if ((vblocks[i]->rem_tet_wrap_dirs[4 * j + 3] & DIY_Z1) == DIY_Z1)
	p3.z -= (data_max.z - data_min.z);

      // add the vertices
      tet_verts.push_back(p0);
      tet_verts.push_back(p1);
      tet_verts.push_back(p2);
      tet_verts.push_back(p3);

      num_rem_tets++;

    } // remote tets

  } // blocks

  if (min_vol_act < min_vol_clamp)
    min_vol_act = min_vol_clamp;
  if (min_vol == 0.0)
    min_vol = min_vol_act;
  if (max_vol == 0.0)
    max_vol = max_vol_act;

  fprintf(stderr, "Number of particles = %d\n"
	  "Number of visible cells = %d\n"
	  "Number of tets = %d (%d local + %d remote)\n"
	  "Minimum volume = %.4f Maximum volume = %.4f\n",
	  (int)sites.size(), num_vis_cells, 
	  num_loc_tets + num_rem_tets, num_loc_tets, num_rem_tets,
	  min_vol, max_vol);

  // start glut
  glutInit(&argc, argv); 
  glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH); 
  glutInitWindowSize(win_size.x, win_size.y); 
  glutCreateWindow("Voronoi"); 
  glutDisplayFunc(display); 
  glutTimerFunc(10, timer, 0); 
  glutMouseFunc(mouse); 
  glutMotionFunc(motion);
  glutKeyboardFunc(key); 
  glutReshapeFunc(reshape);
  glutMainLoop(); 

}
//--------------------------------------------------------------------------
//
// rendering
//
void display() {

  static bool first = true;
  int n;

  if (first)
    init_display();
  first = false;

  // set the headlight
  headlight();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity(); 
  gluPerspective(60.0, 1.0, near, 100.0); 

  glMatrixMode(GL_MODELVIEW); 
  glLoadIdentity(); 
  gluLookAt(0.0, 0.0, 5.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0); 

  // mouse interactions: pan, rotate, zoom
  glTranslatef(trans.x, trans.y, 0.0);
  glRotatef(rot.x, 0.0, 1.0, 0.0); 
  glRotatef(rot.y, 1.0, 0.0, 0.0); 
  glScalef(scale, scale, scale);

  // center the data in the window
  glTranslatef(-site_center.x, -site_center.y, -site_center.z);

  glEnable(GL_COLOR_MATERIAL);

  // axes
//   draw_axes();

  // block bounds
  if (block_mode) {
    for (int i = 0; i < nblocks; i++)
      draw_cube(vblocks[i]->mins, vblocks[i]->maxs, 1.0, 0.0, 1.0);
  }

  // delaunay tets
  if (draw_del)
    draw_tets();

  // cell edges
  if (draw_tess) {

    glDisable(GL_LIGHTING);
    glColor4f(0.7, 0.7, 0.7, 1.0);
    if (draw_fancy)
      glLineWidth(3.0);
    else
      glLineWidth(1.0);
    n = 0;

    // for all faces
    for (int i = 0; i < (int)num_face_verts.size(); i++) {

      // scan all vertices to see if the face should be clipped or drawn
      bool draw_face = true;
      if (clip > 0.0) {
	int m = n;
	for (int j = 0; j < num_face_verts[i]; j++) {
	  if (verts[m].z > z_clip) {
	    draw_face = false;
	    break;
	  }
	  m++;
	}
      }

      if (draw_face) {
	glBegin(GL_LINE_STRIP);
	int n0 = n; // index of first vertex in this face
	for (int j = 0; j < num_face_verts[i]; j++) {
	  glVertex3f(verts[n].x, verts[n].y, verts[n].z);
	  n++;
	}
	// repeat first vertex
	glVertex3f(verts[n0].x, verts[n0].y, verts[n0].z);
	glEnd();
      }

      else { // just need to increment n
	for (int j = 0; j < num_face_verts[i]; j++)
	  n++;
      }

    } // for all faces

  } // draw tess

  // sites
  if (draw_fancy) {
    glDisable(GL_COLOR_MATERIAL);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHTING);
    GLfloat amb_mat[] = {0.6, 0.6, 0.6, 1.0};
    GLfloat spec_mat[] = {1.0, 1.0, 1.0, 1.0};
    GLfloat shine[] = {1}; // 0 - 128, 0 = shiny, 128 = dull
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec_mat);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, shine);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, amb_mat);
    if (draw_particle)
      draw_spheres(sites, sphere_rad);
  }
  else {
    glDisable(GL_LIGHTING);
    glColor3f(0.9, 0.9, 0.9);
    glEnable(GL_POINT_SMOOTH);
    glPointSize(1.0);
    if (draw_particle) {
      glBegin(GL_POINTS);
      for (int i = 0; i < (int)sites.size(); i++) {
	if (clip == 0.0 || sites[i].z < z_clip)
	  glVertex3f(sites[i].x, sites[i].y, sites[i].z);
      }
      glEnd();
    }
    glDisable(GL_COLOR_MATERIAL);
  }

  // cell faces
  if (draw_tess) {

    if (draw_fancy) {

      float d = size / 3000.0; // face shift found by trial and error
      GLfloat spec[] = {0.5, 0.5, 0.5, 1.0};
      GLfloat shine[] = {16}; // 0 - 128, 0 = shiny, 128 = dull
      glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
      glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, shine);

      if (color_density) {

	n = 0;

	// for all faces
	for (int i = 0; i < (int)num_face_verts.size(); i++) {

	  // scan all vertices to see if the face should be clipped or drawn
	  bool draw_face = true;
	  if (clip > 0.0) {
	    int m = n;
	    for (int j = 0; j < num_face_verts[i]; j++) {
	      if (verts[m].z > z_clip) {
		draw_face = false;
		break;
	      }
	      m++;
	    }
	  }

	  if (draw_face) {
	    // flat shading, one normal per face
	    vec3d normal;
	    ComputeNormal(&verts[n], num_face_verts[i], normal);
	    // logartithmic face color from red = small vol to blue = big vol
	    float r, b;
	    b = (log10f(face_vols[i]) - log10f(min_vol_act)) / 
	      (log10f(max_vol_act) - log10f(min_vol_act));
	    r = 1.0 - b;
	    GLfloat mat[] = {r, 0.1, b, 1.0};
	    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat);
	    // shift the face to set it back from the edges
	    float dx = normal.x * d;
	    float dy = normal.y * d;
	    float dz = normal.z * d;
	    if (clip > 0.0) {
	      dx = 0.0;
	      dy = 0.0;
	      dz = 0.0;
	      normal.x *= -1.0;
	      normal.y *= -1.0;
	      normal.z *= -1.0;
	    }
	    glBegin(GL_POLYGON);
	    // draw the face
	    for (int j = 0; j < num_face_verts[i]; j++) {
	      if (clip == 0.0 || verts[n].z - dz < z_clip) {
		glNormal3f(normal.x, normal.y, normal.z);
		glVertex3f(verts[n].x - dx, verts[n].y - dy, verts[n].z - dz);
	      }
	      n++;
	    }
	    glEnd();
	  }

	  else { // just need to increment n
	    for (int j = 0; j < num_face_verts[i]; j++)
	      n++;
	  }

	} // for all faces

      } // color density

      else { // ! color_density

	GLfloat mat[] = {0.3, 0.35, 0.6, 1.0};
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat);
	n = 0;

	// for all faces
	for (int i = 0; i < (int)num_face_verts.size(); i++) {

	  // scan all vertices to see if the face should be clipped or drawn
	  bool draw_face = true;
	  if (clip > 0.0) {
	    int m = n;
	    for (int j = 0; j < num_face_verts[i]; j++) {
	      if (verts[m].z > z_clip) {
		draw_face = false;
		break;
	      }
	      m++;
	    }
	  }

	  if (draw_face) {
	    // flat shading, one normal per face
	    vec3d normal;
	    ComputeNormal(&verts[n], num_face_verts[i], normal);
	    // shift the face to set it back from the edges
	    float dx = normal.x * d;
	    float dy = normal.y * d;
	    float dz = normal.z * d;
	    if (clip > 0.0) {
	      dx = 0.0;
	      dy = 0.0;
	      dz = 0.0;
// 	      normal.x *= -1.0;
// 	      normal.y *= -1.0;
// 	      normal.z *= -1.0;
	    }
	    // draw the face
	    glBegin(GL_POLYGON);
	    for (int j = 0; j < num_face_verts[i]; j++) {
	      glNormal3f(normal.x, normal.y, normal.z);
	      glVertex3f(verts[n].x - dx, verts[n].y - dy, verts[n].z - dz);
	      n++;
	    }
	    glEnd();
	  }

	  else { // just need to increment n
	    for (int j = 0; j < num_face_verts[i]; j++)
	      n++;
	  }

	} // for all faces

      } // ! color density

    } // draw fancy

  } // draw tess

  glutSwapBuffers();

}
//--------------------------------------------------------------------------
//
// first time drawing initialization
//
void init_display() {

  // extents
  for (int i = 0; i < (int)sites.size(); i++) {
    if (i == 0) {
      site_min.x = sites[i].x;
      site_min.y = sites[i].y;
      site_min.z = sites[i].z;
      site_max.x = sites[i].x;
      site_max.y = sites[i].y;
      site_max.z = sites[i].z;
    }
    if (sites[i].x < site_min.x)
      site_min.x = sites[i].x;
    if (sites[i].y < site_min.y)
      site_min.y = sites[i].y;
    if (sites[i].z < site_min.z)
      site_min.z = sites[i].z;
    if (sites[i].x > site_max.x)
      site_max.x = sites[i].x;
    if (sites[i].y > site_max.y)
      site_max.y = sites[i].y;
    if (sites[i].z > site_max.z)
      site_max.z = sites[i].z;
  }
  site_center.x = (site_min.x + site_max.x) / 2.0;
  site_center.y = (site_min.y + site_max.y) / 2.0;
  site_center.z = (site_min.z + site_max.z) / 2.0;
  sizes.x = site_max.x - site_min.x;
  sizes.y = site_max.y - site_min.y;
  sizes.z = site_max.z - site_min.z;
  size = sizes.x;
  if (sizes.y > size)
    size = sizes.y;
  if (sizes.z > size)
    size = sizes.z;
  fprintf(stderr, "max size = %.4f\n", size);
  sphere_rad = SPHERE_RAD_FACTOR * size;

  init_model();
  init_viewport(true);

  // background
  glClearColor(0.0, 0.0, 0.0, 1.0); 

  // gl state
//   glEnable(GL_COLOR_MATERIAL);
  glEnable(GL_DEPTH_TEST);
  glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glEnable(GL_LIGHT1);
  glEnable(GL_LIGHT2);
  glEnable(GL_NORMALIZE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glShadeModel(GL_SMOOTH);

  // initialize headlight
  headlight();

  // general purpose quadrics
  q = gluNewQuadric();

  // point sprite texture
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 5; j++) {
      sprite_rgba[i][j][0] = sprite_intensity[i][j];
      sprite_rgba[i][j][1] = sprite_intensity[i][j];
      sprite_rgba[i][j][2] = sprite_intensity[i][j];
      sprite_rgba[i][j][3] = sprite_intensity[i][j];
    }
  }
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 5, 5, 0, GL_RGBA, 
	       GL_UNSIGNED_BYTE, sprite_rgba);

}
//--------------------------------------------------------------------------
//
// draw delaunay tets
//
void draw_tets() {

  glDisable(GL_LIGHTING);
  glColor4f(0.7, 0.7, 0.7, 1.0);
  if (draw_fancy)
    glLineWidth(3.0);
  else
    glLineWidth(1.0);

  // for all tets
  for (int t = 0; t < (int)tet_verts.size() / 4; t++) {

    int n = t * 4;

    // first triangle
    glBegin(GL_LINE_STRIP);
    glVertex3f(tet_verts[n].x, tet_verts[n].y, tet_verts[n].z);
    glVertex3f(tet_verts[n + 1].x, tet_verts[n + 1].y, tet_verts[n + 1].z);
    glVertex3f(tet_verts[n + 2].x, tet_verts[n + 2].y, tet_verts[n + 2].z);
    glVertex3f(tet_verts[n].x, tet_verts[n].y, tet_verts[n].z);
    glEnd();

    // second triangle
    glBegin(GL_LINE_STRIP);
    glVertex3f(tet_verts[n + 3].x, tet_verts[n + 3].y, tet_verts[n + 3].z);
    glVertex3f(tet_verts[n + 1].x, tet_verts[n + 1].y, tet_verts[n + 1].z);
    glVertex3f(tet_verts[n + 2].x, tet_verts[n + 2].y, tet_verts[n + 2].z);
    glVertex3f(tet_verts[n + 3].x, tet_verts[n + 3].y, tet_verts[n + 3].z);
    glEnd();

    // third triangle
    glBegin(GL_LINE_STRIP);
    glVertex3f(tet_verts[n].x, tet_verts[n].y, tet_verts[n].z);
    glVertex3f(tet_verts[n + 3].x, tet_verts[n + 3].y, tet_verts[n + 3].z);
    glVertex3f(tet_verts[n + 2].x, tet_verts[n + 2].y, tet_verts[n + 2].z);
    glVertex3f(tet_verts[n].x, tet_verts[n].y, tet_verts[n].z);
    glEnd();

    // fourth triangle
    glBegin(GL_LINE_STRIP);
    glVertex3f(tet_verts[n].x, tet_verts[n].y, tet_verts[n].z);
    glVertex3f(tet_verts[n + 1].x, tet_verts[n + 1].y, tet_verts[n + 1].z);
    glVertex3f(tet_verts[n + 3].x, tet_verts[n + 3].y, tet_verts[n + 3].z);
    glVertex3f(tet_verts[n].x, tet_verts[n].y, tet_verts[n].z);
    glEnd();


  } // for all tets

  if (draw_fancy) {

    glDisable(GL_COLOR_MATERIAL);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHTING);
    GLfloat amb_mat[] = {0.6, 0.6, 0.6, 1.0};
    GLfloat spec_mat[] = {1.0, 1.0, 1.0, 1.0};
    GLfloat shine[] = {1}; // 0 - 128, 0 = shiny, 128 = dull
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec_mat);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, shine);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, amb_mat);

    // normals for four triangles in the tet
    vec3d normal0, normal1, normal2, normal3;
    vec3d tri[3]; // temporary vertices in one triangle
    glShadeModel(GL_FLAT);

    // material
    GLfloat mat[] = {0.3, 0.35, 0.6, 1.0};
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat);

    // for all tets
    for (int t = 0; t < (int)tet_verts.size() / 4; t++) {

      int n = t * 4;

      // flat shading, one normal per face
      // package verts into a contigous array to compute normal
      tri[0].x = tet_verts[n + 2].x;
      tri[0].y = tet_verts[n + 2].y;
      tri[0].z = tet_verts[n + 2].z;
      tri[1].x = tet_verts[n + 1].x;
      tri[1].y = tet_verts[n + 1].y;
      tri[1].z = tet_verts[n + 1].z;
      tri[2].x = tet_verts[n    ].x;
      tri[2].y = tet_verts[n    ].y;
      tri[2].z = tet_verts[n    ].z;
      ComputeNormal(tri, 3, normal0);

      tri[0].x = tet_verts[n + 3].x;
      tri[0].y = tet_verts[n + 3].y;
      tri[0].z = tet_verts[n + 3].z;
      tri[1].x = tet_verts[n    ].x;
      tri[1].y = tet_verts[n    ].y;
      tri[1].z = tet_verts[n    ].z;
      tri[2].x = tet_verts[n + 1].x;
      tri[2].y = tet_verts[n + 1].y;
      tri[2].z = tet_verts[n + 1].z;
      ComputeNormal(tri, 3, normal1);

      tri[0].x = tet_verts[n    ].x;
      tri[0].y = tet_verts[n    ].y;
      tri[0].z = tet_verts[n    ].z;
      tri[1].x = tet_verts[n + 3].x;
      tri[1].y = tet_verts[n + 2].y;
      tri[1].z = tet_verts[n + 3].z;
      tri[2].x = tet_verts[n + 2].x;
      tri[2].y = tet_verts[n + 2].y;
      tri[2].z = tet_verts[n + 2].z;
      ComputeNormal(tri, 3, normal2);

      tri[0].x = tet_verts[n + 1].x;
      tri[0].y = tet_verts[n + 1].y;
      tri[0].z = tet_verts[n + 1].z;
      tri[1].x = tet_verts[n + 2].x;
      tri[1].y = tet_verts[n + 2].y;
      tri[1].z = tet_verts[n + 2].z;
      tri[2].x = tet_verts[n + 3].x;
      tri[2].y = tet_verts[n + 3].y;
      tri[2].z = tet_verts[n + 3].z;
      ComputeNormal(tri, 3, normal3);

      // render triangles
      glBegin(GL_TRIANGLE_STRIP);
      glVertex3f(tet_verts[n + 2].x, tet_verts[n + 2].y, tet_verts[n + 2].z);
      glVertex3f(tet_verts[n + 1].x, tet_verts[n + 1].y, tet_verts[n + 1].z);
      glNormal3f(-normal0.x, -normal0.y, -normal0.z);
      glVertex3f(tet_verts[n    ].x, tet_verts[n    ].y, tet_verts[n    ].z);
      glNormal3f(-normal1.x, -normal1.y, -normal1.z);
      glVertex3f(tet_verts[n + 3].x, tet_verts[n + 3].y, tet_verts[n + 3].z);
      glNormal3f(-normal2.x, -normal2.y, -normal2.z);
      glVertex3f(tet_verts[n + 2].x, tet_verts[n + 2].y, tet_verts[n + 2].z);
      glNormal3f(-normal3.x, -normal3.y, -normal3.z);
      glVertex3f(tet_verts[n + 1].x, tet_verts[n + 1].y, tet_verts[n + 1].z);
      glEnd();

    } // for all tets

  } // all tets

}
//--------------------------------------------------------------------------
//
// set a headlight
//
void headlight() {

  GLfloat light_ambient[4] = {0.1, 0.1, 0.1, 1.0};  
  GLfloat light_diffuse[4] = {0.2, 0.2, 0.2, 1.0};  
  GLfloat light_specular[4] = {0.8, 0.8, 0.8, 1.0};

  glPushMatrix();
  glMatrixMode(GL_MODELVIEW); 
  glLoadIdentity(); 
  glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
  glLightfv(GL_LIGHT0, GL_SPECULAR, light_specular);
  glEnable(GL_LIGHT0);

  glPopMatrix();

}
//--------------------------------------------------------------------------
//
// cube, useful for block boundaries, etc.
//
void draw_cube(float *mins, float *maxs, float r, float g, float b) {

  glPushMatrix();
  glTranslatef((mins[0] + maxs[0]) / 2.0, (mins[1] + maxs[1]) / 2.0, 
	       (mins[2] + maxs[2]) / 2.0); 
  glScalef(maxs[0] - mins[0], maxs[1] - mins[1], maxs[2] - mins[2]);
  glColor3f(r, g, b); 
  glutWireCube(1.0);
  glPopMatrix();

}
//--------------------------------------------------------------------------
// 
// sphere for rendering voronoi sites (particles)
//
void draw_sphere(rgba &color, vec3d &pos, float rad) {

  glColor3f(color.r, color.g, color.b); 
  glPushMatrix();
  glTranslatef(pos.x, pos.y, pos.z);
  gluSphere(q, rad, 7, 7);
  glPopMatrix();

}
//--------------------------------------------------------------------------
// 
// all spheres for rendering voronoi sites (particles)
//
void draw_spheres(vector<vec3d> &sites, float rad) {

  for (int i = 0; i < (int)sites.size(); i++) {

    if (clip == 0.0 || sites[i].z < z_clip) {
      glPushMatrix();
      glTranslatef(sites[i].x, sites[i].y, sites[i].z);
     gluSphere(q, rad, 7, 7);
      glPopMatrix();
    }

  }

}
//--------------------------------------------------------------------------
// 
// point sprite for rendering voronoi sites (particles)
//
void draw_sprites(vector<vec3d> &sites, float size) {

  glPushAttrib(GL_ALL_ATTRIB_BITS);

//   glDisable(GL_DEPTH_TEST);
//   glEnable (GL_BLEND); 

  glColor3f(1.0, 1.0, 1.0);  // color doesn't matter, will be textured over

  glPointSize(size);

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_POINT_SPRITE);
  glTexEnvf(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE);
  glBindTexture(GL_TEXTURE_2D, tex);
  glEnable(GL_POINT_SMOOTH);
  glBegin(GL_POINTS);
  for(int i = 0; i < (int)sites.size(); i++)
    glVertex3f(sites[i].x, sites[i].y, sites[i].z);
  glEnd();
  glDisable(GL_POINT_SPRITE);

//   glDisable (GL_BLEND); 
//   glEnable(GL_DEPTH_TEST);

  glPopAttrib();

}
//--------------------------------------------------------------------------
//
// axes
//
void draw_axes() {

  glPushMatrix();

  // x
  glColor3f(1.0, 0.0, 0.0); 
  glPushMatrix();
  glRotatef(90.0, 0.0, 1.0, 0.0);
  gluCylinder(q, size * 0.007, size * 0.007, sizes.x * 1.2, 15, 1);
  glTranslatef(0.0, 0.0, sizes.x * 1.2);
  gluCylinder(q, size * 0.015, 0.0, size * .015, 20, 1);
  glPopMatrix();

  // y
  glColor3f(0.0, 1.0, 0.0); 
  glPushMatrix();
  glRotatef(-90.0, 1.0, 0.0, 0.0);
  gluCylinder(q, size * 0.007, size * 0.007, sizes.y * 1.2, 15, 1);
  glTranslatef(0.0, 0.0, sizes.y * 1.2);
  gluCylinder(q, size * 0.015, 0.0, size * .015, 20, 1);
  glPopMatrix();

  // z
  glColor3f(0.0, 0.0, 1.0); 
  glPushMatrix();
  gluCylinder(q, size * 0.007, size * 0.007, sizes.z * 1.2, 15, 1);
  glTranslatef(0.0, 0.0, sizes.z * 1.2);
  gluCylinder(q, size * 0.015, 0.0, size * .015, 20, 1);
  glPopMatrix();

  glPopMatrix();

}
//--------------------------------------------------------------------------
//
// mouse button events
//
void mouse(int button, int state, int x, int y) {

  if (state == GLUT_DOWN) {

    press_x = x;
    press_y = y; 
    if (button == GLUT_LEFT_BUTTON)
      xform_mode = XFORM_ROTATE; 
    else if (button == GLUT_RIGHT_BUTTON) 
      xform_mode = XFORM_SCALE; 
    else if (button == GLUT_MIDDLE_BUTTON) 
      xform_mode = XFORM_TRANS; 

  }
  else if (state == GLUT_UP)
    xform_mode = XFORM_NONE; 

}
//--------------------------------------------------------------------------
//
// mouse motion events
//
void motion(int x, int y) {

  if (xform_mode == XFORM_ROTATE) {

    rot.x += (x - press_x) * rot_rate; 
    if (rot.x > 180)
      rot.x -= 360; 
    else if (rot.x < -180)
      rot.x += 360; 
    press_x = x; 
	   
    rot.y += (y - press_y) * rot_rate;
    if (rot.y > 180)
      rot.y -= 360; 
    else if (rot.y <-180)
      rot.y += 360; 
    press_y = y; 

  }
  else if (xform_mode == XFORM_TRANS) {

    trans.x += (x - press_x) * trans_rate; 
    trans.y -= (y - press_y) * trans_rate;  // subtract to reverse y dir.
    press_x = x;
    press_y = y; 

  }
  else if (xform_mode == XFORM_SCALE){

    float old_scale = scale;
    scale /= (1 + (y - press_y) * scale_rate);  // divided to reverse y dir.
    if (scale < 0) 
      scale = old_scale; 
    press_y = y; 

  }

  glutPostRedisplay(); 

}
//--------------------------------------------------------------------------
//
// keyboard events
//
void key(unsigned char key, int x, int y) {

  x = x; // quiet compiler warnings
  y = y;

  switch(key) {

  case 'q':  // quit
    exit(1);
    break; 
  case 't':  // show voronoi tessellation
    draw_tess = !draw_tess;
    draw_del = false;
    break;
  case 'y':  // show delaunay tets
    draw_del = !draw_del;
    draw_tess = false;
    break;
  case 'p':  // show particles
    draw_particle = !draw_particle;
    break;
  case 'd':  // color by density
    color_density = !color_density;
    break;
  case 'z':  // zoom mouse motion
    xform_mode = XFORM_SCALE; 
    break; 
  case 'a':  // panning mouse motion
    xform_mode = XFORM_TRANS; 
    break; 
  case 'r': // reset rotate, pan, zoom, viewport
    init_model();
    init_viewport(true);
    break;
  case 'b': // toggle block visibility
    block_mode = !block_mode;
    break;
  case 'f': // toggle fancy rendering
    draw_fancy = !draw_fancy;
    break;
  case 'c': // increase near clip plane
    clip += 0.1;
    if (clip >= 1.0)
      clip = 1.0;
    z_clip = site_max.z - clip * (site_max.z - site_min.z);
    fprintf(stderr, "clipping at = %.1f of z range\n", clip);
    break;
  case 'C': // decrease near clip plane
    clip -= 0.1;
    if (clip <= 0.0)
      clip = 0.0;
    z_clip = site_max.z - clip * (site_max.z - site_min.z);
    fprintf(stderr, "clipping at = %.1f of z range\n", clip);
    break;
  case 'v': // restrict (minimum) volume range
    min_vol += vol_step;
    fprintf(stderr, "Minimum volume = %.4lf\n", min_vol);
    filter_volume(min_vol, max_vol);
    break;
  case 'V': //  expand (minimum) volume range
    min_vol -= vol_step;
    if (min_vol < 0.0)
      min_vol = 0.0;
    fprintf(stderr, "Minimum volume = %.4lf\n", min_vol);
    filter_volume(min_vol, max_vol);
    break;
  case 'x': // restrict (maximum) volume range
    max_vol -= vol_step;
    if (max_vol < 0.0)
      max_vol = 0.0;
    fprintf(stderr, "Maximum volume = %.4lf\n", max_vol);
    filter_volume(min_vol, max_vol);
    break;
  case 'X': //  expand (maximum) volume range
    max_vol += vol_step;
    fprintf(stderr, "Maximum volume = %.4lf\n", max_vol);
    filter_volume(min_vol, max_vol);
    break;
  case 'R': // reset volume range
    min_vol = 0.0;
    fprintf(stderr, "Minimum volume = %.4lf\n", min_vol);
    filter_volume(min_vol, max_vol);
    break;
  case 's': // decrease volume step size
    vol_step *= 0.1;
    if (vol_step < 0.0001)
      vol_step = 0.0001;
    fprintf(stderr, "Volume step size = %.4lf\n", vol_step);
    break;
  case 'S': // increase volume step size
    vol_step *= 10.0;
    fprintf(stderr, "Volume step size = %.4lf\n", vol_step);
    break;
  default:
    break;

  }
}
//--------------------------------------------------------------------------
//
// filter volume
//
void filter_volume(float min_vol, float max_vol) {

  int num_vis_cells = 0; // number of visible cells

  num_face_verts.clear();
  verts.clear();
  face_vols.clear();

  // package rendering data
  for (int i = 0; i < nblocks; i++) { // blocks

    int  n = 0;
    int m = 0;

    for (int j = 0; j < vblocks[i]->num_complete_cells; j++) { // cells

      for (int k = 0; k < vblocks[i]->num_cell_faces[j]; k++) { // faces

	if (vblocks[i]->vols[j] >= min_vol &&
	    (max_vol <= 0.0 || vblocks[i]->vols[j] <= max_vol)) {
	  num_face_verts.push_back(vblocks[i]->num_face_verts[n]);
	  face_vols.push_back(vblocks[i]->vols[j]);
	}

	for (int l = 0; l < vblocks[i]->num_face_verts[n]; l++) { // vertices

	  int v = vblocks[i]->face_verts[m];
	  vec3d s;
	  s.x = vblocks[i]->save_verts[3 * v];
	  s.y = vblocks[i]->save_verts[3 * v + 1];
	  s.z = vblocks[i]->save_verts[3 * v + 2];
	  m++;
	  if (vblocks[i]->vols[j] >= min_vol &&
	      (max_vol <= 0.0 || vblocks[i]->vols[j] <= max_vol))
	    verts.push_back(s);

	} // vertices

	n++;

      } // faces

      if (vblocks[i]->vols[j] >= min_vol &&
	  (max_vol <= 0.0 || vblocks[i]->vols[j] <= max_vol))
	num_vis_cells++;

    } // cells

  } // blocks

  fprintf(stderr, "Number of visible cells = %d\n", num_vis_cells);

}
//--------------------------------------------------------------------------
//
// clip cells
//
void clip_cells(float z_clip) {

  int face = 0; // faces counter
  int vert = 0; // face vertices counter
  float cell_min[3], cell_max[3], cell_centroid[3]; // cell bounds

  sites.clear();

  // blocks
  for (int block = 0; block< nblocks; block++) {

    // cells
    for (int cell = 0; cell < vblocks[block]->num_complete_cells; cell++) {

      // cell bounds
      CellBounds(vblocks[block], cell, face, vert, cell_min, cell_max,
		 cell_centroid);

      if (cell_min[2] < z_clip) {
	vec3d s;
	int n = vblocks[block]->complete_cells[cell];
	s.x = vblocks[block]->sites[3 * n];
	s.y = vblocks[block]->sites[3 * n + 1];
	s.z = vblocks[block]->sites[3 * n + 2];
	sites.push_back(s);
      }

      // increment face and vert to point to start of next cell
      for (int i = 0; i < vblocks[block]->num_cell_faces[cell]; i++) {
	for (int j = 0; j < vblocks[block]->num_face_verts[face]; j++)
	  vert++;
	face++;
      }

    } // cells

  } // blocks

}
//--------------------------------------------------------------------------
//
// get cell bounds
//
// vblock: one voronoi block
// cell: current cell counter
// face: current face counter
// vert: current vertex counter
// cell_min, cell_max: cell bounds (output)
// centroid: centroid, mean of all vertices (output)
//
void CellBounds(vblock_t *vblock, int cell, int face, int vert,
		float *cell_min, float *cell_max, float *centroid) {

  centroid[0] = 0.0;
  centroid[1] = 0.0;
  centroid[2] = 0.0;
  int tot_verts = 0;

  // get cell bounds
  for (int k = 0; k < vblock->num_cell_faces[cell]; k++) { // faces

    for (int l = 0; l < vblock->num_face_verts[face]; l++) { // vertices

      int v = vblock->face_verts[vert];
	  
      if (k == 0 && l == 0 || vblock->save_verts[3 * v] < cell_min[0])
	cell_min[0] = vblock->save_verts[3 * v];
      if (k == 0 && l == 0 || vblock->save_verts[3 * v] > cell_max[0])
	cell_max[0] = vblock->save_verts[3 * v];
      centroid[0] += vblock->save_verts[3 * v];

      if (k == 0 && l == 0 || vblock->save_verts[3 * v + 1] < cell_min[1])
	cell_min[1] = vblock->save_verts[3 * v + 1];
      if (k == 0 && l == 0 || vblock->save_verts[3 * v + 1] > cell_max[1])
	cell_max[1] = vblock->save_verts[3 * v + 1];
      centroid[1] += vblock->save_verts[3 * v + 1];

      if (k == 0 && l == 0 || vblock->save_verts[3 * v + 2] < cell_min[2])
	cell_min[2] = vblock->save_verts[3 * v + 2];
      if (k == 0 && l == 0 || vblock->save_verts[3 * v + 2] > cell_max[2])
	cell_max[2] = vblock->save_verts[3 * v + 2];
      centroid[2] += vblock->save_verts[3 * v + 2];

      vert++;
      tot_verts++;

    } // vertices

    face++;

  } // faces

  centroid[0] /= tot_verts;
  centroid[1] /= tot_verts;
  centroid[2] /= tot_verts;

} 
//--------------------------------------------------------------------------
//
// timer events
//
void timer(int val) {

  val = val; // quiet compiler warning

  glutPostRedisplay();
  glutTimerFunc(10, timer, 0); 

}
//--------------------------------------------------------------------------
//
// reshape events
//
void reshape(int w, int h) {

  // update window and viewport size and aspect ratio
  win_size.x = w;
  win_size.y = h;

  init_viewport(false);

  glutPostRedisplay();

}
//--------------------------------------------------------------------------
//
// initialize model
//
void init_model() {

  // rotate
  rot.x = rot.y = 0.0;

  // translate
  trans.x = trans.y = 0.0;

  // scale (initial scale 1.5 makes the model fill the screen better)
  scale = 1.5 / size;

}
//--------------------------------------------------------------------------
//
// initialize viewport
//
// reset: true = first time or reset to initial viewport
//        false = modify existing viewport
//
void init_viewport(bool reset) {

  if (win_size.x > win_size.y) {
    aspect.x = 1.0;
    aspect.y = win_size.x / win_size.y;
    if (reset)
      trans.y -= (win_size.x - win_size.y) / win_size.y;
  }
  else {
    aspect.x = win_size.y / win_size.x;
    aspect.y = 1.0;
    if (reset)
      trans.x -= (win_size.y - win_size.x) / win_size.x;
  }

  if (!reset) {
    trans.x += (win_size.x - old_win_size.x) / old_win_size.x;
    trans.y += (win_size.y - old_win_size.y) / old_win_size.y;
  }

  old_win_size.x = win_size.x;
  old_win_size.y = win_size.y;

  glViewport(0, 0, win_size.x * aspect.x, win_size.y * aspect.y);

}
//--------------------------------------------------------------------------
//
// compute normal of a face using Newell's method
//
// Newell's method is more robust than simply computing the cross product of
//   three points when the points are colinear or slightly nonplanar. 
//
void ComputeNormal(vec3d *verts, int num_verts, vec3d &normal) {

  normal.x = 0.0;
  normal.y = 0.0;
  normal.z = 0.0;

  for (int i = 0; i < num_verts; i++) {
    int cur = i;
    int next = (i + 1) % num_verts;
    normal.x += (verts[cur].y - verts[next].y) * (verts[cur].z + verts[next].z);
    normal.y += (verts[cur].z - verts[next].z) * (verts[cur].x + verts[next].x);
    normal.z += (verts[cur].x - verts[next].x) * (verts[cur].y + verts[next].y);
  }

  float mag = sqrt(normal.x * normal.x + normal.y * normal.y +
		   normal.z * normal.z);
  // normalize
  normal.x /= mag;
  normal.y /= mag;
  normal.z /= mag;

  // direction is inward, need to invert
  normal.x *= -1.0;
  normal.y *= -1.0;
  normal.z *= -1.0;

}
//--------------------------------------------------------------------------
// 
// comparison function for qsort (debugging)
//
int compare(const void *a, const void *b) {

  if (*((int*)a) < *((int*)b))
    return -1;
  if (*((int*)a) == *((int*)b))
    return 0;
  return 1;

}
//--------------------------------------------------------------------------

