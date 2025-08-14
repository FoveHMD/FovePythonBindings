#define FOVE_DEFINE_CXX_API 1
#include <FoveAPI.h>
#include <pybind11/pybind11.h>

#include "bindings.h"

#pragma warning(disable : 4996)

namespace FovePython
{
namespace py = pybind11;

// This module is to be called `fove.capi` (i.e. `fove/capi.so`) within the `fove` SDK package.
PYBIND11_MODULE(capi, m)
{
	m.doc() = "Python binding to the Fove SDK API";

	defstruct_Headsets(m);
	defstruct_Compositor(m);

	// structures
	defenum_ClientCapabilities(m);
	defenum_ErrorCode(m);
	defenum_CompositorLayerType(m);
	defenum_ObjectGroup(m);

	defstruct_Versions(m);
	defstruct_LicenseInfo(m);
	defstruct_HeadsetHardwareInfo(m);

	defstruct_Quaternion(m);
	defstruct_Vec3(m);
	defstruct_Vec2(m);
	defstruct_Vec2i(m);
	defstruct_Ray(m);
	defstruct_FrameTimestamp(m);
	defstruct_Pose(m);
	defenum_LogLevel(m);
	defenum_Eye(m);
	defenum_EyeState(m);
	defstruct_Matrix44(m);

	defstruct_ProjectionParams(m);

	defstruct_BoundingBox(m);
	defstruct_ObjectPose(m);
	defenum_ColliderType(m);
	defstruct_ColliderCube(m);
	defstruct_ColliderSphere(m);
	defstruct_ColliderMesh(m);
	defstruct_ObjectCollider(m);
	defstruct_GazableObject(m);
	defstruct_CameraObject(m);

	defenum_GraphicsAPI(m);
	defenum_AlphaMode(m);
	defstruct_CompositorLayerCreateInfo(m);
	defstruct_CompositorLayer(m);

	defstruct_CompositorTexture(m);
	defstruct_DX11Texture(m);
	defstruct_GLTexture(m);
	defstruct_MetalTexture(m);

	defstruct_TextureBounds(m);
	defstruct_CompositorLayerEyeSubmitInfo(m);
	defstruct_CompositorLayerSubmitInfo(m);
	defstruct_AdapterId(m);

	defstruct_Buffer(m);

	defstruct_EyeShape(m);
	defstruct_PupilShape(m);
	defstruct_BitmapImage(m);

	defstruct_CalibrationTarget(m);
	defenum_CalibrationState(m);
	defenum_CalibrationMethod(m);
	defenum_EyeByEyeCalibration(m);
	defenum_EyeTorsionCalibration(m);
	defstruct_CalibrationData(m);
	defstruct_CalibrationOptions(m);
	defstruct_HmdAdjustmentData(m);

	defstruct_Wrappers(m);

	bind_CAPIs(m);
}

} // namespace FovePython
