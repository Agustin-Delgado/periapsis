#include "PeriapsisCore/CelestialSystem.h"

#include "PeriapsisCore/Kepler.h"

#include <limits>
#include <utility>

namespace Periapsis::Core
{
	int CelestialSystem::AddBody(CelestialBody NewBody)
	{
		const bool bIsRoot = NewBody.ParentIndex == kNoBody;
		if (bIsRoot ? !Bodies.empty() : (NewBody.ParentIndex < 0 || NewBody.ParentIndex >= BodyCount()))
		{
			return kNoBody;
		}

		StateVector EpochState;
		if (bIsRoot)
		{
			NewBody.SphereOfInfluence = std::numeric_limits<double>::infinity();
		}
		else
		{
			const double ParentMu = Bodies[static_cast<size_t>(NewBody.ParentIndex)].Mu;
			if (NewBody.SphereOfInfluence <= 0.0)
			{
				NewBody.SphereOfInfluence = LaplaceSphereOfInfluence(SemiMajorAxis(NewBody.Orbit), NewBody.Mu, ParentMu);
			}
			EpochState = ElementsToState(NewBody.Orbit, ParentMu);
		}

		const int Index = BodyCount();
		if (!bIsRoot)
		{
			ChildIndices[static_cast<size_t>(NewBody.ParentIndex)].push_back(Index);
		}
		Bodies.push_back(std::move(NewBody));
		EpochStates.push_back(EpochState);
		ChildIndices.emplace_back();
		return Index;
	}

	int CelestialSystem::FindBody(std::string_view Id) const
	{
		for (int Index = 0; Index < BodyCount(); ++Index)
		{
			if (Bodies[static_cast<size_t>(Index)].Id == Id)
			{
				return Index;
			}
		}
		return kNoBody;
	}

	StateVector CelestialSystem::StateRelativeToParent(int Index, double UniversalTime) const
	{
		const CelestialBody& B = Body(Index);
		if (B.ParentIndex == kNoBody)
		{
			return {};
		}
		return PropagateKepler(EpochStates[static_cast<size_t>(Index)], Body(B.ParentIndex).Mu,
			UniversalTime - B.Orbit.Epoch);
	}

	StateVector CelestialSystem::StateRelativeTo(int BodyIndex, int FrameBodyIndex, double UniversalTime) const
	{
		auto Depth = [this](int Index)
		{
			int D = 0;
			for (int Current = Index; Body(Current).ParentIndex != kNoBody; Current = Body(Current).ParentIndex)
			{
				++D;
			}
			return D;
		};

		// Climb both chains to their common ancestor; body side adds, frame side subtracts.
		StateVector Result;
		int A = BodyIndex;
		int B = FrameBodyIndex;
		int DepthA = Depth(A);
		int DepthB = Depth(B);
		auto ClimbA = [&]()
		{
			const StateVector S = StateRelativeToParent(A, UniversalTime);
			Result.Position += S.Position;
			Result.Velocity += S.Velocity;
			A = Body(A).ParentIndex;
			--DepthA;
		};
		auto ClimbB = [&]()
		{
			const StateVector S = StateRelativeToParent(B, UniversalTime);
			Result.Position -= S.Position;
			Result.Velocity -= S.Velocity;
			B = Body(B).ParentIndex;
			--DepthB;
		};
		while (DepthA > DepthB)
		{
			ClimbA();
		}
		while (DepthB > DepthA)
		{
			ClimbB();
		}
		while (A != B)
		{
			ClimbA();
			ClimbB();
		}
		return Result;
	}
}
