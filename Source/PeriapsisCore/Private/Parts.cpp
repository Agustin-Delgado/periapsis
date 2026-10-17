#include "PeriapsisCore/Parts.h"

#include "PeriapsisCore/Constants.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace Periapsis::Core
{
	namespace
	{
		// Integrates a polynomial of degree 7 or less over [A, B] exactly (4-point Gauss-Legendre).
		template <typename FunctionType>
		double Integrate(double A, double B, FunctionType&& F)
		{
			static constexpr double Nodes[] = {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526};
			static constexpr double Weights[] = {0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538};
			const double Half = 0.5 * (B - A);
			const double Mid = 0.5 * (A + B);
			double Sum = 0.0;
			for (int Index = 0; Index < 4; ++Index)
			{
				Sum += Weights[Index] * F(Mid + Half * Nodes[Index]);
			}
			return Sum * Half;
		}

		std::string Describe(const PartDefinition& Part) { return "part '" + Part.Id + "'"; }
	}

	double PartGeometry::RadiusAt(double Height) const
	{
		if (Shape == EShape::Box)
		{
			return 0.5 * Width;
		}
		const double T = std::clamp(Height / Length + 0.5, 0.0, 1.0);
		return BottomRadius + (TopRadius - BottomRadius) * T;
	}

	double PartGeometry::Support(const Vec3& Direction) const
	{
		if (Shape == EShape::Box)
		{
			return 0.5 * (Width * (std::abs(Direction.X) + std::abs(Direction.Y)) + Length * std::abs(Direction.Z));
		}
		const double Side = std::sqrt(Direction.X * Direction.X + Direction.Y * Direction.Y);
		const double Bottom = -0.5 * Length * Direction.Z + BottomRadius * Side;
		const double Top = 0.5 * Length * Direction.Z + TopRadius * Side;
		return std::max(Bottom, Top);
	}

	double PartGeometry::CentroidHeight() const
	{
		if (Shape == EShape::Box)
		{
			return 0.0;
		}
		const double Half = 0.5 * Length;
		const double Volume = Integrate(-Half, Half, [&](double Z) { return RadiusAt(Z) * RadiusAt(Z); });
		return Volume > 0.0 ? Integrate(-Half, Half, [&](double Z) { return Z * RadiusAt(Z) * RadiusAt(Z); }) / Volume : 0.0;
	}

	Mat3 PartGeometry::UnitInertia(double Mass) const
	{
		if (Shape == EShape::Box)
		{
			const double Across = Mass * (Width * Width + Length * Length) / 12.0;
			const double Around = Mass * (2.0 * Width * Width) / 12.0;
			return {{Across, 0.0, 0.0}, {0.0, Across, 0.0}, {0.0, 0.0, Around}};
		}
		// Slices of a solid of revolution with uniform density, radius linear in height.
		const double Half = 0.5 * Length;
		auto R = [&](double Z) { return RadiusAt(Z); };
		const double Volume = Integrate(-Half, Half, [&](double Z) { return R(Z) * R(Z); }); // over pi
		if (!(Volume > 0.0))
		{
			return Mat3::Zero();
		}
		const double Density = Mass / Volume;
		const double Around = Density * 0.5 * Integrate(-Half, Half, [&](double Z) { return std::pow(R(Z), 4); });
		const double Across = Density * Integrate(-Half, Half, [&](double Z) { return 0.25 * std::pow(R(Z), 4) + Z * Z * R(Z) * R(Z); });
		return {{Across, 0.0, 0.0}, {0.0, Across, 0.0}, {0.0, 0.0, Around}};
	}

	std::vector<Vec3> PartGeometry::HullPoints(int Segments) const
	{
		std::vector<Vec3> Points;
		if (Shape == EShape::Box)
		{
			for (int Corner = 0; Corner < 8; ++Corner)
			{
				Points.push_back({(Corner & 1 ? 0.5 : -0.5) * Width, (Corner & 2 ? 0.5 : -0.5) * Width, (Corner & 4 ? 0.5 : -0.5) * Length});
			}
			return Points;
		}
		for (const double Z : {-0.5 * Length, 0.5 * Length})
		{
			const double Radius = RadiusAt(Z);
			if (Radius < 1e-6)
			{
				Points.push_back({0.0, 0.0, Z});
				continue;
			}
			for (int Index = 0; Index < Segments; ++Index)
			{
				const double Angle = kTwoPi * Index / Segments;
				Points.push_back({Radius * std::cos(Angle), Radius * std::sin(Angle), Z});
			}
		}
		return Points;
	}

	const AttachNode* PartDefinition::FindNode(const std::string& NodeId) const
	{
		const auto Found = std::find_if(Nodes.begin(), Nodes.end(), [&](const AttachNode& Node) { return Node.Id == NodeId; });
		return Found == Nodes.end() ? nullptr : &*Found;
	}

	double PartDefinition::WetMass() const
	{
		double Mass = DryMass;
		for (const ResourceCapacity& Capacity : Resources)
		{
			Mass += Capacity.Mass;
		}
		return Mass;
	}

	int PartCatalog::FindPart(const std::string& Id) const
	{
		for (size_t Index = 0; Index < Parts.size(); ++Index)
		{
			if (Parts[Index].Id == Id)
			{
				return static_cast<int>(Index);
			}
		}
		return -1;
	}

	int PartCatalog::FindResource(const std::string& Id) const
	{
		for (size_t Index = 0; Index < ResourceTypes.size(); ++Index)
		{
			if (ResourceTypes[Index].Id == Id)
			{
				return static_cast<int>(Index);
			}
		}
		return -1;
	}

	std::vector<std::string> PartCatalog::Validate() const
	{
		std::vector<std::string> Errors;
		const int ResourceCount = static_cast<int>(ResourceTypes.size());
		std::set<std::string> ResourceIds;
		for (const ResourceDefinition& Resource : ResourceTypes)
		{
			if (Resource.Id.empty() || !ResourceIds.insert(Resource.Id).second)
			{
				Errors.push_back("resource '" + Resource.Id + "': missing or duplicate id");
			}
		}
		std::set<std::string> PartIds;
		for (const PartDefinition& Part : Parts)
		{
			const std::string Where = Describe(Part);
			if (Part.Id.empty() || !PartIds.insert(Part.Id).second)
			{
				Errors.push_back(Where + ": missing or duplicate id");
			}
			if (!(Part.DryMass > 0.0))
			{
				Errors.push_back(Where + ": dry mass must be positive");
			}
			const PartGeometry& G = Part.Geometry;
			const bool bGeometryOk = G.Length > 0.0
				&& (G.Shape == PartGeometry::EShape::Box ? G.Width > 0.0 : G.BottomRadius >= 0.0 && G.TopRadius >= 0.0 && G.MaxRadius() > 0.0);
			if (!bGeometryOk)
			{
				Errors.push_back(Where + ": geometry needs a positive length and size");
			}
			if (!(Part.JointForce > 0.0) || !(Part.JointTorque > 0.0))
			{
				Errors.push_back(Where + ": joint strength must be positive");
			}
			std::set<std::string> NodeIds;
			for (const AttachNode& Node : Part.Nodes)
			{
				if (Node.Id.empty() || Node.Id == "surface" || !NodeIds.insert(Node.Id).second)
				{
					Errors.push_back(Where + ": node '" + Node.Id + "' has a missing, reserved or duplicate id");
				}
				if (std::abs(Norm(Node.Direction) - 1.0) > 1e-6)
				{
					Errors.push_back(Where + ": node '" + Node.Id + "' needs a unit direction");
				}
			}
			for (const ResourceCapacity& Capacity : Part.Resources)
			{
				if (Capacity.Resource < 0 || Capacity.Resource >= ResourceCount)
				{
					Errors.push_back(Where + ": unknown resource");
				}
				if (!(Capacity.Mass > 0.0))
				{
					Errors.push_back(Where + ": resource capacity must be positive");
				}
			}
			if (Part.Engine)
			{
				const EngineModule& Engine = *Part.Engine;
				if (!(Engine.ThrustVacuum > 0.0) || !(Engine.IspVacuum > 0.0) || !(Engine.IspSeaLevel >= 0.0))
				{
					Errors.push_back(Where + ": engine needs positive thrust and Isp");
				}
				double Fractions = 0.0;
				for (const Propellant& P : Engine.Propellants)
				{
					if (P.Resource < 0 || P.Resource >= ResourceCount)
					{
						Errors.push_back(Where + ": engine burns an unknown resource");
					}
					if (!(P.MassFraction > 0.0))
					{
						Errors.push_back(Where + ": propellant fractions must be positive");
					}
					Fractions += P.MassFraction;
				}
				if (Engine.Propellants.empty() || std::abs(Fractions - 1.0) > 1e-9)
				{
					Errors.push_back(Where + ": propellant fractions must add up to 1");
				}
			}
			if (Part.Decoupler && !(Part.Decoupler->EjectionImpulse >= 0.0))
			{
				Errors.push_back(Where + ": ejection impulse cannot be negative");
			}
		}
		return Errors;
	}

	PartMesh BuildPartMesh(const PartGeometry& Geometry, int Segments)
	{
		PartMesh Mesh;
		auto AddQuad = [&](const Vec3 (&Corners)[4], const Vec3 (&Normals)[4])
		{
			const int Base = static_cast<int>(Mesh.Positions.size());
			for (int Index = 0; Index < 4; ++Index)
			{
				Mesh.Positions.push_back(Corners[Index]);
				Mesh.Normals.push_back(Normals[Index]);
			}
			Mesh.Triangles.insert(Mesh.Triangles.end(), {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
		};

		if (Geometry.Shape == PartGeometry::EShape::Box)
		{
			const Vec3 Half{0.5 * Geometry.Width, 0.5 * Geometry.Width, 0.5 * Geometry.Length};
			const Vec3 Axes[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
			for (int Axis = 0; Axis < 3; ++Axis)
			{
				for (const double Sign : {1.0, -1.0})
				{
					const Vec3 N = Axes[Axis] * Sign;
					const Vec3 U = Axes[(Axis + 1) % 3] * Sign;
					const Vec3 V = Cross(N, U);
					auto Corner = [&](double A, double B)
					{
						const Vec3 P = N + U * A + V * B;
						return Vec3{P.X * Half.X, P.Y * Half.Y, P.Z * Half.Z};
					};
					AddQuad({Corner(-1, -1), Corner(1, -1), Corner(1, 1), Corner(-1, 1)}, {N, N, N, N});
				}
			}
			return Mesh;
		}

		const double Half = 0.5 * Geometry.Length;
		const double Rb = Geometry.BottomRadius;
		const double Rt = Geometry.TopRadius;
		// The side's normal leans up when the part narrows towards the top.
		const double Lean = (Rb - Rt) / Geometry.Length;
		for (int Index = 0; Index < Segments; ++Index)
		{
			const double A0 = kTwoPi * Index / Segments;
			const double A1 = kTwoPi * (Index + 1) / Segments;
			const Vec3 D0{std::cos(A0), std::sin(A0), 0.0};
			const Vec3 D1{std::cos(A1), std::sin(A1), 0.0};
			const Vec3 N0 = Normalized(D0 + Vec3{0.0, 0.0, Lean});
			const Vec3 N1 = Normalized(D1 + Vec3{0.0, 0.0, Lean});
			AddQuad({D0 * Rb - Vec3{0.0, 0.0, Half}, D1 * Rb - Vec3{0.0, 0.0, Half}, D1 * Rt + Vec3{0.0, 0.0, Half}, D0 * Rt + Vec3{0.0, 0.0, Half}},
				{N0, N1, N1, N0});
		}
		auto AddCap = [&](double Z, double Radius, double Sign)
		{
			if (Radius < 1e-6)
			{
				return;
			}
			const Vec3 N{0.0, 0.0, Sign};
			const int Center = static_cast<int>(Mesh.Positions.size());
			Mesh.Positions.push_back({0.0, 0.0, Z});
			Mesh.Normals.push_back(N);
			for (int Index = 0; Index < Segments; ++Index)
			{
				const double Angle = kTwoPi * Index / Segments;
				Mesh.Positions.push_back({Radius * std::cos(Angle), Radius * std::sin(Angle), Z});
				Mesh.Normals.push_back(N);
			}
			for (int Index = 0; Index < Segments; ++Index)
			{
				const int A = Center + 1 + Index;
				const int B = Center + 1 + (Index + 1) % Segments;
				if (Sign > 0.0)
				{
					Mesh.Triangles.insert(Mesh.Triangles.end(), {Center, A, B});
				}
				else
				{
					Mesh.Triangles.insert(Mesh.Triangles.end(), {Center, B, A});
				}
			}
		};
		AddCap(Half, Rt, 1.0);
		AddCap(-Half, Rb, -1.0);
		return Mesh;
	}

	namespace
	{
		struct ProfilePoint
		{
			double R = 0.0;
			double Z = 0.0;
			PartPaint Paint = PartPaint::Body; // of the stretch from this point to the next
		};

		PartMesh& MeshFor(std::vector<PartLookPiece>& Pieces, PartPaint Paint)
		{
			for (PartLookPiece& Piece : Pieces)
			{
				if (Piece.Paint == Paint)
				{
					return Piece.Mesh;
				}
			}
			Pieces.push_back({Paint, {}});
			return Pieces.back().Mesh;
		}

		// Turns a profile (radius against height, in order) about Z. A stretch that goes down
		// faces out and one that goes up faces in, so an open nozzle shows inside and out.
		void Lathe(const std::vector<ProfilePoint>& Profile, int Segments, std::vector<PartLookPiece>& Pieces)
		{
			for (size_t Index = 0; Index + 1 < Profile.size(); ++Index)
			{
				const ProfilePoint& A = Profile[Index];
				const ProfilePoint& B = Profile[Index + 1];
				const double DR = B.R - A.R;
				const double DZ = B.Z - A.Z;
				const double Span = std::sqrt(DR * DR + DZ * DZ);
				if (Span < 1e-9)
				{
					continue;
				}
				PartMesh& Mesh = MeshFor(Pieces, A.Paint);
				for (int Step = 0; Step < Segments; ++Step)
				{
					const double A0 = kTwoPi * Step / Segments;
					const double A1 = kTwoPi * (Step + 1) / Segments;
					const Vec3 D0{std::cos(A0), std::sin(A0), 0.0};
					const Vec3 D1{std::cos(A1), std::sin(A1), 0.0};
					const Vec3 N0 = D0 * (-DZ / Span) + Vec3{0.0, 0.0, DR / Span};
					const Vec3 N1 = D1 * (-DZ / Span) + Vec3{0.0, 0.0, DR / Span};
					const int Base = static_cast<int>(Mesh.Positions.size());
					Mesh.Positions.insert(Mesh.Positions.end(), {D0 * B.R + Vec3{0.0, 0.0, B.Z}, D1 * B.R + Vec3{0.0, 0.0, B.Z},
						D1 * A.R + Vec3{0.0, 0.0, A.Z}, D0 * A.R + Vec3{0.0, 0.0, A.Z}});
					Mesh.Normals.insert(Mesh.Normals.end(), {N0, N1, N1, N0});
					Mesh.Triangles.insert(Mesh.Triangles.end(), {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
				}
			}
		}

		// A flat disc facing +Z (bUp) or -Z.
		void Disc(double Z, double Radius, bool bUp, PartPaint Paint, int Segments, std::vector<PartLookPiece>& Pieces)
		{
			if (Radius < 1e-6)
			{
				return;
			}
			PartMesh& Mesh = MeshFor(Pieces, Paint);
			const Vec3 N{0.0, 0.0, bUp ? 1.0 : -1.0};
			const int Center = static_cast<int>(Mesh.Positions.size());
			Mesh.Positions.push_back({0.0, 0.0, Z});
			Mesh.Normals.push_back(N);
			for (int Step = 0; Step < Segments; ++Step)
			{
				const double Angle = kTwoPi * Step / Segments;
				Mesh.Positions.push_back({Radius * std::cos(Angle), Radius * std::sin(Angle), Z});
				Mesh.Normals.push_back(N);
			}
			for (int Step = 0; Step < Segments; ++Step)
			{
				const int A = Center + 1 + Step;
				const int B = Center + 1 + (Step + 1) % Segments;
				if (bUp)
				{
					Mesh.Triangles.insert(Mesh.Triangles.end(), {Center, A, B});
				}
				else
				{
					Mesh.Triangles.insert(Mesh.Triangles.end(), {Center, B, A});
				}
			}
		}
	}

	std::vector<PartLookPiece> BuildPartLook(const PartDefinition& Part, int Segments)
	{
		std::vector<PartLookPiece> Pieces;
		const PartGeometry& G = Part.Geometry;
		if (G.Shape == PartGeometry::EShape::Box)
		{
			Pieces.push_back({PartPaint::Body, BuildPartMesh(G, Segments)});
			return Pieces;
		}
		const double L = G.Length;
		const double Top = 0.5 * L;
		const double Bottom = -0.5 * L;
		auto R = [&](double Z) { return G.RadiusAt(Z); };
		// Rings at the ends, about as thick as a joint looks next to the part.
		const double Ring = std::min(0.1 * L, 0.12);

		std::vector<ProfilePoint> Profile;
		// Clamped to the solid: what is drawn never sticks out of what collides.
		auto Point = [&](double Radius, double Z, PartPaint Paint) { Profile.push_back({std::min(Radius, R(Z)), Z, Paint}); };
		if (Part.Category == "engine")
		{
			// A housing on top, a neck, then a bell flaring out to the bottom radius, open below.
			const double Housing = Top - 0.3 * L;
			const double Throat = Top - 0.42 * L;
			const double Neck = 0.5 * std::min(G.TopRadius, G.BottomRadius);
			const double Wall = 0.04 * G.BottomRadius;
			constexpr int BellSteps = 8;
			auto BellRadius = [&](double S) { return Neck + (G.BottomRadius - Neck) * std::sqrt(S); };
			Point(G.TopRadius, Top, PartPaint::Trim);
			Point(G.TopRadius, Top - Ring, PartPaint::Body);
			Point(0.9 * G.TopRadius, Housing, PartPaint::Trim);
			for (int Step = 0; Step <= BellSteps; ++Step)
			{
				const double S = static_cast<double>(Step) / BellSteps;
				Point(BellRadius(S), Throat + (Bottom - Throat) * S, PartPaint::Bell);
			}
			Profile.back().Paint = PartPaint::Inside; // the lip, then up the inside
			for (int Step = BellSteps; Step >= 0; --Step)
			{
				const double S = static_cast<double>(Step) / BellSteps;
				Point(std::max(BellRadius(S) - Wall, 0.0), Throat + (Bottom - Throat) * S, PartPaint::Inside);
			}
			Lathe(Profile, Segments, Pieces);
			Disc(Top, R(Top), true, PartPaint::Trim, Segments, Pieces);
			Disc(Throat, std::min(std::max(Neck - Wall, 0.0), R(Throat)), false, PartPaint::Inside, Segments, Pieces);
			return Pieces;
		}
		if (Part.Category == "command")
		{
			// A docking ring on top, a window band, a heat shield underneath.
			const double Shield = Bottom + std::min(0.1 * L, 0.15);
			const double WindowTop = Top - 0.22 * L;
			const double WindowBottom = Top - 0.34 * L;
			Point(R(Top), Top, PartPaint::Body);
			Point(R(WindowTop), WindowTop, PartPaint::Window);
			Point(R(WindowBottom), WindowBottom, PartPaint::Body);
			Point(R(Shield), Shield, PartPaint::Shield);
			Point(R(Bottom), Bottom, PartPaint::Shield);
			Lathe(Profile, Segments, Pieces);
			Disc(Top, R(Top), true, PartPaint::Trim, Segments, Pieces);
			Disc(Bottom, R(Bottom), false, PartPaint::Shield, Segments, Pieces);
			return Pieces;
		}
		if (Part.Category == "decoupler")
		{
			// Two halves with a dark seam where they part.
			const double Seam = 0.12 * L;
			Point(R(Top), Top, PartPaint::Body);
			Point(R(Seam), Seam, PartPaint::Trim);
			Point(R(-Seam), -Seam, PartPaint::Body);
			Point(R(Bottom), Bottom, PartPaint::Body);
			Lathe(Profile, Segments, Pieces);
			Disc(Top, R(Top), true, PartPaint::Body, Segments, Pieces);
			Disc(Bottom, R(Bottom), false, PartPaint::Body, Segments, Pieces);
			return Pieces;
		}
		if (G.TopRadius < 0.25 * G.BottomRadius)
		{
			// A nose: the tip painted, a ring at the base.
			const double Tip = Top - 0.22 * L;
			Point(R(Top), Top, PartPaint::Accent);
			Point(R(Tip), Tip, PartPaint::Body);
			Point(R(Bottom + Ring), Bottom + Ring, PartPaint::Trim);
			Point(R(Bottom), Bottom, PartPaint::Trim);
			Lathe(Profile, Segments, Pieces);
			Disc(Top, R(Top), true, PartPaint::Accent, Segments, Pieces);
			Disc(Bottom, R(Bottom), false, PartPaint::Trim, Segments, Pieces);
			return Pieces;
		}
		// Tanks, adapters and the rest: dark rings at both joints.
		Point(R(Top), Top, PartPaint::Trim);
		Point(R(Top - Ring), Top - Ring, PartPaint::Body);
		Point(R(Bottom + Ring), Bottom + Ring, PartPaint::Trim);
		Point(R(Bottom), Bottom, PartPaint::Trim);
		Lathe(Profile, Segments, Pieces);
		Disc(Top, R(Top), true, PartPaint::Trim, Segments, Pieces);
		Disc(Bottom, R(Bottom), false, PartPaint::Trim, Segments, Pieces);
		return Pieces;
	}
}
