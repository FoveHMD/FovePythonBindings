// Note:
// - The C API provides a canonical API to which other language bindings are made
// - The C++ API is a bit special in that (currently) it is not really a binding to the C API
//   but defined by sharing codes with the C API
// - Here, we bind to the C API compiled as C++ codes; this in particular means:
//   - C++ APIs are not defined
//   - `FoveAPI.h` is pulled in with `FOVE_DEFINE_CXX_API` set to false (0)
//   - Structs exclusively uses containment than inheritance;
//     but they are compiled as C++, so that we can use member pointers
//   - Enums are C++ enum class (for symmetry and better type safety);
//     but this comes at a cost of we having have to duplicate some functions
//     that operates on some enums (`operator|` for ClientCapabilities etc.)
//   - Maybe systematically expose a fraction of CXX_API to python bindings using
//     FOVE_PYTHON_BINDINGS cpp flag?
//
// Note2: On initialization of objects
// - As a matter of principle, we prefer to have objects initialized properly
//   as soon as they get constructed.  This is certainly so when objects in concern
//   have sensible defaults; even if not (e.g. `Fove_Versions`) we prefer them
//   (as a matter of taste) to be initialized to arbitrary but predictable values,
//   with possible exceptions that doing so may incur significant performance penalties.
// - So, it would nice if we could initialize structs in `FoveAPI.h`
//   with their non-static data member initializers (NSDMI) as in the C++ API.
// - In C++11, however, we cannot aggregate initialize (Struct x = {...}) objects
//   with NSDMIs. (In C++14, we can.)
// - We do not want to force our clients to use C++14. (We do force C++11, or later)
// - So when we use NSDMI, `pybind11` ctors like
//   ```
//   py::class_<C>(..)
//     .def(py::init<Arg>(py::arg("arg0") = ..))
//   ```
//   chokes (should choke) when we compile in a strict C++11 mode.
// - There is `py::class_<C>(..).def(py::init([](Arg arg0, ..) { .. }))`, but it does not appear
//   to provide a handle for controlling keyword argument names as of yet.
// - There are two choices at this point:
//   (1) give up NSDMI on the C++ layer and (repeat and) inject default values from this file to `FoveAPI.h`
//   (2) use NSDMI and provide `py::init<>()` and `py::init([](Args..) {..})`
//   And both have pros and cons.
// - Bottom line: Choose (1) for now.
//   (Would be more python user friendly, in case we ever ended up opening up `fove.capi` to users)
// - [Remark: for those that do not have sensible defaults (e.g. `Fove_Versions`, `Fove_Matrix44`)
//    we do not really care and just provide a default constructor.]
#define FOVE_DEFINE_CXX_API 0
#define FOVE_CXX_NAMESPACE SHOULD_CAUSE_SYNTAX_ERROR_WHEN_USED !
#define FOVE_EXTERN_C extern "C"
#define FOVE_ENUM(enumName) enum class Fove_##enumName
#define FOVE_ENUM_VAL(enumName, valueName) valueName
#define FOVE_ENUM_END(enumName) ;
#define FOVE_STRUCT(structName) struct Fove_##structName
// We do not set default values upon constructions although we are in a C++ mode;
// see the comments above for a rationale.
// #define FOVE_STRUCT_VAL(memberName, defaultVal) memberName = defaultVal
#define FOVE_STRUCT_VAL(memberName, defaultVal) memberName;
#define FOVE_STRUCT_END(structName) ;
#define FOVE_STRUCT_END_NO_CXX_ALIAS(structName) ;
#include <FoveAPI.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <sstream>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <type_traits>

#include "bindings.h"

using namespace std;

// FoveAPI.h uses these names to define opaque types.
// We do not really need define these, but pybind11 requires them to be defined.
// clang-format off
struct Fove_Headset_{};
struct Fove_Compositor_{};
// clang-format on

namespace Fove
{

// Don't write logs to stdout, otherwise we will pollute other peoples programs output
bool logWriteToStdoutDefault = false;

} // namespace Fove

