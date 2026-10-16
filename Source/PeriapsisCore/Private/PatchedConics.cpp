#include "PeriapsisCore/PatchedConics.h"

#include "PeriapsisCore/Constants.h"
#include "PeriapsisCore/Kepler.h"
#include "PeriapsisCore/Surface.h"

#include <algorithm>
#include <cmath>

namespace Periapsis::Core
{
	namespace
	{
		constexpr int kMaxSamplesPerPatch = 200000;

		// An event fires when its function goes from positive to <= 0.
		struct EventSource
		{
			PatchEnd Kind = PatchEnd::Horizon;
			int ChildIndex = kNoBody;
		};

		class PatchEvents
		{
		public:
			PatchEvents(const CelestialSystem& InSystem, int InBody, const StateVector& InState, double InStartTime)
				: System(InSystem), BodyIndex(InBody), Start(InState), StartTime(InStartTime)
			{
				const CelestialBody& B = System.Body(BodyIndex);
				const OrbitalElements El = StateToElements(Start, B.Mu, StartTime);
				if (std::isfinite(B.SphereOfInfluence))
				{
					Sources.push_back({PatchEnd::SoiExit, kNoBody});
				}
				if (B.Radius > 0.0 && PeriapsisRadius(El) < MaxSurfaceRadius(B))
				{
					Sources.push_back({PatchEnd::Impact, kNoBody});
					bCanHitGround = true;
				}
				for (const int Child : System.Children(BodyIndex))
				{
					Sources.push_back({PatchEnd::SoiEntry, Child});
				}
			}

			size_t Count() const { return Sources.size(); }
			bool CanHitGround() const { return bCanHitGround; }

			// Longest time step that cannot skip over the ground: the ship cannot reach the top
			// of the relief, nor the ground itself (whose slope is bounded), any sooner.
			double SafeStepAboveGround(double Time) const
			{
				const CelestialBody& B = System.Body(BodyIndex);
				const StateVector S = ShipAt(Time);
				const double Speed = std::max(Norm(S.Velocity), 1e-9);
				const double AboveRelief = Norm(S.Position) - MaxSurfaceRadius(B);
				const double AboveGround = AltitudeAboveTerrain(B, S.Position, Time);
				const double Slope = B.Terrain.MaxSlope();
				return std::max(AboveRelief, AboveGround / std::sqrt(1.0 + Slope * Slope)) / Speed;
			}
			const EventSource& Source(size_t Index) const { return Sources[Index]; }

			StateVector ShipAt(double Time) const
			{
				return PropagateKepler(Start, System.Body(BodyIndex).Mu, Time - StartTime);
			}

			double Evaluate(size_t Index, double Time) const
			{
				const CelestialBody& B = System.Body(BodyIndex);
				const Vec3 Ship = ShipAt(Time).Position;
				const EventSource& Src = Sources[Index];
				switch (Src.Kind)
				{
				case PatchEnd::SoiExit:
					return B.SphereOfInfluence - Norm(Ship);
				case PatchEnd::Impact:
					return AltitudeAboveTerrain(B, Ship, Time);
				case PatchEnd::SoiEntry:
				{
					const Vec3 Child = System.StateRelativeToParent(Src.ChildIndex, Time).Position;
					return Norm(Ship - Child) - System.Body(Src.ChildIndex).SphereOfInfluence;
				}
				case PatchEnd::Horizon:
					break;
				}
				return 1.0;
			}

			// Lo has a positive value and Hi a non-positive one; returns the first time
			// found on the non-positive side, so the next patch starts inside the event.
			double Bisect(size_t Index, double Lo, double Hi, double Tolerance) const
			{
				for (int Iteration = 0; Iteration < 200 && Hi - Lo > Tolerance; ++Iteration)
				{
					const double Mid = 0.5 * (Lo + Hi);
					if (Evaluate(Index, Mid) > 0.0)
					{
						Lo = Mid;
					}
					else
					{
						Hi = Mid;
					}
				}
				return Hi;
			}

			// Minimum of the event function inside [A, B], for close approaches that dip
			// below zero between two samples that are both positive.
			double GoldenMinimum(size_t Index, double A, double B, double Tolerance, double& OutValue) const
			{
				constexpr double InvPhi = 0.6180339887498949;
				double C = B - (B - A) * InvPhi;
				double D = A + (B - A) * InvPhi;
				double FC = Evaluate(Index, C);
				double FD = Evaluate(Index, D);
				for (int Iteration = 0; Iteration < 200 && B - A > Tolerance; ++Iteration)
				{
					if (FC < FD)
					{
						B = D;
						D = C;
						FD = FC;
						C = B - (B - A) * InvPhi;
						FC = Evaluate(Index, C);
					}
					else
					{
						A = C;
						C = D;
						FC = FD;
						D = A + (B - A) * InvPhi;
						FD = Evaluate(Index, D);
					}
				}
				if (FC < FD)
				{
					OutValue = FC;
					return C;
				}
				OutValue = FD;
				return D;
			}

