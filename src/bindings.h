#pragma once

#include <pybind11/pybind11.h>

namespace FovePython
{
namespace py = pybind11;

void defstruct_Headsets(py::module&);
void defstruct_Compositor(py::module&);
void defstruct_Wrappers(py::module&);

// structures
void defenum_ClientCapabilities(py::module&);
void defenum_ErrorCode(py::module&);
void defenum_CompositorLayerType(py::module&);
void defenum_ObjectGroup(py::module&);

void defstruct_Versions(py::module&);
void defstruct_LicenseInfo(py::module&);
void defstruct_HeadsetHardwareInfo(py::module&);

void defstruct_Quaternion(py::module&);
void defstruct_Vec3(py::module&);
void defstruct_Vec2(py::module&);
void defstruct_Vec2i(py::module&);
void defstruct_Ray(py::module&);
void defstruct_FrameTimestamp(py::module&);
void defstruct_Pose(py::module&);
void defenum_LogLevel(py::module&);
void defenum_Eye(py::module&);
void defenum_EyeState(py::module&);
void defstruct_Matrix44(py::module&);

void defstruct_ProjectionParams(py::module&);

void defstruct_BoundingBox(py::module&);
void defstruct_ObjectPose(py::module&);
void defenum_ColliderType(py::module&);
void defstruct_ColliderCube(py::module&);
void defstruct_ColliderSphere(py::module&);
void defstruct_ColliderMesh(py::module&);
void defstruct_ObjectCollider(py::module&);
void defstruct_GazableObject(py::module&);
void defstruct_CameraObject(py::module&);

void defenum_GraphicsAPI(py::module&);
void defenum_AlphaMode(py::module&);
void defstruct_CompositorLayerCreateInfo(py::module&);
void defstruct_CompositorLayer(py::module&);

void defstruct_CompositorTexture(py::module&);
void defstruct_DX11Texture(py::module&);
void defstruct_GLTexture(py::module&);
void defstruct_MetalTexture(py::module&);

void defstruct_TextureBounds(py::module&);
void defstruct_CompositorLayerEyeSubmitInfo(py::module&);
void defstruct_CompositorLayerSubmitInfo(py::module&);
void defstruct_AdapterId(py::module&);

void defstruct_Buffer(py::module&);
void defstruct_EyeShape(py::module&);
void defstruct_PupilShape(py::module&);
void defstruct_BitmapImage(py::module&);

void defstruct_CalibrationTarget(py::module&);
void defenum_CalibrationState(py::module&);
void defenum_CalibrationMethod(py::module&);
void defenum_EyeByEyeCalibration(py::module&);
void defenum_EyeTorsionCalibration(py::module&);
void defstruct_CalibrationData(py::module&);
void defstruct_CalibrationOptions(py::module&);
void defstruct_HmdAdjustmentData(py::module&);

void bind_CAPIs(py::module&);

} // namespace FovePython
