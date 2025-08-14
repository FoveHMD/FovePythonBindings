# @package fove.headset
#
# This module provides a set of higher-level APIs that wraps the FOVE C API.
# Internally, it uses a lower level API in `fove.capi` namespace,
# which we do not document at the moment.
#
# - @ref fove.headset.Headset\
# - @ref fove.headset.Compositor
#
# @file fove/headset.py
#
# This file implements the `fove.headset` module

# Note on Doxygen:
# - remove ../../Python/src/__init_.py when generating docs,
#   otherwise Doxygen would think `src` is a package.
#   (It is, but we do not want to expose it to the doc.)
#
# Launch: with the directory fove/ in PYTHONPATH:
# $ python -m fove.headset

# # For Python 3.11+, just from typing import Self
from __future__ import annotations  # python 3.7+ only

from typing import Generic, List, Optional, Tuple, Type, TypeVar
from types import TracebackType
import logging
import time

logger = logging.getLogger(__name__)

# FIXME
from . import capi

T = TypeVar("T")


# Note: This sort of leaks capi types to clients,
# but python2 does not come with class Enum by default
# List of capabilities usable by clients
#
# Most features require registering for the relevant capability.
# If a client queries data related to a capability it has not registered API_NotRegistered will be returned.
# After a new capability registration the Data_NoUpdate error may be returned for a few frames while
# the service is bootstrapping the new capability.
#
# This enum is designed to be used as a flag set, so items may be binary logic operators like |.
#
# The FOVE runtime will keep any given set of hardware/software running so long as one client is registering a capability.
#
# The registration of a capability does not necessarily mean that the capability is running.
# For example, if no position tracking camera is attached, no position
# tracking will occur regardless of how many clients registered for it.


# FIXME remove
# class ClientCapabilities(object):
#     # No capabilities requested
#     None_ = capi.ClientCapabilities.None_
#     # Enables headset orientation tracking
#     OrientationTracking = capi.ClientCapabilities.OrientationTracking
#     # Enables headset position tracking
#     PositionTracking = capi.ClientCapabilities.PositionTracking
#     # Enables Position camera image transfer from the runtime service to the
#     # client
#     PositionImage = capi.ClientCapabilities.PositionImage
#     # Enables headset eye tracking
#     EyeTracking = capi.ClientCapabilities.EyeTracking
#     # Enables gaze depth computation
#     GazeDepth = capi.ClientCapabilities.GazeDepth
#     # Enables user presence detection
#     UserPresence = capi.ClientCapabilities.UserPresence
#     # Enables user attention shift computation
#     UserAttentionShift = capi.ClientCapabilities.UserAttentionShift
#     # Enables the calculation of the user IOD
#     UserIOD = capi.ClientCapabilities.UserIOD
#     # Enables the calculation of the user IPD
#     UserIPD = capi.ClientCapabilities.UserIPD
#     # Enables the calculation of the user eye torsion
#     EyeTorsion = capi.ClientCapabilities.EyeTorsion
#     # Enables the detection of the eyes shape
#     EyeShape = capi.ClientCapabilities.EyeShape
#     # Enables Eye camera image transfer from the runtime service to the client
#     EyesImage = capi.ClientCapabilities.EyesImage
#     # Enables the calculation of the user eyeball radius
#     EyeballRadius = capi.ClientCapabilities.EyeballRadius
#     # Enables the calculation of the user iris radius
#     IrisRadius = capi.ClientCapabilities.IrisRadius
#     # Enables the calculation of the user pupil radius
#     PupilRadius = capi.ClientCapabilities.PupilRadius
#     # Enables gazed object detection based on registered gazable objects
#     GazedObjectDetection = capi.ClientCapabilities.GazedObjectDetection
#     # Give you direct access to the HMD screen and disable the Fove compositor
#     DirectScreenAccess = capi.ClientCapabilities.DirectScreenAccess
#     # Enables the detection of the pupil shape
#     PupilShape = capi.ClientCapabilities.PupilShape
#     # Enables eye blink detection and counting
#     EyeBlink = capi.ClientCapabilities.EyeBlink


# Class containing a FOVE API call result value as well as the operation
# error code status
class Result(Generic[T]):
    # Create a new Result object from a value and error code
    def __init__(self, value: T, error=capi.ErrorCode.None_) -> None:
        self._value = value
        self._error = error

    def __bool__(self) -> bool:
        return self.isAcceptable()

    def __str__(self) -> str:
        return (self._value if self.succeeded() else self._error).__str__()

    def isAcceptable(self) -> bool:
        return (
            self.error == capi.ErrorCode.None_
            or self.error == capi.ErrorCode.Data_LowAccuracy
            or self.error == capi.ErrorCode.Data_Unreliable
        )

    # True if value contains valid data
    def isValid(self) -> bool:
        return (
            self.error == capi.ErrorCode.None_
            or self.error == capi.ErrorCode.Data_LowAccuracy
        )

    # True if value contains valid and accurate data
    def isReliable(self) -> bool:
        return self.error == capi.ErrorCode.None_

    # True if the API call succedeed
    def succeeded(self) -> bool:
        return self.error == capi.ErrorCode.None_

    # The error code returned by the FOVE API call
    @property
    def error(self) -> capi.ErrorCode:
        return self._error

    # The value return by the FOVE API call
    @property
    def value(self) -> T:
        return self._value


