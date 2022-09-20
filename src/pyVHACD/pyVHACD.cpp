#include <iostream>
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#define ENABLE_VHACD_IMPLEMENTATION 1
#define VHACD_DISABLE_THREADING 0
#include "VHACD.h"

namespace py = pybind11;

// Return a list of convex hulls
// Each convex hull is a tuple of (vertices, indices)
// vertices is a numpy array of shape (n, 3)
// indices is a numpy array of shape (m,)
std::vector<std::pair<py::array_t<double>, py::array_t<uint32_t>>> compute_vhacd(py::array_t<double> points, py::array_t<uint32_t> faces) {

	/*  read input arrays buffer_info */
	auto buf_points = points.request();
	auto buf_faces = faces.request();

	/*  variables */
	double *ptr_points = (double *) buf_points.ptr;
	uint32_t *ptr_faces = (uint32_t *) buf_faces.ptr;
	size_t num_points = buf_points.shape[0];
	size_t num_faces = buf_faces.shape[0] / 4;
	size_t num_triangle_indices = num_faces * 3;

	printf("num_points = %d\n", num_points);
	printf("num_faces = %d\n", num_faces);
	printf("num_triangle_indices = %d\n", num_triangle_indices);

	// Prepare our input arrays for VHACD
	u_int32_t *triangles = new uint32_t[num_triangle_indices];
	for (uint32_t i=0; i<num_faces; i++)
	{
		// We skip the first index of the face array, which is the number of vertices in the face
		triangles[3*i] = ptr_faces[4*i+1];
		triangles[3*i+1] = ptr_faces[4*i+2];
		triangles[3*i+2] = ptr_faces[4*i+3];

		printf("f %d %d %d\n", triangles[3*i], triangles[3*i+1], triangles[3*i+2]);

		// printf("triangles[%d] = %d \n", 3*i, ptr_faces[4*i+1]);
		// printf("triangles[%d] = %d \n", 3*i+1, ptr_faces[4*i+2]);
		// printf("triangles[%d] = %d \n", 3*i+2, ptr_faces[4*i+3]);
	}

	VHACD::IVHACD::Parameters p;

#if VHACD_DISABLE_THREADING
	VHACD::IVHACD *iface = VHACD::CreateVHACD();
#else
	VHACD::IVHACD *iface = p.m_asyncACD ? VHACD::CreateVHACD_ASYNC() : VHACD::CreateVHACD();
#endif

	iface->Compute(ptr_points, num_points, triangles, num_faces, p);

	while ( !iface->IsReady() )
	{
		std::this_thread::sleep_for(std::chrono::nanoseconds(10000)); // s
	}

	// Build our output arrays from VHACD outputs
	std::vector<std::pair<py::array_t<double>, py::array_t<uint32_t>>> res;
	const int nConvexHulls = iface->GetNConvexHulls();
	res.reserve(nConvexHulls);

	if (nConvexHulls)
	{	
		printf("Exporting Convex Decomposition results of %d convex hulls\n", iface->GetNConvexHulls());

		uint32_t baseIndex = 1;
		for (uint32_t i=0; i<iface->GetNConvexHulls(); i++)
		{	
			VHACD::IVHACD::ConvexHull ch;
			iface->GetConvexHull(i, ch);

			/*  allocate the output buffers */
			py::array_t<double> res_vertices = py::array_t<double>(ch.m_points.size() * 3);
			py::array_t<uint32_t> res_faces = py::array_t<uint32_t>(ch.m_triangles.size() * 4);
			
			py::buffer_info buf_res_vertices = res_vertices.request();
			py::buffer_info buf_res_faces = res_faces.request();

			double *ptr_res_vertices = (double *) buf_res_vertices.ptr;
			uint32_t *ptr_res_faces = (uint32_t *) buf_res_faces.ptr;

			printf("Convex Hull %d has %d vertices and %d faces\n", i, ch.m_points.size(), ch.m_triangles.size());

			for (uint32_t j = 0; j < ch.m_points.size(); j++)
			{
				const VHACD::Vertex& pos = ch.m_points[j];
				ptr_res_vertices[3*j] = pos.mX;
				ptr_res_vertices[3*j+1] = pos.mY;
				ptr_res_vertices[3*j+2] = pos.mZ;
				printf("v %0.9f %0.9f %0.9f\n", pos.mX, pos.mY, pos.mZ);
			}

			const uint32_t num_v = 3;
			for (uint32_t j = 0; j < ch.m_triangles.size(); j++)
			{
				uint32_t i1 = ch.m_triangles[j].mI0 + baseIndex;
				uint32_t i2 = ch.m_triangles[j].mI1 + baseIndex;
				uint32_t i3 = ch.m_triangles[j].mI2 + baseIndex;
				ptr_res_faces[4*j] = num_v;
				ptr_res_faces[4*j+1] = i1;
				ptr_res_faces[4*j+2] = i2;
				ptr_res_faces[4*j+3] = i3;
				printf("f %d %d %d\n", i1, i2, i3);
			}
			baseIndex += uint32_t(ch.m_points.size());

			// Reshape the vertices array to be (n, 3)
			const long width = 3;
			res_vertices.resize({(long)ch.m_points.size(), width});

			// Push on our list
			res.emplace_back(std::move(res_vertices), std::move(res_faces));
		}
	}
	return res;
}


/* Wrapping routines with PyBind */
PYBIND11_MODULE(pyVHACD, m) {
	    m.doc() = ""; // optional module docstring
	    m.def("compute_vhacd", &compute_vhacd, "Compute convex hulls");
}