namespace FovePython
{
namespace py = pybind11;

namespace
{

// pybind11 does not support like C arrays and C double pointers, so we wrap
// structs that contains those in an Obj.
// Also, primitive types such as bool, int, .. are immutable, so we wrap them
// as well.
template <typename T>
struct Obj
{
	T val;
	operator T&() { return val; }
	operator T*() { return &val; }
};

// clang-format off
template <typename T>
bool operator==(const Obj<T>& a, const Obj<T>& b) { return a.val == b.val; }
template <typename T>
bool operator!=(const Obj<T>& a, const Obj<T>& b) { return !(a.val == b.val); }
// clang-format on

} // namespace

////////////////////////////////////////////////////////////////
// structures

// copied from FoveAPI.h
inline constexpr Fove_ClientCapabilities operator|(Fove_ClientCapabilities a, Fove_ClientCapabilities b) { return static_cast<Fove_ClientCapabilities>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr Fove_ClientCapabilities operator&(Fove_ClientCapabilities a, Fove_ClientCapabilities b) { return static_cast<Fove_ClientCapabilities>(static_cast<int>(a) & static_cast<int>(b)); }
inline constexpr Fove_ClientCapabilities operator~(Fove_ClientCapabilities a) { return static_cast<Fove_ClientCapabilities>(~static_cast<int>(a)); }
inline constexpr Fove_ObjectGroup operator|(Fove_ObjectGroup a, Fove_ObjectGroup b) { return static_cast<Fove_ObjectGroup>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr Fove_ObjectGroup operator&(Fove_ObjectGroup a, Fove_ObjectGroup b) { return static_cast<Fove_ObjectGroup>(static_cast<int>(a) & static_cast<int>(b)); }
inline constexpr Fove_ObjectGroup operator~(Fove_ObjectGroup a) { return static_cast<Fove_ObjectGroup>(~static_cast<int>(a)); }

namespace
{

std::ostream& operator<<(std::ostream& out, const Fove_Versions& v)
{
	out << "<Versions:"
		<< " client: " << v.clientMajor << '.' << v.clientMinor << '.' << v.clientBuild
		<< ", runtime: " << v.runtimeMajor << '.' << v.runtimeMinor << '.' << v.runtimeBuild
		<< ", protocol: " << v.clientProtocol
		<< ", min_firmware: " << v.minFirmware
		<< ", max_firmware: " << v.maxFirmware
		<< ", too_old_headset: " << v.tooOldHeadsetConnected
		<< ">";
	return out;
};
std::ostream& operator<<(std::ostream& out, const Fove_Quaternion& self)
{
	out << "<Quaternion: " << self.x << ", " << self.y << ", " << self.z << ", " << self.w << ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_Vec3& self)
{
	out << "<Vec3: " << self.x << ", " << self.y << ", " << self.z << ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_Vec2& self)
{
	out << "<Vec2: " << self.x << ", " << self.y << ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_Vec2i& self)
{
	out << "<Vec2i: " << self.x << ", " << self.y << ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_Ray& self)
{
	out << "<Ray: " << self.origin << ", " << self.direction << ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_FrameTimestamp& self)
{
	out << "<FrameTimestamp: "
		<< "id: " << self.id
		<< ", timestamp: " << self.timestamp
		<< ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_Pose& self)
{
	out << "<Pose: "
		<< "id: " << self.id
		<< ", timestamp: " << self.timestamp
		<< ", orientation: " << self.orientation
		<< ", angularVelocity: " << self.angularVelocity
		<< ", angularAcceleration: " << self.angularAcceleration
		<< ", position: " << self.position
		<< ", standingPosition: " << self.standingPosition
		<< ", velocity: " << self.velocity
		<< ", acceleration: " << self.acceleration
		<< ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_ProjectionParams& self)
{
	out << "<ProjectionParams: "
		<< "left: " << self.left
		<< ", right: " << self.right
		<< ", top: " << self.top
		<< ", bottom: " << self.bottom
		<< ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_BoundingBox& self)
{
	out << "<BoundingBox: "
		<< "center: " << self.center
		<< ", extend: " << self.extend
		<< ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_ObjectPose& self)
{
	out << "<ObjectPose: "
		<< "scale: " << self.scale
		<< ", rotation: " << self.rotation
		<< ", position: " << self.position
		<< ", velocity: " << self.velocity
		<< ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_ColliderCube& self)
{
	out << "<ColliderCube: " << self.size << ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_ColliderSphere& self)
{
	out << "<ColliderSphere: " << self.radius << ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_ColliderMesh& self)
{
	out << "<ColliderMesh: "
		<< "VextexCount: " << self.vertexCount
		<< ", TriangleCount: " << self.triangleCount
		<< ", BoundingBox: " << self.boundingBox
		<< ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_GazableObject& self)
{
	out << "<GazableObject: "
		<< "Id: " << self.id
		<< ", Pose: " << self.pose
		<< ", Group: " << std::hex << static_cast<unsigned>(self.group)
		<< ", ColliderCount: " << self.colliderCount
		<< ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_CameraObject& self)
{
	out << "<GazableObject: "
		<< "Id: " << self.id
		<< ", Pose: " << self.pose
		<< ", GroupMask: " << std::hex << static_cast<unsigned>(self.groupMask)
		<< ">";
	return out;
}
std::ostream& operator<<(std::ostream& out, const Fove_ObjectCollider& self)
{
	out << "<ObjectCollider: "
		<< "Center: " << self.center;
		// << ", ShapeType: " << Fove::stringForEnum(self.shapeType);
	switch (self.shapeType)
	{
		case Fove_ColliderType::Cube:
			out << ", size: " << self.shapeDefinition.cube.size;
			break;
		case Fove_ColliderType::Sphere:
			out << ", radius: " << self.shapeDefinition.sphere.radius;
			break;
		case Fove_ColliderType::Mesh:
			out << ", VertexCount: " << self.shapeDefinition.mesh.vertexCount;
			out << ", TriangleCount: " << self.shapeDefinition.mesh.triangleCount;
			out << ", BoundingBox: " << self.shapeDefinition.mesh.boundingBox;
			break;
	}
	out << ">";

	return out;
}

template <typename T>
std::string repr(const T& self)
{
	std::ostringstream ss;
	ss << self;
	return ss.str();
}

// pybind11 uses __repr__ for generating type signature, and this is done at the declaration time.
// So to make things work, one has to forward declare classes as in
// ```
// auto pyFoo = py::class_<Foo>(m, "Foo");
// auto pyBar = py::class_<Bar>(m, "Bar");
//
// pyFoo.def(py::init<const Bar&>());
// pyBar.def(py::init<const Foo&>());
//
// ```
// and define __repr__ to generate a string that python understands.
// Alternatively, we could use `py::arg_v(..)` construct (for now we go this route).
// refs:
// - https://pybind11.readthedocs.io/en/latest/advanced/misc.html#avoiding-cpp-types-in-docstrings
// - https://pybind11.readthedocs.io/en/latest/advanced/functions.html#default-arguments-revisited
//
// template <>
// std::string repr<Fove_Vec3>(const Fove_Vec3& self)
// {
// 	std::ostringstream ss;
// 	ss << "fove.capi.Vec3(0,0,0)";
// 	return ss.str();
// }

} // namespace

// XXX docstring annotation on `py::enum_` `.values` will be supported by pybind11 v2.3.0 on,
// (which is not released as of now).
void defenum_ClientCapabilities(py::module& m)
{
	py::enum_<Fove_ClientCapabilities>(m, "ClientCapabilities",
									   R"(List of capabilities usable by clients

Most features require registering for the relevant capability.
If a client queries data related to a capability it has not registered API_NotRegistered will be returned.
After a new capability registration the Data_NoUpdate error may be returned for a few frames while
the service is bootstrapping the new capability.

This enum is designed to be used as a flag set, so items may be binary logic operators like |.

The FOVE runtime will keep any given set of hardware/software running so long as one client is registering a capability.

The registration of a capability does not necessarily mean that the capability is running.
For example, if no position tracking camera is attached, no position tracking will occur regardless of how many clients registered for it.

- `None_`:  No capabilities requested
- `OrientationTracking`:  Enables headset orientation tracking
- `PositionTracking`:  Enables headset position tracking
- `PositionImage`:  Enables Position camera image transfer from the runtime service to the client
- `EyeTracking`:  Enables headset eye tracking
- `GazeDepth`:  Enables gaze depth computation
- `UserPresence`:  Enables user presence detection
- `UserAttentionShift`:  Enables user attention shift computation
- `UserIOD`:  Enables the calculation of the user IOD
- `UserIPD`:  Enables the calculation of the user IPD
- `EyeTorsion`:  Enables the calculation of the user eye torsion
- `EyeShape`:  Enables the detection of the eyes shape
- `EyesImage`:  Enables Eye camera image transfer from the runtime service to the client
- `EyeballRadius`:  Enables the calculation of the user eyeball radius
- `IrisRadius`:  Enables the calculation of the user iris radius
- `PupilRadius`:  Enables the calculation of the user pupil radius
- `GazedObjectDetection`:  Enables gazed object detection based on registered gazable objects
- `DirectScreenAccess`:  Give you direct access to the HMD screen and disable the Fove compositor
- `PupilShape`:  Enables the detection of the pupil shape
- `EyeBlink`:  Enables eye blink detection and counting
)")
		// XXX symbol None is reserved in python
		.value("None_", Fove_ClientCapabilities::None)
		.value("OrientationTracking", Fove_ClientCapabilities::OrientationTracking)
		.value("PositionTracking", Fove_ClientCapabilities::PositionTracking)
		.value("PositionImage", Fove_ClientCapabilities::PositionImage)
		.value("EyeTracking", Fove_ClientCapabilities::EyeTracking)
		.value("GazeDepth", Fove_ClientCapabilities::GazeDepth)
		.value("UserPresence", Fove_ClientCapabilities::UserPresence)
		.value("UserAttentionShift", Fove_ClientCapabilities::UserAttentionShift)
		.value("UserIOD", Fove_ClientCapabilities::UserIOD)
		.value("UserIPD", Fove_ClientCapabilities::UserIPD)
		.value("EyeTorsion", Fove_ClientCapabilities::EyeTorsion)
		.value("EyeShape", Fove_ClientCapabilities::EyeShape)
		.value("EyesImage", Fove_ClientCapabilities::EyesImage)
		.value("EyeballRadius", Fove_ClientCapabilities::EyeballRadius)
		.value("IrisRadius", Fove_ClientCapabilities::IrisRadius)
		.value("PupilRadius", Fove_ClientCapabilities::PupilRadius)
		.value("GazedObjectDetection", Fove_ClientCapabilities::GazedObjectDetection)
		.value("DirectScreenAccess", Fove_ClientCapabilities::DirectScreenAccess)
		.value("PupilShape", Fove_ClientCapabilities::PupilShape)
		.value("EyeBlink", Fove_ClientCapabilities::EyeBlink)
		.def("__eq__", [](const Fove_ClientCapabilities cap1, const Fove_ClientCapabilities cap2) -> bool {
			return cap1 == cap2;
		},
			 py::is_operator(), "Returns True if two capabilities `cap1` and `cap2` are the same.")
		.def("__bool__", [](const Fove_ClientCapabilities cap) -> bool {
			return cap != Fove_ClientCapabilities::None;
		},
			 py::is_operator(), "Returns True if a capability `cap`is not Empty")
		.def("__and__", [](const Fove_ClientCapabilities cap1, const Fove_ClientCapabilities cap2) {
			return cap1 & cap2;
		},
			 py::is_operator(), "Returns the intersection of two capabilities `cap1` and `cap2`.")
		.def("__or__", [](const Fove_ClientCapabilities cap1, const Fove_ClientCapabilities cap2) {
			return cap1 | cap2;
		},
			 py::is_operator(), "Returns the union of two capabilities `cap1` and `cap2`.")
		.def("__add__", [](const Fove_ClientCapabilities cap1, const Fove_ClientCapabilities cap2) {
			return cap1 | cap2;
		},
			 py::is_operator(), "Returns the union of two capabilities `cap1` and `cap2`.")
		.def("__sub__", [](const Fove_ClientCapabilities cap1, const Fove_ClientCapabilities cap2) {
			return cap1 & ~cap2;
		},
			 py::is_operator(), "Returns the capability `cap2` but with `cap1` removed.")
		.def("__contains__", [](const Fove_ClientCapabilities cap1, const Fove_ClientCapabilities cap2) -> bool {
			return (cap1 & cap2) == cap2; // cap1 contains cap2, note the order
		},
			 py::is_operator(), "Returns `True` if `cap2 in cap1`.");
}

void defenum_ErrorCode(py::module& m)
{
	py::enum_<Fove_ErrorCode>(m, "ErrorCode", R"(The error codes that the Fove system may return
- `None`:  Indicates that no error occurred

// Connection Errors
- `Connect_NotConnected`:  The client lost the connection with the Fove service
- `Connect_RuntimeVersionTooOld`:  The FOVE runtime version is too old for this client
- `Connect_ClientVersionTooOld`:  The client version is too old for the installed runtime

// API usage errors
- `API_InvalidArgument`:  An argument passed to an API function was invalid for a reason other than one of the below reasons
- `API_NotRegistered`:  Data was queried without first registering for that data
- `API_NullInPointer`:  An input argument passed to an API function was invalid for a reason other than the below reasons
- `API_InvalidEnumValue`:  An enum argument passed to an API function was invalid
- `API_NullOutPointersOnly`:  All output arguments were null on a function that requires at least one output (all getters that have no side effects)
- `API_OverlappingOutPointers`:  Two (or more) output parameters passed to an API function overlap in memory. Each output parameter should be a unique, separate object
- `API_MissingArgument`:  The service was expecting extra arguments that the client didn't provide
- `API_Timeout`:  A call to an API could not be completed within a timeout

// Data Errors
- `Data_Unreadable`:  The data couldn't be read properly from the shared memory and may be corrupted
- `Data_NoUpdate`:  The data has not been updated by the system yet and is invalid
- `Data_Uncalibrated`:  The data is invalid because the feature in question is not calibrated
- `Data_Unreliable`:  The data is unreliable because the eye tracking has been lost
- `Data_LowAccuracy`:  The accuracy of the data is low

// Hardware Errors
- `Hardware_Disconnected`:  The hardware has been physically disconnected
- `Hardware_WrongFirmwareVersion`:  A wrong version of hardware firmware has been detected

// Code and placeholders
- `Code_NotImplementedYet`:  The function hasn't been implemented yet
- `Code_FunctionDeprecated`:  The function has been deprecated

// Position Tracking
- `Position_ObjectNotTracked`:  The object is inactive or currently not tracked

// Compositor
- `Compositor_NotSwapped`:  This comes from submitting without calling WaitForRenderPose after a complete submit
- `Compositor_UnableToCreateDeviceAndContext`:  Compositor was unable to initialize its backend component
- `Compositor_UnableToUseTexture`:  Compositor was unable to use the given texture (likely due to mismatched client and data types or an incompatible format)
- `Compositor_DeviceMismatch`:  Compositor was unable to match its device to the texture's, either because of multiple GPUs or a failure to get the device from the texture
- `Compositor_DisconnectedFromRuntime`:  Compositor was running and is no longer responding
- `Compositor_ErrorCreatingTexturesOnDevice`:  Failed to create shared textures for compositor
- `Compositor_NoEyeSpecifiedForSubmit`:  The supplied Fove_Eye for submit is invalid (i.e. is Both or Neither)

// Generic
- `UnknownError`:  Errors that are unknown or couldn't be classified. If possible, info will be logged about the nature of the issue

// Objects
- `Object_AlreadyRegistered`:  The scene object that you attempted to register was already present in the object registry

// Render
- `Render_OtherRendererPrioritized`:  Another renderer registered to render the process have a higher priority than current client

// License
- `License_FeatureAccessDenied`:  You don't have the license rights to use the corresponding feature

// Profiles
- `Profile_DoesntExist`:  The profile doesn't exist
- `Profile_NotAvailable`:  The profile already exists when it shouldn't, or is otherwise taken or not available
- `Profile_InvalidName`:  The profile name is not a valid name

// Config
- `Config_DoesntExist`:  The provided key doesn't exist in the config
- `Config_TypeMismatch`:  The value type of the key doesn't match

// System Errors, errors that originate from the OS level API (files, sockets, etc)
- `System_UnknownError`: Any system error not otherwise specified
- `System_PathNotFound`: Unix: ENOENT, Windows: ERROR_PATH_NOT_FOUND or ERROR_FILE_NOT_FOUND
- `System_AccessDenied`: Unix: EACCES, Windows: ERROR_ACCESS_DENIED
)")
		.value("None_", Fove_ErrorCode::None) // = 0
		// Connection errors
		.value("Connect_NotConnected", Fove_ErrorCode::Connect_NotConnected)
		.value("Connect_RuntimeVersionTooOld", Fove_ErrorCode::Connect_RuntimeVersionTooOld)
		.value("Connect_ClientVersionTooOld", Fove_ErrorCode::Connect_ClientVersionTooOld)

		// API usage errors
		.value("API_InvalidArgument", Fove_ErrorCode::API_InvalidArgument)
		.value("API_NotRegistered", Fove_ErrorCode::API_NotRegistered)
		.value("API_NullInPointer", Fove_ErrorCode::API_NullInPointer)
		.value("API_InvalidEnumValue", Fove_ErrorCode::API_InvalidEnumValue)
		.value("API_NullOutPointersOnly", Fove_ErrorCode::API_NullOutPointersOnly)
		.value("API_OverlappingOutPointers", Fove_ErrorCode::API_OverlappingOutPointers)
		.value("API_MissingArgument", Fove_ErrorCode::API_MissingArgument)
		.value("API_Timeout", Fove_ErrorCode::API_Timeout)
		.value("API_AlreadyInTheDesiredState", Fove_ErrorCode::API_AlreadyInTheDesiredState)

		// Data errors
		.value("Data_Unreadable", Fove_ErrorCode::Data_Unreadable)
		.value("Data_NoUpdate", Fove_ErrorCode::Data_NoUpdate)
		.value("Data_Uncalibrated", Fove_ErrorCode::Data_Uncalibrated)
		.value("Data_Unreliable", Fove_ErrorCode::Data_Unreliable)
		.value("Data_LowAccuracy", Fove_ErrorCode::Data_LowAccuracy)

		// Hardware Errors
		.value("Hardware_Disconnected", Fove_ErrorCode::Hardware_Disconnected)
		.value("Hardware_WrongFirmwareVersion", Fove_ErrorCode::Hardware_WrongFirmwareVersion)

		// Code and placeholders
		.value("Code_NotImplementedYet", Fove_ErrorCode::Code_NotImplementedYet)
		.value("Code_FunctionDeprecated", Fove_ErrorCode::Code_FunctionDeprecated)

		// Position Tracking
		.value("Position_ObjectNotTracked", Fove_ErrorCode::Position_ObjectNotTracked)

		// Compositor
		.value("Compositor_NotSwapped", Fove_ErrorCode::Compositor_NotSwapped)
		.value("Compositor_UnableToCreateDeviceAndContext", Fove_ErrorCode::Compositor_UnableToCreateDeviceAndContext)
		.value("Compositor_UnableToUseTexture", Fove_ErrorCode::Compositor_UnableToUseTexture)
		.value("Compositor_DeviceMismatch", Fove_ErrorCode::Compositor_DeviceMismatch)
		.value("Compositor_DisconnectedFromRuntime", Fove_ErrorCode::Compositor_DisconnectedFromRuntime)
		.value("Compositor_ErrorCreatingTexturesOnDevice", Fove_ErrorCode::Compositor_ErrorCreatingTexturesOnDevice)
		.value("Compositor_NoEyeSpecifiedForSubmit", Fove_ErrorCode::Compositor_NoEyeSpecifiedForSubmit)

		// Generic
		.value("UnknownError", Fove_ErrorCode::UnknownError) // = 9000

		// Objects
		.value("Object_AlreadyRegistered", Fove_ErrorCode::Object_AlreadyRegistered)

		// Render
		.value("Render_OtherRendererPrioritized", Fove_ErrorCode::Render_OtherRendererPrioritized)

		// License
		.value("License_FeatureAccessDenied", Fove_ErrorCode::License_FeatureAccessDenied)

		// Profiles
		.value("Profile_DoesntExist", Fove_ErrorCode::Profile_DoesntExist)
		.value("Profile_NotAvailable", Fove_ErrorCode::Profile_NotAvailable)
		.value("Profile_InvalidName", Fove_ErrorCode::Profile_InvalidName)

		// Config
		.value("Config_DoesntExist", Fove_ErrorCode::Config_DoesntExist)
		.value("Config_TypeMismatch", Fove_ErrorCode::Config_TypeMismatch)

		// System Errors, errors that originate from the OS level API (files, sockets, etc)
		.value("System_UnknownError", Fove_ErrorCode::System_UnknownError)
		.value("System_PathNotFound", Fove_ErrorCode::System_PathNotFound)
		.value("System_AccessDenied", Fove_ErrorCode::System_AccessDenied);
}

void defenum_CompositorLayerType(py::module& m)
{
	py::enum_<Fove_CompositorLayerType>(m, "CompositorLayerType", R"(Compositor layer type, which defines the order that clients are composited

- Base: The first and main application layer
- Overlay: Layer over the base
- Diagnostic: Layer over Overlay)")
		.value("Base", Fove_CompositorLayerType::Base)
		.value("Overlay", Fove_CompositorLayerType::Overlay)
		.value("Diagnostic", Fove_CompositorLayerType::Diagnostic);
}

void defenum_ObjectGroup(py::module& m)
{
	py::enum_<Fove_ObjectGroup>(m, "ObjectGroup", R"(The groups of objects of the scene)")
		.value("Group0", Fove_ObjectGroup::Group0)
		.value("Group1", Fove_ObjectGroup::Group1)
		.value("Group2", Fove_ObjectGroup::Group2)
		.value("Group3", Fove_ObjectGroup::Group3)
		.value("Group4", Fove_ObjectGroup::Group4)
		.value("Group5", Fove_ObjectGroup::Group5)
		.value("Group6", Fove_ObjectGroup::Group6)
		.value("Group7", Fove_ObjectGroup::Group7)
		.value("Group8", Fove_ObjectGroup::Group8)
		.value("Group9", Fove_ObjectGroup::Group9)
		.value("Group10", Fove_ObjectGroup::Group10)
		.value("Group11", Fove_ObjectGroup::Group11)
		.value("Group12", Fove_ObjectGroup::Group12)
		.value("Group13", Fove_ObjectGroup::Group13)
		.value("Group14", Fove_ObjectGroup::Group14)
		.value("Group15", Fove_ObjectGroup::Group15)
		.value("Group16", Fove_ObjectGroup::Group16)
		.value("Group17", Fove_ObjectGroup::Group17)
		.value("Group18", Fove_ObjectGroup::Group18)
		.value("Group19", Fove_ObjectGroup::Group19)
		.value("Group20", Fove_ObjectGroup::Group20)
		.value("Group21", Fove_ObjectGroup::Group21)
		.value("Group22", Fove_ObjectGroup::Group22)
		.value("Group23", Fove_ObjectGroup::Group23)
		.value("Group24", Fove_ObjectGroup::Group24)
		.value("Group25", Fove_ObjectGroup::Group25)
		.value("Group26", Fove_ObjectGroup::Group26)
		.value("Group27", Fove_ObjectGroup::Group27)
		.value("Group28", Fove_ObjectGroup::Group28)
		.value("Group29", Fove_ObjectGroup::Group29)
		.value("Group30", Fove_ObjectGroup::Group30)
		.value("Group31", Fove_ObjectGroup::Group31)
		.def(
			"__add__", [](const Fove_ObjectGroup grp1, const Fove_ObjectGroup grp2) {
				return grp1 | grp2;
			},
			py::is_operator(), "Returns the union of two groups `grp1` and `grp2`.")
		.def("__sub__", [](const Fove_ObjectGroup grp1, const Fove_ObjectGroup grp2) {
			return grp1 & ~grp2;
		},
			 py::is_operator(), "Returns the group `grp2` but with `grp1` removed.")
		.def("__contains__", [](const Fove_ObjectGroup grp1, const Fove_ObjectGroup grp2) -> bool {
			return (grp1 & grp2) == grp2;
		},
			 py::is_operator(), "Returns `True` if `grp2 in grp1`.");
}

// pybind11 does not support C arrays, so we translate Fove_Versions
// to Python_Versions in Headset_queryHardwareInfo().
namespace
{
struct Python_Versions
{
	int clientMajor = -1;
	int clientMinor = -1;
	int clientBuild = -1;
	int clientProtocol = -1;
	string clientHash;
	int runtimeMajor = -1;
	int runtimeMinor = -1;
	int runtimeBuild = -1;
	string runtimeHash;
	int firmware = -1;
	int maxFirmware = -1;
	int minFirmware = -1;
	bool tooOldHeadsetConnected = false;
};
} // namespace
void defstruct_Versions(py::module& m)
{
	py::class_<Python_Versions>(m, "Versions",
								R"(Struct to list various version info about the FOVE software

Contains the version for the software (both runtime and client versions).
A negative value in any int field represents unknown.)")
		.def(py::init<int, int, int, int, string, int, int, int, string, int, int, int, bool>(),
			 py::arg("clientMajor") = -1,
			 py::arg("clientMinor") = -1,
			 py::arg("clientBuild") = -1,
			 py::arg("clientProtocol") = -1,
			 py::arg("clientHash") = "",
			 py::arg("runtimeMajor") = -1,
			 py::arg("runtimeMinor") = -1,
			 py::arg("runtimeBuild") = -1,
			 py::arg("runtimeHash") = "",
			 py::arg("firmware") = -1,
			 py::arg("maxFirmware") = -1,
			 py::arg("minFirmware") = -1,
			 py::arg("tooOldHeadsetConnected") = false)
		.def_readwrite("clientMajor", &Python_Versions::clientMajor) // initialized to -1 in c++
		.def_readwrite("clientMinor", &Python_Versions::clientMinor)
		.def_readwrite("clientBuild", &Python_Versions::clientBuild)
		.def_readwrite("clientProtocol", &Python_Versions::clientProtocol)
		.def_readwrite("clientHash", &Python_Versions::clientHash)
		.def_readwrite("runtimeMajor", &Python_Versions::runtimeMajor)
		.def_readwrite("runtimeMinor", &Python_Versions::runtimeMinor)
		.def_readwrite("runtimeBuild", &Python_Versions::runtimeBuild)
		.def_readwrite("runtimeHash", &Python_Versions::runtimeHash)
		.def_readwrite("firmware", &Python_Versions::firmware)
		.def_readwrite("maxFirmware", &Python_Versions::maxFirmware)
		.def_readwrite("minFirmware", &Python_Versions::minFirmware)
		.def_readwrite("tooOldHeadsetConnected", &Python_Versions::tooOldHeadsetConnected) // initialized to false in c++
		.def("__repr__", repr<Fove_Versions>, "Returns a string representation of versions");
}

// pybind11 does not support C arrays, so we translate Fove_LicenseInfo to Python_LicenseInfo
namespace
{
struct Python_LicenseInfo
{
	std::string uuid;
	int expirationYear = 0;
	int expirationMonth = 0;
	int expirationDay = 0;
	std::string licenseType;
	std::string licensee;
};
} // namespace
void defstruct_LicenseInfo(py::module& m)
{
	py::class_<Python_LicenseInfo>(m, "LicenseInfo",
								   R"(Struct with details about a FOVE license)")
		.def(py::init<>())
		.def_readwrite("uuid", &Python_LicenseInfo::uuid, "128-bit uuid of this license, in binary form")
		.def_readwrite("expirationYear", &Python_LicenseInfo::expirationYear, "Expiration, year (eg. 2028), 0 if there is no expiration")
		.def_readwrite("expirationMonth", &Python_LicenseInfo::expirationMonth, "Expiration month (1 - 12), 0 if there is no expiration")
		.def_readwrite("expirationDay", &Python_LicenseInfo::expirationDay, "Expiration day (1 - 31), 0 if there is no expiration")
		.def_readwrite("licenseType", &Python_LicenseInfo::licenseType, "Null-termianted type of license, such as \"Professional\"")
		.def_readwrite("licensee", &Python_LicenseInfo::licensee, "Null-terminated name of the person or organization that this license is for, truncated as needed");
}

// pybind11 does not support C arrays, so we translate Fove_HeadsetHardwareInfo
// to Python_HeadsetHardwareInfo in Headset_queryHardwareInfo().
namespace
{
struct Python_HeadsetHardwareInfo
{
	std::string serialNumber;
	std::string manufacturer;
	std::string modelName;
};
} // namespace
void defstruct_HeadsetHardwareInfo(py::module& m)
{
	py::class_<Python_HeadsetHardwareInfo>(m, "HeadsetHardwareInfo",
										   R"(Struct Contains hardware information for the headset

Contains the serial number, manufacturer and model name for the headset.
Values of the member fields originates from their UTF-8 string representations
defined by headset manufacturers, and passed to us (FoveClient) by FoveService
server through an IPC message.
The server may be sending very long strings, but the FoveClient library will
be truncating them in an unspecified manner to 0-terminated strings of length
at most 256.)")
		.def(py::init<>())
		.def_readwrite("serialNumber", &Python_HeadsetHardwareInfo::serialNumber, "Serial number, as a null-terminated UTF8 string")
		.def_readwrite("manufacturer", &Python_HeadsetHardwareInfo::manufacturer, "Manufacturer info, as a null-terminated UTF8 string")
		.def_readwrite("modelName", &Python_HeadsetHardwareInfo::modelName, "Model name, as a null-terminated UTF8 string");
}

// XXX maybe move to upstream
template <typename Struct, typename ElemType, std::size_t Size>
constexpr void assert_layout()
{
	static_assert(std::is_standard_layout<Struct>::value,
				  "Struct not in a standard layout where one was expected.");
	static_assert(sizeof(Struct) == Size * sizeof(ElemType),
				  "Struct not an array of single elem type where one was expected.");
}

// define buffer_protocol for a C array type with element type Elem
template <typename Elem, std::size_t Size,
		  typename = typename std::enable_if<std::is_arithmetic<Elem>::value, Elem>::type>
auto define_1D_buffer_protocol(Elem (&data)[Size])
{
	return py::buffer_info{
		reinterpret_cast<void*>(&data[0]),
		sizeof(Elem),
		py::format_descriptor<Elem>::format(),
		1,             // ndims
		{Size},        // dims
		{sizeof(Elem)} // strides
	};
}

namespace
{
bool operator==(const Fove_Quaternion& self, const Fove_Quaternion& other)
{
	return self.x == other.x && self.y == other.y && self.z == other.z && self.w == other.w;
}
bool operator==(const Fove_Vec3& self, const Fove_Vec3& other)
{
	return self.x == other.x && self.y == other.y && self.z == other.z;
}
bool operator==(const Fove_Vec2& self, const Fove_Vec2& other)
{
	return self.x == other.x && self.y == other.y;
}
bool operator==(const Fove_Vec2i& self, const Fove_Vec2i& other)
{
	return self.x == other.x && self.y == other.y;
}
bool operator==(const Fove_Ray& self, const Fove_Ray& other)
{
	return self.origin == other.origin && self.direction == other.direction;
}
bool operator==(const Fove_FrameTimestamp& self, const Fove_FrameTimestamp& other)
{
	return self.id == other.id && self.timestamp == other.timestamp;
}
bool operator==(const Fove_TextureBounds& self, const Fove_TextureBounds& other)
{
	return self.left == other.left && self.top == other.top && self.right == other.right && self.bottom == other.bottom;
}
bool operator==(const Fove_BoundingBox& self, const Fove_BoundingBox& other)
{
	return self.center == other.center && self.extend == other.extend;
}
bool operator==(const Fove_ObjectPose& self, const Fove_ObjectPose& other)
{
	return self.scale == other.scale && self.rotation == other.rotation && self.position == other.position && self.velocity == other.velocity;
}
bool operator==(const Fove_CalibrationTarget& self, const Fove_CalibrationTarget& other)
{
	return self.position == other.position && self.recommendedSize == other.recommendedSize;
}

// This is a very annoying consequence of our choice of not initializing things
// on the C++ level: Chains of initializations of contained structures
// have to be done manually as containments are on the C++ level than on
// the python level. To ameliorate clutter to some extent, we provide these
// helper functions for structs that may be contained in others..
Fove_Quaternion default_Quaternion()
{
	return Fove_Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
}
Fove_Vec3 default_Vec3()
{
	return Fove_Vec3{0.0f, 0.0f, 0.0f};
}
Fove_Vec2 default_Vec2()
{
	return Fove_Vec2{0.0f, 0.0f};
}
Fove_Vec2i default_Vec2i()
{
	return Fove_Vec2i{0, 0};
}
Fove_Pose default_Pose()
{
	return Fove_Pose{0, 0, default_Quaternion(), default_Vec3(), default_Vec3(), default_Vec3(),
					 default_Vec3(), default_Vec3(), default_Vec3()};
}
Fove_TextureBounds default_TextureBounds()
{
	return Fove_TextureBounds{0.0f, 0.0f, 0.0f, 0.0f};
}
Fove_CompositorLayerEyeSubmitInfo default_CompositorLayerEyeSubmitInfo()
{
	return Fove_CompositorLayerEyeSubmitInfo{nullptr, default_TextureBounds()};
}
Fove_Buffer default_Buffer()
{
	return Fove_Buffer{nullptr, 0};
}

Fove_ObjectPose default_ObjectPose()
{
	return Fove_ObjectPose{
		Fove_Vec3{1, 1, 1},
		default_Quaternion(),
		default_Vec3(),
		default_Vec3()};
}

Fove_CalibrationTarget default_CalibrationTarget()
{
	return Fove_CalibrationTarget{{0, 0, 0}, 0};
}

} // namespace

void defstruct_Quaternion(py::module& m)
{
	assert_layout<Fove_Quaternion, float, 4>();

	py::class_<Fove_Quaternion>(m, "Quaternion", py::buffer_protocol(),
								R"(Struct representation on a quaternion

A quaternion represents an orientation in 3D space)")
		.def(py::init<float, float, float, float>(),
			 py::arg("x") = 0.0f,
			 py::arg("y") = 0.0f,
			 py::arg("z") = 0.0f,
			 py::arg("w") = 1.0f)
		.def_readwrite("x", &Fove_Quaternion::x) // 0.0
		.def_readwrite("y", &Fove_Quaternion::y) // 0.0
		.def_readwrite("z", &Fove_Quaternion::z) // 0.0
		.def_readwrite("w", &Fove_Quaternion::w) // 1.0
		.def_buffer([](Fove_Quaternion& obj) {
			using Arr4 = float(&)[4];
			return define_1D_buffer_protocol(reinterpret_cast<Arr4>(obj));
		})
		.def(
			"__eq__", [](const Fove_Quaternion& self, const Fove_Quaternion& other) {
				return self == other;
			},
			py::is_operator(), "Returns `True` if two quaternions are the same. Quaternions that differ by an overall factor are considered different.")
		.def("__ne__", [](const Fove_Quaternion& self, const Fove_Quaternion& other) {
			return !(self == other);
		},
			 py::is_operator(), "Returns `True` if two quaternions are not the same. See `__eq__()`.")
		// XXX these operations are duplicating functionalities in FoveMath.h
		.def("__mul__", [](const Fove_Quaternion& self, const Fove_Quaternion& other) {
			const auto& q1 = self;
			const auto& q2 = other;
			return Fove_Quaternion{q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
								   q1.w * q2.y + q1.y * q2.w + q1.z * q2.x - q1.x * q2.z,
								   q1.w * q2.z + q1.z * q2.w + q1.x * q2.y - q1.y * q2.x,
								   q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z};
		},
			 py::is_operator(), "Returns the product of two quaternions.")
		.def("__mul__", [](const Fove_Quaternion& self, const float other) {
			const auto& q = self;
			const auto a = other;
			return Fove_Quaternion{a * q.x, a * q.y, a * q.z, a * q.w};
		},
			 py::is_operator(), "Returns the quaternion rescaled by a scalar.")
		.def("__rmul__", [](const Fove_Quaternion& self, const float other) {
			const auto& q = self;
			const auto a = other;
			return Fove_Quaternion{a * q.x, a * q.y, a * q.z, a * q.w};
		},
			 py::is_operator(), "Returns the quaternion rescaled by a scalar.")
		.def("__neg__", [](const Fove_Quaternion& self) {
			const auto& q = self;
			return Fove_Quaternion{-q.x, -q.y, -q.z, -q.w};
		},
			 py::is_operator(), "Returns the quaternion whose components are all negated.")
		.def("normalise", [](const Fove_Quaternion& self) {
			const auto& q = self;
			const auto norm2 = self.x * self.x + self.y * self.y + self.z * self.z + self.w * self.w;
			const auto a = 1.0f / std::sqrt(norm2);
			return Fove_Quaternion{a * q.x, a * q.y, a * q.z, a * q.w};
		},
			 "Returns the quaternion of a unit norm by appropriately rescaling the given quaternion.")
		.def("conjugate", [](const Fove_Quaternion& self) {
			const auto& q = self;
			return Fove_Quaternion{-q.x, -q.y, -q.z, q.w};
		},
			 "Returns the conjugated quaternion.")
		// XXX this diverges from FoveMath.h that we normalize the inverse of original norm; probably we should not support non-normalized quaternions altogether
		.def("invert", [](const Fove_Quaternion& self) {
			const auto& q = self;
			const auto norm2 = self.x * self.x + self.y * self.y + self.z * self.z + self.w * self.w;
			const auto a = 1.0f / norm2;
			return Fove_Quaternion{-a * q.x, -a * q.y, -a * q.z, a * q.w};
		},
			 "Returns the inverse of the given quaternion.")
		.def("__repr__", repr<Fove_Quaternion>, "Returns a string representation of the quaternion.");
}

void defstruct_Vec3(py::module& m)
{
	assert_layout<Fove_Vec3, float, 3>();

	py::class_<Fove_Vec3>(m, "Vec3", py::buffer_protocol(),
						  R"(Struct to represent a 3D-vector

A vector that represents an position in 3D space.)")
		.def(py::init<float, float, float>(),
			 py::arg("x") = 0.0f,
			 py::arg("y") = 0.0f,
			 py::arg("z") = 0.0f)
		.def_readwrite("x", &Fove_Vec3::x) // 0.0
		.def_readwrite("y", &Fove_Vec3::y) // 0.0
		.def_readwrite("z", &Fove_Vec3::z) // 0.0
		.def_buffer([](Fove_Vec3& obj) {
			using Arr3 = float(&)[3];
			return define_1D_buffer_protocol(reinterpret_cast<Arr3>(obj));
		})
		.def(
			"__eq__", [](const Fove_Vec3& self, const Fove_Vec3& other) {
				return self == other;
			},
			py::is_operator(), "Returns `True` if the two vectors are the same.")
		.def("__ne__", [](Fove_Vec3& self, Fove_Vec3& other) {
			return !(self == other);
		},
			 py::is_operator(), "Returns `True` if the two vectors are not the same.")
		.def("__add__", [](Fove_Vec3& self, Fove_Vec3& other) {
			return Fove_Vec3{self.x + other.x, self.y + other.y, self.z + other.z};
		},
			 py::is_operator(), "Returns the sum of two vectors.")
		.def("__sub__", [](Fove_Vec3& self, Fove_Vec3& other) {
			return Fove_Vec3{self.x - other.x, self.y - other.y, self.z - other.z};
		},
			 py::is_operator(), "Returns the difference of two vectors.")
		.def("__mul__", [](Fove_Vec3& self, const float a) {
			return Fove_Vec3{a * self.x, a * self.y, a * self.z};
		},
			 py::is_operator(), "Returns the vector rescaled by a scalar factor.")
		.def("__rmul__", [](const float& a, Fove_Vec3& self) {
			return Fove_Vec3{a * self.x, a * self.y, a * self.z};
		},
			 py::is_operator(), "Returns the vector rescaled by a scalar factor.")
		.def("__repr__", repr<Fove_Vec3>, "Returns a string representation of the vector.");
}

void defstruct_Vec2(py::module& m)
{
	assert_layout<Fove_Vec2, float, 2>();

	py::class_<Fove_Vec2>(m, "Vec2", py::buffer_protocol(),
						  R"(Struct to represent a 2D-vector

A vector that represents a position or orientation in 2D space, such as screen or image coordinates.)")
		.def(py::init<float, float>(),
			 py::arg("x") = 0.0,
			 py::arg("y") = 0.0)
		.def_readwrite("x", &Fove_Vec2::x) // 0.0
		.def_readwrite("y", &Fove_Vec2::y) // 0.0
		.def_buffer([](Fove_Vec2& obj) {
			using Arr2 = float(&)[2];
			return define_1D_buffer_protocol(reinterpret_cast<Arr2>(obj));
		})
		.def(
			"__eq__", [](const Fove_Vec2& self, const Fove_Vec2& other) {
				return self == other;
			},
			py::is_operator(), "Returns `True` if the two vectors are the same.")
		.def("__ne__", [](Fove_Vec2& self, Fove_Vec2& other) {
			return !(self == other);
		},
			 py::is_operator(), "Returns `True` if the two vectors are not the same.")
		.def("__add__", [](Fove_Vec2& self, Fove_Vec2& other) {
			return Fove_Vec2{self.x + other.x, self.y + other.y};
		},
			 py::is_operator(), "Returns the sum of two vectors.")
		.def("__sub__", [](Fove_Vec2& self, Fove_Vec2& other) {
			return Fove_Vec2{self.x - other.x, self.y - other.y};
		},
			 py::is_operator(), "Returns the difference of two vectors.")
		.def("__mul__", [](Fove_Vec2& self, const float a) {
			return Fove_Vec2{a * self.x, a * self.y};
		},
			 py::is_operator(), "Returns the vector rescaled by a scalar factor.")
		.def("__rmul__", [](const float& a, Fove_Vec2& self) {
			return Fove_Vec2{a * self.x, a * self.y};
		},
			 py::is_operator(), "Returns the vector rescaled by a scalar factor.")
		.def("__repr__", repr<Fove_Vec2>, "Returns the string representation of the vector");
}

void defstruct_Vec2i(py::module& m)
{
	assert_layout<Fove_Vec2i, int, 2>();

	py::class_<Fove_Vec2i>(m, "Vec2i", py::buffer_protocol(),
						   R"(Struct to represent a 2D integral vector)")
		.def(py::init<int, int>(),
			 py::arg("x") = 0,
			 py::arg("y") = 0)
		.def_readwrite("x", &Fove_Vec2i::x)
		.def_readwrite("y", &Fove_Vec2i::y)
		.def_buffer([](Fove_Vec2i& obj) {
			using Arr2i = int(&)[2];
			return define_1D_buffer_protocol(reinterpret_cast<Arr2i>(obj));
		})
		.def(
			"__eq__", [](const Fove_Vec2i& self, const Fove_Vec2i& other) {
				return self == other;
			},
			py::is_operator(), "Returns `True` if the two vectors are the same.")
		.def("__ne__", [](Fove_Vec2i& self, Fove_Vec2i& other) {
			return !(self == other);
		},
			 py::is_operator(), "Returns `True` if the two vectors are the same.")
		.def("__add__", [](Fove_Vec2i& self, Fove_Vec2i& other) {
			return Fove_Vec2i{self.x + other.x, self.y + other.y};
		},
			 py::is_operator(), "Returns the sum of two vectors.")
		.def("__sub__", [](Fove_Vec2i& self, Fove_Vec2i& other) {
			return Fove_Vec2i{self.x - other.x, self.y - other.y};
		},
			 py::is_operator(), "Returns the difference of two vectors.")
		// XXX deliberately not supporting rescaling for integral vectors
		// .def("__mul__", [](Fove_Vec2i& self, const int a) {
		// 	return Fove_Vec2i{a * self.x, a * self.y};
		// },
		// 	 py::is_operator())
		// .def("__rmul__", [](const int& a, Fove_Vec2i& self) {
		// 	return Fove_Vec2i{a * self.x, a * self.y};
		// },
		// 	 py::is_operator())
		//
		.def("__repr__", repr<Fove_Vec2i>, "Returns a string representation of the vector.");
}

// TODO this should be mapped to a python type whose fields are numpy. how to do that automatically?
void defstruct_Ray(py::module& m)
{
	py::class_<Fove_Ray>(m, "Ray",
						 R"(Struct to represent a Ray

Stores the start point and direction of a Ray)")
		.def(py::init<Fove_Vec3, Fove_Vec3>(),
			 py::arg_v("origin", Fove_Vec3{0, 0, 0}, "Vec3(0, 0, 0)"),
			 py::arg_v("direction", Fove_Vec3{0, 0, 1}, "Vec3(0, 0, 1)"))
		.def_readwrite("origin", &Fove_Ray::origin, "The start point of the Ray")     // Fove_Vec3 {0,0,0}
		.def_readwrite("direction", &Fove_Ray::direction, "The direction of the Ray") // Fove_Vec3 {0,0,1}
		.def(
			"__eq__", [](const Fove_Ray& self, const Fove_Ray& other) {
				return self == other;
			},
			py::is_operator(), "Returns `True` if the two rays are the same.")
		.def("__ne__", [](Fove_Ray& self, Fove_Ray& other) {
			return !(self == other);
		},
			 py::is_operator(), "Returns `True` if the two rays are not the same.")
		.def("__repr__", repr<Fove_Ray>, "Returns a string representation of the ray.");
}

void defstruct_FrameTimestamp(py::module& m)
{
	py::class_<Fove_FrameTimestamp>(m, "FrameTimestamp",
									R"(A frame timestamp information.

It is returned by every update function so that you can know which frame the new data correspond to)")
		.def(py::init<uint64_t, uint64_t>(),
			 py::arg("id") = 0,
			 py::arg("timestamp") = 0)
		.def_readwrite("id", &Fove_FrameTimestamp::id, "Incremental frame counter")
		.def_readwrite("timestamp", &Fove_FrameTimestamp::timestamp, "The time at which the data was captured, in microseconds since an unspecified epoch")
		.def(
			"__eq__", [](const Fove_FrameTimestamp& self, const Fove_FrameTimestamp& other) {
				return self == other;
			},
			py::is_operator(), "Returns `True` if the two frame timestamps are the same.")
		.def("__ne__", [](Fove_FrameTimestamp& self, Fove_FrameTimestamp& other) {
			return !(self == other);
		},
			 py::is_operator(), "Returns `True` if the two frame timestamps are not the same.")
		.def("__repr__", repr<Fove_FrameTimestamp>, "Returns a string representation of the frame timestamps.");
}

// XXX we do not define __eq__ just yet, as the desired semantics is not so clear: should id and timestamp participate in equalities?
void defstruct_Pose(py::module& m)
{
	py::class_<Fove_Pose>(m, "Pose",
						  R"(Struct to represent a combination of position and orientation of Fove Headset

This structure is a combination of the Fove headset position and orientation in 3D space, collectively known as the "pose".
In the future this may also contain acceleration information for the headset, and may also be used for controllers.)")
		.def(py::init<uint64_t, uint64_t, Fove_Quaternion,
					  Fove_Vec3, Fove_Vec3, Fove_Vec3, Fove_Vec3, Fove_Vec3, Fove_Vec3>(),
			 py::arg("id") = 0,
			 py::arg("timestamp") = 0,
			 py::arg_v("orientation", default_Quaternion(), "Quaternion()"),
			 py::arg_v("angularVelocity", default_Vec3(), "Vec3()"),
			 py::arg_v("angularAcceleration", default_Vec3(), "Vec3()"),
			 py::arg_v("position", default_Vec3(), "Vec3()"),
			 py::arg_v("standingPosition", default_Vec3(), "Vec3()"),
			 py::arg_v("velocity", default_Vec3(), "Vec3()"),
			 py::arg_v("acceleration", default_Vec3(), "Vec3()"))
		.def_readwrite("id", &Fove_Pose::id,
					   "Incremental counter which tells if the coord captured is a fresh value at a given frame") // uint64_t 0
		.def_readwrite("timestamp", &Fove_Pose::timestamp,
					   "The time at which the pose was captured, in microseconds since an unspecified epoch") // uint64_t 0
		.def_readwrite("orientation", &Fove_Pose::orientation,
					   "The Quaternion which represents the orientation of the head") // Fove_Quaternion {}
		.def_readwrite("angularVelocity", &Fove_Pose::angularVelocity,
					   "The angular velocity of the head") // Fove_Vec3 {}
		.def_readwrite("angularAcceleration", &Fove_Pose::angularAcceleration,
					   "The angular acceleration of the head") // Fove_Vec3 {}
		.def_readwrite("position", &Fove_Pose::position,
					   "The position of headset in 3D space. Tares to (0, 0, 0). Use for sitting applications") // Fove_Vec3 {}
		.def_readwrite("standingPosition", &Fove_Pose::standingPosition,
					   "The position of headset including offset for camera location. Will not tare to zero. Use for standing applications") // Fove_Vec3 {}
		.def_readwrite("velocity", &Fove_Pose::velocity,
					   "The velocity of headset in 3D space") // Fove_Vec3 {}
		.def_readwrite("acceleration", &Fove_Pose::acceleration,
					   "The acceleration of headset in 3D space") // Fove_Vec3 {}
		.def("__repr__", repr<Fove_Pose>, "Returns a string representation of the pose.");
}

void defenum_LogLevel(py::module& m)
{
	py::enum_<Fove_LogLevel>(m, "LogLevel", "Severity level of log messages")
		.value("Debug", Fove_LogLevel::Debug)     // = 0,
		.value("Warning", Fove_LogLevel::Warning) // = 1,
		.value("Error", Fove_LogLevel::Error)     // = 2,
		;
}

void defenum_Eye(py::module& m)
{
	py::enum_<Fove_Eye>(m, "Eye", R"(Enum specifying the left or right eye
- Left: Left eye
- Right: Right eye
)")
		.value("Left", Fove_Eye::Left)
		.value("Right", Fove_Eye::Right);
}

void defenum_EyeState(py::module& m)
{
	py::enum_<Fove_EyeState>(m, "EyeState", R"(Enum specifying the state of an eye

- `NotDetected`: The eye is missing or the tracking was lost
- `Opened`: The eye is present and opened
- `Closed`: The eye is present and closed
)")
		.value("NotDetected", Fove_EyeState::NotDetected)
		.value("Opened", Fove_EyeState::Opened)
		.value("Closed", Fove_EyeState::Closed);
}

// define buffer_protocol for a C double array type with element type Elem
template <typename Elem, std::size_t Rows, std::size_t Cols,
		  typename = typename std::enable_if<std::is_arithmetic<Elem>::value, Elem>::type>
auto define_2D_buffer_protocol(Elem (&data)[Rows][Cols])
{
	return py::buffer_info{
		reinterpret_cast<void*>(&data[0][0]),
		sizeof(Elem),
		py::format_descriptor<Elem>::format(),
		2,                                  // ndims
		{Rows, Cols},                       // dims
		{sizeof(Elem) * Cols, sizeof(Elem)} // strides
	};
}

using Python_Matrix44 = Obj<Fove_Matrix44>;
void defstruct_Matrix44(py::module& m)
{
	py::class_<Python_Matrix44>(m, "Matrix44", py::buffer_protocol(),
								R"(Struct to hold a rectangular array

This struct implements buffer_protocol, and thus can be converted
to a numpy array:
m = fove.capi.Matrix44()
a = numpy.array(m, copy=False)
)")
		.def(py::init<>())
		.def_buffer([](Python_Matrix44& obj) {
			return define_2D_buffer_protocol(obj.val.mat);
		});
}

void defstruct_ProjectionParams(py::module& m)
{
	py::class_<Fove_ProjectionParams>(m, "ProjectionParams",
									  R"(Struct holding information about projection frustum planes

Values are given for a depth of 1 so that it's easy to multiply them by your near clipping plan, for example, to get the correct values for your use.)")
		.def(py::init<float, float, float, float>(),
			 py::arg("left") = -1.0f,
			 py::arg("right") = 1.0f,
			 py::arg("top") = 1.0f,
			 py::arg("bottom") = -1.0f)
		.def_readwrite("left", &Fove_ProjectionParams::left, "Left side (low-X)")     // float -1
		.def_readwrite("right", &Fove_ProjectionParams::right, "Right side (high-X)") // float +1
		.def_readwrite("top", &Fove_ProjectionParams::top, "Top (high-Y)")            // float +1
		.def_readwrite("bottom", &Fove_ProjectionParams::bottom, "Bottom (low-Y)")    // float -1
		.def("__repr__", repr<Fove_ProjectionParams>, "Returns a string representation of the projection params.");
}

void defstruct_BoundingBox(py::module& m)
{
	py::class_<Fove_BoundingBox>(m, "BoundingBox",
								 R"(A bounding box)")
		.def(py::init<Fove_Vec3, Fove_Vec3>(),
			 py::arg_v("center", default_Vec3(), "Vec3()"),
			 py::arg_v("extend", default_Vec3(), "Vec3()"))
		.def_readwrite("center", &Fove_BoundingBox::center, "The position of the center of the bounding box")
		.def_readwrite("extend", &Fove_BoundingBox::extend, "The extend of the bounding box (e.g. half of its size)")
		.def("__repr__", repr<Fove_BoundingBox>, "Returns a string representation of the bounding box.")
		.def(
			"__eq__", [](const Fove_BoundingBox& self, const Fove_BoundingBox& other) {
				return self == other;
			},
			py::is_operator(), "Returns `True` if the two bounding box are the same.")
		.def("__ne__", [](Fove_BoundingBox& self, Fove_BoundingBox& other) {
			return !(self == other);
		},
			 py::is_operator(), "Returns `True` if the two bounding box are not the same.");
}

void defstruct_ObjectPose(py::module& m)
{
	py::class_<Fove_ObjectPose>(m, "ObjectPose",
								R"(Represents the pose of an object of the scene

Pose transformations are applied in the following order on the object: scale, rotation, translation)")
		.def(py::init<Fove_Vec3, Fove_Quaternion, Fove_Vec3, Fove_Vec3>(),
			 py::arg_v("scale", Fove_Vec3{1, 1, 1}, "Vec3(1,1,1)"),
			 py::arg_v("rotation", default_Quaternion(), "Quaternion()"),
			 py::arg_v("position", default_Vec3(), "Vec3()"),
			 py::arg_v("velocity", default_Vec3(), "Vec3()"))
		.def_readwrite("scale", &Fove_ObjectPose::scale, "The scale of the object in world space")
		.def_readwrite("rotation", &Fove_ObjectPose::rotation, "The rotation of the object in world space")
		.def_readwrite("position", &Fove_ObjectPose::position, "The position of the object in world space")
		.def_readwrite("velocity", &Fove_ObjectPose::velocity, "Velocity of the object in world space")
		.def("__repr__", repr<Fove_ObjectPose>, "Returns a string representation of the object pose.")
		.def(
			"__eq__", [](const Fove_ObjectPose& self, const Fove_ObjectPose& other) {
				return self == other;
			},
			py::is_operator(), "Returns `True` if the two poses are the same.")
		.def("__ne__", [](Fove_ObjectPose& self, Fove_ObjectPose& other) {
			return !(self == other);
		},
			 py::is_operator(), "Returns `True` if the two poses are not the same.");
	;
}

void defenum_ColliderType(py::module& m)
{
	py::enum_<Fove_ColliderType>(m, "ColliderType", R"(Specify the different collider shape types

- `Cube`: A cube shape
- `Sphere`: A sphere shape
- `Mesh`: A shape defined by a mesh
)")
		.value("Cube", Fove_ColliderType::Cube)
		.value("Sphere", Fove_ColliderType::Sphere)
		.value("Mesh", Fove_ColliderType::Mesh);
}

void defstruct_ColliderCube(py::module& m)
{
	py::class_<Fove_ColliderCube>(m, "ColliderCube",
								  R"(Define a cube collider shape)")
		.def(py::init<Fove_Vec3>(),
			 py::arg_v("size", Fove_Vec3{1, 1, 1}, "Vec3(1,1,1)"))
		.def_readwrite("size", &Fove_ColliderCube::size, "The size of the cube")
		.def("__repr__", repr<Fove_ColliderCube>, "Returns a string representation of the cube collider.");
}

void defstruct_ColliderSphere(py::module& m)
{
	py::class_<Fove_ColliderSphere>(m, "ColliderSphere",
									R"(Define a sphere collider shape)")
		.def(py::init<float>(),
			 py::arg("radius") = 0.5f)
		.def_readwrite("radius", &Fove_ColliderSphere::radius, "The radius of the sphere")
		.def("__repr__", repr<Fove_ColliderSphere>, "Returns a string representation of the sphere collider.");
}

struct VertexBuffer
{
	float* vertices;
	unsigned int vertexCount;
};

void defstruct_VertexBuffer(py::module& m)
{
	py::class_<VertexBuffer>(m, "VertexBuffer", py::buffer_protocol(),
							 R"(Contains the vertices defining a mesh)")
		.def(py::init([](py::buffer b) {
			/* Request a buffer descriptor from Python */
			py::buffer_info info = b.request();

			/* Some sanity checks ... */
			if (info.format != py::format_descriptor<float>::format())
				throw std::runtime_error("Incompatible format: expected a float array! received:" + info.format);

			if (info.ndim != 2)
				throw std::runtime_error("Incompatible buffer dimension!");

			if (info.shape[1] != 3)
				throw std::runtime_error("Vertex should be composed of 3 components (x,y,z)!");

			if (info.strides[0] != 3 * sizeof(float))
				throw std::runtime_error("Row stride should be 3 floats");

			if (info.strides[1] != sizeof(float))
				throw std::runtime_error("Col stride should be 1 float");

			return VertexBuffer{static_cast<float*>(info.ptr), static_cast<unsigned int>(info.shape[0] * 3)};
		}));
}

struct IndexBuffer
{
	unsigned int* indices;
	unsigned int triangleCount;
};

void defstruct_IndexBuffer(py::module& m)
{
	py::class_<IndexBuffer>(m, "IndexBuffer", py::buffer_protocol(),
							R"(Contains the triangle indices defining a mesh)")
		.def(py::init([](py::buffer b) {
			/* Request a buffer descriptor from Python */
			py::buffer_info info = b.request();

			if (info.ndim != 1)
				throw std::runtime_error("Incompatible buffer dimension!");

			if (info.strides[0] != sizeof(unsigned int))
				throw std::runtime_error("Row stride should be 1 unsigned int");

			if (info.shape[0] % 3 != 0)
				throw std::runtime_error("Index buffer index count should be a multiple of 3 as it represent triangles");

			return IndexBuffer{static_cast<unsigned int*>(info.ptr), static_cast<unsigned int>(info.shape[0] / 3)};
		}));
}

void defstruct_ColliderMesh(py::module& m)
{
	defstruct_VertexBuffer(m);
	defstruct_IndexBuffer(m);

	py::class_<Fove_ColliderMesh>(m, "ColliderMesh",
								  R"(Define a mesh collider shape

A mesh collider can either be defined through a triangle list or through a vertex/index buffer set.
If the index buffer pointer is null, then the vertex buffer is interpreted as a regular triangle list.
)")
		.def(py::init<>())
		.def_property("vertexBuffer",
					  [](Fove_ColliderMesh& self) -> const VertexBuffer {
						  return VertexBuffer{self.vertices, self.vertexCount};
					  },
					  [](Fove_ColliderMesh& self, const VertexBuffer& value) {
						  self.vertices = value.vertices;
						  self.vertexCount = value.vertexCount;
					  },
					  R"(The vertices of the mesh.

It contains the X, Y, Z positions of mesh vertices.
Triangles are defined using "indices
)")
		.def_property("indexBuffer",
					  [](Fove_ColliderMesh& self) -> const IndexBuffer {
						  return IndexBuffer{self.indices, self.triangleCount};
					  },
					  [](Fove_ColliderMesh& self, const IndexBuffer& value) {
						  self.indices = value.indices;
						  self.triangleCount = value.triangleCount;
					  },
					  R"(The vertex indices defining the triangles of the mesh

Triangles are listed one after the others (and not combined using a fan or strip algorithm).
The number of elements must equal `3 x triangleCount`.

Outward faces are defined to be specified counter-clockwise.
Face-direction information is not currently used but may be in the future.

If null, the vertices are interpreted as a simple triangle list.
)")
		.def_readwrite("boundingBox", &Fove_ColliderMesh::boundingBox, "If null the bounding box is re-calculated internally")
		.def("__repr__", repr<Fove_ColliderMesh>, "Returns a string representation of the mesh collider.");
}

void defstruct_ObjectCollider(py::module& m)
{
	py::class_<Fove_ObjectCollider>(m, "ObjectCollider",
									R"(Represents a colliding part of a gazable object
Colliders are used to calculate intersection between gaze rays and gazable objects)")
		.def(py::init<Fove_Vec3>(), py::arg_v("center", default_Vec3(), "Vec3()"))
		.def_readwrite("center", &Fove_ObjectCollider::center, "The offset of the collider center collider raw shape")
		.def_readonly("shapeType", &Fove_ObjectCollider::shapeType, "The shape type of the collider")
		.def_property("cubeDefinition",
					  [](Fove_ObjectCollider& self) -> const Fove_ColliderCube& {
						  if (self.shapeType != Fove_ColliderType::Cube)
							  throw std::runtime_error("Error the collider is not of cube type");

						  return self.shapeDefinition.cube;
					  },
					  [](Fove_ObjectCollider& self, const Fove_ColliderCube& value) {
						  self.shapeType = Fove_ColliderType::Cube;
						  self.shapeDefinition.cube = value;
					  },
					  R"(Set the object collider as a cube collider)")
		.def_property("sphereDefinition",
					  [](Fove_ObjectCollider& self) -> const Fove_ColliderSphere& {
						  if (self.shapeType != Fove_ColliderType::Sphere)
							  throw std::runtime_error("Error the collider is not of sphere type");

						  return self.shapeDefinition.sphere;
					  },
					  [](Fove_ObjectCollider& self, const Fove_ColliderSphere& value) {
						  self.shapeType = Fove_ColliderType::Sphere;
						  self.shapeDefinition.sphere = value;
					  },
					  R"(Set the object collider as a sphere collider)")
		.def_property("meshDefinition",
					  [](Fove_ObjectCollider& self) -> const Fove_ColliderMesh& {
						  if (self.shapeType != Fove_ColliderType::Mesh)
							  throw std::runtime_error("Error the collider is not of mesh type");

						  return self.shapeDefinition.mesh;
					  },
					  [](Fove_ObjectCollider& self, const Fove_ColliderMesh& value) {
						  self.shapeType = Fove_ColliderType::Mesh;
						  self.shapeDefinition.mesh = value;
					  },
					  R"(Set the object collider as a mesh collider)")
		.def("__repr__", repr<Fove_ObjectCollider>, "Returns a string representation of the object collider.");
}

struct ColliderArray
{
	std::vector<Fove_ObjectCollider> colliders;
};

void defstruct_ColliderArray(py::module& m)
{
	py::class_<ColliderArray>(m, "ColliderArray", py::buffer_protocol(),
							  R"(Contains the triangle indices defining a mesh)")
		.def(py::init<>())
		.def("add", [](ColliderArray& colliderArray, const Fove_ObjectCollider& collider) {
			colliderArray.colliders.push_back(collider);
		});
}

void defstruct_GazableObject(py::module& m)
{
	defstruct_ColliderArray(m);

	py::class_<Fove_GazableObject>(m, "GazableObject",
								   R"(Represents an object in a 3D world
The bounding shapes of this object are used for ray casts to determine what the user is looking at.
Note that multiple bounding shape types can be used simultaneously, such as a sphere and a mesh.
\see fove_Headset_registerGazableObject
\see fove_Headset_updateGazableObject
\see fove_Headset_removeGazableObject
)")
		.def(py::init<int, Fove_ObjectPose, Fove_ObjectGroup>(),
			 py::arg("id") = fove_ObjectIdInvalid,
			 py::arg_v("pose", default_ObjectPose(), "ObjectPose()"),
			 py::arg_v("group", Fove_ObjectGroup::Group0, "ObjectGroup(0)"))
		.def_readwrite("id", &Fove_GazableObject::id, "Unique ID of the object. User-defined objects should use positive integers.")
		.def_readwrite("pose", &Fove_GazableObject::pose, "The initial pose of the object")
		.def_readwrite("group", &Fove_GazableObject::group, "The gazable object group this object belongs to")
		.def("setColliders", [](Fove_GazableObject& self, ColliderArray& value) {
			self.colliders = value.colliders.data();
			self.colliderCount = static_cast<unsigned>(value.colliders.size());
		},
			 R"(Set the colliders of the gazable object)")
		.def("__repr__", repr<Fove_GazableObject>, "Returns a string representation of the gazable object.");
}

void defstruct_CameraObject(py::module& m)
{
	py::class_<Fove_CameraObject>(m, "CameraObject",
								  R"(Represents a camera in a 3D world
The camera view pose determine what the user is looking at and the object mask specify which objects are rendered.)")
		.def(py::init<int, Fove_ObjectPose, Fove_ObjectGroup>(),
			 py::arg("id") = fove_ObjectIdInvalid,
			 py::arg_v("pose", default_ObjectPose(), "ObjectPose()"),
			 py::arg_v("groupMask", static_cast<Fove_ObjectGroup>(0xffffffff), "ObjectGroup(0xffffffff)"))
		.def_readwrite("id", &Fove_CameraObject::id, "Unique ID of the camera. User-defined id should use positive integers.")
		.def_readwrite("pose", &Fove_CameraObject::pose, "The camera initial pose")
		.def_readwrite("groupMask", &Fove_CameraObject::groupMask, "The bit mask specifying which object groups the camera renders")
		.def("__repr__", repr<Fove_CameraObject>, "Returns a string representation of the camera object.");
}

void defenum_GraphicsAPI(py::module& m)
{
	py::enum_<Fove_GraphicsAPI>(m, "GraphicsAPI",
								R"(enum for type of Graphics API

Type of Graphics API
Note: We currently only support DirectX)

- `DirectX`: , DirectX (Windows only)
- `OpenGL`: , OpenGL (All platforms, currently in BETA)
- `Metal`: Metal (Mac only))")
		.value("DirectX", Fove_GraphicsAPI::DirectX) // = 0
		.value("OpenGL", Fove_GraphicsAPI::OpenGL)   // = 1
		.value("Metal", Fove_GraphicsAPI::Metal)     // = 2
		;
}

void defenum_AlphaMode(py::module& m)
{
	py::enum_<Fove_AlphaMode>(m, "AlphaMode",
							  R"(Enum to help interpret the alpha of texture

Determines how to interpret the alpha of a compositor client texture

- `Auto`: Base layers will use One, overlay layers will use Sample
- `One`: Alpha will always be one (fully opaque)
- `Sample`: Alpha fill be sampled from the alpha channel of the buffer)")
		.value("Auto", Fove_AlphaMode::Auto)     // = 0
		.value("One", Fove_AlphaMode::One)       // = 1
		.value("Sample", Fove_AlphaMode::Sample) // = 2
		;
}

void defstruct_CompositorLayerCreateInfo(py::module& m)
{
	py::class_<Fove_CompositorLayerCreateInfo>(m, "CompositorLayerCreateInfo",
											   R"(Struct used to define the settings for a compositor client.

Structure used to define the settings for a compositor client.)")
		.def(py::init<Fove_CompositorLayerType, bool, Fove_AlphaMode, bool, bool>(),
			 py::arg_v("type", Fove_CompositorLayerType::Base, "CompositorLayer()"),
			 py::arg("disableTimeWarp") = false,
			 py::arg_v("alphaMode", Fove_AlphaMode::Auto, "AlphaMode(0)"),
			 py::arg("disableFading") = false,
			 py::arg("disableDistortion") = false)
		.def_readwrite("type", &Fove_CompositorLayerCreateInfo::type,
					   "The type (layer) upon which the client will draw") // Fove_CompositorLayerType Fove_CompositorLayerType::Base
		.def_readwrite("disableTimeWarp", &Fove_CompositorLayerCreateInfo::disableTimeWarp,
					   "Setting to disable timewarp, e.g. if an overlay client is operating in screen space") // bool false
		.def_readwrite("alphaMode", &Fove_CompositorLayerCreateInfo::alphaMode,
					   "Setting about whether to use alpha sampling or not, e.g. for a base client") // Fove_AlphaMode Fove_AlphaMode::Auto
		.def_readwrite("disableFading", &Fove_CompositorLayerCreateInfo::disableFading,
					   "Setting to disable fading when the base layer is misbehaving, e.g. for a diagnostic client") // bool false
		.def_readwrite("disableDistortion", &Fove_CompositorLayerCreateInfo::disableDistortion,
					   "Setting to disable a distortion pass, e.g. for a diagnostic client, or a client intending to do its own distortion") // bool false
		;
}

// Struct used to store information about an existing compositor layer (after it is created)
void defstruct_CompositorLayer(py::module& m)
{
	py::class_<Fove_CompositorLayer>(m, "CompositorLayer",
									 R"(Struct used to store information about an existing compositor layer (after it is created)

This exists primarily for future expandability.)")
		.def(py::init<int, Fove_Vec2i>(),
			 py::arg("layerId") = 0,
			 py::arg_v("idealResolutionPerEye", default_Vec2i(), "Vec2i()"))
		.def_readwrite("layerId", &Fove_CompositorLayer::layerId,
					   "Uniquely identifies a layer created within an IFVRCompositor object") // int 0
		.def_readwrite("idealResolutionPerEye", &Fove_CompositorLayer::idealResolutionPerEye,
					   R"(The optimal resolution for a submitted buffer on this layer (for a single eye).

		Clients are allowed to submit buffers of other resolutions.
		In particular, clients can use a lower resolution buffer to reduce their rendering overhead.)") // Fove_Vec2i {}
		;
}

void defstruct_CompositorTexture(py::module& m)
{
	py::class_<Fove_CompositorTexture>(m, "CompositorTexture",
									   "Base class of API-specific texture classes")
		.def(py::init<Fove_GraphicsAPI>(),
			 py::arg_v("graphicsAPI", Fove_GraphicsAPI::DirectX, "GraphicsAPI(0)"))
		.def_readwrite("graphicsAPI", &Fove_CompositorTexture::graphicsAPI,
					   R"(If this is DirectX, this object must be a Fove_DX11Texture
If this is OpenGL, this object must be a Fove_GLTexture
In C++ this field is initialized automatically by the subclass)" // GraphicsAPI
		);
}

void defstruct_DX11Texture(py::module& m)
{
	py::class_<Fove_DX11Texture>(m, "DX11Texture", "Struct used to submit a DirectX 11 texture")
		.def(py::init<>([]() {
			return Fove_DX11Texture{Fove_CompositorTexture{Fove_GraphicsAPI::DirectX}, nullptr};
		}))
		.def_readonly("parent", &Fove_DX11Texture::parent,
					  "Parent object") // Fove_GraphicsAPI Fove_GraphicsAPI::OpenGL
		.def_readwrite("texture", &Fove_DX11Texture::texture,
					   "This must point to a ID3D11Texture2D") // void* nullptr
		;
}

void defstruct_GLTexture(py::module& m)
{
	py::class_<Fove_GLTexture>(m, "GLTexture",
							   R"(Struct used to submit an OpenGL texture

The GL context must be active on the thread that submits this.)")
		.def(py::init([]() {
			return Fove_GLTexture{Fove_CompositorTexture{Fove_GraphicsAPI::OpenGL}, 0, nullptr};
		}))
		.def_readonly("parent", &Fove_GLTexture::parent, "Parent object")
		.def_readwrite("textureId", &Fove_GLTexture::textureId,
					   "The opengl id of the texture, as returned by glGenTextures") // uint32_t 0
		.def_readwrite("context", &Fove_GLTexture::context,
					   "On mac, this is a CGLContextObj, otherwise this field is reserved and you must pass null") // void* nullptr
		;
}

void defstruct_MetalTexture(py::module& m)
{
	py::class_<Fove_MetalTexture>(m, "MetalTexture", "Struct used to submit a texture using the Apple Metal API")
		.def(py::init<>([]() {
			return Fove_MetalTexture{Fove_CompositorTexture{Fove_GraphicsAPI::Metal}, nullptr};
		}))
		.def_readonly("parent", &Fove_MetalTexture::parent, "Parent object") // Fove_GraphicsAPI Fove_GraphicsAPI::Metal
		.def_readwrite("texture", &Fove_MetalTexture::texture,
					   "Pointer to an MTLTexture (which must have MTLTextureUsageShaderRead specified).") // void* nullptr
		;
}

void defstruct_TextureBounds(py::module& m)
{
	py::class_<Fove_TextureBounds>(m, "TextureBounds",
								   R"(Struct to represent coordinates in normalized space

Coordinates in normalized space where 0 is left/top and 1 is bottom/right)")
		.def(py::init<float, float, float, float>(),
			 py::arg("left") = 0.0f,
			 py::arg("top") = 0.0f,
			 py::arg("right") = 0.0f,
			 py::arg("bottom") = 0.0f)
		.def_readwrite("left", &Fove_TextureBounds::left)     // float 0.0f
		.def_readwrite("top", &Fove_TextureBounds::top)       // float 0.0f
		.def_readwrite("right", &Fove_TextureBounds::right)   // float 0.0f
		.def_readwrite("bottom", &Fove_TextureBounds::bottom) // float 0.0f
		.def(
			"__eq__", [](const Fove_TextureBounds& self, const Fove_TextureBounds& other) {
				return self == other;
			},
			py::is_operator(), "Returns `True` if the two texture bounds are the same.")
		.def("__ne__", [](const Fove_TextureBounds& self, const Fove_TextureBounds& other) {
			return !(self == other);
		},
			 py::is_operator(), "Returns `True` if the two texture bounds are not the same.");
}

void defstruct_CompositorLayerEyeSubmitInfo(py::module& m)
{
	py::class_<Fove_CompositorLayerEyeSubmitInfo>(m, "CompositorLayerEyeSubmitInfo",
												  "Struct used to conglomerate the texture settings for a single eye, when submitting a given layer")
		.def(py::init<const Fove_CompositorTexture*, Fove_TextureBounds>(),
			 py::arg("texInfo") = nullptr,
			 py::arg_v("bounds", default_TextureBounds(), "TextureBounds()"))
		.def_readwrite("texInfo", &Fove_CompositorLayerEyeSubmitInfo::texInfo, R"(Texture to submit for this eye
This may be null as long as the other submitted eye's texture isn't (thus allowing each eye to be submitted separately))")
		.def_readwrite("bounds", &Fove_CompositorLayerEyeSubmitInfo::bounds, "The portion of the texture that is used to represent the eye (Eg. half of it if the texture contains both eyes)");
}

void defstruct_CompositorLayerSubmitInfo(py::module& m)
{
	py::class_<Fove_CompositorLayerSubmitInfo>(m, "CompositorLayerSubmitInfo", "Struct used to conglomerate the texture settings when submitting a given layer")
		.def(py::init<int, Fove_Pose, Fove_CompositorLayerEyeSubmitInfo, Fove_CompositorLayerEyeSubmitInfo>(),
			 py::arg("layerId") = 0,
			 py::arg_v("pose", default_Pose(), "Pose()"),
			 py::arg_v("left", default_CompositorLayerEyeSubmitInfo(), "CompositorLayerEyeSubmitInfo()"),
			 py::arg_v("right", default_CompositorLayerEyeSubmitInfo(), "CompositorLayerEyeSubmitInfo()"))
		.def_readwrite("layerId", &Fove_CompositorLayerSubmitInfo::layerId, "The layer ID as fetched from Fove_CompositorLayer")
		.def_readwrite("pose", &Fove_CompositorLayerSubmitInfo::pose, "The pose used to draw this layer, usually coming from Compositor_waitForRenderPose")
		.def_readwrite("left", &Fove_CompositorLayerSubmitInfo::left, "Information about the left eye")
		.def_readwrite("right", &Fove_CompositorLayerSubmitInfo::right, "Information about the left eye");
}

// Struct used to identify a GPU adapter
// Currently only used on Windows
void defstruct_AdapterId(py::module& m)
{
	py::class_<Fove_AdapterId>(m, "AdapterId", "Struct used to identify a GPU adapter (Windows only)")
	// This is a corner case. We choose to throw if users try to construct Fove_AdapterId
	// on non-Win platforms
	// .def(py::init<>())
#ifdef _WIN32
		.def(py::init<uint32_t, int32_t>(),
			 py::arg("lowPart") = 0u,
			 py::arg("highPart") = 0)
		.def_readwrite("lowPart", &Fove_AdapterId::lowPart, "On windows, this together with `highPart` forms a LUID structure")
		.def_readwrite("highPart", &Fove_AdapterId::highPart, "On windows, this together with `lowPart` forms a LUID structure")
#endif // _WIN32
		;
}

void defstruct_Buffer(py::module& m)
{
	py::class_<Fove_Buffer>(m, "Buffer", py::buffer_protocol(),
							R"(A generic memory buffer

No ownership or lifetime semantics are specified. Please see the comments on the functions that use this.)")
		.def(py::init<void*, std::size_t>(),
			 py::arg("data") = nullptr,
			 py::arg("length") = 0)
		.def_readwrite("data", &Fove_Buffer::data, "Pointer to the start of the memory buffer")
		.def_readwrite("length", &Fove_Buffer::length, "Length, in bytes, of the buffer")
		.def_buffer([](Fove_Buffer& buf) {
			return py::buffer_info{
				reinterpret_cast<unsigned char*>(const_cast<void*>(buf.data)), // XXX const
				sizeof(unsigned char),
				py::format_descriptor<unsigned char>::format(),
				1,                      // ndims
				{buf.length},           // dims
				{sizeof(unsigned char)} // strides
			};
		});
}

using Python_EyeShape = Obj<Fove_EyeShape>;
void defstruct_EyeShape(py::module& m)
{
	py::class_<Python_EyeShape>(m, "EyeShape", py::buffer_protocol(),
								R"(Specify the shape of an eye

This struct implements buffer_protocol, and thus can be converted to a numpy array:
m = fove.capi.EyeShape()
a = numpy.array(m, copy=False)
)")
		.def(py::init<>())
		.def_buffer([](Python_EyeShape& obj) {
			return py::buffer_info{
				reinterpret_cast<void*>(&obj.val.outline[0]),
				sizeof(float),
				py::format_descriptor<float>::format(),
				2,                                 // ndims
				{12, 2},                           // dims
				{sizeof(float) * 2, sizeof(float)} // strides
			};
		});
}

void defstruct_PupilShape(py::module& m)
{
	py::class_<Fove_PupilShape>(m, "PupilShape", R"(Specity the shape of a pupil as an ellipse

Coordinates are in eye-image pixels from (0,0) to (camerawidth, cameraheight), with (0,0) being the top left.)")
		.def(py::init<Fove_Vec2, Fove_Vec2, float>(),
			 py::arg_v("center", default_Vec2(), "Vec2()"),
			 py::arg_v("size", default_Vec2(), "Vec2()"),
			 py::arg("angle") = 0.0f)
		.def_readwrite("center", &Fove_PupilShape::center, "The center of the ellipse")
		.def_readwrite("size", &Fove_PupilShape::size, "The width and height of the ellipse")
		.def_readwrite("angle", &Fove_PupilShape::angle, "A clockwise rotation around the center, in degrees");
}

void defstruct_BitmapImage(py::module& m)
{
	py::class_<Fove_BitmapImage>(m, "BitmapImage", "A 2D bitmap image")
		.def(py::init<uint64_t, Fove_Buffer>(),
			 py::arg("timestamp") = 0,
			 py::arg_v("image", default_Buffer(), "Buffer()"))
		.def_readwrite("timestamp", &Fove_BitmapImage::timestamp,
					   "Timestamp of the image, in microseconds since an unspecified epoch") // uint64_t 0
		.def_readwrite("image", &Fove_BitmapImage::image,
					   R"(BMP data (including full header that contains size, format, etc)

The height may be negative to specify a top-down bitmap.)") // Fove_Buffer {}
		;
}

void defstruct_CalibrationTarget(py::module& m)
{
	py::class_<Fove_CalibrationTarget>(m, "CalibrationTarget", "Represent a calibration target of the calibration process")
		.def(py::init<Fove_Vec3, float>(),
			 py::arg_v("position", default_Vec3(), "Vec3()"),
			 py::arg("scale") = 0)
		.def_readwrite("position", &Fove_CalibrationTarget::position,
					   "The position of the calibration target in the 3D world space")
		.def_readwrite("recommendedSize", &Fove_CalibrationTarget::recommendedSize,
					   R"(The recommended size for the calibration target in world space unit.
A recommended size of 0 means that the display of the target is not recommended at the current time)")
		.def(
			"__eq__", [](const Fove_CalibrationTarget& self, const Fove_CalibrationTarget& other) {
				return self == other;
			},
			py::is_operator(), "Returns `True` if the two targets are the same.")
		.def("__ne__", [](Fove_CalibrationTarget& self, Fove_CalibrationTarget& other) {
			return !(self == other);
		},
			 py::is_operator(), "Returns `True` if the two targets are not the same.");
	;
}

void defenum_CalibrationState(py::module& m)
{
	py::enum_<Fove_CalibrationState>(m, "CalibrationState", R"(Indicates the state of a calibration process
A calibration process always starts from the `NotStarted` state,
	then it can go back and forth between the `WaitingForUser` & `CollectingData` states,
	then it goes to the `ProcessingData` state and finishes with the `Successful` state.

	A failure can happen any time during the process, and stops the process where it was.

	From the `ProcessingData` state the calibration process do not require any rendering
	and gameplay can be started if wanted but new calibration won't be effective before reaching the `Successful` state.)")
		.value("NotStarted", Fove_CalibrationState::NotStarted)
		.value("HeadsetAdjustment", Fove_CalibrationState::HeadsetAdjustment)
		.value("WaitingForUser", Fove_CalibrationState::WaitingForUser)
		.value("CollectingData", Fove_CalibrationState::CollectingData)
		.value("ProcessingData", Fove_CalibrationState::ProcessingData)
		.value("Successful_HighQuality", Fove_CalibrationState::Successful_HighQuality)
		.value("Successful_MediumQuality", Fove_CalibrationState::Successful_MediumQuality)
		.value("Successful_LowQuality", Fove_CalibrationState::Successful_LowQuality)
		.value("Failed_Unknown", Fove_CalibrationState::Failed_Unknown)
		.value("Failed_InaccurateData", Fove_CalibrationState::Failed_InaccurateData)
		.value("Failed_NoRenderer", Fove_CalibrationState::Failed_NoRenderer)
		.value("Failed_NoUser", Fove_CalibrationState::Failed_NoUser)
		.value("Failed_Aborted", Fove_CalibrationState::Failed_Aborted);
}

void defenum_CalibrationMethod(py::module& m)
{
	py::enum_<Fove_CalibrationMethod>(m, "CalibrationMethod", "Indicates the calibration method to use")
		.value("Default", Fove_CalibrationMethod::Default)
		.value("OnePoint", Fove_CalibrationMethod::OnePoint)
		.value("Spiral", Fove_CalibrationMethod::Spiral)
		.value("OnePointWithNoGlassesSpiralWithGlasses", Fove_CalibrationMethod::OnePointWithNoGlassesSpiralWithGlasses)
		.value("ZeroPoint", Fove_CalibrationMethod::ZeroPoint)
		.value("DefaultCalibration", Fove_CalibrationMethod::DefaultCalibration);
}

void defenum_EyeByEyeCalibration(py::module& m)
{
	py::enum_<Fove_EyeByEyeCalibration>(m, "EyeByEyeCalibration", "Indicate whether each eye should be calibrated separately or not")
		.value("Default", Fove_EyeByEyeCalibration::Default)
		.value("Disabled", Fove_EyeByEyeCalibration::Disabled)
		.value("Enabled", Fove_EyeByEyeCalibration::Enabled);
}

void defenum_EyeTorsionCalibration(py::module& m)
{
	py::enum_<Fove_EyeTorsionCalibration>(m, "EyeTorsionCalibration", "Indicate whether eye torsion calibration should be run or not")
		.value("Default", Fove_EyeTorsionCalibration::Default)
		.value("IfEnabled", Fove_EyeTorsionCalibration::IfEnabled)
		.value("Always", Fove_EyeTorsionCalibration::Always);
}

struct CalibrationData
{
	Fove_CalibrationMethod method;
	Fove_CalibrationState state;
	std::string stateInfo;
	Fove_CalibrationTarget targetL;
	Fove_CalibrationTarget targetR;

	static CalibrationData FromNative(const Fove_CalibrationData* nativeData)
	{
		return {
			nativeData->method,
			nativeData->state,
			nativeData->stateInfo,
			nativeData->targetL,
			nativeData->targetR,
		};
	}
};

void defstruct_CalibrationData(py::module& m)
{
	py::class_<CalibrationData>(m, "CalibrationData", "Provide all the calibration data needed to render the current state of the calibration process")
		.def(py::init<Fove_CalibrationMethod, Fove_CalibrationState, std::string, Fove_CalibrationTarget, Fove_CalibrationTarget>(),
			 py::arg_v("method", Fove_CalibrationMethod::Spiral, "CalibrationMethod(0)"),
			 py::arg_v("state", Fove_CalibrationState::NotStarted, "CalibrationState(0)"),
			 py::arg("stateInfo") = std::string(),
			 py::arg_v("targetL", default_CalibrationTarget(), "CalibrationTarget()"),
			 py::arg_v("targetR", default_CalibrationTarget(), "CalibrationTarget()"))
		.def_readwrite("method", &CalibrationData::method, "The calibration method currently used, or Default if the method is unknown (from a future update)")
		.def_readwrite("state", &CalibrationData::state, "The current state of the calibration")
		.def_readwrite("stateInfo", &CalibrationData::state, "Human readable extra information about the current calibration state")
		.def_readwrite("targetL", &CalibrationData::targetL, "The current calibration target to display for the left eye")
		.def_readwrite("targetR", &CalibrationData::targetR, "The current calibration target to display for the right eye");
}

void defstruct_HmdAdjustmentData(py::module& m)
{
	py::class_<Fove_HmdAdjustmentData>(m, "HmdAdjustmentData", "Provide all the HMD positioning data needed to render the current state of the HMD adjustment process")
		.def(py::init<Fove_Vec2, float, bool, bool, Fove_Vec2, Fove_Vec2, float, float, Fove_Vec2, Fove_Vec2>(),
			 py::arg_v("translation", default_Vec2(), "Vec2()"),
			 py::arg("rotation") = 0,
			 py::arg("adjustmentNeeded") = false,
			 py::arg("hasTimeout") = false,
			 py::arg_v("idealPositionL", default_Vec2(), "Vec2()"),
			 py::arg_v("idealPositionR", default_Vec2(), "Vec2()"),
			 py::arg("idealPositionSpanL") = 0,
			 py::arg("idealPositionSpanR") = 0,
			 py::arg_v("estimatedPositionL", default_Vec2(), "Vec2()"),
			 py::arg_v("estimatedPositionR", default_Vec2(), "Vec2()"))
		.def_readwrite("translation", &Fove_HmdAdjustmentData::translation, "The HMD translation offset in eyes camera in relative units ([-1, 1])")
		.def_readwrite("rotation", &Fove_HmdAdjustmentData::rotation, "The rotation of HMD to the eye line in radian")
		.def_readwrite("adjustmentNeeded", &Fove_HmdAdjustmentData::adjustmentNeeded, "Indicate whether the HMD adjustment GUI should be displayed to correct user HMD alignment")
		.def_readwrite("hasTimeout", &Fove_HmdAdjustmentData::hasTimeout, "Indicate if the adjustment process has timeout in which case the GUI should close")
		.def_readwrite("idealPositionL", &Fove_HmdAdjustmentData::idealPositionL, "Pixel coordinate on the left camera image for the expected ideal eye position")
		.def_readwrite("idealPositionR", &Fove_HmdAdjustmentData::idealPositionR, "Pixel coordinate on the right camera image for the expected ideal eye position")
		.def_readwrite("idealPositionSpanL", &Fove_HmdAdjustmentData::idealPositionSpanL, "Radius of the tolerance area for the expected ideal eye position on the left camera image in pixels")
		.def_readwrite("idealPositionSpanR", &Fove_HmdAdjustmentData::idealPositionSpanR, "Radius of the tolerance area for the expected ideal eye position on the right camera image in pixels")
		.def_readwrite("estimatedPositionL", &Fove_HmdAdjustmentData::estimatedPositionL, "Pixel coordinate of left eye position which is independent on eye orientation")
		.def_readwrite("estimatedPositionR", &Fove_HmdAdjustmentData::estimatedPositionR, "Pixel coordinate of right eye position which is independent on eye orientation");
}

void defstruct_CalibrationOptions(py::module& m)
{
	py::class_<Fove_CalibrationOptions>(m, "CalibrationOptions", "Options specifying how to run a calibration process")
		.def(py::init<bool, bool, Fove_EyeByEyeCalibration, Fove_CalibrationMethod, Fove_EyeTorsionCalibration>(),
			 py::arg("lazy") = false,
			 py::arg("restart") = false,
			 py::arg_v("eyeByEye", Fove_EyeByEyeCalibration::Default, "EyeByEyeCalibration(0)"),
			 py::arg_v("method", Fove_CalibrationMethod::Default, "CalibrationMethod(0)"),
			 py::arg_v("eyeTorsion", Fove_EyeTorsionCalibration::Default, "EyeTorsionCalibration(0)"))
		.def_readwrite("lazy", &Fove_CalibrationOptions::lazy, "Do not restart the calibration process if it is already calibrated")
		.def_readwrite("restart", &Fove_CalibrationOptions::restart, "Restart the calibration process from the beginning if it is already running")
		.def_readwrite("eyeByEye", &Fove_CalibrationOptions::eyeByEye, "Calibrate both eyes simultaneously or separately")
		.def_readwrite("method", &Fove_CalibrationOptions::method, "The calibration method to use")
		.def_readwrite("eyeTorsion", &Fove_CalibrationOptions::eyeTorsion, "Whether to perform eye torsion calibration or not");
}

////////////////////////////////////////////////////////////////
// C APIs

using Headset = Obj<Fove_Headset*>;
using Compositor = Obj<Fove_Compositor*>;

void defstruct_Headsets(py::module& m)
{
	py::class_<Headset>(m, "Fove_Headset", "Opaque type representing a headset object")
		.def(py::init<>());
}

void defstruct_Compositor(py::module& m)
{
	py::class_<Compositor>(m, "Fove_Compositor", "Opaque type representing a compositor connection")
		.def(py::init<>());
}

void defstruct_Wrappers(py::module& m)
{
	py::class_<Obj<bool>>(m, "Bool", R"(An object wrapper for boolean values.

This is necessary as we use boolean values as out variables, but primitives in python are immutable.)")
		.def(py::init<bool>(), py::arg("val") = false)
		.def_readonly("val", &Obj<bool>::val, "The actual value contained in the object wrapper")
		.def(
			"__bool__", [](const Obj<bool>& b) { return b.val; }, py::is_operator())
		.def(
			"__eq__", [](const Obj<bool>& self, const Obj<bool>& other) { return self == other; }, py::is_operator())
		.def(
			"__eq__", [](const Obj<bool>& self, const bool other) { return self.val == other; }, py::is_operator())
		.def(
			"__eq__", [](const bool other, const Obj<bool>& self) { return self.val == other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<bool>& self, const Obj<bool>& other) { return self != other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<bool>& self, const bool other) { return self.val != other; }, py::is_operator())
		.def(
			"__ne__", [](const bool other, const Obj<bool>& self) { return self.val != other; }, py::is_operator())
		.def(
			"__not__", [](const Obj<bool>& self) { return Obj<bool>{!self.val}; }, py::is_operator());

	py::class_<Obj<int>>(m, "Int", R"(An object wrapper for int values.

This is necessary as we use int values as out variables, but primitives in python are immutable.)")
		.def(py::init<int>(), py::arg("val") = 0, "The actual value contained in the object wrapper")
		.def_readonly("val", &Obj<int>::val)
		.def(
			"__int__", [](const Obj<int>& b) { return b.val; }, py::is_operator())
		.def(
			"__eq__", [](const Obj<int>& self, const Obj<int>& other) { return self == other; }, py::is_operator())
		.def(
			"__eq__", [](const Obj<int>& self, const int other) { return self.val == other; }, py::is_operator())
		.def(
			"__eq__", [](const int other, const Obj<int>& self) { return self.val == other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<int>& self, const Obj<int>& other) { return self != other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<int>& self, const int other) { return self.val != other; }, py::is_operator())
		.def(
			"__ne__", [](const int other, const Obj<int>& self) { return self.val != other; }, py::is_operator())
		.def(
			"__neg__", [](const Obj<int>& self) { return Obj<int>{-self.val}; }, py::is_operator());

	py::class_<Obj<float>>(m, "Float", R"(An object wrapper for float values.

This is necessary as we use float values as out variables, but primitives in python are immutable.)")
		.def(py::init<float>(), py::arg("val") = 0.0f, "The actual value contained in the object wrapper")
		.def_readonly("val", &Obj<float>::val)
		.def(
			"__float__", [](const Obj<float>& b) { return b.val; }, py::is_operator())
		.def(
			"__eq__", [](const Obj<float>& self, const Obj<float>& other) { return self == other; }, py::is_operator())
		.def(
			"__eq__", [](const Obj<float>& self, const float other) { return self.val == other; }, py::is_operator())
		.def(
			"__eq__", [](const float other, const Obj<float>& self) { return self.val == other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<float>& self, const Obj<float>& other) { return self != other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<float>& self, const float other) { return self.val != other; }, py::is_operator())
		.def(
			"__ne__", [](const float other, const Obj<float>& self) { return self.val != other; }, py::is_operator())
		.def(
			"__neg__", [](const Obj<float>& self) { return Obj<float>{-self.val}; }, py::is_operator());

	py::class_<Obj<std::string>>(m, "String", R"(An object wrapper for string values.

This is necessary as we use string values as out variables, but primitives in python are immutable.)")
		.def(py::init<std::string>(), py::arg("val") = "", "The actual value contained in the object wrapper")
		.def_readonly("val", &Obj<std::string>::val)
		.def(
			"__string__", [](const Obj<std::string>& b) { return b.val; }, py::is_operator())
		.def(
			"__eq__", [](const Obj<std::string>& self, const Obj<std::string>& other) { return self == other; }, py::is_operator())
		.def(
			"__eq__", [](const Obj<std::string>& self, const std::string other) { return self.val == other; }, py::is_operator())
		.def(
			"__eq__", [](const std::string other, const Obj<std::string>& self) { return self.val == other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<std::string>& self, const Obj<std::string>& other) { return self != other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<std::string>& self, const std::string other) { return self.val != other; }, py::is_operator())
		.def(
			"__ne__", [](const std::string other, const Obj<std::string>& self) { return self.val != other; }, py::is_operator());

	py::class_<Obj<Fove_EyeState>>(m, "EyeStateObj", R"(An object wrapper for EyeState enum values.

This is necessary as we use EyeState enum values as out variables, but primitives in python are immutable.)")
		.def(py::init<Fove_EyeState>(), py::arg_v("val", Fove_EyeState::NotDetected, "EyeState(0)"), "The actual value contained in the object wrapper")
		.def_readonly("val", &Obj<Fove_EyeState>::val)
		.def(
			"__eq__", [](const Obj<Fove_EyeState>& self, const Obj<Fove_EyeState>& other) { return self == other; }, py::is_operator())
		.def(
			"__eq__", [](const Obj<Fove_EyeState>& self, const Fove_EyeState other) { return self.val == other; }, py::is_operator())
		.def(
			"__eq__", [](const Fove_EyeState other, const Obj<Fove_EyeState>& self) { return self.val == other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<Fove_EyeState>& self, const Obj<Fove_EyeState>& other) { return self != other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<Fove_EyeState>& self, const Fove_EyeState other) { return self.val != other; }, py::is_operator())
		.def(
			"__ne__", [](const Fove_EyeState other, const Obj<Fove_EyeState>& self) { return self.val != other; }, py::is_operator());

	py::class_<Obj<Fove_CalibrationState>>(m, "CalibrationStateObj", R"(An object wrapper for CalibrationState enum values.

This is necessary as we use CalibrationState enum values as out variables, but primitives in python are immutable.)")
		.def(py::init<Fove_CalibrationState>(), py::arg_v("val", Fove_CalibrationState::NotStarted, "CalibrationState(0)"), "The actual value contained in the object wrapper")
		.def_readonly("val", &Obj<Fove_CalibrationState>::val)
		.def(
			"__eq__", [](const Obj<Fove_CalibrationState>& self, const Obj<Fove_CalibrationState>& other) { return self == other; }, py::is_operator())
		.def(
			"__eq__", [](const Obj<Fove_CalibrationState>& self, const Fove_CalibrationState other) { return self.val == other; }, py::is_operator())
		.def(
			"__eq__", [](const Fove_CalibrationState other, const Obj<Fove_CalibrationState>& self) { return self.val == other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<Fove_CalibrationState>& self, const Obj<Fove_CalibrationState>& other) { return self != other; }, py::is_operator())
		.def(
			"__ne__", [](const Obj<Fove_CalibrationState>& self, const Fove_CalibrationState other) { return self.val != other; }, py::is_operator())
		.def(
			"__ne__", [](const Fove_CalibrationState other, const Obj<Fove_CalibrationState>& self) { return self.val != other; }, py::is_operator());
}

void bind_CAPIs(py::module& m)
{
	m.def("logText", &fove_logText,
		  R"(Writes some text to the FOVE log something to the FOVE log

\param level What severity level the log will use
\param utf8Text null-terminated text string in UTF8
)");

	// Headset
	// XXX doc changed from CAPI
	// - "A pointer" -> "Fove_Headset" object
	m.def(
		"createHeadset", [](const Fove_ClientCapabilities capabilities, Headset& outHeadset) {
			return fove_createHeadset(capabilities, outHeadset);
		},
		R"(Creates and returns an Fove_Headset object, which is the entry point to the entire API

The result headset should be destroyed using `Headset_destroy` when no longer needed.
\param capabilities The desired capabilities (Gaze, Orientation, Position), for multiple capabilities, use bitwise-or input: Fove_ClientCapabilities::Gaze | Fove_ClientCapabilities::Position
\param outHeadset A Fove_Headset object where the address of the newly created headset will be written upon success
\see Headset_destroy
)");

	m.def(
		"Headset_destroy", [](Headset& headset) {
			return fove_Headset_destroy(headset);
		},
		R"(Frees resources used by a headset object, including memory and sockets

Upon return, this headset pointer, and any research headsets from it, should no longer be used.
\see createHeadset
)");

	m.def(
		"Headset_isHardwareConnected", [](Headset& headset, Obj<bool>& outConnected) {
			return fove_Headset_isHardwareConnected(headset, outConnected);
		},
		R"(Writes out whether an HMD is know to be connected or not

\param outHardwareConnected A pointer to the value to be written
\return Any error detected that might make the out data unreliable
\see createHeadset
)");

	m.def(
		"Headset_isMotionReady", [](Headset& headset, Obj<bool>& outMotionReady) {
			return fove_Headset_isMotionReady(headset, outMotionReady);
		},
		R"(Writes out whether motion tracking hardware has started

\return Any error detected while fetching and writing the data)");

	m.def(
		"Headset_checkSoftwareVersions", [](Headset& headset) {
			return fove_Headset_checkSoftwareVersions(headset);
		},
		R"(Checks whether the client can run against the installed version of the FOVE SDK

This makes a blocking call to the runtime.

\return None if this client is compatible with the currently running service
Connect_RuntimeVersionTooOld if not compatible with the currently running service
Otherwise returns an error representing why this can't be determined
)");

	m.def(
		"Headset_querySoftwareVersions", [](Headset& headset, Python_Versions& outVersions) {
			Fove_Versions versions;
			const auto ret = fove_Headset_querySoftwareVersions(headset, &versions);
			outVersions.clientMajor = versions.clientMajor;
			outVersions.clientMinor = versions.clientMinor;
			outVersions.clientBuild = versions.clientBuild;
			outVersions.clientProtocol = versions.clientProtocol;
			outVersions.clientHash = std::string(versions.clientHash);
			outVersions.runtimeMajor = versions.runtimeMajor;
			outVersions.runtimeMinor = versions.runtimeMinor;
			outVersions.runtimeBuild = versions.runtimeBuild;
			outVersions.runtimeHash = std::string(versions.runtimeHash);
			outVersions.firmware = versions.firmware;
			outVersions.maxFirmware = versions.maxFirmware;
			outVersions.minFirmware = versions.minFirmware;
			outVersions.tooOldHeadsetConnected = versions.tooOldHeadsetConnected;
			return ret;
		},
		R"(Writes out information about the current software versions

Allows you to get detailed information about the client and runtime versions.
Instead of comparing software versions directly, you should simply call
`CheckSoftwareVersions` to ensure that the client and runtime are compatible.
This makes a blocking call to the runtime.
)");

	m.def(
		"Headset_queryLicenses", [](Headset& headset, Fove_ErrorCode& error) -> vector<Python_LicenseInfo> {
			size_t numLicenses = 0;
			error = fove_Headset_queryLicenses(headset, nullptr, &numLicenses);
			if (error != Fove_ErrorCode::None)
				return {};

			unique_ptr<Fove_LicenseInfo[]> licenses;
			if (numLicenses > 0)
			{
				licenses = make_unique<Fove_LicenseInfo[]>(numLicenses);

				error = fove_Headset_queryLicenses(headset, licenses.get(), &numLicenses);
				if (error != Fove_ErrorCode::None)
					return {};
			}

			vector<Python_LicenseInfo> outLicenses;
			outLicenses.resize(numLicenses);
			for (size_t i = 0; i < numLicenses; ++i)
			{
				Python_LicenseInfo& out = outLicenses[i];
				Fove_LicenseInfo& in = licenses[i];

				// Fove::UUID uuid;
				// for (size_t i2 = 0; i2 < 16; ++i2)
				// 	uuid.bytes[i2] = in.uuid[i2];

				// out.uuid = toString(uuid, false);
				out.expirationYear = in.expirationYear;
				out.expirationMonth = in.expirationMonth;
				out.expirationDay = in.expirationDay;
				out.licenseType = std::string(in.licenseType); // Null terminated
				out.licensee = std::string(in.licensee);       // Null terminated
			}

			return outLicenses;
		},
		R"(Returns information about any licenses currently activated

There is the possibility of having more than one license, or none at all, so an array is provided.

This will only return valid, activated, licenses.
As soon as a license expires or is otherwise deactivated, it will no longer be returned from this.

Usually you do not need to call this function directly.
To check if a feature is available, simply use the feature, and see if you get a `License_FeatureAccessDenied` error.
)");

	// Note: this is treated somewhat specially as pybind11 does not support C arrays directly
	m.def(
		"Headset_queryHardwareInfo", [](Headset& headset, Python_HeadsetHardwareInfo& outHardwareInfo) -> Fove_ErrorCode {
			Fove_HeadsetHardwareInfo info;
			const auto ret = fove_Headset_queryHardwareInfo(headset, &info);
			// Note: it is a bug of Headset_queryHardwareInfo() if the C arrays are not null terminated.
			outHardwareInfo.serialNumber = std::string(info.serialNumber);
			outHardwareInfo.manufacturer = std::string(info.manufacturer);
			outHardwareInfo.modelName = std::string(info.modelName);
			return ret;
		},
		R"(Writes out information about the hardware information

Allows you to get serial number, manufacturer, and model name of the headset.
)");

	m.def(
		"Headset_registerCapabilities", [](Headset& headset, Fove_ClientCapabilities caps) {
			return fove_Headset_registerCapabilities(headset, caps);
		},
		R"(Registers a client capability, enabling the required hardware as needed

Usually you provide the required capabilities at the creation of the headset
But you can add and remove capabilities anytime while the object is alive.
\param caps A set of capabilities to register. Registering an existing capability is a no-op
\return #Fove_ErrorCode_None if the capability has been properly registered
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_License_FeatureAccessDenied if your license doesn't offer access to this capability
)");
	
	m.def(
		"Headset_registerPassiveCapabilities", [](Headset& headset, Fove_ClientCapabilities caps) {
			return fove_Headset_registerPassiveCapabilities(headset, caps);
		},
		R"(Registers passive capabilities for this client

	The difference between active capabilties (those registered with `fove_Headset_registerCapabilities`) is that
	passive capabilities are not used to enable hardware or software components. There must be at least one active
	capability registered for the required hardware/software modules to be enabled.

	However, if another app registers the same capability actively, you can use passive capabilities to read the data
	being exported from the service on behalf of another client who has registered the capability actively.

	Basically, this means "if it's on I want it, but I don't want to turn it on myself".

	Within a single client, there's no point to registering a capability passively if it's already registered actively.
	However, this is not an error, and the capability will be registered passively. The two lists are kept totally separate.

	\param caps A set of capabilities to register. Registering an existing capability is a no-op
	\return #Fove_ErrorCode_None if the capability has been properly registered locally\n
	        #Fove_ErrorCode_API_InvalidArgument if the headset object is invalid\n
	        #Fove_ErrorCode_API_NullInPointer if the param pointer is null\n
	        #Fove_ErrorCode_UnknownError if an unexpected internal error occurred\n
	\see    fove_createHeadset
	\see    fove_Headset_unregisterCapabilities
)");

	m.def(
		"Headset_unregisterCapabilities", [](Headset& headset, Fove_ClientCapabilities caps) {
			return fove_Headset_unregisterCapabilities(headset, caps);
		},
		R"(Unregisters passive capabilities previously registered by this client
	Removes passive capabilities previously added by `fove_registerPassiveCapabilities`.

	It has no effect on active capabilities registered with `fove_registerCapabilities` or `fove_createHeadset`.

	\param caps A set of capabilities to unregister. Unregistering an not-existing capability is a no-op
	\return #Fove_ErrorCode_None if the capability has been properly unregistered\n
	        #Fove_ErrorCode_API_InvalidArgument if the headset object is invalid\n
	        #Fove_ErrorCode_UnknownError if an unexpected internal error occurred\n
	\see    fove_createHeadset
	\see    fove_Headset_registerCapabilities
)");

	m.def(
		"Headset_unregisterPassiveCapabilities", [](Headset& headset, Fove_ClientCapabilities caps) {
			return fove_Headset_unregisterPassiveCapabilities(headset, caps);
		},
		R"(Unregisters a client capability previously registered
\param caps A set of capabilities to unregister. Unregistering an not-existing capability is a no-op
\return #Fove_ErrorCode_None if the capability has been properly unregistered
)");

	m.def(
		"Headset_waitForProcessedEyeFrame", [](Headset& headset) {
			return fove_Headset_waitForProcessedEyeFrame(headset);
		},
		R"(Waits for next eye camera frame to be processed

Allows you to sync your eye tracking loop to the actual eye-camera loop.
On each loop, you would first call this blocking function to wait for the next eye frame to be processed,
then update the local cache of eye tracking data using the fetch functions,
and finally get the desired eye tracking data using the getters.

Eye tracking should be enabled by registering the `Fove_ClientCapabilities_EyeTracking` before calling this function.

\return #Fove_ErrorCode_None if the call succeeded
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
)");

	m.def(
		"Headset_fetchEyeTrackingData", [](Headset& headset, Fove_FrameTimestamp* out) {
			return fove_Headset_fetchEyeTrackingData(headset, out);
		},
		R"(Fetch the latest eye tracking data from the runtime service

This function updates a local cache of eye tracking data, which other getters will fetch from.

A cache is used as a means to ensure that multiple getters can be called without a frame update in between.
Everything in the cache is from the same frame, thus you can make sequential queries for data,
and you will get data from the same frame as long as you do not refetch in between.

This function never blocks the thread. If no new data is available, no operation is performed.
The timestamp can be used to know if the data has been updated or not.

Usually, you want to call this function at the beginning of your update loop if your thread is synchronized
with the HMD display. On the other hand, if your thread is synchronized with the eye tracker thread,
you usually want to call it just after `fove_Headset_waitForProcessedEyeFrame`.

Eye tracking should be enabled by registering the `Fove_ClientCapabilities_EyeTracking` before calling this function.

\param outTimestamp A pointer to write the frame timestamp of fetched data. If null, the timestamp is not written.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
\see    fove_Headset_getCombinedGazeDepth
\see    fove_Headset_getCombinedGazeRay
\see    fove_Headset_getEyeballRadius
\see    fove_Headset_getEyeBlinkCount
\see    fove_Headset_getEyeShape
\see    fove_Headset_getEyeState
\see    fove_Headset_getEyeTorsion
\see    fove_Headset_getEyeTrackingCalibrationState
\see    fove_Headset_getEyeTrackingCalibrationStateDetails
\see    fove_Headset_getGazeScreenPosition
\see    fove_Headset_getGazeScreenPositionCombined
\see    fove_Headset_getGazeVector
\see    fove_Headset_getGazeVectorRaw
\see    fove_Headset_getIrisRadius
\see    fove_Headset_getPupilRadius
\see    fove_Headset_getPupilShape
\see    fove_Headset_getUserIOD
\see    fove_Headset_getUserIPD
\see    fove_Headset_hasHmdAdjustmentGuiTimeout
\see    fove_Headset_isEyeBlinking
\see    fove_Headset_isEyeTrackingCalibrated
\see    fove_Headset_isEyeTrackingCalibratedForGlasses
\see    fove_Headset_isEyeTrackingCalibrating
\see    fove_Headset_isHmdAdjustmentGuiVisible
\see    fove_Headset_isUserPresent
\see    fove_Headset_isUserShiftingAttention
\see    fove_Headset_waitForProcessedEyeFrame
)");

	m.def(
		"Headset_fetchEyesImage", [](Headset& headset, Fove_FrameTimestamp* out) {
			return fove_Headset_fetchEyesImage(headset, out);
		},
		R"(Fetch the latest eyes camera image from the runtime service

This function updates a local cache of eyes image, that can be retrieved through `fove_Headset_getEyesImage`.

A cache is used to ensure that multiple calls to `fove_Headset_getEyesImage` return exactly the same data
until we request an explicit data update through the next fetch call.

This function never blocks the thread. If no new data is available, no operation is performed.
The timestamp can be used to know if the data has been updated or not.

Usually, you want to call this function in conjunction with `fove_Headset_fetchEyeTrackingData` either at the beginning
of your update loop of just after `fove_Headset_waitForProcessedEyeFrame` depending on your thread synchronization.

Eyes image capability should be enabled by registering `Fove_ClientCapabilities_EyesImage` before calling this function.

\param outTimestamp A pointer to write the frame timestamp of fetched data. If null, the timestamp is not written.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
\see    fove_Headset_getEyesImage
\see    fove_Headset_fetchEyeTrackingData
\see    fove_Headset_waitForProcessedEyeFrame
)");

	m.def(
		"Headset_getEyeTrackingDataTimestamp", [](Headset& headset, Fove_FrameTimestamp* out) {
			return fove_Headset_getEyeTrackingDataTimestamp(headset, out);
		},
		R"(Writes out the eye frame timestamp of the cached eye tracking data

Basically returns the timestamp returned by the last call to `fove_Headset_fetchEyeTrackingData`.

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param outTimestamp A pointer to write the frame timestamp of the currently cached data.
\return #Fove_ErrorCode_None if the call succeeded
	    #Fove_ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet
	    #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
	    #Fove_ErrorCode_API_NullInPointer if outTimestamp is null
)");

	m.def(
		"Headset_getEyesImageTimestamp", [](Headset& headset, Fove_FrameTimestamp* out) {
			return fove_Headset_getEyesImageTimestamp(headset, out);
		},
		R"(Writes out the eye frame timestamp of the cached eyes image

Basically returns the timestamp returned by the last call to `fove_Headset_fetchEyesImage`.

`Fove_ClientCapabilities_EyesImage` should be registered to use this function.

\param outTimestamp A pointer to write the frame timestamp of the currently cached data.
\return #Fove_ErrorCode_None if the call succeeded
	    #Fove_ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet
	    #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
	    #Fove_ErrorCode_API_NullInPointer if outTimestamp is null
)");

	m.def(
		"Headset_getGazeVector", [](Headset& headset, Fove_Eye eye, Fove_Vec3& out) {
			return fove_Headset_getGazeVector(headset, eye, &out);
		},
		R"(Writes out the gaze vector of an individual eye

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param eye Specify which eye to get the value for
\param outVector  A pointer to the eye gaze vector to write to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if both outVector is `nullptr`
)");

	m.def(
		"Headset_getGazeVectorRaw", [](Headset& headset, Fove_Eye eye, Fove_Vec3& out) {
			return fove_Headset_getGazeVectorRaw(headset, eye, &out);
		},
		R"(Writes out the raw gaze vector of an individual eye

Returns the eye gaze vector without any final smoothing or compensatory processing.
Some processing inherent to the eye tracker logic that can't avoided still happens internally.

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param eye Specify which eye to get the value for
\param outVector  A pointer to the eye raw gaze vector to write to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if both outVector is `nullptr`
)");

	m.def(
		"Headset_getGazeScreenPosition", [](Headset& headset, Fove_Eye eye, Fove_Vec2& out) {
			return fove_Headset_getGazeScreenPosition(headset, eye, &out);
		},
		R"(Writes out the user's 2D gaze position on the screens seen through the HMD's lenses

The use of lenses and distortion correction creates a screen in front of each eye.
This function returns 2D vectors representing where on each eye's screen the user
is looking.
The vectors are normalized in the range [-1, 1] along both X and Y axes such that the
following points are true:

Center: (0, 0)
Bottom-Left: (-1, -1)
Top-Right: (1, 1)

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param eye Specify which eye to get the value for
\param outPos A pointer to the eye gaze point in the HMD's virtual screen space
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if both outPos is `nullptr`
)");

	m.def(
		"Headset_getGazeScreenPositionCombined", [](Headset& headset, Fove_Vec2& out) {
			return fove_Headset_getGazeScreenPositionCombined(headset, &out);
		},
		R"(Writes out the user's 2D gaze position on a virtual screen in front of the user.

This is a 2D equivalent of `fove_Headset_getCombinedGazeRay`, and is perhaps the simplest gaze estimation function.
It returns an X/Y coordinate of where on the screen the user is looking.

While in reality each eye is looking in a different direction at a different [portion of the] screen,
they mostly agree, and this function returns effectively an average to get you a simple X/Y value.

The vector is normalized in the range [-1, 1] along both X and Y axes such that the
following points are true:

Center: (0, 0)
Bottom-Left: (-1, -1)
Top-Right: (1, 1)

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param eye Specify which eye to get the value for
\param outPos A pointer to the eye gaze point in the HMD's virtual screen space
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if both outPos is `nullptr`
)");

	m.def(
		"Headset_getCombinedGazeRay", [](Headset& headset, Fove_Ray& out) {
			return fove_Headset_getCombinedGazeRay(headset, &out);
		},
		R"(Writes out eyes gaze ray resulting from the two eye gazes combined together

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

To get individual eye rays use `fove_Headset_getGazeVectors` instead

\param  outRay  A pointer to the gaze ray struct to write to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `outRay` is `nullptr`
)");

	m.def(
		"Headset_getCombinedGazeDepth", [](Headset& headset, Obj<float>& out) {
			return fove_Headset_getCombinedGazeDepth(headset, out);
		},
		R"(Writes out eyes gaze depth resulting from the two eye gazes combined together

`Fove_ClientCapabilities_GazeDepth` should be registered to use this function.

\param  outDepth  A pointer to the gaze depth variable to write to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `outDepth` is `nullptr`
)");

	m.def(
		"Headset_isUserShiftingAttention", [](Headset& headset, Obj<bool>& out) {
			return fove_Headset_isUserShiftingAttention(headset, out);
		},
		R"(Writes out whether the user is shifting its attention between objects or looking at something specific (fixation or pursuit).

This can be used to ignore eye data during large eye motions when the user is not looking at anything specific.

`Fove_ClientCapabilities_UserAttentionShift` should be registered to use this function.

\param  outIsShiftingAttention A pointer to a output variable to write the user attention shift status to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `outIsShiftingAttention` is `nullptr`
)");

	m.def(
		"Headset_getEyeState", [](Headset& headset, Fove_Eye eye, Obj<Fove_EyeState>& out) {
			return fove_Headset_getEyeState(headset, eye, out);
		},
		R"(Writes out the state of an individual eye

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param eye Specify which eye to get the value for
\param  out A pointer to the output variable to write the eye state to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `outState` is `nullptr`
)");

	m.def(
		"Headset_isEyeBlinking", [](Headset& headset, Fove_Eye eye, Obj<bool>& out) {
			return fove_Headset_isEyeBlinking(headset, eye, out);
		},
		R"( Writes out whether the user is currently performing a blink for the given eye

`Fove_ClientCapabilities_EyeBlink` should be registered to use this function.

\param eye Specify which eye to get the value for
\param  out A pointer to the output variable to write the eye blinking state to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `out` is `nullptr`
)");

	m.def(
		"Headset_getEyeBlinkCount", [](Headset& headset, Fove_Eye eye, Obj<int>& out) {
			return fove_Headset_getEyeBlinkCount(headset, eye, out);
		},
		R"( Writes out the number of blink performed for the given eye since the eye tracking service started

To count the number blinks performed during a given period of time call this function at the
beginning and at the end of the period and make the subtraction of the two values.

`Fove_ClientCapabilities_EyeBlink` should be registered to use this function.

\param eye Specify which eye to get the value for
\param  out A pointer to the output variable to write the blink count to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `out` is `nullptr`
)");

	m.def(
		"Headset_isEyeTrackingEnabled", [](Headset& headset, Obj<bool>& outEyeTrackingEnabled) {
			return fove_Headset_isEyeTrackingEnabled(headset, outEyeTrackingEnabled);
		},
		R"(Writes out whether the eye tracking hardware has started

\param  outEyeTrackingEnabled A pointer to the output variable to write the eye tracking status to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if `outEyeTrackingEnabled` is `nullptr`
)");

	m.def(
		"Headset_isEyeTrackingCalibrated", [](Headset& headset, Obj<bool>& outEyeTrackingCalibrated) {
			return fove_Headset_isEyeTrackingCalibrated(headset, outEyeTrackingCalibrated);
		},
		R"(Writes out whether eye tracking has been calibrated

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param  outEyeTrackingCalibrated A pointer to the output variable to write the eye tracking calibrated status to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if `outEyeTrackingCalibrated` is `nullptr`
)");

	m.def(
		"Headset_isEyeTrackingCalibrating", [](Headset& headset, Obj<bool>& outEyeTrackingCalibrating) {
			return fove_Headset_isEyeTrackingCalibrating(headset, outEyeTrackingCalibrating);
		},
		R"(Writes out whether eye tracking is in the process of performing a calibration

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param  outEyeTrackingCalibrating A pointer to the output variable to write the eye tracking calibrating status to
\return #Fove_ErrorCode_None if the call succeeded
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
	    #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if `outEyeTrackingCalibrating` is `nullptr`
)");

	m.def(
		"Headset_isEyeTrackingCalibratedForGlasses", [](Headset& headset, Obj<bool>& out) {
			return fove_Headset_isEyeTrackingCalibratedForGlasses(headset, out);
		},
		R"(Writes out whether the eye tracking system is currently calibrated for glasses.

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

This basically indicates if the user was wearing glasses during the calibration or not.
This function returns 'Data_Uncalibrated' if the eye tracking system has not been calibrated yet

\param outGlasses A pointer to the variable to be written
\return #Fove_ErrorCode_None if the call succeeded\n
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service\n
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call\n
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet\n
	    #Fove_ErrorCode_Data_Uncalibrated if the eye tracking system is currently uncalibrated\n
		#Fove_ErrorCode_API_NullInPointer if `outGlasses` is `nullptr`
)");

	m.def(
		"Headset_isHmdAdjustmentGuiVisible", [](Headset& headset, Obj<bool>& outVisible) {
			return fove_Headset_isHmdAdjustmentGuiVisible(headset, outVisible);
		},
		R"(Writes out whether or not the GUI that asks the user to adjust their headset is being displayed

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param  outHmdAdjustmentGuiVisible A pointer to the output variable to write the GUI visibility status to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if `outHmdAdjustmentGuiVisible` is `nullptr`
)");

	m.def(
		"Headset_hasHmdAdjustmentGuiTimeout", [](Headset& headset, Obj<bool>& out) {
			return fove_Headset_hasHmdAdjustmentGuiTimeout(headset, out);
		},
		R"(Writes out whether or not the GUI that asks the user to adjust their headset was hidden by timeout

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param  outTimeout A pointer to the output variable to write the timeout status to
\return #Fove_ErrorCode_None if the call succeeded
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
	    #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if `outTimeout` is `nullptr`
)");

	m.def(
		"Headset_isEyeTrackingReady", [](Headset& headset, Obj<bool>& outEyeTrackingReady) {
			return fove_Headset_isEyeTrackingReady(headset, outEyeTrackingReady);
		},
		R"(Writes out whether eye tracking is actively tracking an eye - or eyes

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param  outEyeTrackingReady A pointer to the output variable to write the eye tracking ready status to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if `outEyeTrackingReady` is `nullptr`
)");

	m.def(
		"Headset_isUserPresent", [](Headset& headset, Obj<bool>& out) {
			return fove_Headset_isUserPresent(headset, out);
		},
		R"(Writes out whether the user is wearing the headset or not

When user is not present Eye tracking values shouldn't be used, as invalid.

`Fove_ClientCapabilities_UserPresence` should be registered to use this function.

\param  outUserPresent A pointer to the output variable to write the user presence status to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `outUserPresent` is `nullptr`
)");

	m.def(
		"Headset_getEyesImage", [](Headset& headset, Fove_BitmapImage& outImage) {
			return fove_Headset_getEyesImage(headset, &outImage);
		},
		R"(Returns the eyes camera image

The eyes image is synchronized with and fetched at the same as the gaze
during the call to `fove_Headset_fetchEyeTrackingData`.

The image data buffer is invalidated upon the next call to this function.
`Fove_ClientCapabilities_EyesImage` should be registered to use this function.

\param outImage the raw image data buffer to write the eyes image data to.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
		#Fove_ErrorCode_Data_Unreadable if the data couldn't be read properly from memory
		#Fove_ErrorCode_API_NullInPointer if `outImage` is `nullptr`
)");

	m.def(
		"Headset_getUserIPD", [](Headset& headset, Obj<float>& out) {
			return fove_Headset_getUserIPD(headset, out);
		},
		R"(Returns the user IPD (Inter Pupillary Distance), in meters

`Fove_ClientCapabilities_UserIPD` should be registered to use this function.

\param outIPD A pointer to the output variable to write the user IPD to.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `outIPD` is `nullptr`
)");

	m.def(
		"Headset_getUserIOD", [](Headset& headset, Obj<float>& out) {
			return fove_Headset_getUserIOD(headset, out);
		},
		R"(Returns the user IOD (Inter Occular Distance), in meters

`Fove_ClientCapabilities_UserIOD` should be registered to use this function.

\param outIPD A pointer to the output variable to write the user IPD to.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `outIPD` is `nullptr`
)");

	m.def(
		"Headset_getPupilRadius", [](Headset& headset, Fove_Eye eye, Obj<float>& out) {
			return fove_Headset_getPupilRadius(headset, eye, out);
		},
		R"(Returns the user pupils radius, in meters

`Fove_ClientCapabilities_PupilRadius` should be registered to use this function.

\param eye Specify which eye to get the value for
\param outRadius A pointer to the output variable to write the user pupil radius to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if both `outRadius` is `nullptr`
)");

	m.def(
		"Headset_getIrisRadius", [](Headset& headset, Fove_Eye eye, Obj<float>& out) {
			return fove_Headset_getIrisRadius(headset, eye, out);
		},
		R"(Returns the user iris radius, in meters

`Fove_ClientCapabilities_IrisRadius` should be registered to use this function.

\param eye Specify which eye to get the value for
\param outRadius A pointer to the output variable to write the user iris radius to.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if both `outRadius` is `nullptr`
)");

	m.def(
		"Headset_getEyeballRadius", [](Headset& headset, Fove_Eye eye, Obj<float>& out) {
			return fove_Headset_getEyeballRadius(headset, eye, out);
		},
		R"(Returns the user eyeballs radius, in meters

`Fove_ClientCapabilities_EyeballRadius` should be registered to use this function.

\param eye Specify which eye to get the value for
\param outRadius A pointer to the output variable to write the user eyeball radius to.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if both `outRadius` is `nullptr`
)");

	m.def(
		"Headset_getEyeTorsion", [](Headset& headset, Fove_Eye eye, Obj<float>& out) {
			return fove_Headset_getEyeTorsion(headset, eye, out);
		},
		R"(Returns the user eye torsion, in degrees

`Fove_ClientCapabilities_EyeTorsion` should be registered to use this function.

\param eye Specify which eye to get the value for
\param outAngle A pointer to the output variable to write the user eye torsion to.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if both `outAngle` is `nullptr`
        #Fove_ErrorCode_License_FeatureAccessDenied if the current license is not sufficient for this feature
)");

	m.def(
		"Headset_getEyeShape", [](Headset& headset, Fove_Eye eye, Python_EyeShape& out) {
			return fove_Headset_getEyeShape(headset, eye, out);
		},
		R"(Returns the outline shape of the specified user eye in the Eyes camera image.

`Fove_ClientCapabilities_EyeShape` should be registered to use this function.

\param eye Specify which eye to get the value for
\param outShape A pointer to the EyeShape struct to write eye shape to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if both `outShape` is `nullptr`
        #Fove_ErrorCode_License_FeatureAccessDenied if the current license is not sufficient for this feature
)");

	m.def(
		"Headset_getPupilShape", [](Headset& headset, Fove_Eye eye, Fove_PupilShape& out) {
			return fove_Headset_getPupilShape(headset, eye, &out);
		},
		R"(Returns the pupil ellipse of the specified user eye in the Eyes camera image.

`Fove_ClientCapabilities_PupilShape` should be registered to use this function.

\param eye Specify which eye to get the value for
\param outShape A pointer to the PupilShape struct to write pupil shape to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
        #Fove_ErrorCode_License_FeatureAccessDenied if the current license is not sufficient for this feature
)");

	m.def(
		"Headset_startHmdAdjustmentProcess", [](Headset& headset, bool lazy) {
			return fove_Headset_startHmdAdjustmentProcess(headset, lazy);
		},
		R"(Start the HMD adjustment process. Doing this will display the HMD adjustment GUI.

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param lazy If true, the headset adjustment GUI doesn't show if the headset position is already perfect.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
)");

	m.def(
		"Headset_tickHmdAdjustmentProcess", [](Headset& headset, float deltaTime, bool isVisible, Fove_HmdAdjustmentData& data) {
			return fove_Headset_tickHmdAdjustmentProcess(headset, deltaTime, isVisible, &data);
		},
		R"(Tick the current HMD adjustment process and retrieve data information to render the current HMD positioning state

This function is how the client declares to the FOVE system that it is available to render the HMD adjustment process.
The FOVE system determines which of the available renderers has the highest priority,
and returns to that renderer the information needed to render HMD adjustment process via the outData parameter.
Even while ticking this, you may get no result because either no HMD adjustment is running,
or a HMD adjustment process is running but some other higher priority renderer is doing the rendering.

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

Note that it is perfectly fine not to call this function, in which case the Fove service will automatically render the HMD adjustment process for you.

\param deltaTime The time elapsed since the last rendered frame
\param isVisible Indicate to the FOVE system that GUI for HMD adjustment is being drawn to the screen.
This allows the HMD adjustment renderer to take as much time as it wants to display fade-in/out or other animations
before the HMD adjustment processes is marked as completed by the `IsHmdAdjustmentGUIVisible` function.
\param outData The current HMD positioning information

\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_License_FeatureAccessDenied if a sufficient license is not registered on this machine
        #Fove_ErrorCode_Calibration_OtherRendererPrioritized if another process has currently the priority for rendering the process
)");

	m.def(
		"Headset_startEyeTrackingCalibration", [](Headset& headset, const Fove_CalibrationOptions& options) {
			return fove_Headset_startEyeTrackingCalibration(headset, &options);
		},
		R"(Starts eye tracking calibration

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param options The calibration options to use, or null to use default options
\return #Fove_ErrorCode_None if the call succeeded
	    #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_License_FeatureAccessDenied if any of the enabled options require a license beyond what is active on this machine
)");

	m.def(
		"Headset_stopEyeTrackingCalibration", [](Headset& headset) {
			return fove_Headset_stopEyeTrackingCalibration(headset);
		},
		R"(Stops eye tracking calibration if it's running, does nothing if it's not running.

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
)");

	m.def(
		"Headset_getEyeTrackingCalibrationState", [](Headset& headset, Obj<Fove_CalibrationState>& state) {
			return fove_Headset_getEyeTrackingCalibrationState(headset, state);
		},
		R"(Get the state of the currently running calibration process.

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

\param outCalibrationState A pointer to the calibration state variable to write to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if `outCalibrationState` is `nullptr`
)");

	m.def(
		"Headset_getEyeTrackingCalibrationStateDetails", [](Headset& headset, CalibrationData& data) {
			auto callback = [](const Fove_CalibrationData* nativeData, void* userData) {
				auto* data = reinterpret_cast<CalibrationData*>(userData);
				*data = CalibrationData::FromNative(nativeData);
			};
			return fove_Headset_getEyeTrackingCalibrationStateDetails(headset, callback, &data);
		},
		R"(Get the detailed information about the state of the currently running calibration process.

\param outCalibrationData The calibration current detailed state information

When the calibration process is not running, this returns the final state of the previously run calibration process.
Value is undefined if no calibration process has begun since the service was started.

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

Note that it is perfectly fine not to call this function, in which case the Fove service will automatically render the calibration process for you.

\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
)");

	m.def(
		"Headset_tickEyeTrackingCalibration", [](Headset& headset, float deltaTime, bool isVisible, CalibrationData& data) {
			auto callback = [](const Fove_CalibrationData* nativeData, void* userData) {
				auto* data = reinterpret_cast<CalibrationData*>(userData);
				*data = CalibrationData::FromNative(nativeData);
			};
			return fove_Headset_tickEyeTrackingCalibration(headset, deltaTime, isVisible, callback, &data);
		},
		R"(Tick the current calibration process and retrieve data information to render the current calibration state.

\param deltaTime The time elapsed since the last rendered frame
\param isVisible Indicate to the calibration system that something is being drawn to the screen.
This allows the calibration renderer to take as much time as it wants to display success/failure messages
and animate away before the calibration processes is marked as completed by the `IsEyeTrackingCalibrating` function.
\param outCalibrationData The calibration current state information

This function is how the client declares to the calibration system that is available to render calibration.
The calibration system determines which of the available renderers has the highest priority,
and returns to that render the information needed to render calibration via the outTarget parameter.
Even while ticking this, you may get no result because either no calibration is running,
or a calibration is running but some other higher priority renderer is doing the rendering.

`Fove_ClientCapabilities_EyeTracking` should be registered to use this function.

Note that it is perfectly fine not to call this function, in which case the Fove service will automatically render the calibration process for you.

\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_License_FeatureAccessDenied if a sufficient license is not registered on this machine
        #Fove_ErrorCode_Calibration_OtherRendererPrioritized if another process has currently the priority for rendering calibration process
)");

	m.def(
		"Headset_getGazedObjectId", [](Headset& headset, Obj<int>& id) {
			return fove_Headset_getGazedObjectId(headset, id);
		},
		R"(Get the id of the object gazed by the user.

In order to be detected an object first need to be registered using the `fove_Headset_registerGazableObject` function.
If the user is currently not looking at any specific object the `fove_ObjectIdInvalid` value is returned.
To use this function, you need to register the `Fove_ClientCapabilities_GazedObjectDetection` first.

\param outObjectId A pointer to the output id identifying the object the user is currently looking at
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `outObjectId` is `nullptr`

\see                fove_Headset_updateGazableObject
\see                fove_Headset_removeGazableObject
\see                Fove_GazeConvergenceData
)");

	m.def(
		"Headset_registerGazableObject", [](Headset& headset, const Fove_GazableObject& gazableObj) {
			return fove_Headset_registerGazableObject(headset, &gazableObj);
		},
		R"(Registers an object in the 3D world

Registering 3D world objects allows FOVE software to identify which objects are being gazed at.
We recommend that clients opt-in to this functionality rather than doing it themselves, as our algorithm may improve over time.
Clients of course may do their own detection if they have special needs, such as performance needs, or just want to use their own algorithm.

Use #fove_Headset_registerCameraObject to set the pose of the corresponding camera in the 3D world.

Connection to the service is not required for object registration, thus you can register your world objects at will and not worry about connection or reconnection status.

\param object       A description of the object in the 3D world. Data is copied and no reference is kept to this memory after return.
\return             #Fove_ErrorCode_None if the object is successfully added or updated
                    #Fove_ErrorCode_API_NullInPointer if either parameter is null
                    #Fove_ErrorCode_API_InvalidArgument if the object is invalid in any way (such as an invalid object id)
                    #Fove_ErrorCode_Object_AlreadyRegistered if an object with same id is already registered
\see                fove_Headset_updateGazableObject
\see                fove_Headset_removeGazableObject
\see                Fove_GazeConvergenceData
)");

	m.def(
		"Headset_updateGazableObject", [](Headset& headset, const int id, const Fove_ObjectPose& pose) {
			return fove_Headset_updateGazableObject(headset, id, &pose);
		},
		R"(Update a previously registered 3D object pose.

\param objectId     Id of the object passed to fove_Headset_registerGazableObject()
\param pose         the updated pose of the object
\return             #Fove_ErrorCode_None if the object was in the scene and is now updated
                    #Fove_ErrorCode_API_NullInPointer if either parameter is null
                    #Fove_ErrorCode_API_InvalidArgument if the object was not already registered
\see                fove_Headset_registerCameraObject
\see                fove_Headset_removeGazableObject
)");

	m.def(
		"Headset_removeGazableObject", [](Headset& headset, const int id) {
			return fove_Headset_removeGazableObject(headset, id);
		},
		R"(Removes a previously registered 3D object from the scene.

Because of the asynchronous nature of the FOVE system, this object may still be referenced in future frames for a very short period of time.

\param objectId     Id of the object passed to fove_Headset_registerGazableObject()
\return             #Fove_ErrorCode_None if the object was in the scene and is now removed
                    #Fove_ErrorCode_API_InvalidArgument if the object was not already registered
\see                fove_Headset_registerGazableObject
\see                fove_Headset_updateGazableObject
)");

	m.def(
		"Headset_registerCameraObject", [](Headset& headset, const Fove_CameraObject& cameraObj) {
			return fove_Headset_registerCameraObject(headset, &cameraObj);
		},
		R"(Registers an camera in the 3D world

Registering 3D world objects and camera allows FOVE software to identify which objects are being gazed at.
We recommend that clients opt-in to this functionality rather than doing it themselves, as our algorithm may improve over time.
Clients of course may do their own detection if they have special needs, such as performance needs, or just want to use their own algorithm.

At least 1 camera needs to be registered for automatic object gaze recognition to work. Use the object group mask of the camera to
specify which objects the camera is capturing. The camera view pose determine the gaze raycast direction and position.
The camera view pose should include any and all offsets from position tracking. No transforms from the headset are added in automatically.

Connection to the service is not required for object registration, thus you can register your world objects at will and not worry about connection or reconnection status.

\param camera       A description of the camera. Data is copied and no reference is kept to this memory after return.
\return             #Fove_ErrorCode_None if the camera is successfully added or updated
                    #Fove_ErrorCode_API_NullInPointer if either parameter is null
                    #Fove_ErrorCode_API_InvalidArgument if the object is invalid in any way (such as an invalid object id)
                    #Fove_ErrorCode_Object_AlreadyRegistered if an object with same id is already registered
\see                fove_Headset_updateCameraObject
\see                fove_Headset_removeCameraObject
\see                Fove_GazeConvergenceData
)");

	m.def(
		"Headset_updateCameraObject", [](Headset& headset, const int id, const Fove_ObjectPose& pose) {
			return fove_Headset_updateCameraObject(headset, id, &pose);
		},
		R"(Update the pose of a registered camera

\param cameraId     Id of the camera passed to fove_Headset_registerCameraObject()
\param pose         the updated pose of the camera
\return             #Fove_ErrorCode_None if the object was in the scene and is now removed
                    #Fove_ErrorCode_API_InvalidArgument if the object was not already registered
\see                fove_Headset_registerCameraObject
\see                fove_Headset_removeCameraObject
)");

	m.def(
		"Headset_removeCameraObject", [](Headset& headset, const int id) {
			return fove_Headset_removeCameraObject(headset, id);
		},
		R"(Removes a previously registered camera from the scene.

\param cameraId     Id of the camera passed to fove_Headset_registerCameraObject()
\return             #Fove_ErrorCode_None if the object was in the scene and is now removed
                    #Fove_ErrorCode_API_InvalidArgument is returned if the object was not already registered
\see                fove_Headset_registerCameraObject
\see                fove_Headset_updateCameraObject
)");

	m.def(
		"Headset_tareOrientationSensor", [](Headset& headset) {
			return fove_Headset_tareOrientationSensor(headset);
		},
		R"(Tares the orientation of the headset

Any or both of `Fove_ClientCapabilities_OrientationTracking` and `Fove_ClientCapabilities_PositionTracking`
should be registered to use this function.

\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
)");

	m.def(
		"Headset_isPositionReady", [](Headset& headset, Obj<bool>& outPositionReady) {
			return fove_Headset_isPositionReady(headset, outPositionReady);
		},
		R"( Writes out whether position tracking hardware has started and returns whether it was successful

`Fove_ClientCapabilities_PositionTracking` should be registered to use this function.

\param outPositionReady A pointer to the variable to be written
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if `outPositionReady` is `nullptr`
)");

	m.def(
		"Headset_tarePositionSensors", [](Headset& headset) {
			return fove_Headset_tarePositionSensors(headset);
		},
		R"(Tares the position of the headset

`Fove_ClientCapabilities_PositionTracking` should be registered to use this function.

\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
)");

	m.def(
		"Headset_fetchPoseData", [](Headset& headset, Fove_FrameTimestamp* out) {
			return fove_Headset_fetchPoseData(headset, out);
		},
		R"(Fetch the latest pose data, and cache it locally

This function caches the headset pose for later retrieval by `fove_Headset_getPose`.

This function never blocks the thread. If no new data is available, no operation is performed.
The timestamp can be used to know if the data has been updated or not.

The HMD pose is updated at much higher frame rate than the eye tracking data and there is no equivalent to
`fove_Headset_waitForProcessedEyeFrame` for the pose. For rendering purposes you should use the pose returned by
`fove_Compositor_waitForRenderPose` which provide which provide the best render pose estimate for the current frame.
For other purposes, just fetch the HMD pose once at the beginning of your update loop. This will ensure consistent data
throughout all your update loop code.

\param outTimestamp A pointer to write the frame timestamp of fetched data. If null, the timestamp is not written.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet
        #Fove_ErrorCode_API_NotRegistered if neither position nor orientation tracking is registered
\see    fove_Headset_getPose
\see    fove_Compositor_waitForRenderPose
)");

	m.def(
		"Headset_fetchPositionImage", [](Headset& headset, Fove_FrameTimestamp* out) {
			return fove_Headset_fetchPositionImage(headset, out);
		},
		R"(Fetch the latest position camera image, and cache it locally

This function caches the position camera image for later retrieval by `fove_Headset_getPositionImage`.

This function never blocks the thread. If no new data is available, no operation is performed.
The timestamp can be used to know if the data has been updated or not.

There is no equivalent to `fove_Headset_waitForProcessedEyeFrame` for the position image that allow you to synchronize
with the position image update. We recommend you to fetch the position image only once every beginning of update
loop if needed to ensure consistent data throughout the update loop code.

\param outTimestamp A pointer to the timestamp of fetched data. If null, the timestamp is not written.
\return #Fove_ErrorCode_None if the call succeeded\n
        #Fove_ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet\n
        #Fove_ErrorCode_API_NotRegistered if neither position nor orientation tracking is registered
\see    fove_Headset_getPositionImage
)");

	m.def(
		"Headset_getPoseDataTimestamp", [](Headset& headset, Fove_FrameTimestamp* out) {
			return fove_Headset_getPoseDataTimestamp(headset, out);
		},
		R"(Writes out the frame timestamp of the cached pose data

Basically returns the timestamp returned by the last call to `fove_Headset_fetchPoseData`.

\param outTimestamp A pointer to write the frame timestamp of the currently cached data.
\return #Fove_ErrorCode_None if the call succeeded\n
	    #Fove_ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet\n
	    #Fove_ErrorCode_API_NotRegistered if neither position nor orientation tracking is registered\n
	    #Fove_ErrorCode_API_NullInPointer if outTimestamp is null
)");

	m.def(
		"Headset_getPositionImageTimestamp", [](Headset& headset, Fove_FrameTimestamp* out) {
			return fove_Headset_getPositionImageTimestamp(headset, out);
		},
		R"(Writes out the frame timestamp of the cached position image

Basically returns the timestamp returned by the last call to `fove_Headset_fetchPositionImage`.

\param outTimestamp A pointer to write the frame timestamp of the currently cached data.
\return #Fove_ErrorCode_None if the call succeeded
	    #Fove_ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet
	    #Fove_ErrorCode_API_NotRegistered if position image is not registered
	    #Fove_ErrorCode_API_NullInPointer if outTimestamp is null
)");

	m.def(
		"Headset_getPose", [](Headset& headset, Fove_Pose& outPose) {
			return fove_Headset_getPose(headset, &outPose);
		},
		R"(Writes out the pose of the head-mounted display

\param outPose  A pointer to the variable to be written
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
        #Fove_ErrorCode_Data_Unreliable if the returned data is too unreliable to be used
        #Fove_ErrorCode_Data_LowAccuracy if the returned data is of low accuracy
		#Fove_ErrorCode_API_NullInPointer if `outPose` is `nullptr`
)");

	m.def(
		"Headset_getPositionImage", [](Headset& headset, Fove_BitmapImage& outImage) {
			return fove_Headset_getPositionImage(headset, &outImage);
		},
		R"(Returns the position camera image

The position image is synchronized with and fetched at the same as the pose
during the call to `fove_Headset_fetchPoseData`.

The image data buffer is invalidated upon the next call to this function.
`Fove_ClientCapabilities_PositionImage` should be registered to use this function.

\param outImage the raw image data buffer to write the position image data to.
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
        #Fove_ErrorCode_Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
		#Fove_ErrorCode_Data_Unreadable if the data couldn't be read properly from memory
		#Fove_ErrorCode_API_NullInPointer if `outImage` is `nullptr`
)");

	m.def(
		"Headset_getProjectionMatricesLH", [](Headset& headset, const float zNear, const float zFar, Python_Matrix44& outLeftMat, Python_Matrix44& outRightMat) {
			return fove_Headset_getProjectionMatricesLH(headset, zNear, zFar, outLeftMat, outRightMat);
		},
		R"(Writes out the values of passed-in left-handed 4x4 projection matrices

Writes 4x4 projection matrices for both eyes using near and far planes in a left-handed coordinate
system. Either outLeftMat or outRightMat may be `nullptr` to only write the other matrix, however setting
both to `nullptr` is considered invalid and will return `Fove_ErrorCode::API_NullOutPointersOnly`.
\param zNear        The near plane in float, Range: from 0 to zFar
\param zFar         The far plane in float, Range: from zNear to infinity
\param outLeftMat   A pointer to the matrix you want written
\param outRightMat  A pointer to the matrix you want written
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if both `outLeftMat` and `outRightMat` are `nullptr`
)");

	m.def(
		"Headset_getProjectionMatricesRH", [](Headset& headset, const float zNear, const float zFar, Python_Matrix44& outLeftMat, Python_Matrix44& outRightMat) {
			return fove_Headset_getProjectionMatricesRH(headset, zNear, zFar, outLeftMat, outRightMat);
		},
		R"(Writes out the values of passed-in right-handed 4x4 projection matrices

Writes 4x4 projection matrices for both eyes using near and far planes in a right-handed coordinate
system. Either outLeftMat or outRightMat may be `nullptr` to only write the other matrix, however setting
both to `nullptr` is considered invalid and will return `Fove_ErrorCode::API_NullOutPointersOnly`.
\param zNear        The near plane in float, Range: from 0 to zFar
\param zFar         The far plane in float, Range: from zNear to infinity
\param outLeftMat   A pointer to the matrix you want written
\param outRightMat  A pointer to the matrix you want written
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if both `outLeftMat` and `outRightMat` are `nullptr`
)");

	m.def(
		"Headset_getRawProjectionValues", [](Headset& headset, Fove_ProjectionParams& outLeft, Fove_ProjectionParams& outRight) {
			return fove_Headset_getRawProjectionValues(headset, &outLeft, &outRight);
		},
		R"(Writes out values for the view frustum of the specified eye at 1 unit away.

Writes out values for the view frustum of the specified eye at 1 unit away. Please multiply them by zNear to
convert to your correct frustum near-plane. Either outLeft or outRight may be `nullptr` to only write the
other struct, however setting both to `nullptr` is considered and error and the function will return
`Fove_ErrorCode::API_NullOutPointersOnly`.
\param outLeft  A pointer to the struct describing the left camera projection parameters
\param outRight A pointer to the struct describing the right camera projection parameters
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if both `outLeft` and `outRight` are `nullptr`
)");

	m.def(
		"Headset_getEyeToHeadMatrices", [](Headset& headset, Python_Matrix44& outLeft, Python_Matrix44& outRight) {
			return fove_Headset_getEyeToHeadMatrices(headset, outLeft, outRight);
		},
		R"(Writes out the matrices to convert from eye- to head-space coordinates

This is simply a translation matrix that returns +/- IOD/2
\param outLeft   A pointer to the matrix where left-eye transform data will be written
\param outRight  A pointer to the matrix where right-eye transform data will be written
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if both `outLeft` and `outRight` are `nullptr`
)");

	m.def(
		"Headset_getRenderIOD", [](Headset& headset, Obj<float>& outIOD) {
			return fove_Headset_getRenderIOD(headset, outIOD);
		},
		R"(Interocular distance, returned in meters

This may or may not reflect the actual IOD of the user (see getUserIOD),
but is the value used by the rendering system for the distance to split the left/right
cameras for stereoscopic rendering.
We recommend calling this each frame when doing stereoscopic rendering.

\param outIOD A pointer to the render IOD variable to write to
\return #Fove_ErrorCode_None if the call succeeded
        #Fove_ErrorCode_Connect_NotConnected if not connected to the service
        #Fove_ErrorCode_Data_NoUpdate if no valid data has been returned by the service yet
		#Fove_ErrorCode_API_NullInPointer if `outIOD` is `nullptr`
)");

	m.def(
		"Headset_createProfile", [](Headset& headset, const std::string& profileName) {
			return fove_Headset_createProfile(headset, profileName.c_str());
		},
		R"(Creates a new profile

The FOVE system keeps a set of profiles so that different users on the same system can store data, such as calibrations, separately.
Profiles persist to disk and survive restart.
Third party applications can control the profile system and store data within it.

This function creates a new profile, but does not add any data or switch to it.
\param newName Null-terminated UTF-8 unique name of the profile to create
\return #Fove_ErrorCode_None if the profile was successfully created
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_Profile_InvalidName if newName was invalid
	    #Fove_ErrorCode_Profile_NotAvailable if the name is already taken
	    #Fove_ErrorCode_API_NullInPointer if newName is null
\see fove_Headset_renameProfile
\see fove_Headset_deleteProfile
\see fove_Headset_listProfiles
\see fove_Headset_setCurrentProfile
\see fove_Headset_queryCurrentProfile
\see fove_Headset_queryProfileDataPath)");

	m.def(
		"Headset_renameProfile", [](Headset& headset, const std::string& oldName, const std::string& newName) {
			return fove_Headset_renameProfile(headset, oldName.c_str(), newName.c_str());
		},
		R"(Renames an existing profile

This function renames an existing profile. This works on the current profile as well.
\param oldName Null-terminated UTF-8 name of the profile to be renamed
\param newName Null-terminated UTF-8 unique new name of the profile
\return #Fove_ErrorCode_None if the profile was successfully renamed
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_Profile_DoesntExist if the requested profile at oldName doesn't exist
	    #Fove_ErrorCode_Profile_NotAvailable If the new named is already taken
	    #Fove_ErrorCode_API_InvalidArgument If the old name and new name are the same
	    #Fove_ErrorCode_API_NullInPointer if oldName or newName is null
\see fove_Headset_createProfile
\see fove_Headset_deleteProfile
\see fove_Headset_listProfiles
\see fove_Headset_setCurrentProfile
\see fove_Headset_queryCurrentProfile
\see fove_Headset_queryProfileDataPath)");

	m.def(
		"Headset_deleteProfile", [](Headset& headset, const std::string& profileName) {
			return fove_Headset_deleteProfile(headset, profileName.c_str());
		},
		R"(Deletes an existing profile

This function deletes an existing profile.

If the deleted profile is the current profile, then no current profile is set after this returns.
In such a case, it is undefined whether any existing profile data loaded into memory may be kept around.

\param profileName Null-terminated UTF-8 name of the profile to be deleted
\return #Fove_ErrorCode_None if the profile was successfully deleted
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_Profile_DoesntExist if the requested profile at profileName doesn't exist
	    #Fove_ErrorCode_API_NullInPointer if profileName is null
\see fove_Headset_createProfile
\see fove_Headset_renameProfile
\see fove_Headset_listProfiles
\see fove_Headset_setCurrentProfile
\see fove_Headset_queryCurrentProfile
\see fove_Headset_queryProfileDataPath)");

	m.def(
		"Headset_listProfiles", [](Headset& headset, Fove_ErrorCode& err) -> std::vector<std::string> {
			std::vector<std::string> ret;
			auto callback = [](const char* val, void* data) {
				auto vectorPtr = reinterpret_cast<std::vector<std::string>*>(data);
				vectorPtr->push_back(val);
			};
			err = fove_Headset_listProfiles(headset, callback, &ret);
			return ret;
		},
		R"(Lists all existing profiles

\param outProfileNames The list of existing profile names
\return #Fove_ErrorCode_None if the profile names were successfully listed
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_API_NullInPointer if callback is null
\see fove_Headset_createProfile
\see fove_Headset_renameProfile
\see fove_Headset_deleteProfile
\see fove_Headset_setCurrentProfile
\see fove_Headset_queryCurrentProfile
\see fove_Headset_queryProfileDataPath)");

	m.def(
		"Headset_setCurrentProfile", [](Headset& headset, const std::string& profileName) {
			return fove_Headset_setCurrentProfile(headset, profileName.c_str());
		},
		R"(Sets the current profile

When changing profile, the FOVE system will load up data, such as calibration data, if it is available.
If loading a profile with no calibration data, whether or not the FOVE system keeps old data loaded into memory is undefined.

Please note that no-ops are OK but you should check for #Fove_ErrorCode_Profile_NotAvailable.

\param profileName Name of the profile to make current, in UTF-8
\return #Fove_ErrorCode_None if the profile was successfully set as the current profile
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_Profile_DoesntExist if there is no such profile
	    #Fove_ErrorCode_Profile_NotAvailable if the requested profile is the current profile
	    #Fove_ErrorCode_API_NullInPointer if profileName is null
\see fove_Headset_createProfile
\see fove_Headset_renameProfile
\see fove_Headset_deleteProfile
\see fove_Headset_listProfiles
\see fove_Headset_queryCurrentProfile
\see fove_Headset_queryProfileDataPath)");

	m.def(
		"Headset_queryCurrentProfile", [](Headset& headset, Obj<std::string>& profileName) {
			auto callback = [](const char* val, void* data) {
				auto valuePtr = reinterpret_cast<std::string*>(data);
				*valuePtr = val;
			};
			return fove_Headset_queryCurrentProfile(headset, callback, &profileName.val);
		},
		R"(Gets the current profile

\param profileName The name of the current profile
\return #Fove_ErrorCode_None if the profile name was successfully retrieved
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_API_NullInPointer if callback is null
\see fove_Headset_createProfile
\see fove_Headset_renameProfile
\see fove_Headset_deleteProfile
\see fove_Headset_listProfiles
\see fove_Headset_setCurrentProfile
\see fove_Headset_queryProfileDataPath)");

	m.def(
		"Headset_queryProfileDataPath", [](Headset& headset, const std::string& profileName, Obj<std::string>& dataPath) {
			auto callback = [](const char* val, void* data) {
				auto valuePtr = reinterpret_cast<std::string*>(data);
				*valuePtr = val;
			};
			return fove_Headset_queryProfileDataPath(headset, profileName.c_str(), callback, &dataPath.val);
		},
		R"(Gets the data folder for a given profile

Allows you to retrieve a filesytem directory where third party apps can write data associated with this profile. This directory will be created before return.

Since multiple applications may write stuff to a profile, please prefix any files you create with something unique to your application.

There are no special protections on profile data, and it may be accessible to any other app on the system. Do not write sensitive data here.

This is intended for simple uses. For advanced uses that have security concerns, or want to sync to a server, etc,
third party applications are encouraged to use their own separate data store keyed by profile name.
They will need to test for profile name changes and deletions manually in that case.

\param dataPath The data folder for the given profile name
\param profileName A null-terminated UTF-8 string with the name of the profile to be queried, or an empty string if no profile is set
\return #Fove_ErrorCode_None if the profile was successfully deleted
	    #Fove_ErrorCode_Profile_DoesntExist if there is no such profile
	    #Fove_ErrorCode_Connect_NotConnected if not connected to the service
	    #Fove_ErrorCode_API_NullInPointer if profileName or callback is null
\see fove_Headset_createProfile
\see fove_Headset_renameProfile
\see fove_Headset_deleteProfile
\see fove_Headset_listProfiles
\see fove_Headset_setCurrentProfile
\see fove_Headset_queryCurrentProfile)");

	m.def(
		"Headset_hasAccessToFeature", [](Headset& headset, const std::string& featureName, Obj<bool>& hasAccess) {
			return fove_Headset_hasAccessToFeature(headset, featureName.c_str(), &hasAccess.val);
		},
		R"(Returns whether the Headset has access to the given feature.

If the provided feature name doesn't exist, then `false` and `#Fove_ErrorCode_None` are returned.

\param featureName A null-terminated UTF-8 string with the name of the feature to query
\param hasAccess Output variable set to true if the headset can access the given feature
\return #Fove_ErrorCode_None if the call succeeded
	    #Fove_ErrorCode_API_NullInPointer if inFeatureName is null
	    #Fove_ErrorCode_API_NullOutPointersOnly if outHasAccess is null
)");

	m.def(
		"Headset_activateLicense", [](Headset& headset, const std::string& licenseKey) {
			return fove_Headset_activateLicense(headset, licenseKey.c_str());
		},
		R"(Returns whether the license is activated successfully

\param licenseKey
\return #Fove_ErrorCode_None if the activation succeeded\n
		#Fove_ErrorCode_UnknownError if the activation failed
)");

	m.def(
		"Headset_deactivateLicense", [](Headset& headset, const std::string& licenseData) {
			return fove_Headset_deactivateLicense(headset, licenseData.c_str());
		},
		R"(Returns whether the license is deactivated successfully

\param licenseData The license information used for deactivation, can be empty or a guid or a license key
\return #Fove_ErrorCode_None if the deactivation succeeded\n
		#Fove_ErrorCode_UnknownError if the deactivation failed
)");

	// Compositor
	m.def(
		"Headset_createCompositor", [](Headset& headset, Compositor& outCompositor) {
			return fove_Headset_createCompositor(headset, outCompositor);
		},
		R"(Returns a compositor interface from the given headset

Each call to this function creates a new object. The object should be destroyed with Compositor_destroy
It is fine to call this function multiple times with the same headset, the same pointer will be returned.
It is ok for the compositor to outlive the headset passed in.
\see Compositor_destroy)");

	m.def(
		"Compositor_destroy", [](Compositor& compositor) {
			return fove_Compositor_destroy(compositor);
		},
		R"(Frees resources used by the compositor object, including memory and sockets

Upon return, this compositor pointer should no longer be used.
\see Headset_createCompositor)");

	m.def(
		"Compositor_createLayer", [](Compositor& compositor, const Fove_CompositorLayerCreateInfo& layerInfo, Fove_CompositorLayer& outLayer) {
			return fove_Compositor_createLayer(compositor, &layerInfo, &outLayer);
		},
		R"(Create a layer for this client.

This function create a layer upon which frames may be submitted to the compositor by this client.

A connection to the compositor must exists for this to pass.
This means you need to wait for Compositor_isReady before calling this function.
However, if connection to the compositor is lost and regained, this layer will persist.
For this reason, you should not recreate your layers upon reconnection, simply create them once.

There is no way to delete a layer once created, other than to destroy the Fove_Compositor object.
This is a feature we would like to add in the future.

\param layerInfo The settings for the layer to be created
\param outLayer A struct where the defaults of the newly created layer will be written
\see Compositor_submit)");

	m.def(
		"Compositor_submit", [](Compositor& compositor, const Fove_CompositorLayerSubmitInfo& submitInfo, const std::size_t layerCount) {
			return fove_Compositor_submit(compositor, &submitInfo, layerCount);
		},
		R"(Submit a frame to the compositor

This function takes the feed from your game engine to the compositor for output.
\param submitInfo   An array of layerCount Fove_LayerSubmitInfo structs, each of which provides texture data for a unique layer
\param layerCount   The number of layers you are submitting
\see Compositor_submit)");

	m.def(
		"Compositor_waitForRenderPose", [](Compositor& compositor, Fove_Pose& outPose) {
			return fove_Compositor_waitForRenderPose(compositor, &outPose);
		},
		R"(Wait for the next pose to use for rendering purposes

All compositor clients should use this function as the sole means of limiting their frame rate.
This allows the client to render at the correct frame rate for the HMD display and with the most adequate HMD pose.
Upon this function returning, the client should proceed directly to rendering, to reduce the chance of missing the frame.

If outPose is not null, this function returns the pose that should be use to render the current frame.
This pose can also be get later using the `fove_Compositor_getLastRenderPose` function.

In general, a client's main loop should look like:
{
	Update();                            // Run AI, physics, etc, for the next frame
	compositor.WaitForRenderPose(&pose); // Wait for the next frame, and get the pose
	Draw(pose);                          // Render the scene using the new pose
}

\param outPose The latest pose of the headset.
\see fove_Compositor_getLastRenderPose
	})");

	m.def(
		"Compositor_getLastRenderPose", [](Compositor& compositor, Fove_Pose& outPose) {
			return fove_Compositor_getLastRenderPose(compositor, &outPose);
		},
		R"(Get the last cached pose for rendering purposes)");

	m.def(
		"Compositor_isReady", [](Compositor& compositor, Obj<bool>& outIsReady) {
			return fove_Compositor_isReady(compositor, outIsReady);
		},
		R"(Returns true if we are connected to a running compositor and ready to submit frames for compositing)");

	m.def(
		"Compositor_queryAdapterId", [](Compositor& compositor, Fove_AdapterId& outAdapterId) {
			return fove_Compositor_queryAdapterId(compositor, &outAdapterId);
		},
		R"(Returns the ID of the GPU currently attached to the headset.

For systems with multiple GPUs, submitted textures to the compositor must from the same GPU that the compositor is using
)");

	m.def(
		"Config_getValue_bool", [](const char* key, Obj<bool>& outValue) {
			return fove_Config_getValue_bool(key, &outValue.val);
		},
		R"(Get the value of the provided key from the FOVE config

\param key The key name of the value to retrieve, null-terminated and in UTF-8
\param outValue The value associated to the key if found.
\return #Fove_ErrorCode_None if the value was successfully retrieved
	    #Fove_ErrorCode_API_NullInPointer if key or outValue is null
	    #Fove_ErrorCode_Config_DoesntExist if the queried key doesn't exist
	    #Fove_ErrorCode_Config_TypeMismatch if the key exists but its value type is not a boolean)");

	m.def(
		"Config_getValue_int", [](const char* key, Obj<int>& outValue) {
			return fove_Config_getValue_int(key, &outValue.val);
		},
		R"(Get the value of the provided key from the FOVE config

\param key The key name of the value to retrieve, null-terminated and in UTF-8
\param outValue The value associated to the key if found.
\return #Fove_ErrorCode_None if the value was successfully retrieved
	    #Fove_ErrorCode_API_NullInPointer if key or outValue is null
	    #Fove_ErrorCode_Config_DoesntExist if the queried key doesn't exist
	    #Fove_ErrorCode_Config_TypeMismatch if the key exists but its value type is not an int)");

	m.def(
		"Config_getValue_float", [](const char* key, Obj<float>& outValue) {
			return fove_Config_getValue_float(key, &outValue.val);
		},
		R"(Get the value of the provided key from the FOVE config

\param key The key name of the value to retrieve, null-terminated and in UTF-8
\param outValue The value associated to the key if found.
\return #Fove_ErrorCode_None if the value was successfully retrieved
	    #Fove_ErrorCode_API_NullInPointer if key or outValue is null
	    #Fove_ErrorCode_Config_DoesntExist if the queried key doesn't exist
	    #Fove_ErrorCode_Config_TypeMismatch if the key exists but its value type is not an float)");

	m.def(
		"Config_getValue_string", [](const char* key, Obj<std::string>& outValue) {
			auto callback = [](const char* val, void* data) {
				auto valuePtr = reinterpret_cast<std::string*>(data);
				*valuePtr = val;
			};
			return fove_Config_getValue_string(key, callback, &outValue.val);
		},
		R"(Get the value of the provided key from the FOVE config

\param key The key name of the value to retrieve, null-terminated and in UTF-8
\param outValue The value associated to the key if found.
\return #Fove_ErrorCode_None if the value was successfully retrieved
	    #Fove_ErrorCode_API_NullInPointer if key or outValue is null
	    #Fove_ErrorCode_Config_DoesntExist if the queried key doesn't exist
	    #Fove_ErrorCode_Config_TypeMismatch if the key exists but its value type is not an float
	    #Fove_ErrorCode_System_AccessDenied if the config file is not writable
	    #Fove_ErrorCode_System_UnknownError if any other system error happened with the config file)");

	m.def(
		"Config_setValue_bool", &fove_Config_setValue_bool,
		R"(Set the value of the provided key to the FOVE config

\param key The key name of the value to set, null-terminated and in UTF-8
\param value The new value to set as the key value.
\return #Fove_ErrorCode_None if the value was successfully set
	    #Fove_ErrorCode_API_NullInPointer if key is null
	    #Fove_ErrorCode_Config_DoesntExist if the provided key doesn't exist
	    #Fove_ErrorCode_Config_TypeMismatch if the key exists but its value type is not a boolean
	    #Fove_ErrorCode_System_AccessDenied if the config file is not writable
	    #Fove_ErrorCode_System_UnknownError if any other system error happened with the config file)");

	m.def(
		"Config_setValue_int", &fove_Config_setValue_int,
		R"(Set the value of the provided key to the FOVE config

\param key The key name of the value to set, null-terminated and in UTF-8
\param value The new value to set as the key value.
\return #Fove_ErrorCode_None if the value was successfully set
	    #Fove_ErrorCode_API_NullInPointer if key is null
	    #Fove_ErrorCode_Config_DoesntExist if the provided key doesn't exist
	    #Fove_ErrorCode_Config_TypeMismatch if the key exists but its value type is not an int
	    #Fove_ErrorCode_System_AccessDenied if the config file is not writable
	    #Fove_ErrorCode_System_UnknownError if any other system error happened with the config file)");

	m.def(
		"Config_setValue_float", &fove_Config_setValue_float,
		R"(Set the value of the provided key to the FOVE config

\param key The key name of the value to set, null-terminated and in UTF-8
\param value The new value to set as the key value.
\return #Fove_ErrorCode_None if the value was successfully set
	    #Fove_ErrorCode_API_NullInPointer if key is null
	    #Fove_ErrorCode_Config_DoesntExist if the provided key doesn't exist
	    #Fove_ErrorCode_Config_TypeMismatch if the key exists but its value type is not a float
	    #Fove_ErrorCode_System_AccessDenied if the config file is not writable
	    #Fove_ErrorCode_System_UnknownError if any other system error happened with the config file)");

	m.def(
		"Config_setValue_string", &fove_Config_setValue_string,
		R"(Set the value of the provided key to the FOVE config

\param key The key name of the value to set, null-terminated and in UTF-8
\param value The new value to set as the key value.
\return #Fove_ErrorCode_None if the value was successfully set
	    #Fove_ErrorCode_API_NullInPointer if key is null
	    #Fove_ErrorCode_Config_DoesntExist if the provided key doesn't exist
	    #Fove_ErrorCode_Config_TypeMismatch if the key exists but its value type is not a string
	    #Fove_ErrorCode_System_AccessDenied if the config file is not writable
	    #Fove_ErrorCode_System_UnknownError if any other system error happened with the config file)");

	m.def(
		"Config_clearValue", &fove_Config_clearValue,
		R"(Reset the value of the provided key to its default value

\param key The key name of the value to reset, null-terminated and in UTF-8
\param value The new value to set as the key value.
	\return #Fove_ErrorCode_None if the value was successfully reset
	        #Fove_ErrorCode_API_NullInPointer if key is null
	        #Fove_ErrorCode_Config_DoesntExist if the provided key doesn't exist)");
}

} // namespace FovePython
