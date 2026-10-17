// Written by tools/dragonbuild.py poses - do not edit: the glider dragon's solved poses (tools/dragonbuild.py).
#pragma once
namespace DragonPoses
{
	struct BoneQuat { const char* bone; float w, x, y, z; };
	// the wings folded tight for a dive (local rotations, on top of the rest pose)
	inline constexpr BoneQuat kDive[] = {
		{ "DG_WingL1", 0.942357f, -0.148951f, 0.126766f, 0.271490f },
		{ "DG_WingL2", 0.936181f, -0.011028f, 0.107851f, -0.334382f },
		{ "DG_WingLF11", 0.526137f, -0.279715f, -0.377139f, 0.709017f },
		{ "DG_WingLF21", 0.627987f, -0.161839f, -0.376366f, 0.661656f },
		{ "DG_WingLF31", 0.834086f, 0.025690f, -0.272150f, 0.479139f },
		{ "DG_WingLF41", 0.985033f, 0.095461f, -0.057758f, 0.131379f },
		{ "DG_WingR1", 0.942357f, -0.148951f, -0.126766f, -0.271490f },
		{ "DG_WingR2", 0.936181f, -0.011028f, -0.107851f, 0.334382f },
		{ "DG_WingRF11", 0.526137f, -0.279715f, 0.377139f, -0.709017f },
		{ "DG_WingRF21", 0.627987f, -0.161839f, 0.376366f, -0.661656f },
		{ "DG_WingRF31", 0.834086f, 0.025690f, 0.272150f, -0.479139f },
		{ "DG_WingRF41", 0.985033f, 0.095461f, 0.057758f, -0.131379f },
	};
	struct Axis { const char* bone; float x, y, z; float amount; };
	// each talon joint: the axis it curls about (its parent's space = the dragon's), and how far of a full curl
	inline constexpr Axis kToes[] = {
		{ "DG_ToeL1", 0.63121f, 0.77561f, -0.00000f, 0.95f },
		{ "DG_ToeL2", 0.63121f, 0.77561f, -0.00000f, 1.25f },
		{ "DG_ToeL3", 0.90889f, 0.41703f, -0.00000f, 0.95f },
		{ "DG_ToeL4", 0.90889f, 0.41703f, -0.00000f, 1.25f },
		{ "DG_ToeL5", 0.99478f, -0.10208f, 0.00000f, 0.95f },
		{ "DG_ToeL6", 0.99478f, -0.10208f, 0.00000f, 1.25f },
		{ "DG_ToeR1", 0.63433f, -0.77307f, 0.00000f, 0.95f },
		{ "DG_ToeR2", 0.63433f, -0.77307f, 0.00000f, 1.25f },
		{ "DG_ToeR3", 0.90989f, -0.41485f, 0.00000f, 0.95f },
		{ "DG_ToeR4", 0.90989f, -0.41485f, 0.00000f, 1.25f },
		{ "DG_ToeR5", 0.99381f, 0.11107f, -0.00000f, 0.95f },
		{ "DG_ToeR6", 0.99381f, 0.11107f, -0.00000f, 1.25f },
	};
	inline constexpr float kLegGlide[3] = { -1.25f, 0.5f, -1.9f };  // hip, knee, foot (rad, about X)
	inline constexpr float kLegDive[3] = { -1.45f, 0.25f, -1.6f };  // hip, knee, foot (rad, about X)
	inline constexpr float kLegFlap[3] = { -0.75f, 0.7f, -1.3f };  // hip, knee, foot (rad, about X)
	inline constexpr float kToesGlide = 0.05f, kToesDive = 0.3f, kToesFlap = -0.2f;  // talon curl
}