		private:
			const CelestialSystem& System;
			int BodyIndex;
			StateVector Start;
			double StartTime;
			std::vector<EventSource> Sources;
			bool bCanHitGround = false;
		};
	}

	std::vector<TrajectoryPatch> PredictTrajectory(const CelestialSystem& System, int BodyIndex,
		const StateVector& State, double StartTime, const PredictionSettings& Settings)
	{
		std::vector<TrajectoryPatch> Patches;
		const double AbsoluteEnd = StartTime + Settings.Horizon;

		int CurrentBody = BodyIndex;
		StateVector CurrentState = State;
		double PatchStart = StartTime;

		while (static_cast<int>(Patches.size()) < Settings.MaxPatches && PatchStart < AbsoluteEnd)
		{
			const CelestialBody& B = System.Body(CurrentBody);
			const double Mu = B.Mu;

			TrajectoryPatch Patch;
			Patch.BodyIndex = CurrentBody;
			Patch.StartTime = PatchStart;
			Patch.StartState = CurrentState;
			Patch.Elements = StateToElements(CurrentState, Mu, PatchStart);

			const bool bClosed = SpecificEnergy(CurrentState, Mu) < 0.0;
			const double Period = OrbitalPeriod(Patch.Elements, Mu);

			// Around the root body (no SOI to leave), no event can happen while the ship is
			// beyond every child's reach and the surface. Out there the search takes long
			// steps: sampling evenly cost a second per prediction on escape or near-escape
			// orbits, and the game predicts every frame while the engine burns.
			const bool bRoot = !std::isfinite(B.SphereOfInfluence);
			double Reach = MaxSurfaceRadius(B);
			for (const int Child : System.Children(CurrentBody))
			{
				const CelestialBody& C = System.Body(Child);
				Reach = std::max(Reach, ApoapsisRadius(C.Orbit) + C.SphereOfInfluence);
			}
			// Speed bound beyond the reach, from energy conservation.
			const double MaxSpeedBeyondReach = std::sqrt(std::max(2.0 * SpecificEnergy(CurrentState, Mu) + 2.0 * Mu / Reach, 0.0));

			// Search window: a few revolutions if closed, up to the SOI if open.
			double WindowEnd = AbsoluteEnd;
			// Events are only searched up to here; past it, none can happen on this conic.
			double SearchEnd = AbsoluteEnd;
			if (bClosed)
			{
				WindowEnd = std::min(WindowEnd, PatchStart + Settings.OrbitsPerPatch * Period);
				SearchEnd = WindowEnd;
			}
			else if (bRoot)
			{
				WindowEnd = std::min(WindowEnd, PatchStart + Settings.OpenOrbitSpan);
				// Escaping: once the ship flies outward beyond the reach, it never comes back.
				const auto IsPastReach = [&](const StateVector& S)
				{
					return Norm(S.Position) > Reach && Dot(S.Position, S.Velocity) > 0.0;
				};
				double Probe = 0.0;
				if (!IsPastReach(CurrentState))
				{
					Probe = std::max(Norm(CurrentState.Position) / Norm(CurrentState.Velocity), 1.0);
					for (int Doubling = 0; Doubling < 200 && !IsPastReach(PropagateKepler(CurrentState, Mu, Probe)); ++Doubling)
					{
						Probe *= 2.0;
					}
				}
				SearchEnd = std::min(WindowEnd, PatchStart + Probe);
			}
			else
			{
				// Open orbit: find a time already past the SOI by doubling.
				double Probe = std::max(Norm(CurrentState.Position) / Norm(CurrentState.Velocity), 1.0);
				for (int Doubling = 0; Doubling < 200; ++Doubling)
				{
					if (Norm(PropagateKepler(CurrentState, Mu, Probe).Position) >= B.SphereOfInfluence)
					{
						break;
					}
					Probe *= 2.0;
				}
				WindowEnd = std::min(WindowEnd, PatchStart + Probe);
				SearchEnd = WindowEnd;
			}

			const double Span = SearchEnd - PatchStart;
			const int Samples = std::max(Settings.SamplesPerOrbit, 8);
			double Step = bClosed ? Period / Samples : Span / (4.0 * Samples);
			for (const int Child : System.Children(CurrentBody))
			{
				Step = std::min(Step, OrbitalPeriod(System.Body(Child).Orbit, Mu) / Samples);
			}
			Step = std::max(Step, Span / kMaxSamplesPerPatch);

			const PatchEvents Events(System, CurrentBody, CurrentState, PatchStart);
			const size_t EventCount = Events.Count();

			// An event only arms once its function has been seen positive: a patch that
			// starts exactly on an SOI boundary must not fire on that same boundary.
			std::vector<bool> Armed(EventCount, false);
			std::vector<double> Prev2(EventCount, 0.0);
			std::vector<double> Prev1(EventCount, 0.0);
			std::vector<double> Now(EventCount, 0.0);
			for (size_t J = 0; J < EventCount; ++J)
			{
				Prev1[J] = Events.Evaluate(J, PatchStart);
				Armed[J] = Prev1[J] > 0.0;
			}
			double TimePrev2 = PatchStart;
			double TimePrev1 = PatchStart;

			double EventTime = WindowEnd;
			int EventIndex = -1;

			for (int K = 1; K <= kMaxSamplesPerPatch + 1; ++K)
			{
				double Advance = Step;
				if (bRoot && MaxSpeedBeyondReach > 0.0)
				{
					const double Radius = Norm(Events.ShipAt(TimePrev1).Position);
					Advance = std::max(Advance, (Radius - Reach) / MaxSpeedBeyondReach);
				}
				if (Events.CanHitGround())
				{
					// Near the relief, small enough steps not to fly through a mountain between
					// two samples; never so small that a grazing pass stalls the search.
					Advance = std::min(Advance, std::max(Events.SafeStepAboveGround(TimePrev1), Settings.TimeTolerance * 10.0));
				}
				const double Time = std::min(TimePrev1 + Advance, SearchEnd);
				for (size_t J = 0; J < EventCount; ++J)
				{
					Now[J] = Events.Evaluate(J, Time);
					if (!Armed[J])
					{
						Armed[J] = Now[J] > 0.0;
						continue;
					}

					double Candidate = EventTime + 1.0;
					if (Now[J] <= 0.0)
					{
						Candidate = Events.Bisect(J, TimePrev1, Time, Settings.TimeTolerance);
					}
					else if (K >= 2 && Prev2[J] > 0.0 && Prev1[J] > 0.0 && Prev1[J] < Prev2[J] && Prev1[J] < Now[J])
					{
						double MinValue = 0.0;
						const double MinTime = Events.GoldenMinimum(J, TimePrev2, Time, Settings.TimeTolerance, MinValue);
						if (MinValue <= 0.0)
						{
							Candidate = Events.Bisect(J, TimePrev2, MinTime, Settings.TimeTolerance);
						}
					}
					if (Candidate <= EventTime)
					{
						EventTime = Candidate;
						EventIndex = static_cast<int>(J);
					}
				}
				if (EventIndex >= 0 || Time >= SearchEnd)
				{
					break;
				}
				Prev2 = Prev1;
				Prev1 = Now;
				TimePrev2 = TimePrev1;
				TimePrev1 = Time;
			}

			Patch.EndTime = EventTime;
			Patch.EndState = Events.ShipAt(EventTime);

			if (EventIndex < 0)
			{
				Patch.EndReason = PatchEnd::Horizon;
				Patches.push_back(Patch);
				break;
			}

			const EventSource& Src = Events.Source(static_cast<size_t>(EventIndex));
			Patch.EndReason = Src.Kind;
			if (Src.Kind == PatchEnd::Impact)
			{
				Patches.push_back(Patch);
				break;
			}

			if (Src.Kind == PatchEnd::SoiExit)
			{
				const StateVector BodyState = System.StateRelativeToParent(CurrentBody, EventTime);
				CurrentState = {Patch.EndState.Position + BodyState.Position, Patch.EndState.Velocity + BodyState.Velocity};
				CurrentBody = B.ParentIndex;
			}
			else
			{
				const StateVector ChildState = System.StateRelativeToParent(Src.ChildIndex, EventTime);
				CurrentState = {Patch.EndState.Position - ChildState.Position, Patch.EndState.Velocity - ChildState.Velocity};
				CurrentBody = Src.ChildIndex;
			}
			Patch.NextBodyIndex = CurrentBody;
			Patches.push_back(Patch);
			PatchStart = EventTime;
		}
		return Patches;
	}
}