# Class that manages accesses to headsets
#
# All Headset-related API requests will be done through an instance of this class.
#
# The class provides `Headset.__enter__` and `Headset.__exit__` methods
# that do relevant resource managements, so the typical use of this class
# would be as follows:
#
# @code
# with Headset(ClientCapabilities.EyeTracking + ClientCapabilities.OrientationTracking) as headset:
#     # use headset
#     pass
# @endcode
class Headset(object):
    Caps = capi.ClientCapabilities
    ET_CAPS = (
        Caps.None_
        + Caps.EyeTracking
        + Caps.GazeDepth
        + Caps.UserPresence
        + Caps.UserAttentionShift
        + Caps.UserIOD
        + Caps.UserIPD
        + Caps.EyeTorsion
        + Caps.EyeShape
        + Caps.PupilShape
        + Caps.EyesImage
        + Caps.EyeballRadius
        + Caps.IrisRadius
        + Caps.PupilRadius
        + Caps.GazedObjectDetection
    )

    POS_CAPS = (
        Caps.None_
        + Caps.OrientationTracking
        + Caps.PositionTracking
        + Caps.PositionImage
    )

    # Defines a headset with the given capabilities
    #
    # This does not automatically create or connect to the headset.
    # For that, the user has to call `Headset.__enter__` (and call `Headset.__exit__`
    # when the headset is no longer needed) but consider using the `with` statement
    # instead of manually calling them.
    #
    # @param capabilities The desired capabilities (EyeTrackign, OrientationTracking, etc.)
    # For multiple capabilities, use arithmetic operators as in:
    # `ClientCapabilities.EyeTracking + ClientCapabilities.PositionTracking`.
    # @see Headset.__enter__
    def __init__(self, capabilities: capi.ClientCapabilities) -> None:
        # Capabilities that the user intends to use
        self._caps: capi.ClientCapabilities = capabilities
        # A Fove_Headset object where the address of the newly created headset
        # will be written upon success
        self._headset: capi.Fove_Headset = capi.Fove_Headset()

    # Creates and tries to connect to the headset.
    #
    # The result headset should be destroyed using `Headset.__exit__` when no longer needed,
    # but consider using the `with` statement instead of manually calling it.
    #
    # @return A Headset object where the handle to the newly created headset is written upon success
    # @exception RuntimeError When failed to create a headset
    # @see Headset.__exit__
    def __enter__(self) -> Headset:
        logger.debug("Creating headset: {}".format(self._caps))
        err = capi.createHeadset(self._caps, self._headset)
        if err != capi.ErrorCode.None_:
            raise RuntimeError("Failed to create headset: {}".format(err))
        return self

    # Frees resources used by a headset object, including memory and sockets
    #
    # Upon return, this headset instance should no longer be used.
    # @see Headset.__enter__
    def __exit__(
        self,
        _e_type: Optional[Type[BaseException]],
        _e_val: Optional[Type[BaseException]],
        _traceback: Optional[TracebackType],
    ) -> bool:
        if _e_type is not None:
            logger.error("Headset: exception raised: {}".format(_e_val))
        if self._headset is not None:
            capi.Headset_destroy(self._headset)
            logger.debug("Destroyed headset")
        return True if _e_type is None else False

    # Checks whether the headset is connected or not.
    #
    # @return Whether an HMD is known to be connected, and the call success status
    # @see Headset.createHeadset
    def isHardwareConnected(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_isHardwareConnected(self._headset, b)
        return Result(b.val, err)

    # Checks if motion tracking hardware has started
    #
    # @return Whether motion tracking hardware has started, and the call success status
    def isMotionReady(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_isMotionReady(self._headset, b)
        return Result(b.val, err)

    # Checks whether the client can run against the installed version of the FOVE SDK.
    #
    # This makes a blocking call to the runtime.
    #
    # @return capi.ErrorCode.None_ if this client is compatible with the installed FOVE service,
    # or an error indicating the problem otherwise
    def checkSoftwareVersions(self) -> Result[None]:
        err = capi.Headset_checkSoftwareVersions(self._headset)
        return Result(None, err)

    # Gets the information about the current software versions.
    #
    # Allows you to get detailed information about the client and runtime versions.
    # Instead of comparing software versions directly, you should simply call
    # `Headset.checkSoftwareVersions` to ensure that the client and runtime are compatible.
    #
    # This makes a blocking call to the runtime.
    #
    # @return information about the current software versions, and the call success status
    def querySoftwareVersions(self) -> Result[capi.Versions]:
        versions = capi.Versions()
        err = capi.Headset_querySoftwareVersions(self._headset, versions)
        return Result(versions, err)

    # Returns information about any licenses currently activated
    #
    # There is the possibility of having more than one license, or none at all, so a list is returned.
    #
    # This will only return valid, activated, licenses.
    # As soon as a license expires or is otherwise deactivated, it will no longer be returned from this.
    #
    # Usually you do not need to call this function directly.
    # To check if a feature is available, simply use the feature, and see if you get a `License_FeatureAccessDenied` error.
    #
    # @return information about the currently activated licenses, and the call success status
    def queryLicenses(self) -> Result[List[capi.LicenseInfo]]:
        err = capi.ErrorCode.UnknownError
        list = capi.Headset_queryLicenses(self._headset, err)
        return Result(list, err)

    # Gets the information about the hardware information
    #
    # Allows you to get serial number, manufacturer, and model name of the headset.
    #
    # @return information about the hardware, and the call success status
    def queryHardwareInfo(self) -> Result[capi.HeadsetHardwareInfo]:
        hardwareInfo = capi.HeadsetHardwareInfo()
        err = capi.Headset_queryHardwareInfo(self._headset, hardwareInfo)
        return Result(hardwareInfo, err)

    # Returns whether the Headset has access to the given feature
    #
    # If the provided feature name doesn't exist, then `false` and `#Fove_ErrorCode_None` are returned.
    #
    # @param featureName specify which feature is queried.
    #
    # @return capi.ErrorCode.None if the call succeeded
    def hasAccessToFeature(self, featureName: str) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_hasAccessToFeature(self._headset, featureName, b)
        return Result(b.val, err)

    # Activate by using a license key
    #
    # @return capi.ErrorCode.None if the activation succeeded
    # @return capi.ErrorCode.UnknownError if the activation failed
    def activateLicense(self, licenseKey: str) -> Result[None]:
        err = capi.Headset_activateLicense(self._headset, licenseKey)
        return Result(None, err)

    # Deactivate license(s)
    #
    # licenseData can be empty, a GUID, or a license key. If it is empty, then all licenses will be deactivated.
    #
    # @return capi.ErrorCode.None if the deactivation succeeded
    # @return capi.ErrorCode.UnknownError if the deactivation failed
    def deactivateLicense(self, licenseData: str) -> Result[None]:
        err = capi.Headset_deactivateLicense(self._headset, licenseData)
        return Result(None, err)

    # Registers a client capability, enabling the required hardware as needed
    #
    # Usually you provide the required capabilities at the creation of the headset
    # But you can add and remove capabilities anytime while the object is alive.
    #
    # @param caps A set of capabilities to register. Registering an existing capability is a no-op
    # @returncapi.ErrorCode.None if the capability has been properly registered
    # @returncapi.ErrorCode.Connect_NotConnected if not connected to the service
    # @returncapi.ErrorCode.License_FeatureAccessDenied if your license doesn't offer access to this capability
    def registerCapabilities(self, caps: capi.ClientCapabilities) -> Result[None]:
        err = capi.Headset_registerCapabilities(self._headset, caps)
        return Result(None, err)
    
    # Registers a passive client capability, enabling the required hardware as needed
    #
	# The difference between active capabilties (those registered with `fove_Headset_registerCapabilities`) is that
	# passive capabilities are not used to enable hardware or software components. There must be at least one active
	# capability registered for the required hardware/software modules to be enabled.

	# However, if another app registers the same capability actively, you can use passive capabilities to read the data
	# being exported from the service on behalf of another client who has registered the capability actively.

	# Basically, this means "if it's on I want it, but I don't want to turn it on myself".

	# Within a single client, there's no point to registering a capability passively if it's already registered actively.
	# However, this is not an error, and the capability will be registered passively. The two lists are kept totally separate.
    #
    # @param caps A set of capabilities to register. Registering an existing capability is a no-op
    # @returncapi.ErrorCode.None if the capability has been properly registered
    # @returncapi.ErrorCode.Connect_NotConnected if not connected to the service
    # @returncapi.ErrorCode.License_FeatureAccessDenied if your license doesn't offer access to this capability
    def registerPassiveCapabilities(self, caps: capi.ClientCapabilities) -> Result[None]:
        err = capi.Headset_registerPassiveCapabilities(self._headset, caps)
        return Result(None, err)

    # Unregisters a client capability previously registered
    #
    # @param caps A set of capabilities to unregister. Unregistering an not-existing capability is a no-op
    # @returncapi.ErrorCode.None if the capability has been properly unregistered
    # @returncapi.ErrorCode.Connect_NotConnected if not connected to the service
    def unregisterCapabilities(self, caps: capi.ClientCapabilities) -> Result[None]:
        err = capi.Headset_unregisterCapabilities(self._headset, caps)
        return Result(None, err)

    # Unregisters a passive client capability previously registered
    #
    # @param caps A set of capabilities to unregister. Unregistering an not-existing capability is a no-op
    # @returncapi.ErrorCode.None if the capability has been properly unregistered
    # @returncapi.ErrorCode.Connect_NotConnected if not connected to the service
    def unregisterPassiveCapabilities(self, caps: capi.ClientCapabilities) -> Result[None]:
        err = capi.Headset_unregisterPassiveCapabilities(self._headset, caps)
        return Result(None, err)

    # Waits for next eye camera frame to be processed
    #
    # Allows you to sync your eye tracking loop to the actual eye-camera loop.
    # On each loop, you would first call this blocking function to wait for the next eye frame to be processed,
    # then update the local cache of eye tracking data using the fetch functions,
    # and finally get the desired eye tracking data using the getters.
    #
    # Eye tracking should be enabled by registering the `Fove_ClientCapabilities_EyeTracking` before calling this function.
    #
    # @return capi.ErrorCode.None_ if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    #
    # @see Headset.fetchEyeTrackingData
    # @see HeadsetfetchEyesImage
    def waitForProcessedEyeFrame(self) -> Result[None]:
        err = capi.Headset_waitForProcessedEyeFrame(self._headset)
        return Result(None, err)

    # Fetch the latest eye tracking related data from runtime service
    #
    # This function is never blocking, if the data is already up-to-date no operation is performed.
    # It outputs the timestamp of the new gaze information. This can be used to know if data has been
    # updated or not.
    #
    # Eye tracking should be enabled by registering the `capi.ClientCapabilities.EyeTracking` before calling this function.
    #
    # @return The latest Eye Frame timestamp, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    def fetchEyeTrackingData(self) -> Result[capi.FrameTimestamp]:
        timestamp = capi.FrameTimestamp()
        err = capi.Headset_fetchEyeTrackingData(self._headset, timestamp)
        return Result(timestamp, err)

    # Fetch the latest eyes camera image from the runtime service
    #
    # This function updates a local cache of eyes image, that can be retrieved through `fove_Headset_getEyesImage`.
    #
    # A cache is used to ensure that multiple calls to `fove_Headset_getEyesImage` return exactly the same data
    # until we request an explicit data update through the next fetch call.
    #
    # This function never blocks the thread. If no new data is available, no operation is performed.
    # The timestamp can be used to know if the data has been updated or not.
    #
    # Usually, you want to call this function in conjunction with `fove_Headset_fetchEyeTrackingData` either at the beginning
    # of your update loop of just after `fove_Headset_waitForProcessedEyeFrame` depending on your thread synchronization.
    #
    # Eyes image capability should be enabled by registering `Fove_ClientCapabilities_EyesImage` before calling this function.
    #
    # @return The latest Eye Frame timestamp, and the call success status:
    # - capi.ErrorCode_None if the call succeeded
    # - capi.ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet
    # - capi.ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call
    # @see Headset.getEyesImage
    # @see Headset.fetchEyeTrackingData
    # @see Headset.waitForProcessedEyeFrame
    def fetchEyesImage(self) -> Result[capi.FrameTimestamp]:
        timestamp = capi.FrameTimestamp()
        err = capi.Headset_fetchEyesImage(self._headset, timestamp)
        return Result(timestamp, err)

    # Writes out the eye frame timestamp of the cached eyes image
    #
    # Basically returns the timestamp returned by the last call to `fove_Headset_fetchEyesImage`.
    #
    # `Fove_ClientCapabilities_EyesImage` should be registered to use this function.
    #
    # @return The frame timestamp of the currently cached data, and the call status of the latest fetch call:
    # - capi.ErrorCode_None if the call succeeded\n
    # - capi.ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet\n
    # - capi.ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call\n
    # - capi.ErrorCode_API_NullInPointer if outTimestamp is null
    # @see Headset.fetchEyesImage
    def getEyeTrackingDataTimestamp(self) -> Result[capi.FrameTimestamp]:
        timestamp = capi.FrameTimestamp()
        err = capi.Headset_getEyeTrackingDataTimestamp(self._headset, timestamp)
        return Result(timestamp, err)

    # Writes out the eye frame timestamp of the cached eyes image
    #
    # Basically returns the timestamp returned by the last call to `fove_Headset_fetchEyesImage`.
    #
    # `Fove_ClientCapabilities_EyesImage` should be registered to use this function.
    #
    # @return The frame timestamp of the currently cached eyes image, with the status of hte latest fetch call:
    # - capi.ErrorCode_None if the call succeeded\n
    # - capi.ErrorCode_Data_NoUpdate if not connected to the service or if the service hasn't written any data out yet\n
    # - capi.ErrorCode_API_NotRegistered if the required capability has not been registered prior to this call\n
    # - capi.ErrorCode_API_NullInPointer if outTimestamp is null
    # @see Headset.fetchEyesImage
    def getEyesImageTimestamp(self) -> Result[capi.FrameTimestamp]:
        timestamp = capi.FrameTimestamp()
        err = capi.Headset_getEyesImageTimestamp(self._headset, timestamp)
        return Result(timestamp, err)

    # Gets the gaze vector of an individual eye
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @param eye Specify which eye to get the value for
    # @return The 3D gaze vector of the specified eye, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getGazeVector(self, eye: capi.Eye) -> Result[capi.Vec3]:
        vec = capi.Vec3()
        err = capi.Headset_getGazeVector(self._headset, eye, vec)
        return Result(vec, err)

    # Gets the user's 2D gaze position on the screens seen through the HMD's lenses
    #
    # The use of lenses and distortion correction creates a screen in front of each eye.
    # This function returns 2D vectors representing where on each eye's screen the user
    # is looking.
    #
    # The vectors are normalized in the range [-1, 1] along both X and Y axes such that the
    # following points are true:
    #
    # - Center: (0, 0)
    # - Bottom-Left: (-1, -1)
    # - Top-Right: (1, 1)
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @param eye Specify which eye to get the value for
    # @return The 2D screen position of the specified eye gaze, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getGazeScreenPosition(self, eye: capi.Eye) -> Result[capi.Vec2]:
        vec = capi.Vec2()
        err = capi.Headset_getGazeScreenPosition(self._headset, eye, vec)
        return Result(vec, err)

    # Gets the user's 2D gaze position on a virtual screen in front of the user.
    #
    # This is a 2D equivalent of `fove_Headset_getCombinedGazeRay`, and is perhaps the simplest gaze estimation function.
    # It returns an X/Y coordinate of where on the screen the user is looking.
    # While in reality each eye is looking in a different direction at a different [portion of the] screen,
    # they mostly agree, and this function returns effectively an average to get you a simple X/Y value.
    #
    # The vector is normalized in the range [-1, 1] along both X and Y axes such that the
    # following points are true:
    #
    # - Center: (0, 0)
    # - Bottom-Left: (-1, -1)
    # - Top-Right: (1, 1)
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @param eye Specify which eye to get the value for
    # @return The 2D screen position of the specified eye gaze, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getGazeScreenPositionCombined(self) -> Result[capi.Vec2]:
        vec = capi.Vec2()
        err = capi.Headset_getGazeScreenPositionCombined(self._headset, vec)
        return Result(vec, err)

    # Get eyes gaze ray resulting from the two eye gazes combined together
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # To get individual eye rays use `getGazeVector` instead
    #
    # @return The combined gaze ray, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getCombinedGazeRay(self) -> Result[capi.Ray]:
        ray = capi.Ray()
        err = capi.Headset_getCombinedGazeRay(self._headset, ray)
        return Result(ray, err)

    # Get eyes gaze depth resulting from the two eye gazes combined together
    #
    # `capi.ClientCapabilities.GazeDepth` should be registered to use this function.
    #
    # @return The depth of the combine Gaze, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getCombinedGazeDepth(self) -> Result[float]:
        depth = capi.Float()
        err = capi.Headset_getCombinedGazeDepth(self._headset, depth)
        return Result(depth.val, err)

    # Get whether the user is shifting its attention between objects or looking at something specific (fixation or pursuit).
    #
    # This can be used to ignore eye data during large eye motions when the user is not looking at anything specific.
    #
    # `capi.ClientCapabilities.UserAttentionShift` should be registered to use this function.
    #
    # @return Whether the user is shifting attention, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def isUserShiftingAttention(self) -> Result[bool]:
        b = capi.Bool()
        err = capi.Headset_isUserShiftingAttention(self._headset, b)
        return Result(b.val, err)

    # Get the state of an individual eye
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @param eye Specify which eye to get the value for
    # @return The state of the specified eye, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getEyeState(self, eye: capi.Eye) -> Result[capi.EyeState]:
        b = capi.EyeStateObj()
        err = capi.Headset_getEyeState(self._headset, eye, b)
        return Result(b.val, err)

    # Checks if eye tracking hardware has started
    #
    # @return Whether eye tracking is running, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def isEyeTrackingEnabled(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_isEyeTrackingEnabled(self._headset, b)
        return Result(b.val, err)

    # Checks if eye tracking has been calibrated
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @return Whether the eye tracking system is calibrated, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def isEyeTrackingCalibrated(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_isEyeTrackingCalibrated(self._headset, b)
        return Result(b.val, err)

    # Checks if eye tracking is in the process of calibration
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @return Whether the eye tracking system is calibrating, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def isEyeTrackingCalibrating(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_isEyeTrackingCalibrating(self._headset, b)
        return Result(b.val, err)

    # Check whether the eye tracking system is currently calibrated for glasses.
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # This basically indicates if the user was wearing glasses during the calibration or not.
    # This function returns 'Data_Uncalibrated' if the eye tracking system has not been calibrated yet
    #
    # @return Whether the eye tracking system is calibrated for glasses, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Uncalibrated if the eye tracking system is currently uncalibrated
    def isEyeTrackingCalibratedForGlasses(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_isEyeTrackingCalibratedForGlasses(self._headset, b)
        return Result(b.val, err)

    # Check whether or not the GUI that asks the user to adjust their headset is being displayed
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @return Whether the Headset position adjustment GUI is visible on the screen, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def isHmdAdjustmentGuiVisible(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_isHmdAdjustmentGuiVisible(self._headset, b)
        return Result(b.val, err)

    # Check whether the GUI that asks the user to adjust their headset was hidden by timeout
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @return Whether the Headset position adjustment GUI has timeout, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def hasHmdAdjustmentGuiTimeout(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_hasHmdAdjustmentGuiTimeout(self._headset, b)
        return Result(b.val, err)

    # Checks if eye tracking is actively tracking an eye - or eyes.
    #
    # In other words, it returns `true` only when the hardware is ready and eye tracking is calibrated.
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @return Whether the eye tracking system is ready, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def isEyeTrackingReady(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_isEyeTrackingReady(self._headset, b)
        return Result(b.val, err)

    # Checks whether the user is wearing the headset or not
    #
    # When user is not present Eye tracking values shouldn't be used, as invalid.
    #
    # `capi.ClientCapabilities.UserPresence` should be registered to use this function.
    #
    # @return Whether the user is wearing the headset, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def isUserPresent(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_isUserPresent(self._headset, b)
        return Result(b.val, err)

    # Returns the eyes camera image
    #
    # The eyes image is synchronized with and fetched at the same as the gaze
    # during the call to `fetchEyeTrackingData`.
    #
    # The image data buffer is invalidated upon the next call to this function.
    # `capi.ClientCapabilities.EyesImage` should be registered to use this function.
    #
    # @return The Eye camera image, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreadable if the data couldn't be read properly from memory
    def getEyesImage(self) -> Result[capi.BitmapImage]:
        b = capi.BitmapImage()
        err = capi.Headset_getEyesImage(self._headset, b)
        return Result(b, err)

    # Returns the user IPD (Inter Pupillary Distance), in meters
    #
    # `capi.ClientCapabilities.UserIPD` should be registered to use this function.
    #
    # @return The user IPD value, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getUserIPD(self) -> Result[float]:
        b = capi.Float()
        err = capi.Headset_getUserIPD(self._headset, b)
        return Result(b.val, err)

    # Returns the user IOD (Inter Occular Distance), in meters
    #
    # `capi.ClientCapabilities.UserIOD` should be registered to use this function.
    #
    # @return The user IOD value, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getUserIOD(self) -> Result[float]:
        b = capi.Float()
        err = capi.Headset_getUserIOD(self._headset, b)
        return Result(b.val, err)

    # Returns the user pupils radius, in meters
    #
    # `capi.ClientCapabilities.PupilRadius` should be registered to use this function.
    #
    # @param eye Specify which eye to get the value for
    # @return The pupil radius of the specified eye, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call yet
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getPupilRadius(self, eye: capi.Eye) -> Result[float]:
        b = capi.Float()
        err = capi.Headset_getPupilRadius(self._headset, eye, b)
        return Result(b.val, err)

    # Returns the user iris radius, in meters
    #
    # `capi.ClientCapabilities.IrisRadius` should be registered to use this function.
    #
    # @param eye Specify which eye to get the value for
    # @return The iris radius of the specified eye, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getIrisRadius(self, eye: capi.Eye) -> Result[float]:
        b = capi.Float()
        err = capi.Headset_getIrisRadius(self._headset, eye, b)
        return Result(b.val, err)

    # Returns the user eyeballs radius, in meters
    #
    # `capi.ClientCapabilities.EyeballRadius` should be registered to use this function.
    #
    # @param eye Specify which eye to get the value for
    # @return The eyeball radius of the specified eye, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getEyeballRadius(self, eye: capi.Eye) -> Result[float]:
        b = capi.Float()
        err = capi.Headset_getEyeballRadius(self._headset, eye, b)
        return Result(b.val, err)

    # Returns the user eye torsion, in degrees
    #
    # `capi.ClientCapabilities.EyeTorsion` should be registered to use this function.
    #
    # @param eye Specify which eye to get the value for
    # @return The torsion angle of the specified eye, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    # - capi.ErrorCode.API_NullInPointer if both `outAngle` is `nullptr`
    # - capi.ErrorCode.License_FeatureAccessDenied if the current license is not sufficient for this feature
    def getEyeTorsion(self, eye: capi.Eye) -> Result[float]:
        b = capi.Float()
        err = capi.Headset_getEyeTorsion(self._headset, eye, b)
        return Result(b.val, err)

    # Returns the outline shape of the specified user eye in the Eyes camera image.
    #
    # `capi.ClientCapabilities.EyeShape` should be registered to use this function.
    #
    # @param eye Specify which eye to get the value for
    # @return The shape of the specified eye, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    # - capi.ErrorCode.API_NullInPointer if both `outShape` is `nullptr`
    # - capi.ErrorCode.License_FeatureAccessDenied if the current license is not sufficient for this feature
    def getEyeShape(self, eye: capi.Eye) -> Result[capi.EyeShape]:
        b = capi.EyeShape()
        err = capi.Headset_getEyeShape(self._headset, eye, b)
        return Result(b, err)

    # Returns the pupil ellipse of the specified user eye in the Eyes camera image.
    #
    # `capi.ClientCapabilities.PupilShape` should be registered to use this function.
    #
    # @param eye Specify which eye to get the value for
    # @return The parameters of the pupil ellipse of the specified eye, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    # - capi.ErrorCode.License_FeatureAccessDenied if the current license is not sufficient for this feature
    def getPupilShape(self, eye: capi.Eye) -> Result[capi.PupilShape]:
        b = capi.PupilShape()
        err = capi.Headset_getPupilShape(self._headset, eye, b)
        return Result(b, err)

    # Starts eye tracking calibration
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @param options The calibration options to use, or None to use default options
    # @return capi.ErrorCode.None if the call succeeded
    # @return capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # @return capi.ErrorCode.Connect_NotConnected if not connected to the service
    # @return capi.ErrorCode.License_FeatureAccessDenied if any of the enabled options require a license beyond what is active on this machine
    def startEyeTrackingCalibration(
        self, options: Optional[capi.CalibrationOptions]
    ) -> Result[None]:
        if not options:
            options = capi.CalibrationOptions()
        err = capi.Headset_startEyeTrackingCalibration(self._headset, options)
        return Result(None, err)

    # Stops eye tracking calibration if it's running, does nothing if it's not running
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @return capi.ErrorCode.None if the call succeeded
    # @return capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # @return capi.ErrorCode.Connect_NotConnected if not connected to the service
    def stopEyeTrackingCalibration(self) -> Result[None]:
        err = capi.Headset_stopEyeTrackingCalibration(self._headset)
        return Result(None, err)

    # Get the state of the currently running calibration process
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # @return The eye tracking system calibration state, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def getEyeTrackingCalibrationState(self) -> Result[capi.CalibrationState]:
        s = capi.CalibrationStateObj()
        err = capi.Headset_getEyeTrackingCalibrationState(self._headset, s)
        return Result(s.val, err)

    # Tick the current calibration process and retrieve data information to render the current calibration state.
    #
    # @param deltaTime The time elapsed since the last rendered frame
    # @param isVisible Indicate to the calibration system that something is being drawn to the screen.
    # This allows the calibration renderer to take as much time as it wants to display success/failure messages
    # and animate away before the calibration processes is marked as completed by the `IsEyeTrackingCalibrating` function.
    # @param outCalibrationData The calibration current state information
    #
    # This function is how the client declares to the calibration system that is available to render calibration.
    # The calibration system determines which of the available renderers has the highest priority,
    # and returns to that render the information needed to render calibration via the outTarget parameter.
    # Even while ticking this, you may get no result because either no calibration is running,
    # or a calibration is running but some other higher priority renderer is doing the rendering.
    #
    # `capi.ClientCapabilities.EyeTracking` should be registered to use this function.
    #
    # Note that it is perfectly fine not to call this function, in which case the Fove service will automatically render the calibration process for you.
    #
    # @return The current calibration data, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.License_FeatureAccessDenied if a sufficient license is not registered on this machine
    # - capi.ErrorCode.Calibration_OtherRendererPrioritized if another process has currently the priority for rendering calibration process
    def tickEyeTrackingCalibration(
        self, deltaTime: float, isVisible: bool
    ) -> Result[capi.CalibrationData]:
        s = capi.CalibrationData()
        err = capi.Headset_tickEyeTrackingCalibration(
            self._headset, deltaTime, isVisible, s
        )
        return Result(s, err)

    # Get the id of the object gazed by the user.
    #
    # In order to be detected an object first need to be registered using the `registerGazableObject` function.
    # If the user is currently not looking at any specific object the `fove_ObjectIdInvalid` value is returned.
    # To use this function, you need to register the `capi.ClientCapabilities.GazedObjectDetection` first.
    #
    # @return The ID of the gazable object currently gazed at, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    #
    # @see Headset.updateGazableObject
    # @see Headset.removeGazableObject
    # @see Headset.Fove_GazeConvergenceData
    def getGazedObjectId(self) -> Result[int]:
        s = capi.Int()
        err = capi.Headset_getGazedObjectId(self._headset, s)
        return Result(s.val, err)

    # Registers an object in the 3D world
    #
    # Registering 3D world objects allows FOVE software to identify which objects are being gazed at.
    # We recommend that clients opt-in to this functionality rather than doing it themselves, as our algorithm may improve over time.
    # Clients of course may do their own detection if they have special needs, such as performance needs, or just want to use their own algorithm.
    #
    # Use #registerCameraObject to set the pose of the corresponding camera in the 3D world.
    #
    # Connection to the service is not required for object registration, thus you can register your world objects at will and not worry about connection or reconnection status.
    #
    # @param object       A description of the object in the 3D world. Data is copied and no reference is kept to this memory after return.
    # @return capi.ErrorCode.None if the object is successfully added or updated
    # @return capi.ErrorCode.API_InvalidArgument if the object is invalid in any way (such as an invalid object id)
    # @return capi.ErrorCode.Object_AlreadyRegistered if an object with same id is already registered
    # @see Headset.updateGazableObject
    # @see Headset.removeGazableObject
    # @see Headset.Fove_GazeConvergenceData
    def registerGazableObject(self, gazableObject: capi.GazableObject) -> Result[None]:
        err = capi.Headset_registerGazableObject(self._headset, gazableObject)
        return Result(None, err)

    # Update a previously registered 3D object pose.
    #
    # @param objectId     Id of the object passed to registerGazableObject()
    # @param pose         the updated pose of the object
    # @return capi.ErrorCode.None if the object was in the scene and is now updated
    # @return capi.ErrorCode.API_InvalidArgument if the object was not already registered
    # @see Headset.registerCameraObject
    # @see Headset.removeGazableObject
    def updateGazableObject(self, objectId: int, pose: capi.ObjectPose) -> Result[None]:
        err = capi.Headset_updateGazableObject(self._headset, objectId, pose)
        return Result(None, err)

    # Removes a previously registered 3D object from the scene.
    #
    # Because of the asynchronous nature of the FOVE system, this object may still be referenced in future frames for a very short period of time.
    #
    # @param objectId     Id of the object passed to registerGazableObject()
    # @return capi.ErrorCode.None if the object was in the scene and is now removed
    # @return capi.ErrorCode.API_InvalidArgument if the object was not already registered
    # @see Headset.registerGazableObject
    # @see Headset.updateGazableObject
    def removeGazableObject(self, objectId: int) -> Result[None]:
        err = capi.Headset_removeGazableObject(self._headset, objectId)
        return Result(None, err)

    # Registers an camera in the 3D world
    #
    # Registering 3D world objects and camera allows FOVE software to identify which objects are being gazed at.
    # We recommend that clients opt-in to this functionality rather than doing it themselves, as our algorithm may improve over time.
    # Clients of course may do their own detection if they have special needs, such as performance needs, or just want to use their own algorithm.
    #
    # At least 1 camera needs to be registered for automatic object gaze recognition to work. Use the object group mask of the camera to
    # specify which objects the camera is capturing. The camera view pose determine the gaze raycast direction and position.
    # The camera view pose should include any and all offsets from position tracking. No transforms from the headset are added in automatically.
    #
    # Connection to the service is not required for object registration, thus you can register your world objects at will and not worry about connection or reconnection status.
    #
    # @param camera       A description of the camera. Data is copied and no reference is kept to this memory after return.
    # @return capi.ErrorCode.None if the camera is successfully added or updated
    # @return capi.ErrorCode.API_InvalidArgument if the object is invalid in any way (such as an invalid object id)
    # @return capi.ErrorCode.Object_AlreadyRegistered if an object with same id is already registered
    # @see Headset.updateCameraObject
    # @see Headset.removeCameraObject
    def registerCameraObject(self, cameraObject: capi.CameraObject) -> Result[None]:
        err = capi.Headset_registerCameraObject(self._headset, cameraObject)
        return Result(None, err)

    # Update the pose of a registered camera
    #
    # @param cameraId     Id of the camera passed to registerCameraObject()
    # @param pose         the updated pose of the camera
    # @return capi.ErrorCode.None if the object was in the scene and is now removed
    # @return capi.ErrorCode.API_InvalidArgument if the object was not already registered
    # @see Headset.registerCameraObject
    # @see Headset.removeCameraObject
    def updateCameraObject(self, cameraId: int, pose: capi.ObjectPose) -> Result[None]:
        err = capi.Headset_updateCameraObject(self._headset, cameraId, pose)
        return Result(None, err)

    # Removes a previously registered camera from the scene.
    #
    # @param cameraId     Id of the camera passed to registerCameraObject()
    # @return capi.ErrorCode.None if the object was in the scene and is now removed
    # @return capi.ErrorCode.API_InvalidArgument is returned if the object was not already registered
    # @see Headset.registerCameraObject
    # @see Headset.updateCameraObject
    def removeCameraObject(self, cameraId: int) -> Result[None]:
        err = capi.Headset_removeCameraObject(self._headset, cameraId)
        return Result(None, err)

    # Tares the orientation of the headset
    #
    # Any or both of `capi.ClientCapabilities.OrientationTracking` and `capi.ClientCapabilities.PositionTracking`
    # should be registered to use this function.
    #
    # @return capi.ErrorCode.None if the call succeeded
    # @return capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # @return capi.ErrorCode.Connect_NotConnected if not connected to the service
    def tareOrientationSensor(self) -> Result[None]:
        err = capi.Headset_tareOrientationSensor(self._headset)
        return Result(None, err)

    # Writes out whether position tracking hardware has started and returns whether it was successful
    #
    # `capi.ClientCapabilities.PositionTracking` should be registered to use this function.
    #
    # @return Whether the position tracking headset is ready, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def isPositionReady(self) -> Result[bool]:
        b = capi.Bool(False)
        err = capi.Headset_isPositionReady(self._headset, b)
        return Result(b.val, err)

    # Tares the position of the headset
    #
    # `capi.ClientCapabilities.PositionTracking` should be registered to use this function.
    #
    # @return capi.ErrorCode.None if the call succeeded
    # @return capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # @return capi.ErrorCode.Connect_NotConnected if not connected to the service
    def tarePositionSensors(self) -> Result[None]:
        err = capi.Headset_tarePositionSensors(self._headset)
        return Result(None, err)

    # Fetch the latest headset pose related data from runtime service
    #
    # This function is never blocking, if the data is already up-to-date no operation is performed.
    # It outputs the timestamp of the new pose data. This can be used to know if data has been
    # updated or not.
    #
    # At least one of the following capabilities need to be registered to use this function:
    # `capi.ClientCapabilities.PositionTracking`, `capi.ClientCapabilities.OrientationTracking`,
    # `capi.ClientCapabilities.PositionImage`
    #
    # @return The latest pose frame timestamp, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    def fetchPoseData(self) -> Result[capi.FrameTimestamp]:
        b = capi.FrameTimestamp()
        err = capi.Headset_fetchPoseData(self._headset, b)
        return Result(b, err)

    # Writes out the pose of the head-mounted display
    #
    # @return The Headset pose, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreliable if the returned data is too unreliable to be used
    # - capi.ErrorCode.Data_LowAccuracy if the returned data is of low accuracy
    def getPose(self) -> Result[capi.Pose]:
        pose = capi.Pose()
        err = capi.Headset_getPose(self._headset, pose)
        return Result(pose, err)

    # Returns the position camera image
    #
    # The position image is synchronized with and fetched at the same as the pose
    # during the call to `fetchPoseData`.
    #
    # The image data buffer is invalidated upon the next call to this function.
    # `capi.ClientCapabilities.PositionImage` should be registered to use this function.
    #
    # @return The position camera image, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NotRegistered if the required capability has not been registered prior to this call
    # - capi.ErrorCode.Data_NoUpdate if the capability is registered but no valid data has been returned by the service yet
    # - capi.ErrorCode.Data_Unreadable if the data couldn't be read properly from memory
    def getPositionImage(self) -> Result[capi.BitmapImage]:
        i = capi.BitmapImage()
        err = capi.Headset_getPositionImage(self._headset, i)
        return Result(i, err)

    # Gets the valoues of passed-in left-handed 4x4 projection matrices
    #
    # Gets 4x4 projection matrices for both eyes using near and far planes
    # in a left-handed coordinate system.
    #
    # Note: the underlying API in fove.capi allows one to get only one of
    # the two (left/right) projection matrices, but here both matrices
    # will always be returned.
    #
    # @param zNear        The near plane in float, Range: from 0 to zFar
    # @param zFar         The far plane in float, Range: from zNear to infinity
    # @return The 4x4 projection left & right matrices (left-handed), and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def getProjectionMatricesLH(
        self, zNear: float, zFar: float
    ) -> Result[Tuple[capi.Matrix44, capi.Matrix44]]:
        lMat, rMat = capi.Matrix44(), capi.Matrix44()
        err = capi.Headset_getProjectionMatricesLH(
            self._headset, zNear, zFar, lMat, rMat
        )
        return Result((lMat, rMat), err)

    # Gets the valoues of passed-in right-handed 4x4 projection matrices
    #
    # Gets 4x4 projection matrices for both eyes using near and far planes
    # in a right-handed coordinate system.
    #
    # Note: the underlying API in fove.capi allows one to get only one of
    # the two (left/right) projection matrices, but here both matrices
    # will always be returned.
    #
    # @param zNear        The near plane in float, Range: from 0 to zFar
    # @param zFar         The far plane in float, Range: from zNear to infinity
    # @return The left & right 4x4 projection matrices (right-handed), and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def getProjectionMatricesRH(
        self, zNear: float, zFar: float
    ) -> Result[Tuple[capi.Matrix44, capi.Matrix44]]:
        lMat, rMat = capi.Matrix44(), capi.Matrix44()
        err = capi.Headset_getProjectionMatricesRH(
            self._headset, zNear, zFar, lMat, rMat
        )
        return Result((lMat, rMat), err)

    # Gets values for the view frustum of both eyes at 1 unit away
    #
    # Gets values for the view frustum of the specified eye at 1 unit away. Please multiply them by zNear to
    # convert to your correct frustum near-plane. Either outLeft or outRight may be `nullptr` to only write the
    # other struct, however setting both to `nullptr` is considered and error and the function will return
    # `Fove_ErrorCode::API_NullOutPointersOnly`.
    #
    # @return The left & right camera projection parameters, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def getRawProjectionValues(
        self,
    ) -> Result[Tuple[capi.ProjectionParams, capi.ProjectionParams]]:
        lParams, rParams = capi.ProjectionParams(), capi.ProjectionParams()
        err = capi.Headset_getRawProjectionValues(self._headset, lParams, rParams)
        return Result((lParams, rParams), err)

    # Gets the matrices to convert from eye- to head-space coordintes.
    #
    # This is simply a translation matrix that returns +/- IOD/2
    #
    # @return The matrix describing left & right eye and right transforms data, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def getEyeToHeadMatrices(self) -> Result[Tuple[capi.Matrix44, capi.Matrix44]]:
        lMat, rMat = capi.Matrix44(), capi.Matrix44()
        err = capi.Headset_getEyeToHeadMatrices(self._headset, lMat, rMat)
        return Result((lMat, rMat), err)

    # Gets interocular distance in meters
    #
    # This is an estimation of the distance between centers of the left and right eyeballs.
    # Half of the IOD can be used to displace the left and right cameras for stereoscopic rendering.
    # We recommend calling this each frame when doing stereoscoping rendering.
    # Future versions of the FOVE service may update the IOD during runtime as needed.
    #
    # @return A floating point value describing the IOD, and the call success status:
    # - capi.ErrorCode.None if the call succeeded
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.Data_NoUpdate if no valid data has been returned by the service yet
    def getRenderIOD(self) -> Result[float]:
        iod = capi.Float(-1.0)
        err = capi.Headset_getRenderIOD(self._headset, iod)
        return Result(iod.val, err)

    # Creates a new profile
    #
    # The FOVE system keeps a set of profiles so that different users on the same system can store data, such as calibrations, separately.
    # Profiles persist to disk and survive restart.
    # Third party applications can control the profile system and store data within it.
    #
    # This function creates a new profile, but does not add any data or switch to it.
    # @param newName Null-terminated UTF-8 unique name of the profile to create
    # @return capi.ErrorCode.None if the profile was successfully created
    # @return capi.ErrorCode.Connect_NotConnected if not connected to the service
    # @return capi.ErrorCode.Profile_InvalidName if newName was invalid
    # @return capi.ErrorCode.Profile_NotAvailable if the name is already taken
    # @see Headset.renameProfile
    # @see Headset.deleteProfile
    # @see Headset.listProfiles
    # @see Headset.setCurrentProfile
    # @see Headset.queryCurrentProfile
    # @see Headset.queryProfileDataPath
    def createProfile(self, profileName: str) -> Result[None]:
        err = capi.Headset_createProfile(self._headset, profileName)
        return Result(None, err)

    # Renames an existing profile
    #
    # This function renames an existing profile. This works on the current profile as well.
    # @param oldName Null-terminated UTF-8 name of the profile to be renamed
    # @param newName Null-terminated UTF-8 unique new name of the profile
    # @return capi.ErrorCode.None if the profile was successfully renamed
    # @return capi.ErrorCode.Connect_NotConnected if not connected to the service
    # @return capi.ErrorCode.Profile_DoesntExist if the requested profile at oldName doesn't exist
    # @return capi.ErrorCode.Profile_NotAvailable If the new named is already taken
    # @return capi.ErrorCode.API_InvalidArgument If the old name and new name are the same
    # @see Headset.createProfile
    # @see Headset.deleteProfile
    # @see Headset.listProfiles
    # @see Headset.setCurrentProfile
    # @see Headset.queryCurrentProfile
    # @see Headset.queryProfileDataPath
    def renameProfile(self, oldName: str, newName: str) -> Result[None]:
        err = capi.Headset_renameProfile(self._headset, oldName, newName)
        return Result(None, err)

    # Lists all existing profiles
    #
    # @return The list of profile name existing, and the call success status:
    # - capi.ErrorCode.None if the profile names were successfully listed
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # @see Headset.createProfile
    # @see Headset.renameProfile
    # @see Headset.deleteProfile
    # @see Headset.setCurrentProfile
    # @see Headset.queryCurrentProfile
    # @see Headset.queryProfileDataPath
    def listProfiles(self) -> Result[List[str]]:
        err = capi.ErrorCode.UnknownError
        list = capi.Headset_listProfiles(self._headset, err)
        return Result(list, err)

    # Sets the current profile
    #
    # When changing profile, the FOVE system will load up data, such as calibration data, if it is available.
    # If loading a profile with no calibration data, whether or not the FOVE system keeps old data loaded into memory is undefined.
    #
    # Please note that no-ops are OK but you should check for capi.ErrorCode.Profile_NotAvailable.
    #
    # @param profileName Name of the profile to make current, in UTF-8
    # @return capi.ErrorCode.None if the profile was successfully set as the current profile
    # @return capi.ErrorCode.Connect_NotConnected if not connected to the service
    # @return capi.ErrorCode.Profile_DoesntExist if there is no such profile
    # @return capi.ErrorCode.Profile_NotAvailable if the requested profile is the current profile
    # @return capi.ErrorCode.API_NullInPointer if profileName is null
    # @see Headset.createProfile
    # @see Headset.renameProfile
    # @see Headset.deleteProfile
    # @see Headset.listProfiles
    # @see Headset.queryCurrentProfile
    # @see Headset.queryProfileDataPath
    def setCurrentProfile(self, profileName: str) -> Result[None]:
        err = capi.Headset_setCurrentProfile(self._headset, profileName)
        return Result(None, err)

    # Gets the current profile
    #
    # An empty string is returned if no current profile.
    #
    # @return The name of the current profile, and the call success status:
    # - capi.ErrorCode.None if the profile name was successfully retrieved
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # @see Headset.createProfile
    # @see Headset.renameProfile
    # @see Headset.deleteProfile
    # @see Headset.listProfiles
    # @see Headset.setCurrentProfile
    # @see Headset.queryProfileDataPath
    def queryCurrentProfile(self) -> Result[str]:
        s = capi.String()
        err = capi.Headset_queryCurrentProfile(self._headset, s)
        return Result(s.val, err)

    # Gets the data folder for a given profile
    #
    # Allows you to retrieve a filesytem directory where third party apps can write data associated with this profile. This directory will be created before return.
    #
    # Since multiple applications may write stuff to a profile, please prefix any files you create with something unique to your application.
    #
    # There are no special protections on profile data, and it may be accessible to any other app on the system. Do not write sensitive data here.
    #
    # This is intended for simple uses. For advanced uses that have security concerns, or want to sync to a server, etc,
    # third party applications are encouraged to use their own separate data store keyed by profile name.
    # They will need to test for profile name changes and deletions manually in that case.
    #
    # @param profileName A null-terminated UTF-8 string with the name of the profile to be queried, or an empty string if no profile is set
    # @return the path to the profile data directory, and the call success status:
    # - capi.ErrorCode.None if the profile was successfully deleted
    # - capi.ErrorCode.Profile_DoesntExist if there is no such profile
    # - capi.ErrorCode.Connect_NotConnected if not connected to the service
    # - capi.ErrorCode.API_NullInPointer if profileName or callback is null
    # @see Headset.createProfile
    # @see Headset.renameProfile
    # @see Headset.deleteProfile
    # @see Headset.listProfiles
    # @see Headset.setCurrentProfile
    # @see Headset.queryCurrentProfile
    def queryProfileDataPath(self, profileName: str) -> Result[str]:
        s = capi.String()
        err = capi.Headset_queryProfileDataPath(self._headset, profileName, s)
        return Result(s.val, err)

    # Returns a compositor interface from the given headset
    #
    # Each call to this function creates a new object.
    # Once Compositor.__enter__ is called on the object, it should be destroyed with Compositor.__exit__.
    #
    # It is fine to call this function multiple times with the same headset.
    # It is ok for the compositor to outlive the headset passed in.
    #
    # @see Compositor
    # @see Compositor.__enter__
    # @see Compositor.__exit__
    def createCompositor(self) -> Compositor:
        return Compositor(self._headset)


# Class that manages accesses to the compositor
#
# All Compositor-related API requests will be done through an instance of this class.
#
# The class provides `Compositor.__enter__` and `Compositor.__exit__` methods
# that do relevant resource managements, and the `Headset` instance has
# a factory method that creates a compositor.
#
# It is fine to create multiple compositors from the same headset.
# It is also fine for the compositor to outlive the headset passed in.
#
# A typical use of this class
# would be as follows:
#
# @code
# with Headset(ClientCapabilities.Gaze) as headset,
#      headset.createCompositor() as compositor:
#     # use compositor
#     pass
# @endcode
class Compositor(object):
    # Defines a compositor that can be created from a headset
    #
    # Normally, this method is invoked through Headset.createCompositor.
    # But unlike in the C API, the user has to call Compositor.__enter__
    # on the result of Compositor.__init__ or Headset.createCompositor
    # to actually connect to a compositor.
    #
    # @param headset An instance of capi.Headset from which this compositor will be created.
    # (Note that it is not an instance of the Headset class defined in this module.)
    # @see Headset.createCompositor
    # @see Compositor.__enter__
    def __init__(self, headset: capi.Fove_Headset) -> None:
        # XXX this is perhaps ugly, but we cannot pass args to __enter__
        self._headset = headset
        logger.debug("Creating compositor: headset: {}".format(self._headset))
        self._compositor = capi.Fove_Compositor()

    # Creates a compositor interface to the given headset
    #
    # Each call to this function creates and stores a new compositor object,
    # which should be destroyed by calling Compositor.__exit__ on self,
    # which is also retuned from this function.
    #
    # @return self with a reference to a compositor object
    #
    # @see Headset.createCompositor
    # @see Compositor.__exit__
    def __enter__(self) -> Compositor:
        err = capi.Headset_createCompositor(self._headset, self._compositor)
        if err != capi.ErrorCode.None_:
            raise RuntimeError("Failed to create compositor: {}".format(err))
        return self

    # Frees resources used by the compositor object, including memory and sockets
    #
    # Upon return, this instance for the compositor should no longer be used.
    # @see Compositor.__enter__
    # @see Headset.createCompositor
    def __exit__(
        self,
        _e_type: Optional[Type[BaseException]],
        _e_val: Optional[Type[BaseException]],
        _traceback: Optional[TracebackType],
    ) -> bool:
        if _e_type is not None:
            logger.error("Headset: exception raised: {}".format(_e_val))
        if self._compositor is not None:
            capi.Compositor_destroy(self._compositor)
            logger.debug("Destroyed compositor")
        return True if _e_type is None else False

    # Create a layer for this client
    #
    # This function creates a layer upon which frames may be submitted to the compositor by this client.
    #
    # A connection to the compositor must exist for this to pass.
    # This means you need to wait for Compositor.isReady before calling this function.
    # However, if connection to the compositor is lost and regained, this layer will persist.
    # For this reason, you should not recreate your layers upon reconnection, simply create them once.
    #
    # There is no way to delete a layer once created, other than to destroy the Compositor object.
    # This is a feature we would like to add in the future.
    #
    # @param layerInfo The settings for the layer to be created
    # @return A struct that holds the defaults of the newly created layer
    #
    # @see Compositor.isReady
    # @see Compositor.submit
    def createLayer(
        self, layerInfo: capi.CompositorLayerCreateInfo
    ) -> Optional[capi.CompositorLayer]:
        layer = capi.CompositorLayer()
        err = capi.Compositor_createLayer(self._compositor, layerInfo, layer)
        if err != capi.ErrorCode.None_:
            logger.error("compositor.createLayer() failed: {}".format(err))
            return None
        return layer

    # Submit a frame to the compositor
    #
    # This function takes the feed from your game engine to the compositor for output.
    # @param submitInfo   An array of layerCount capi.CompositorLayerSubmitInfo structs, each of which provides texture data for a unique layer
    # @param layerCount   The number of layers you are submitting
    # @return True if successfully submitted, or None in case of API failure
    def submit(
        self, submitInfo: capi.CompositorLayerSubmitInfo, layerCount: int
    ) -> Optional[bool]:
        err = capi.Compositor_submit(self._compositor, submitInfo, layerCount)
        if err != capi.ErrorCode.None_:
            logger.error("compositor.submit() failed: {}".format(err))
            return None
        return True

    # Wait for the most recent pose for rendering purposes
    #
    # All compositor clients should use this function as the sole means of limiting their frame rate.
    # This allows the client to render at the correct frame rate for the HMD display.
    # Upon this function returning, the client should proceed directly to rendering,
    # to reduce the chance of missing the frame.
    # This function will return the latest pose (valid if not None) as a conveience to the caller.
    #
    # In general, a client's main loop should look like:
    # @code
    # # with compositor
    # while True:
    #    Update()                                   # Run AI, physics, etc, for the next frame
    #    pose, err = compositor.waitForRenderPose() # Wait for the next frame, and get the pose
    #    if pose:
    #        Draw(pose)                             # Render the scene using the new pose
    #    elif err is False:
    #        # sleep a bit and retry
    #        continue
    #    else:
    #        # permanent error
    #        break
    # @endcode
    #
    # @return (Optional[capi.Pose], Optional[bool])
    # @return The first return value is the current pose if the call synced with
    # the compositor frames; otherwise it is None.
    # @return When the first return value is None, the second return value indicates
    # whether the error is permanent: i.e. it is False when the call to an internal
    # API has just timed out and retry on the client side might be useful;
    # it is otherwise True, e.g. if the Orientation capability was not
    # registered.
    def waitForRenderPose(self) -> Tuple[Optional[capi.Pose], Optional[bool]]:
        pose = capi.Pose()
        err = capi.Compositor_waitForRenderPose(self._compositor, pose)
        if err == capi.ErrorCode.None_:
            return pose, None
        elif err == capi.ErrorCode.API_Timeout:
            return None, False  # client may retry
        else:
            return None, True

    # Gets the last cached pose for rendering purposes, without waiting for a new frame to arrive.
    #
    # @return Last cached pose, or None in case of API failure
    def getLastRenderPose(self) -> Optional[capi.Pose]:
        pose = capi.Pose()
        err = capi.Compositor_getLastRenderPose(self._compositor, pose)
        if err != capi.ErrorCode.None_:
            logger.error("compositor.getLastRenderPose() failed: {}".format(err))
            return None
        return pose

    # Checks whether we are connected to a running compositor and ready to submit frames for composing
    #
    # @return True if we are connected to a running compositor and ready to submit frames for compositing, False if not,
    # or else None in case of API failure
    def isReady(self) -> Optional[bool]:
        b = capi.Bool(False)
        err = capi.Compositor_isReady(self._compositor, b)
        if err != capi.ErrorCode.None_:
            logger.error("compositor.isReady() failed: {}".format(err))
            return None
        return b.val

    # Returns the ID of the GPU currently attached to the headset
    #
    # For systems with multiple GPUs, submitted textures to the compositor must
    # come from the same GPU that the compositor is using.
    #
    # @return The adapter ID, or None in case of API failure
    def queryAdapterId(self) -> Optional[capi.AdapterId]:
        adapterId = capi.AdapterId()
        err = capi.Compositor_queryAdapterId(self._compositor, adapterId)
        if err != capi.ErrorCode.None_:
            logger.error("compositor.queryAdapterId() failed: {}".format(err))
            return None
        return adapterId


# Establishes a connection to the headset.
# Call this once when start interacting with the headset.
def connectToHeadset(headset: capi.Fove_Headset, caps: capi.ClientCapabilities) -> bool:
    b = capi.Bool()
    t = capi.FrameTimestamp()
    (iter, maxIter) = (0, 1000)

    if caps & Headset.ET_CAPS:
        while (
            iter < maxIter
            and capi.Headset_fetchEyeTrackingData(headset, t)
            == capi.ErrorCode.Data_NoUpdate
        ):
            time.sleep(0.01)
            iter += 1
            if iter % 100 == 0:
                logger.debug("Waiting for eye tracking data to become ready")

    if caps & Headset.POS_CAPS:
        while (
            iter < maxIter
            and capi.Headset_fetchPoseData(headset, t) == capi.ErrorCode.Data_NoUpdate
        ):
            time.sleep(0.01)
            iter += 1
            if iter % 100 == 0:
                logger.debug("Waiting for pos tracking data to become ready")

    return iter < maxIter


if __name__ == "__main__":
    import numpy as np

    handler = logging.StreamHandler()
    handler.setLevel(logging.DEBUG)
    logger.addHandler(handler)
    logger.setLevel(logging.DEBUG)

    caps = capi.ClientCapabilities.EyeTracking + capi.ClientCapabilities.EyesImage
    with Headset(caps) as headset, headset.createCompositor() as compositor:
        connectToHeadset(headset._headset, headset._caps)

        logger.info("Headset connected: {}".format(headset.isHardwareConnected()))
        logger.info("Headset checkVersions: {}".format(headset.checkSoftwareVersions()))
        logger.info("Versions: {}".format(headset.querySoftwareVersions()))
        logger.info("Compositor ready: {}".format(compositor.isReady()))

        # Wrappers of our capi always returns a Result[T].
        # One always has to check its validity either by checking Result[T].__bool__()
        # or more expclicitly Result[T].isValid() before extracting the value of T.
        r: Result[bool] = headset.isEyeTrackingEnabled()
        if not r:
            logger.error("Eye tracking: {}".format(r.error))
        else:
            logger.info("Tracking: {}".format(r.value))

        r = headset.isEyeTrackingCalibrated()
        # In a boolean context, r is a shorthand for r.isValid()
        if not r:
            logger.error("Calibration error: {}".format(r.error))
        else:
            logger.info("Calibrated: {}".format(r.value))

        while True:
            # On each loop, one calls this blocking function to wait for eye tracking
            # to process a single frame.
            # Note that the wait may timeout for one reason or another,
            # so one needs a strategy for handing the that condition.
            # Here, we just retry.
            res: Result[None] = headset.waitForProcessedEyeFrame()
            if not res:
                if res.error == capi.ErrorCode.API_Timeout:
                    logger.warning("Wait for eye frame timed out")
                    continue
                logger.error("Failed to sync eye frame: {}".format(res.error))
                break

            # When the frame is available, explicitly fetch the data so that the
            # following calls to "getters" give the updated data
            timestamp: Result[capi.FrameTimestamp] = headset.fetchEyeTrackingData()
            if not timestamp:
                logger.error(
                    "Failed to fetch eye tracking data: {}".format(timestamp.error)
                )
            logger.debug("Data timestamp updated to: {}".format(timestamp.value))

            # Fetch the latest eyes image
            timestamp2: Result[capi.FrameTimestamp] = headset.fetchEyesImage()
            if not timestamp2:
                logger.error("Failed to fetch eye image: {}".format(timestamp2.error))
            logger.debug("Eye image timestamp updated to: {}".format(timestamp2.value))

            v: Result[capi.Vec3] = headset.getGazeVector(capi.Eye.Left)
            if not v:
                logger.error("Failed to get left gaze vector: {}".format(v.error))
            else:
                lVec = np.array(v.value)
                logger.debug("Left gaze vector:  {} ({})".format(lVec, v.error))

            v = headset.getGazeVector(capi.Eye.Left)
            if not v:
                logger.error("Failed to get right gaze vector: {} ".format(v.error))
            else:
                rVec = np.array(v.value)
                logger.debug("Right gaze vector: {} ({})".format(lVec, v.error))

            v2: Result[capi.Ray] = headset.getCombinedGazeRay()
            if not v2:
                logger.error("Failed to get gaze ray: {}".format(v2.error))
            else:
                gazeRay = np.array(v2.value)
                logger.debug("Gaze ray: {} ({})".format(gazeRay, v2.error))

            img: Result[capi.BitmapImage] = headset.getEyesImage()
            if not img:
                logger.error("Failed to get eye images: {}".format(img.error))
            else:
                arr = np.array(img.value.image)
                with open("data.bmp", "wb") as fout:
                    arr.tofile(fout)
                    time.sleep(3)
