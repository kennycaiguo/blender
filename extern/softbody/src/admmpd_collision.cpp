// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_collision.h"
#include "admmpd_bvh_traverse.h"
#include "admmpd_math.h"

#include "BLI_assert.h"
#include "BLI_task.h"
#include "BLI_threads.h"

#include <iostream>

namespace admmpd {
using namespace Eigen;

VFCollisionPair::VFCollisionPair() :
    p_idx(-1), // point
    p_is_obs(0), // 0 or 1
    q_idx(-1), // face
    q_is_obs(0), // 0 or 1
	pt_on_q(0,0,0),
	q_n(0,0,0)
	{}

void EmbeddedMeshCollision::set_obstacles(
	const float *v0,
	const float *v1,
	int nv,
	const unsigned int *faces,
	int nf)
{
	if (obsdata.V0.rows() != nv)
		obsdata.V0.resize(nv,3);

	if (obsdata.V1.rows() != nv)
		obsdata.V1.resize(nv,3);

	for (int i=0; i<nv; ++i)
	{
		for (int j=0; j<3; ++j)
		{
			obsdata.V0(i,j) = v0[i*3+j];
			obsdata.V1(i,j) = v1[i*3+j];
		}
	}

	if (obsdata.F.rows() != nf)
	{
		obsdata.F.resize(nf,3);
		obsdata.aabbs.resize(nf);
	}

	for (int i=0; i<nf; ++i)
	{
		obsdata.aabbs[i].setEmpty();
		for (int j=0; j<3; ++j)
		{
			int fj = faces[i*3+j];
			obsdata.F(i,j) = fj;
			obsdata.aabbs[i].extend(obsdata.V0.row(fj).transpose());
			obsdata.aabbs[i].extend(obsdata.V1.row(fj).transpose());
		}
	}

	obsdata.tree.init(obsdata.aabbs);

} // end add obstacle

typedef struct DetectThreadData {
	const EmbeddedMeshData *mesh;
	const EmbeddedMeshCollision::ObstacleData *obsdata;
	const Eigen::MatrixXd *x0;
	const Eigen::MatrixXd *x1;
	double floor_z;
	std::vector<std::vector<VFCollisionPair> > *pt_vf_pairs; // per thread pairs
} DetectThreadData;

static void parallel_detect_obstacles(
	void *__restrict userdata,
	const int i,
	const TaskParallelTLS *__restrict tls)
{
	DetectThreadData *td = (DetectThreadData*)userdata;

	// Comments say "don't use this" but how else am I supposed
	// to get the thread ID? It appears to return the right thing...
	int thread_idx = BLI_task_parallel_thread_id(tls);
	std::vector<VFCollisionPair> &tl_pairs = td->pt_vf_pairs->at(thread_idx);

	int tet_idx = td->mesh->vtx_to_tet[i];
	RowVector4i tet = td->mesh->tets.row(tet_idx);
	Vector4d bary = td->mesh->barys.row(i);

	// First, get the surface vertex
	Vector3d pt = 
		bary[0] * td->x1->row(tet[0]) +
		bary[1] * td->x1->row(tet[1]) +
		bary[2] * td->x1->row(tet[2]) +
		bary[3] * td->x1->row(tet[3]);

	// Special case, check if we are below the floor
	if (pt[2] < td->floor_z)
	{
		tl_pairs.emplace_back();
		VFCollisionPair &pair = tl_pairs.back();
		pair.p_idx = i;
		pair.p_is_obs = 0;
		pair.q_idx = -1;
		pair.q_is_obs = 1;
		pair.pt_on_q = Vector3d(pt[0],pt[1],td->floor_z);
		pair.q_n = Vector3d(0,0,1);
	}

	// Any obstacles?
	if (td->obsdata->F.rows()==0)
		return;

	// TODO
	// This won't work for overlapping obstacles.
	// We would instead need something like a signed distance field
	// or continuous collision detection.

	PointInTriangleMeshTraverse<double> pt_in_mesh(
		pt, &td->obsdata->V1, &td->obsdata->F);
	td->obsdata->tree.traverse(pt_in_mesh);
	if (pt_in_mesh.output.num_hits() % 2 != 1)
		return;

	// If we are inside an obstacle, we
	// have to project to the nearest surface.

	// TODO
	// Consider replacing this with BLI codes:
	// BLI_bvhtree_find_nearest in BLI_kdopbvh.h
	// But since it doesn't have a point-in-mesh
	// detection, we may as use our own BVH/traverser
	// for now.

	NearestTriangleTraverse<double> find_nearest_tri(
		pt, &td->obsdata->V1, &td->obsdata->F);
	td->obsdata->tree.traverse(find_nearest_tri);
	if (find_nearest_tri.output.prim < 0) // error
		return;

	// Create collision pair
	tl_pairs.emplace_back();
	VFCollisionPair &pair = tl_pairs.back();
	pair.p_idx = i;
	pair.p_is_obs = 0;
	pair.q_idx = find_nearest_tri.output.prim;
	pair.q_is_obs = 1;
	pair.pt_on_q = find_nearest_tri.output.pt_on_tri;

	// Compute face normal
	BLI_assert(pair.q_idx >= 0);
	BLI_assert(pair.q_idx < td->obsdata->F.rows());
	RowVector3i tri_inds = td->obsdata->F.row(pair.q_idx);
	Vector3d tri[3] = {
		td->obsdata->V1.row(tri_inds[0]),
		td->obsdata->V1.row(tri_inds[1]),
		td->obsdata->V1.row(tri_inds[2])
	};
	pair.q_n = ((tri[1]-tri[0]).cross(tri[2]-tri[0])).normalized();

} // end parallel detect against obstacles

static void parallel_detect(
	void *__restrict userdata,
	const int i,
	const TaskParallelTLS *__restrict tls)
{
	parallel_detect_obstacles(userdata,i,tls);
	//parallel_detect_self(userdata,i,tls);	
} // end parallel detect

int EmbeddedMeshCollision::detect(
	const Eigen::MatrixXd *x0,
	const Eigen::MatrixXd *x1)
{
	if (mesh==NULL)
		return 0;

	update_bvh(x0,x1);

	int max_threads = std::max(1,BLI_system_thread_count());
	std::vector<std::vector<VFCollisionPair> > pt_vf_pairs
		(max_threads, std::vector<VFCollisionPair>());






floor_z = -2;











	DetectThreadData thread_data = {
		.mesh = mesh,
		.obsdata = &obsdata,
		.x0 = x0,
		.x1 = x1,
		.floor_z = floor_z,
		.pt_vf_pairs = &pt_vf_pairs
	};

	int nev = mesh->x_rest.rows();
	TaskParallelSettings settings;
	BLI_parallel_range_settings_defaults(&settings);
	BLI_task_parallel_range(0, nev, &thread_data, parallel_detect, &settings);

	// Combine thread-local results
	vf_pairs.clear();
	for (int i=0; i<max_threads; ++i)
	{
		const std::vector<VFCollisionPair> &tl_pairs = pt_vf_pairs[i];
		vf_pairs.insert(vf_pairs.end(), tl_pairs.begin(), tl_pairs.end());
	}

	return vf_pairs.size();
} // end detect

void EmbeddedMeshCollision::jacobian(
	const Eigen::MatrixXd *x,
	std::vector<Eigen::Triplet<double> > *trips_x,
	std::vector<Eigen::Triplet<double> > *trips_y,
	std::vector<Eigen::Triplet<double> > *trips_z,
	std::vector<double> *l)
{
	BLI_assert(x != NULL);
	BLI_assert(x->cols() == 3);

	int np = vf_pairs.size();
	if (np==0)
		return;

	l->reserve((int)l->size() + np);
	trips_x->reserve((int)trips_x->size() + np*3);
	trips_y->reserve((int)trips_y->size() + np*3);
	trips_z->reserve((int)trips_z->size() + np*3);

	for (int i=0; i<np; ++i)
	{
		VFCollisionPair &pair = vf_pairs[i];

		int emb_p_idx = pair.p_idx;
		if (pair.p_is_obs)
		{ // TODO: obstacle point intersecting mesh
			continue;
		}

		// Obstacle collision
		if (pair.q_is_obs)
		{
			RowVector4d bary = mesh->barys.row(emb_p_idx);
			int tet_idx = mesh->vtx_to_tet[emb_p_idx];
			RowVector4i tet = mesh->tets.row(tet_idx);
			int c_idx = l->size();
			for( int j=0; j<4; ++j ){
				trips_x->emplace_back(c_idx, tet[j], bary[j]*pair.q_n[0]);
				trips_y->emplace_back(c_idx, tet[j], bary[j]*pair.q_n[1]);
				trips_z->emplace_back(c_idx, tet[j], bary[j]*pair.q_n[2]);
			}
			l->emplace_back( pair.q_n.dot(pair.pt_on_q));
			continue;
		}

	} // end loop pairs

} // end jacobian

} // namespace admmpd
