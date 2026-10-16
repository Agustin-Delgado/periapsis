#include "PeriapsisCore/TerrainMesh.h"

#include "PeriapsisCore/CelestialSystem.h"
#include "PeriapsisCore/Constants.h"
#include "PeriapsisCore/LocalFrame.h"
#include "PeriapsisCore/Surface.h"

#include <algorithm>
#include <cmath>

namespace Periapsis::Core
{
	namespace
	{
		struct FaceAxes
		{
			Vec3 Normal;
			Vec3 A; // along U
			Vec3 B; // along V; A x B = Normal, so U then V turns counter-clockwise seen from outside
		};

		const FaceAxes Faces[kTerrainFaces] = {
			{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
			{{-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}},
			{{0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}},
			{{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
			{{0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}},
			{{0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}},
		};

		struct NodeRect
		{
			double U0, V0, Size;
		};

		NodeRect RectOf(const TerrainNode& Node)
		{
			const double Size = 2.0 / static_cast<double>(1u << Node.Level);
			return {-1.0 + Node.X * Size, -1.0 + Node.Y * Size, Size};
		}

	}

	Vec3 CubeToSphere(int Face, double U, double V)
	{
		const FaceAxes& F = Faces[Face];
		const Vec3 P = F.Normal + F.A * U + F.B * V;
		const double X2 = P.X * P.X;
		const double Y2 = P.Y * P.Y;
		const double Z2 = P.Z * P.Z;
		return {P.X * std::sqrt(1.0 - Y2 / 2.0 - Z2 / 2.0 + Y2 * Z2 / 3.0), P.Y * std::sqrt(1.0 - Z2 / 2.0 - X2 / 2.0 + Z2 * X2 / 3.0),
			P.Z * std::sqrt(1.0 - X2 / 2.0 - Y2 / 2.0 + X2 * Y2 / 3.0)};
	}

	double NodeSpacing(double Radius, int Level, int Resolution)
	{
		return kHalfPi * Radius / static_cast<double>(1u << Level) / (Resolution - 1);
	}

	TerrainChunkMesh BuildTerrainChunk(const TerrainShape& Shape, double Radius, const TerrainNode& Node, int Resolution)
	{
		const NodeRect R = RectOf(Node);
		const int N = Resolution;
		const double Step = R.Size / (N - 1);
		TerrainChunkMesh Mesh;
		Mesh.Center = CubeToSphere(Node.Face, R.U0 + 0.5 * R.Size, R.V0 + 0.5 * R.Size) * Radius;

		// The grid with one extra ring, for normals that match across chunk borders.
		const int G = N + 2;
		std::vector<Vec3> Grid(static_cast<size_t>(G * G));
		std::vector<double> GridHeight(static_cast<size_t>(G * G));
		for (int J = 0; J < G; ++J)
		{
			for (int I = 0; I < G; ++I)
			{
				const Vec3 Direction = CubeToSphere(Node.Face, R.U0 + (I - 1) * Step, R.V0 + (J - 1) * Step);
				const double Height = Shape.Height(Direction);
				Grid[static_cast<size_t>(J * G + I)] = Direction * (Radius + Height);
				GridHeight[static_cast<size_t>(J * G + I)] = Height;
			}
		}
		auto At = [&](int I, int J) -> const Vec3& { return Grid[static_cast<size_t>((J + 1) * G + I + 1)]; };

		Mesh.Positions.reserve(static_cast<size_t>(N * N + 4 * (N - 1)));
		for (int J = 0; J < N; ++J)
		{
			for (int I = 0; I < N; ++I)
			{
				Mesh.Positions.push_back(At(I, J) - Mesh.Center);
				Mesh.Normals.push_back(Normalized(Cross(At(I + 1, J) - At(I - 1, J), At(I, J + 1) - At(I, J - 1))));
				Mesh.Heights.push_back(GridHeight[static_cast<size_t>((J + 1) * G + I + 1)]);
			}
		}
		for (int J = 0; J + 1 < N; ++J)
		{
			for (int I = 0; I + 1 < N; ++I)
			{
				const int A = J * N + I;
				Mesh.Triangles.insert(Mesh.Triangles.end(), {A, A + 1, A + N + 1, A, A + N + 1, A + N});
			}
		}

		// Skirt: the border loop again, hanging below the ground, joined to the border on both
		// sides (it is seen from either side through a seam).
		std::vector<int> Border;
		for (int I = 0; I < N - 1; ++I) Border.push_back(I);
		for (int J = 0; J < N - 1; ++J) Border.push_back(J * N + N - 1);
		for (int I = N - 1; I > 0; --I) Border.push_back((N - 1) * N + I);
		for (int J = N - 1; J > 0; --J) Border.push_back(J * N);
		const double Depth = 10.0 * NodeSpacing(Radius, Node.Level, N) + 0.1 * (Shape.MaxHeight() - Shape.MinHeight()) / (1 << Node.Level);
		const int SkirtStart = static_cast<int>(Mesh.Positions.size());
		for (const int Index : Border)
		{
			const Vec3 Down = Normalized(Mesh.Positions[static_cast<size_t>(Index)] + Mesh.Center);
			Mesh.Positions.push_back(Mesh.Positions[static_cast<size_t>(Index)] - Down * Depth);
			Mesh.Normals.push_back(Mesh.Normals[static_cast<size_t>(Index)]);
			Mesh.Heights.push_back(Mesh.Heights[static_cast<size_t>(Index)]);
		}
		const int Count = static_cast<int>(Border.size());
		for (int K = 0; K < Count; ++K)
		{
			const int A = Border[static_cast<size_t>(K)];
			const int B = Border[static_cast<size_t>((K + 1) % Count)];
			const int SA = SkirtStart + K;
			const int SB = SkirtStart + (K + 1) % Count;
			Mesh.Triangles.insert(Mesh.Triangles.end(), {A, SA, SB, A, SB, B, A, SB, SA, A, B, SB});
		}

		for (size_t Index = 0; Index < static_cast<size_t>(N * N); ++Index)
		{
			Mesh.BoundingRadius = std::max(Mesh.BoundingRadius, Norm(Mesh.Positions[Index]));
		}

		// Error at the cell centres (every other cell, for speed): the true ground against the
		// middle of the cell's four corners.
		for (int J = 0; J + 1 < N; ++J)
		{
			for (int I = J % 2; I + 1 < N; I += 2)
			{
				const Vec3 Direction = CubeToSphere(Node.Face, R.U0 + (I + 0.5) * Step, R.V0 + (J + 0.5) * Step);
				const Vec3 Truth = Direction * (Radius + Shape.Height(Direction));
				const Vec3 Mesh4 = (At(I, J) + At(I + 1, J) + At(I, J + 1) + At(I + 1, J + 1)) * 0.25;
				Mesh.GeometricError = std::max(Mesh.GeometricError, Norm(Truth - Mesh4));
			}
		}
		return Mesh;
	}

	std::vector<TerrainNode> SelectTerrainNodes(double Radius, const Vec3& CameraBodyFixed, const TerrainLodSettings& Settings,
		const std::function<bool(const TerrainNode&, TerrainNodeBounds&)>& Lookup, const std::function<void(const TerrainNode&)>& OnWanted)
	{
		std::vector<TerrainNode> Selected;
		TerrainNodeBounds Unused;
		auto IsReady = [&](const TerrainNode& Node) { return Lookup(Node, Unused); };
		// Splits while the chunk's error, seen from the camera, is larger than the allowed one.
		auto ShouldSplit = [&](const TerrainNode& Node)
		{
			const double Spacing = NodeSpacing(Radius, Node.Level, Settings.Resolution);
			if (Node.Level >= kMaxTerrainLevel || Spacing * 0.5 < Settings.FinestSpacing)
			{
				return false;
			}
			TerrainNodeBounds Bounds;
			Lookup(Node, Bounds);
			const double Distance = std::max(Norm(CameraBodyFixed - Bounds.Center) - Bounds.Radius, 1e-3);
			return Bounds.GeometricError * Settings.PixelsPerRadian / Distance > Settings.MaxPixelError;
		};

		std::function<void(const TerrainNode&)> Visit = [&](const TerrainNode& Node)
		{
			if (!ShouldSplit(Node))
			{
				Selected.push_back(Node);
				return;
			}
			bool bAllReady = true;
			for (int Index = 0; Index < 4; ++Index)
			{
				const TerrainNode Child = Node.Child(Index);
				if (!IsReady(Child))
				{
					OnWanted(Child);
					bAllReady = false;
				}
			}
			if (!bAllReady)
			{
				Selected.push_back(Node);
				return;
			}
			for (int Index = 0; Index < 4; ++Index)
			{
				Visit(Node.Child(Index));
			}
		};

		for (int Face = 0; Face < kTerrainFaces; ++Face)
		{
			const TerrainNode Root{Face, 0, 0, 0};
			if (IsReady(Root))
			{
				Visit(Root);
			}
			else
			{
				OnWanted(Root);
			}
		}
		return Selected;
	}

	GroundPatch BuildGroundPatch(const LocalFrame& Frame, const CelestialBody& Body, const Vec3& LocalCenter, int Resolution, double Spacing)
	{
		GroundPatch Patch;
		const int N = Resolution;
		const double Half = 0.5 * Spacing * (N - 1);
		Patch.Positions.reserve(static_cast<size_t>(N * N));
		for (int J = 0; J < N; ++J)
		{
			for (int I = 0; I < N; ++I)
			{
				const Vec3 Local{LocalCenter.X - Half + I * Spacing, LocalCenter.Y - Half + J * Spacing, LocalCenter.Z};
				const Vec3 Above = Frame.ToBody({Local, Vec3{}}).Position;
				const Vec3 Ground = Normalized(Above) * SurfaceRadius(Body, Above, Frame.Time());
				Patch.Positions.push_back(Frame.ToLocal({Ground, GroundVelocity(Body, Ground)}).Position);
			}
		}
		for (int J = 0; J + 1 < N; ++J)
		{
			for (int I = 0; I + 1 < N; ++I)
			{
				const int A = J * N + I;
				Patch.Triangles.insert(Patch.Triangles.end(), {A, A + 1, A + N + 1, A, A + N + 1, A + N});
			}
		}
		return Patch;
	}
}
