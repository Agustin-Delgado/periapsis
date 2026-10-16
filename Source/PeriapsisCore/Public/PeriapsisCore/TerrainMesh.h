// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/Terrain.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace Periapsis::Core
{
	class LocalFrame;
	struct CelestialBody;

	// A node of a body's terrain quadtree (spec 6.3): a square of one of the six faces of a
	// cube projected onto the sphere. Level 0 is a whole face; each level splits in four.
	struct TerrainNode
	{
		int Face = 0;  // 0..5: +X, -X, +Y, -Y, +Z, -Z
		int Level = 0;
		uint32_t X = 0; // column and row at this level, 0 .. 2^Level - 1
		uint32_t Y = 0;

		uint64_t Key() const
		{
			return (static_cast<uint64_t>(Face) << 61) | (static_cast<uint64_t>(Level) << 56) | (static_cast<uint64_t>(X) << 28) | Y;
		}
		TerrainNode Child(int Index) const
		{
			return {Face, Level + 1, X * 2 + static_cast<uint32_t>(Index & 1), Y * 2 + static_cast<uint32_t>(Index >> 1)};
		}
		bool operator==(const TerrainNode& O) const { return Key() == O.Key(); }
	};

	inline constexpr int kTerrainFaces = 6;
	inline constexpr int kMaxTerrainLevel = 24;

	// A point of a cube face (U, V in [-1, 1]) to a unit direction, with the "spherified cube"
	// mapping, which spreads the vertices more evenly than plain normalization.
	PERIAPSISCORE_API Vec3 CubeToSphere(int Face, double U, double V);

	// Distance between neighbouring vertices of a node's mesh, at the radius (m).
	PERIAPSISCORE_API double NodeSpacing(double Radius, int Level, int Resolution);

	// A chunk of terrain mesh, in the body-fixed frame, relative to Center. Resolution x
	// Resolution vertices, plus a skirt around the border that hangs below the ground so the
	// seams between chunks of different detail never show the sky.
	struct TerrainChunkMesh
	{
		Vec3 Center;                 // body-fixed, m
		std::vector<Vec3> Positions; // relative to Center, m
		std::vector<Vec3> Normals;
		std::vector<double> Heights; // above the radius, per vertex (for colouring)
		std::vector<int> Triangles;  // counter-clockwise seen from outside (right-handed)
		double BoundingRadius = 0.0; // of the ground (skirt left out), around Center, m
		// How far the mesh strays from the true ground between its vertices (m), measured at
		// the cell centres: what changes on screen when it is replaced by its children.
		double GeometricError = 0.0;
	};

	PERIAPSISCORE_API TerrainChunkMesh BuildTerrainChunk(const TerrainShape& Shape, double Radius, const TerrainNode& Node, int Resolution);

	struct TerrainLodSettings
	{
		int Resolution = 33;          // vertices per side of a chunk
		double PixelsPerRadian = 800; // of the camera, along the screen height
		// A chunk splits while its geometric error looks larger than this on screen: the most a
		// change of detail can move the ground on screen (the "popping" of spec 14, M4).
		double MaxPixelError = 1.5;
		double FinestSpacing = 1.0; // m: no chunk gets denser than this
	};

	// Where a generated chunk's ground is: a sphere around its centre (body-fixed, m).
	struct TerrainNodeBounds
	{
		Vec3 Center;
		double Radius = 0.0;
		double GeometricError = 0.0; // m
	};

	// The chunks to draw for a camera at CameraBodyFixed (m, body-fixed), coarse far away and
	// fine near it. Lookup says whether a node's chunk is generated, and its bounds. A node
	// whose children are not all generated is drawn instead of them, so the surface never has
	// holes while finer chunks are on their way; the missing children are requested (OnWanted).
	PERIAPSISCORE_API std::vector<TerrainNode> SelectTerrainNodes(double Radius, const Vec3& CameraBodyFixed, const TerrainLodSettings& Settings,
		const std::function<bool(const TerrainNode&, TerrainNodeBounds&)>& Lookup, const std::function<void(const TerrainNode&)>& OnWanted);

	// A square grid of ground under a body-fixed physics frame, for collision: Resolution x
	// Resolution vertices spaced Spacing apart around a local point, on the analytic terrain,
	// in local coordinates.
	struct GroundPatch
	{
		std::vector<Vec3> Positions; // local, m
		std::vector<int> Triangles;  // counter-clockwise seen from above (right-handed)
	};
	PERIAPSISCORE_API GroundPatch BuildGroundPatch(const LocalFrame& Frame, const CelestialBody& Body, const Vec3& LocalCenter,
		int Resolution, double Spacing);
}
