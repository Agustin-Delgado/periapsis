// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/CelestialSystem.h"

#include <limits>
#include <vector>

namespace Periapsis::Core
{
	enum class PatchEnd
	{
		Horizon,  // prediction window ran out
		SoiExit,  // left the body's sphere of influence, continues around the parent
		SoiEntry, // entered a child's sphere of influence
		Impact,   // reached the body's surface
	};

	// One conic segment of a predicted trajectory.
	struct TrajectoryPatch
	{
		int BodyIndex = kNoBody;
		double StartTime = 0.0;
		double EndTime = 0.0;
		StateVector StartState; // relative to BodyIndex at StartTime
		StateVector EndState;   // relative to BodyIndex at EndTime
		OrbitalElements Elements;
		PatchEnd EndReason = PatchEnd::Horizon;
		int NextBodyIndex = kNoBody; // body of the following patch, if any
	};

	struct PredictionSettings
	{
		int MaxPatches = 5;
		// Absolute cap on the predicted time span, from the start time.
		double Horizon = std::numeric_limits<double>::infinity();
		// How many revolutions a closed patch is searched for encounters (KSP shows one).
		double OrbitsPerPatch = 1.0;
		// Time span searched on open orbits around the root body (no SOI to leave).
		double OpenOrbitSpan = 1e9;
		// Sampling density of the event search, per revolution of ship or child body.
		int SamplesPerOrbit = 256;
		// Precision of event times.
		double TimeTolerance = 1e-3;
	};

	// Chains conics through spheres of influence (patched-conic approximation).
	// Designed to run on a worker thread: it only reads the system.
	PERIAPSISCORE_API std::vector<TrajectoryPatch> PredictTrajectory(const CelestialSystem& System, int BodyIndex,
		const StateVector& State, double StartTime, const PredictionSettings& Settings = {});
}
