/**
 * File: faceTrackerImpl_okao.cpp
 *
 * Author: Andrew Stein
 * Date:   11/30/2015
 *
 * Description: Wrapper for OKAO Vision face detection library.
 *
 * NOTE: This file should only be included by faceTracker.cpp
 *
 * Copyright: Anki, Inc. 2015
 **/

#if FACE_TRACKER_PROVIDER == FACE_TRACKER_OKAO

#include "faceTrackerImpl_okao.h"

#include "coretech/common/engine/math/quad_impl.h"
#include "coretech/vision/engine/camera.h"
#include "coretech/vision/engine/okaoParamInterface.h"

#include "util/console/consoleInterface.h"
#include "util/logging/logging.h"
#include "util/random/randomGenerator.h"

#include "opencv2/calib3d.hpp"

#define LOG_CHANNEL "FaceRecognizer"

namespace Anki {
namespace Vision {

  namespace FaceEnrollParams {
    // Faces are not enrollable unless the tracker is above this confidence
    // NOTE: It appears the returned track confidence is set to the fixed value of whatever
    //   the OKAO detection threshold is set to when in default tracking accuracy mode,
    //   so this parameter will have no effect unless the high-accuracy tracker is used
    CONSOLE_VAR(s32, kMinDetectionConfidence,       "Vision.FaceTracker",  500);

    CONSOLE_VAR(f32, kCloseDistanceBetweenEyesMin,  "Vision.FaceTracker",  64.f);
    CONSOLE_VAR(f32, kCloseDistanceBetweenEyesMax,  "Vision.FaceTracker",  128.f);
    CONSOLE_VAR(f32, kFarDistanceBetweenEyesMin,    "Vision.FaceTracker",  16.f);
    CONSOLE_VAR(f32, kFarDistanceBetweenEyesMax,    "Vision.FaceTracker",  32.f);
    CONSOLE_VAR(f32, kLookingStraightMaxAngle_deg,  "Vision.FaceTracker",  25.f);
    //CONSOLE_VAR(f32, kLookingLeftRightMinAngle_deg,  "Vision.FaceTracker",  10.f);
    //CONSOLE_VAR(f32, kLookingLeftRightMaxAngle_deg,  "Vision.FaceTracker",  20.f);
    CONSOLE_VAR(f32, kLookingUpMinAngle_deg,        "Vision.FaceTracker",  25.f);
    CONSOLE_VAR(f32, kLookingUpMaxAngle_deg,        "Vision.FaceTracker",  45.f);
    CONSOLE_VAR(f32, kLookingDownMinAngle_deg,      "Vision.FaceTracker", -10.f);
    CONSOLE_VAR(f32, kLookingDownMaxAngle_deg,      "Vision.FaceTracker", -25.f);

    // No harm in using fixed seed here (just for shuffling order of processing
    // multiple faces in the same image). It's hard to use CozmoContext's RNG here
    // because this runs on a different thread and has no robot/context.
    static const uint32_t kRandomSeed = 1;
  }

  // Assuming a max face detection of 3m, focal length of 300 and distanceBetweenEyes_mm of 62
  // then the smallest distance between eyes in pixels will be ~6
  static const f32 MinDistBetweenEyes_pixels = 6;

  // Average distance between human eyes, used to estimate translation
  static const f32 DistanceBetweenEyes_mm = 62.f;

  // Use this to trigger a reinitialization on next Update()
  #if REMOTE_CONSOLE_ENABLED
  CONSOLE_VAR(bool, kReinitDetector,              "Vision.FaceDetectorCommon", false);
  #endif // REMOTE_CONSOLE_ENABLED

  CONSOLE_VAR(bool, kUseUndistortionForFacePose,  "Vision.FaceDetectorCommon", true);
  CONSOLE_VAR(bool, kAdjustEyeDistByYaw,          "Vision.FaceDetectorCommon", true);
  CONSOLE_VAR(bool, kKeepUndistortedFaceFeatures, "Vision.FaceDetectorCommon", false);

  namespace DetectParams {
    // Parameters common to all face detection modes
    CONSOLE_VAR_RANGED(s32,                  kMaxDetectedFaces,     "Vision.FaceDetectorCommon", 10, 1, 1023);
    CONSOLE_VAR_RANGED(s32,                  kMinFaceSize,          "Vision.FaceDetectorCommon", 48, 20, 8192);
    CONSOLE_VAR_RANGED(s32,                  kMaxFaceSize,          "Vision.FaceDetectorCommon", 640, 20, 8192);
    CONSOLE_VAR_ENUM(s32,                    kPoseAngle,            "Vision.FaceDetectorCommon", Okao::GetIndex(Okao::PoseAngle::Front), Okao::GetConsoleString<Okao::PoseAngle>().c_str());
    CONSOLE_VAR_ENUM(s32,                    kRollAngle,            "Vision.FaceDetectorCommon", Okao::GetIndex(Okao::RollAngle::UpperPm45), Okao::GetConsoleString<Okao::RollAngle>().c_str());
    CONSOLE_VAR_ENUM(s32,                    kSearchDensity,        "Vision.FaceDetectorCommon", Okao::GetIndex(Okao::SearchDensity::Normal), Okao::GetConsoleString<Okao::SearchDensity>().c_str());
    CONSOLE_VAR_RANGED(s32,                  kFaceDetectionThreshold,        "Vision.FaceDetectorCommon", 500, 1, 1000);
    CONSOLE_VAR_ENUM(s32,                    kDetectionMode,        "Vision.FaceDetectorCommon", Okao::GetIndex(Okao::DetectionMode::Movie), Okao::GetConsoleString<Okao::DetectionMode>().c_str());

    // Movie only
    CONSOLE_VAR_RANGED(s32,                  kSearchInitialCycle,   "Vision.FaceDetectorMovie", 2, 1, 45);
    CONSOLE_VAR_RANGED(s32,                  kSearchNewCycle,       "Vision.FaceDetectorMovie", 2, 1, 45);
    CONSOLE_VAR_RANGED(s32,                  kSearchNewInterval,    "Vision.FaceDetectorMovie", 5, -1, 45);
    CONSOLE_VAR_RANGED(s32,                  kLostMaxRetry,         "Vision.FaceDetectorMovie", 2, 0, 300);
    CONSOLE_VAR_RANGED(s32,                  kLostMaxHold,          "Vision.FaceDetectorMovie", 2, 0, 300);
    CONSOLE_VAR_RANGED(s32,                  kSteadinessPosition,   "Vision.FaceDetectorMovie", 10, 0, 30);
    CONSOLE_VAR_RANGED(s32,                  kSteadinessSize,       "Vision.FaceDetectorMovie", 10, 0, 30);
    CONSOLE_VAR_RANGED(s32,                  kTrackingSwapRatio,    "Vision.FaceDetectorMovie", 400, 100, 10000);
    CONSOLE_VAR_RANGED(s32,                  kDelayCount,           "Vision.FaceDetectorMovie", 1, 0, 10);
    CONSOLE_VAR_ENUM(s32,                    kTrackingAccuracy,     "Vision.FaceDetectorMovie", Okao::GetIndex(Okao::TrackingAccuracy::High), Okao::GetConsoleString<Okao::TrackingAccuracy>().c_str());
    CONSOLE_VAR(     bool,                   kEnableAngleExtension, "Vision.FaceDetectorMovie", false);
    CONSOLE_VAR(     bool,                   kEnablePoseExtension,  "Vision.FaceDetectorMovie", true);
    
    // When setting this to true, we were seeing worse part detection performance while tracking.
    // The nPose field in the DetectionInfo struct was sometimes "HEAD" (meaning back of head).
    // From the Omron team:
    //   It returned "Head" because you set bUseHeadTracking as TRUE of OKAO_DT_MV_SetPoseExtension().
    //   (It's default value is FALSE.)
    //   Face Detection engine output "Head" only by tracking, not from the first frame or Still Mode.
    //   It is good for keeping tracking, but not good for Facial Parts Detection.
    //   If you give priority to Facial Parts Detection over tracking, you should turn bUseHeadTracking
    //   off or skip the face.
    // So I'm defaulting this to false, and it seems to help in testing.
    CONSOLE_VAR(     bool,                   kUseHeadTracking,      "Vision.FaceDetectorMovie", false);
    
    CONSOLE_VAR(     bool,                   kDirectionMask,        "Vision.FaceDetectorMovie", false);
  }

  FaceTracker::Impl::Impl(const Camera& camera,
                          const std::string& modelPath,
                          const Json::Value& config)
  : _camera(camera)
  , _recognizer(config)
  , _rng(new Util::RandomGenerator(FaceEnrollParams::kRandomSeed))
  {
    Profiler::SetProfileGroupName("FaceTracker.Profiler");

    Result initResult = Init();
    if(initResult != RESULT_OK) {
      LOG_ERROR("FaceTrackerImpl.Constructor.InitFailed", "");
    }

  } // Impl Constructor()

  Result FaceTracker::Impl::Init()
  {
    _isInitialized = false;

    // Get and print Okao library version as a sanity check that we can even
    // talk to the library
    UINT8 okaoVersionMajor = 0, okaoVersionMinor = 0;
    INT32 okaoResult = OKAO_CO_GetVersion(&okaoVersionMajor, &okaoVersionMinor);
    if(okaoResult != OKAO_NORMAL) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibVersionFail", "");
      return RESULT_FAIL;
    }
    LOG_INFO("FaceTrackerImpl.Init.FaceLibVersion",
             "Initializing with FaceLib version %d.%d",
             okaoVersionMajor, okaoVersionMinor);
    
    okaoResult = OKAO_DT_GetVersion(&okaoVersionMajor, &okaoVersionMinor);
    if(okaoResult != OKAO_NORMAL) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceDetectorVersionFail", "");
      return RESULT_FAIL;
    }
    LOG_INFO("FaceTrackerImpl.Init.FaceDetectorVersion",
             "Initializing with FaceDetector version %d.%d",
             okaoVersionMajor, okaoVersionMinor);
    
    okaoResult = OKAO_PT_GetVersion(&okaoVersionMajor, &okaoVersionMinor);
    if(okaoResult != OKAO_NORMAL) {
      LOG_ERROR("FaceTrackerImpl.Init.PartDetectorVersionFail", "");
      return RESULT_FAIL;
    }
    LOG_INFO("FaceTrackerImpl.Init.PartDetectorVersion",
             "Initializing with PartDetector version %d.%d",
             okaoVersionMajor, okaoVersionMinor);

    _okaoCommonHandle = OKAO_CO_CreateHandle();
    if(NULL == _okaoCommonHandle) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibCommonHandleNull", "");
      return RESULT_FAIL_MEMORY;
    }

    switch(Okao::GetEnum<Okao::DetectionMode>(DetectParams::kDetectionMode))
    {
      case Okao::DetectionMode::Movie:
      {
        _okaoDetectorHandle = OKAO_DT_CreateHandle(_okaoCommonHandle, DETECTION_MODE_MOVIE, DetectParams::kMaxDetectedFaces);
        if(NULL == _okaoDetectorHandle) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibDetectionHandleAllocFail.VideoMode", "");
          return RESULT_FAIL_MEMORY;
        }

        // Adjust some detection parameters
        okaoResult = OKAO_DT_MV_SetSearchCycle(_okaoDetectorHandle,
                                               DetectParams::kSearchInitialCycle,
                                               DetectParams::kSearchNewCycle,
                                               DetectParams::kSearchNewInterval);
        if(OKAO_NORMAL != okaoResult) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetSearchCycleFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }

        okaoResult = OKAO_DT_MV_SetLostParam(_okaoDetectorHandle,
                                             DetectParams::kLostMaxRetry,
                                             DetectParams::kLostMaxHold);
        if(OKAO_NORMAL != okaoResult) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetLostFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }

        okaoResult = OKAO_DT_MV_SetSteadinessParam(_okaoDetectorHandle,
                                                   DetectParams::kSteadinessPosition,
                                                   DetectParams::kSteadinessSize);
        if(OKAO_NORMAL != okaoResult) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetSteadinessFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }

        okaoResult = OKAO_DT_MV_SetTrackingSwapParam(_okaoDetectorHandle, DetectParams::kTrackingSwapRatio);
        if(OKAO_NORMAL != okaoResult) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetSwapRatioFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }

        okaoResult = OKAO_DT_MV_SetDelayCount(_okaoDetectorHandle, DetectParams::kDelayCount); // have to see faces for more than one frame
        if(OKAO_NORMAL != okaoResult) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetDelayCountFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }

        okaoResult = OKAO_DT_MV_SetAccuracy(_okaoDetectorHandle, Okao::GetOkao<Okao::TrackingAccuracy>(DetectParams::kTrackingAccuracy));
        if(OKAO_NORMAL != okaoResult) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetAccuracyFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }

        okaoResult = OKAO_DT_MV_SetAngleExtension(_okaoDetectorHandle, DetectParams::kEnableAngleExtension);
        if(OKAO_NORMAL != okaoResult) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetAngleExtensionFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }

        okaoResult = OKAO_DT_MV_SetPoseExtension(_okaoDetectorHandle, DetectParams::kEnablePoseExtension,
                                                 DetectParams::kUseHeadTracking);
        if(OKAO_NORMAL != okaoResult) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetPoseExtensionFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }

        okaoResult = OKAO_DT_MV_SetDirectionMask(_okaoDetectorHandle, DetectParams::kDirectionMask);
        if(OKAO_NORMAL != okaoResult) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetDirectionMaskFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }

        break;
      }
      case Okao::DetectionMode::Still:
      {
        _okaoDetectorHandle = OKAO_DT_CreateHandle(_okaoCommonHandle, DETECTION_MODE_STILL, DetectParams::kMaxDetectedFaces);
        if(NULL == _okaoDetectorHandle) {
          LOG_ERROR("FaceTrackerImpl.Init.FaceLibDetectionHandleAllocFail.StillMode", "");
          return RESULT_FAIL_MEMORY;
        }
        break;
      }
      default:
      {
        LOG_ERROR("FaceTrackerImpl.Init.UnknownDetectionMode", "");
        return RESULT_FAIL;
      }
    }

    okaoResult = OKAO_DT_SetSizeRange(_okaoDetectorHandle, DetectParams::kMinFaceSize,
                                      DetectParams::kMaxFaceSize);
    if(OKAO_NORMAL != okaoResult) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetSizeRangeFailed", "");
      return RESULT_FAIL_INVALID_PARAMETER;
    }

    okaoResult = OKAO_DT_SetAngle(_okaoDetectorHandle, Okao::GetOkao<Okao::PoseAngle>(DetectParams::kPoseAngle),
                                  Okao::GetOkao<Okao::RollAngle>(DetectParams::kRollAngle));
    if(OKAO_NORMAL != okaoResult) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetAngleFailed", "");
      return RESULT_FAIL_INVALID_PARAMETER;
    }

    okaoResult = OKAO_DT_SetSearchDensity(_okaoDetectorHandle, Okao::GetOkao<Okao::SearchDensity>(DetectParams::kSearchDensity));
    if(OKAO_NORMAL != okaoResult) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetSearchDensityFailed", "");
      return RESULT_FAIL_INVALID_PARAMETER;
    }

    okaoResult = OKAO_DT_SetThreshold(_okaoDetectorHandle, DetectParams::kFaceDetectionThreshold);
    if(OKAO_NORMAL != okaoResult) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibSetThresholdFailed",
                "FaceLib Result Code=%d", okaoResult);
      return RESULT_FAIL_INVALID_PARAMETER;
    }

    _okaoDetectionResultHandle = OKAO_DT_CreateResultHandle(_okaoCommonHandle);
    if(NULL == _okaoDetectionResultHandle) {
      LOG_ERROR("FacetrackerImpl.Init.FaceLibDetectionResultHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    _okaoPartDetectorHandle = OKAO_PT_CreateHandle(_okaoCommonHandle);
    if(NULL == _okaoPartDetectorHandle) {
      LOG_ERROR("FacetrackerImpl.Init.FaceLibPartDetectorHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    okaoResult = OKAO_PT_SetConfMode(_okaoPartDetectorHandle, PT_CONF_NOUSE);
    if(OKAO_NORMAL != okaoResult) {
      LOG_ERROR("FacetrakerImpl.Init.FaceLibPartDetectorConfModeFail",
                "FaceLib Result Code=%d", okaoResult);
      return RESULT_FAIL_INVALID_PARAMETER;
    }

    _okaoPartDetectionResultHandle = OKAO_PT_CreateResultHandle(_okaoCommonHandle);
    if(NULL == _okaoPartDetectionResultHandle) {
      LOG_ERROR("FacetrackerImpl.Init.FaceLibPartDetectionResultHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    _okaoEstimateExpressionHandle = OKAO_EX_CreateHandle(_okaoCommonHandle);
    if(NULL == _okaoEstimateExpressionHandle) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibEstimateExpressionHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    _okaoExpressionResultHandle = OKAO_EX_CreateResultHandle(_okaoCommonHandle);
    if(NULL == _okaoExpressionResultHandle) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibExpressionResultHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    _okaoSmileDetectHandle = OKAO_SM_CreateHandle();
    if(NULL == _okaoSmileDetectHandle) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibSmileDetectionHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    _okaoSmileResultHandle = OKAO_SM_CreateResultHandle();
    if(NULL == _okaoSmileResultHandle) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibSmileResultHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    _okaoGazeBlinkDetectHandle = OKAO_GB_CreateHandle();
    if(NULL == _okaoGazeBlinkDetectHandle) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibGazeBlinkDetectionHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    _okaoGazeBlinkResultHandle = OKAO_GB_CreateResultHandle();
    if(NULL == _okaoGazeBlinkResultHandle) {
      LOG_ERROR("FaceTrackerImpl.Init.FaceLibGazeBlinkResultHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    Result recognizerInitResult = _recognizer.Init(_okaoCommonHandle);

    if(RESULT_OK == recognizerInitResult) {

      _isInitialized = true;

      LOG_INFO("FaceTrackerImpl.Init.Success",
               "FaceLib Vision handles created successfully.");
    }

    return recognizerInitResult;

  } // Init()


  FaceTracker::Impl::~Impl()
  {
    Deinit();
  }

  void FaceTracker::Impl::Deinit()
  {
    _isInitialized = false;

    //Util::SafeDeleteArray(_workingMemory);
    //Util::SafeDeleteArray(_backupMemory);

    // Must release album handles before common handle
    _recognizer.Shutdown();
    _recognizer.EraseAllFaces();

    if(NULL != _okaoSmileDetectHandle) {
      if(OKAO_NORMAL != OKAO_SM_DeleteHandle(_okaoSmileDetectHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibSmileDetectHandleDeleteFail", "");
      }
    }

    if(NULL != _okaoSmileResultHandle) {
      if(OKAO_NORMAL != OKAO_SM_DeleteResultHandle(_okaoSmileResultHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibSmileResultHandleDeleteFail", "");
      }
    }

    if(NULL != _okaoGazeBlinkDetectHandle) {
      if(OKAO_NORMAL != OKAO_GB_DeleteHandle(_okaoGazeBlinkDetectHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibGazeBlinkDetectHandleDeleteFail", "");
      }
    }

    if(NULL != _okaoGazeBlinkResultHandle) {
      if(OKAO_NORMAL != OKAO_GB_DeleteResultHandle(_okaoGazeBlinkResultHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibGazeBlinkResultHandleDeleteFail", "");
      }
    }

    if(NULL != _okaoExpressionResultHandle) {
      if(OKAO_NORMAL != OKAO_EX_DeleteResultHandle(_okaoExpressionResultHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibExpressionResultHandleDeleteFail", "");
      }
    }

    if(NULL != _okaoEstimateExpressionHandle) {
      if(OKAO_NORMAL != OKAO_EX_DeleteHandle(_okaoEstimateExpressionHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibEstimateExpressionHandleDeleteFail", "");
      }
    }

    if(NULL != _okaoPartDetectionResultHandle) {
      if(OKAO_NORMAL != OKAO_PT_DeleteResultHandle(_okaoPartDetectionResultHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibPartDetectionResultHandle1DeleteFail", "");
      }
    }

    if(NULL != _okaoPartDetectorHandle) {
      if(OKAO_NORMAL != OKAO_PT_DeleteHandle(_okaoPartDetectorHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibPartDetectorHandleDeleteFail", "");
      }
    }

    if(NULL != _okaoDetectionResultHandle) {
      if(OKAO_NORMAL != OKAO_DT_DeleteResultHandle(_okaoDetectionResultHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibDetectionResultHandleDeleteFail", "");
      }
    }

    if(NULL != _okaoDetectorHandle) {
      if(OKAO_NORMAL != OKAO_DT_DeleteHandle(_okaoDetectorHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibDetectorHandleDeleteFail", "");
      }
      _okaoDetectorHandle = NULL;
    }

    if(NULL != _okaoCommonHandle) {
      if(OKAO_NORMAL != OKAO_CO_DeleteHandle(_okaoCommonHandle)) {
        LOG_ERROR("FaceTrackerImpl.Destructor.FaceLibCommonHandleDeleteFail", "");
      }
      _okaoCommonHandle = NULL;
    }

    _isInitialized = false;
  } // ~Impl()

  void FaceTracker::Impl::Reset()
  {
    _allowedTrackedFaceID.clear();
    INT32 result = OKAO_DT_MV_ResetTracking(_okaoDetectorHandle);
    if(OKAO_NORMAL != result)
    {
      LOG_WARNING("FaceTrackerImpl.Reset.FaceLibResetFailure",
                  "FaceLib result=%d", result);
    }

    _recognizer.ClearAllTrackingData();
  }

  void FaceTracker::Impl::ClearAllowedTrackedFaces()
  {
    Reset();
  }

  void FaceTracker::Impl::AddAllowedTrackedFace(const FaceID_t faceID)
  {
    _allowedTrackedFaceID.insert(faceID);
  }

  void FaceTracker::Impl::SetRecognitionIsSynchronous(bool isSynchronous)
  {
    _recognizer.SetIsSynchronous(isSynchronous);
  }

  template<class PointType>
  static inline void SetFeatureHelper(const PointType* faceParts,
                                      const int* faceConfs,
                                      std::vector<s32>&& indices,
                                      TrackedFace::FeatureName whichFeature,
                                      TrackedFace& face)
  {
    TrackedFace::Feature feature;
    TrackedFace::FeatureConfidence confs;
    bool allPointsPresent = true;
    for(auto index : indices) {
      if(faceParts[index].x == FEATURE_NO_POINT ||
         faceParts[index].y == FEATURE_NO_POINT)
      {
        allPointsPresent = false;
        break;
      }
      feature.emplace_back(faceParts[index].x, faceParts[index].y);
      confs.emplace_back(faceConfs[index]);
    }

    if(allPointsPresent) {
      face.SetFeature(whichFeature, std::move(feature), std::move(confs));
    }
  } // SetFeatureHelper()


  bool FaceTracker::Impl::DetectFaceParts(INT32 nWidth, INT32 nHeight, RAWIMAGE* dataPtr,
                                          INT32 detectionIndex, Vision::TrackedFace& face)
  {
    INT32 okaoResult = OKAO_PT_SetPositionFromHandle(_okaoPartDetectorHandle, _okaoDetectionResultHandle, detectionIndex);
    
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.DetectFaceParts.FaceLibSetPositionFail",
                  "FaceLib Result Code=%d", okaoResult);
      return false;
    }
    okaoResult = OKAO_PT_DetectPoint_GRAY(_okaoPartDetectorHandle, dataPtr,
                                          nWidth, nHeight, GRAY_ORDER_Y0Y1Y2Y3, _okaoPartDetectionResultHandle);

    if(OKAO_NORMAL != okaoResult) {
      if(OKAO_ERR_PROCESSCONDITION != okaoResult) {
        LOG_WARNING("FaceTrackerImpl.DetectFaceParts.FaceLibPartDetectionFail",
                    "FaceLib Result Code=%d", okaoResult);
      }
      return false;
    }

    okaoResult = OKAO_PT_GetResult(_okaoPartDetectionResultHandle, PT_POINT_KIND_MAX,
                                   _facialParts, _facialPartConfs);
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.DetectFaceParts.FaceLibGetFacePartResultFail",
                  "FaceLib Result Code=%d", okaoResult);
      return false;
    }

    // Set eye centers
    face.SetEyeCenters(Point2f(_facialParts[PT_POINT_LEFT_EYE].x,
                               _facialParts[PT_POINT_LEFT_EYE].y),
                       Point2f(_facialParts[PT_POINT_RIGHT_EYE].x,
                               _facialParts[PT_POINT_RIGHT_EYE].y));

    // Set other facial features
    SetFeatureHelper(_facialParts, _facialPartConfs, {
      PT_POINT_LEFT_EYE_OUT, PT_POINT_LEFT_EYE, PT_POINT_LEFT_EYE_IN
    }, TrackedFace::FeatureName::LeftEye, face);

    SetFeatureHelper(_facialParts, _facialPartConfs, {
      PT_POINT_RIGHT_EYE_IN, PT_POINT_RIGHT_EYE, PT_POINT_RIGHT_EYE_OUT
    }, TrackedFace::FeatureName::RightEye, face);

    SetFeatureHelper(_facialParts, _facialPartConfs, {
      PT_POINT_NOSE_LEFT, PT_POINT_NOSE_RIGHT
    }, TrackedFace::FeatureName::Nose, face);

    SetFeatureHelper(_facialParts, _facialPartConfs, {
      PT_POINT_MOUTH_LEFT, PT_POINT_MOUTH_UP, PT_POINT_MOUTH_RIGHT,
      PT_POINT_MOUTH, PT_POINT_MOUTH_LEFT,
    }, TrackedFace::FeatureName::UpperLip, face);

    return true;
  }

  Result FaceTracker::Impl::EstimateExpression(INT32 nWidth, INT32 nHeight, RAWIMAGE* dataPtr,
                                               Vision::TrackedFace& face)
  {
    INT32 okaoResult = OKAO_EX_SetPointFromHandle(_okaoEstimateExpressionHandle, _okaoPartDetectionResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.Update.FaceLibSetExpressionPointFail",
                  "FaceLib Result Code=%d", okaoResult);
      return RESULT_FAIL;
    }

    okaoResult = OKAO_EX_Estimate_GRAY(_okaoEstimateExpressionHandle, dataPtr, nWidth, nHeight,
                                       GRAY_ORDER_Y0Y1Y2Y3, _okaoExpressionResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      if(OKAO_ERR_PROCESSCONDITION == okaoResult) {
        // This might happen, depending on face parts
        LOG_INFO("FaceTrackerImpl.Update.FaceLibEstimateExpressionNotPossible", "");
      } else {
        // This should not happen
        LOG_WARNING("FaceTrackerImpl.Update.FaceLibEstimateExpressionFail",
                    "FaceLib Result Code=%d", okaoResult);
        return RESULT_FAIL;
      }
    } else {

      okaoResult = OKAO_EX_GetResult(_okaoExpressionResultHandle, EX_EXPRESSION_KIND_MAX, _expressionValues);
      if(OKAO_NORMAL != okaoResult) {
        LOG_WARNING("FaceTrackerImpl.Update.FaceLibGetExpressionResultFail",
                    "FaceLib Result Code=%d", okaoResult);
        return RESULT_FAIL;
      }

      static const FacialExpression TrackedFaceExpressionLUT[EX_EXPRESSION_KIND_MAX] = {
        FacialExpression::Neutral,
        FacialExpression::Happiness,
        FacialExpression::Surprise,
        FacialExpression::Anger,
        FacialExpression::Sadness
      };

      for(INT32 okaoExprIndex = 0; okaoExprIndex < EX_EXPRESSION_KIND_MAX; ++okaoExprIndex) {
        face.SetExpressionValue(TrackedFaceExpressionLUT[okaoExprIndex],
                                Util::numeric_cast<TrackedFace::ExpressionValue>(_expressionValues[okaoExprIndex]));
      }

    }

    return RESULT_OK;
  } // EstimateExpression()

  Result FaceTracker::Impl::DetectSmile(INT32 nWidth, INT32 nHeight, RAWIMAGE* dataPtr,
                                        Vision::TrackedFace& face)
  {
    INT32 okaoResult = OKAO_SM_SetPointFromHandle(_okaoSmileDetectHandle, _okaoPartDetectionResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.DetectSmile.SetPointFromHandleFailed",
                  "FaceLib Result=%d", okaoResult);
      return RESULT_FAIL;
    }

    okaoResult = OKAO_SM_Estimate(_okaoSmileDetectHandle, dataPtr, nWidth, nHeight, _okaoSmileResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.DetectSmile.EstimateFailed",
                  "FaceLib Result=%d", okaoResult);
      return RESULT_FAIL;
    }

    INT32 smileDegree=0;
    INT32 confidence=0;
    okaoResult = OKAO_SM_GetResult(_okaoSmileResultHandle, &smileDegree, &confidence);
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.DetectSmile.GetResultFailed",
                  "FaceLib Result=%d", okaoResult);
      return RESULT_FAIL;
    }

    // NOTE: smileDegree from OKAO is [0,100]. Convert to [0.0, 1.0].
    // Confidence from OKAO is [0,1000]. Also convert to [0.0, 1.0]
    face.SetSmileAmount(static_cast<f32>(smileDegree) * 0.01f, static_cast<f32>(confidence) * 0.001f);

    return RESULT_OK;
  }

  Result FaceTracker::Impl::DetectGazeAndBlink(INT32 nWidth, INT32 nHeight, RAWIMAGE* dataPtr,
                                               Vision::TrackedFace& face)
  {
    INT32 okaoResult = OKAO_GB_SetPointFromHandle(_okaoGazeBlinkDetectHandle, _okaoPartDetectionResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.DetectGazeAndBlink.SetPointFromHandleFailed",
                  "FaceLib Result=%d", okaoResult);
      return RESULT_FAIL;
    }
    
    okaoResult = OKAO_GB_Estimate(_okaoGazeBlinkDetectHandle, dataPtr, nWidth, nHeight, _okaoGazeBlinkResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.DetectGazeAndBlink.EstimateFailed",
                  "FaceLib Result=%d", okaoResult);
      return RESULT_FAIL;
    }
    
    if(_detectGaze)
    {
      INT32 gazeLeftRight_deg = 0;
      INT32 gazeUpDown_deg    = 0;
      okaoResult = OKAO_GB_GetGazeDirection(_okaoGazeBlinkResultHandle, &gazeLeftRight_deg, &gazeUpDown_deg);
      if(OKAO_NORMAL != okaoResult) {
        LOG_WARNING("FaceTrackerImpl.DetectGazeAndBlink.GetGazeDirectionFailed",
                    "FaceLib Result=%d", okaoResult);
        return RESULT_FAIL;
      }
      
      face.SetGaze(gazeLeftRight_deg, gazeUpDown_deg);
    }
    
    if(_detectBlinks)
    {
      INT32 blinkDegreeLeft  = 0;
      INT32 blinkDegreeRight = 0;
      okaoResult = OKAO_GB_GetEyeCloseRatio(_okaoGazeBlinkResultHandle, &blinkDegreeLeft, &blinkDegreeRight);
      if(OKAO_NORMAL != okaoResult) {
        LOG_WARNING("FaceTrackerImpl.DetectGazeAndBlink.GetEyeCloseRatioFailed",
                    "FaceLib Result=%d", okaoResult);
        return RESULT_FAIL;
      }

      // NOTE: blinkDegree from OKAO is [0,1000]. Convert to [0.0, 1.0]
      face.SetBlinkAmount(static_cast<f32>(blinkDegreeLeft) * 0.001f, static_cast<f32>(blinkDegreeRight) * 0.001f);
    }

    return RESULT_OK;
  }

  bool FaceTracker::Impl::DetectEyeContact(const TrackedFace& face,
                                           const TimeStamp_t& timeStamp)
  {
    DEV_ASSERT(face.IsTranslationSet(), "FaceTrackerImpl.DetectEyeContact.FaceTranslationNotSet");
    auto& entry = _facesEyeContact[face.GetID()];
    entry.Update(face, timeStamp);

    // Check if the face is stale
    bool eyeContact = false;
    if (entry.GetExpired(timeStamp))
    {
      _facesEyeContact.erase(face.GetID());
    }
    else
    {
      eyeContact = entry.IsMakingEyeContact();
    }
    return eyeContact;
  }

  static Vec3f GetTranslation(const Point2f& leftEye, const Point2f& rightEye, const f32 intraEyeDist,
                              const CameraCalibration& scaledCalib)
  {
    // Get unit vector along camera ray from the point between the eyes in the image
    Point2f eyeMidPoint(leftEye);
    eyeMidPoint += rightEye;
    eyeMidPoint *= 0.5f;

    Vec3f ray(eyeMidPoint.x(), eyeMidPoint.y(), 1.f);
    ray = scaledCalib.GetInvCalibrationMatrix() * ray;
    ray.MakeUnitLength();

    ray *= scaledCalib.GetFocalLength_x() * DistanceBetweenEyes_mm / intraEyeDist;

    return ray;
  }

  Result FaceTracker::Impl::SetFacePoseWithoutParts(const s32 nrows, const s32 ncols, TrackedFace& face, f32& intraEyeDist)
  {
    // Without face parts detected (which includes eyes), use fake eye centers for finding pose
    auto const& rect = face.GetRect();
    DEV_ASSERT(rect.Area() > 0, "FaceTrackerImpl.SetFacePoseWithoutParts.InvalidFaceRectangle");
    const Point2f leftEye( rect.GetXmid() - .25f*rect.GetWidth(),
                          rect.GetYmid() - .125f*rect.GetHeight() );
    const Point2f rightEye( rect.GetXmid() + .25f*rect.GetWidth(),
                           rect.GetYmid() - .125f*rect.GetHeight() );

    intraEyeDist = std::max((rightEye - leftEye).Length(), MinDistBetweenEyes_pixels);

    const CameraCalibration& scaledCalib = _camera.GetCalibration()->GetScaled(nrows, ncols);

    // Use the eye positions and raw intra-eye distance to compute the head's translation
    const Vec3f& T = GetTranslation(leftEye, rightEye, intraEyeDist, scaledCalib);
    Pose3d headPose = face.GetHeadPose();
    headPose.SetTranslation(T);
    headPose.SetParent(_camera.GetPose());
    face.SetHeadPose(headPose);

    Pose3d eyePose = face.GetEyePose();
    eyePose.SetTranslation(T);
    eyePose.SetParent(_camera.GetPose());
    face.SetEyePose(eyePose);

    // We don't know anything about orientation without parts, so don't update it and assume
    // _not_ facing the camera (without actual evidence that we are)
    face.SetIsFacingCamera(false);

    return RESULT_OK;
  }

  Result FaceTracker::Impl::SetFacePoseFromParts(const s32 nrows, const s32 ncols, TrackedFace& face, f32& intraEyeDist)
  {
    // Init outputs to zero in case anything goes wrong
    intraEyeDist = 0.f;

    if(!ANKI_VERIFY(_camera.IsCalibrated(), "FaceTrackerImpl.SetFacePoseFromParts.CameraNotCalibrated", ""))
    {
      return RESULT_FAIL;
    }

    // Little local (likely inlineable) helper to check if the offset has been set yet, for readability purposes
    auto IsFirstPointOffsetSet = [](INT32 offset) -> bool {
      return (offset != -1);
    };

    // Index of first landmark point within (INT32*)HPTRESULT.
    static INT32 kFirstPointOffset = -1;
    if(!IsFirstPointOffsetSet(kFirstPointOffset))
    {
      // The first time through, empirically determime where the first point is in the void* data structure
      // we get from OKAO. We have already called OKAO_PT_GetResult to populate _facialParts, so we can look for
      // where those values are located within the data structure. The reason we don't just hard-code this, is
      // that we observed a difference b/w the Mac and Android/ARM OKAO libs in how this data structure is
      // organized/packed, so it feels safer to have this as "self documenting" code.
      const INT32* pt = (INT32*)_okaoPartDetectionResultHandle;
      for(INT32 i=0; i<PT_POINT_KIND_MAX; ++i)
      {
        if(!IsFirstPointOffsetSet(kFirstPointOffset))
        {
          // Not set yet: see if we have found the first point in the HPTRESULT yet
          if( (pt[i] == _facialParts[0].x) && (pt[i+1] == _facialParts[0].y) )
          {
            LOG_INFO("FaceTrackerImpl.SetFacePoseFromParts.SetFirstPointOffset", "kFirstPointOffset=%d", i);
            kFirstPointOffset = i;
            break;
          }
        }
      }

      if(ANKI_DEV_CHEATS && IsFirstPointOffsetSet(kFirstPointOffset))
      {
        // Sanity check the FirstPointOffset we just found (all following facialParts points should match too)
        pt = (INT32*)_okaoPartDetectionResultHandle + kFirstPointOffset;
        for(INT32 i=0; i<PT_POINT_KIND_MAX; ++i, pt += 2)
        {
          if(!ANKI_VERIFY((*pt == _facialParts[i].x) && (*(pt+1) == _facialParts[i].y),
                          "FaceTrackerImpl.SetFacePoseFromParts.PointMisMatch",
                          "Point in HPTRESULT data structure (%d,%d) does not match expected (%d,%d)",
                          *pt, *(pt+1), _facialParts[i].x, _facialParts[i].y))
          {
            return RESULT_FAIL;
          }
        }
      }
    }

    if(!ANKI_VERIFY(IsFirstPointOffsetSet(kFirstPointOffset),
                    "FaceTrackerImpl.SetFacePoseFromParts.FirstPointOffSetNotSet", ""))
    {
      return RESULT_FAIL;
    }

    // What I'm about to do is terrible. But OKAO forced my hand by making their HPTRESULT a void* and
    // having it be the only way to get the roll, pitch, and yaw of the face.
    // I'm going to undistort the points internally so that I can pass an undistorted HPTRESULT to
    // GetFaceDirection in order to estimate an improved set of rotation angles.
    INT32* pt = (INT32*)_okaoPartDetectionResultHandle + kFirstPointOffset;
    std::vector<cv::Point2f> distortedPoints(PT_POINT_KIND_MAX);
    for(INT32 i=0; i<PT_POINT_KIND_MAX; ++i)
    {
      auto & distortedPoint = distortedPoints[i];
      distortedPoint.x = (f32) (*pt);
      ++pt;
      distortedPoint.y = (f32) (*pt);
      ++pt;

      // This is to check that we have the kFirstPointOffset set correctly (as well as the assumption
      // that the rest of the x/y entries are contiguous after that)
      DEV_ASSERT( (distortedPoint.x == _facialParts[i].x) && (distortedPoint.y == _facialParts[i].y),
                 "FaceTrackerImpl.SetFacePoseFromParts.BadPointIndexing");
    }

    // Undistort the part locations
    // TODO: consider using Vision::Undistorter for this (challenge: here relying on vectors of cv::Point2f)
    auto const& calib = _camera.GetCalibration()->GetScaled(nrows, ncols);
    std::vector<cv::Point2f> undistortedPoints(distortedPoints.size());

    cv::Matx<f32,3,3> K = calib.GetCalibrationMatrix().get_CvMatx_();
    const std::vector<f32>& distCoeffs = calib.GetDistortionCoeffs();

    try
    {
      cv::undistortPoints(distortedPoints, undistortedPoints, K, distCoeffs, cv::noArray(), K);
    }
    catch(const cv::Exception& e)
    {
      LOG_ERROR("FaceTrackerImpl.SetFacePoseFromParts.UndistortFailed",
                "OpenCV Error: %s", e.what());
      return RESULT_FAIL;
    }

    if(kUseUndistortionForFacePose)
    {
      // Fill the HPTRESULT with the undistorted points
      pt = (INT32*)_okaoPartDetectionResultHandle + kFirstPointOffset;
      for(auto const& undistortedPoint : undistortedPoints)
      {
        *pt = std::round(undistortedPoint.x);
        ++pt;
        *pt = std::round(undistortedPoint.y);
        ++pt;
      }
    }

    // Fill in head orientation, using undistorted landmark locations so we are more accurate
    INT32 roll_deg=0, pitch_deg=0, yaw_deg=0;
    INT32 okaoDirResult = OKAO_PT_GetFaceDirection(_okaoPartDetectionResultHandle, &pitch_deg, &yaw_deg, &roll_deg);

    // Get the undistorted eye locations to use for computing translation below
    POINT undistortedParts[PT_POINT_KIND_MAX];
    INT32 undistortedConfs[PT_POINT_KIND_MAX];
    INT32 okaoGetResult = OKAO_PT_GetResult(_okaoPartDetectionResultHandle, PT_POINT_KIND_MAX,
                                            undistortedParts, undistortedConfs);

    // Put back the original distorted points since the remainder of their usage also needs
    // corresponding image data, which we have _not_ undistorted (to save the computation)
    pt = (INT32*)_okaoPartDetectionResultHandle + kFirstPointOffset;
    for(auto const& distortedPoint : distortedPoints)
    {
      *pt = distortedPoint.x;
      ++pt;
      *pt = distortedPoint.y;
      ++pt;
    }

    // Handle errors here, _after_ restoring distorted points
    if(OKAO_NORMAL != okaoDirResult) {
      LOG_WARNING("FaceTrackerImpl.SetFacePoseFromParts.FaceLibGetFaceDirectionFail",
                  "FaceLib Result Code=%d", okaoDirResult);
      return RESULT_FAIL;
    }
    if(OKAO_NORMAL != okaoGetResult) {
      LOG_WARNING("FaceTrackerImpl.SetFacePoseFromParts.FaceLibGetResultFail",
                  "FaceLib Result Code=%d", okaoGetResult);
      return RESULT_FAIL;
    }

    face.SetHeadOrientation(DEG_TO_RAD(roll_deg),
                            DEG_TO_RAD(pitch_deg),
                            DEG_TO_RAD(yaw_deg));

    if(std::abs(roll_deg)  <= FaceEnrollParams::kLookingStraightMaxAngle_deg &&
       std::abs(pitch_deg) <= FaceEnrollParams::kLookingStraightMaxAngle_deg &&
       std::abs(yaw_deg)   <= FaceEnrollParams::kLookingStraightMaxAngle_deg)
    {
      face.SetIsFacingCamera(true);
    }
    else
    {
      face.SetIsFacingCamera(false);
    }

    // Compute initial intra-eye distance
    const Point2f leftEye(undistortedParts[PT_POINT_LEFT_EYE].x, undistortedParts[PT_POINT_LEFT_EYE].y);
    const Point2f rightEye(undistortedParts[PT_POINT_RIGHT_EYE].x, undistortedParts[PT_POINT_RIGHT_EYE].y);
    intraEyeDist = std::max((leftEye-rightEye).Length(), MinDistBetweenEyes_pixels);

    if(kAdjustEyeDistByYaw)
    {
      // Adjust intra-eye distance to take yaw into account
      f32 yawAdjFrac = std::cos(face.GetHeadYaw().ToFloat());

      if(!Util::IsNearZero(yawAdjFrac))
      {
        intraEyeDist /= yawAdjFrac;
      }
    }

    // Use the eye positions and yaw-adjusted intra-eye distance to compute the head's translation
    const Vec3f& T = GetTranslation(leftEye, rightEye, intraEyeDist, calib);
    Pose3d headPose = face.GetHeadPose();
    headPose.SetTranslation(T);

    // The okao coordindate system is based around the face instead of around the robot
    // and is different than the anki coordindate system. Specifically the x-axis points
    // out of the detected faces nose, the z-axis points of the top of the detected faces
    // head, and the y-axis points out of the left ear of the detected face. Thus the
    // Yaw and Roll angles map without change onto our coordinate system, while the pitch
    // needs to be negated to map correctly from the okao coordinate system
    // to the anki coordindate system.
    const RotationMatrix3d faceRotation(-face.GetHeadPitch(), face.GetHeadRoll(), face.GetHeadYaw());
    headPose.SetRotation(headPose.GetRotation() * faceRotation);

    headPose.SetParent(_camera.GetPose());
    face.SetHeadPose(headPose);

    // This works very similar to the way that the face angles from okao work. The gaze
    // angles are relative to the image plane and are independent of the head rotation
    // angles. Thus to set this in our pose tree and world space we only need update the
    // default eye pose rotation matrix (which is looking orthogonal towards the image
    // plane) and the translation. For right now roll angles are ignored, since
    // that isn't a natural movement of the eye. However, this could occur by the head
    // rotation but since okao doesn't handle this case, neither do we.
    Pose3d eyePose = face.GetEyePose();
    eyePose.SetTranslation(T);
    const Gaze& gaze = face.GetGaze();
    Radians upDown_rad(DEG_TO_RAD(gaze.upDown_deg));
    Radians leftRight_rad(DEG_TO_RAD(gaze.leftRight_deg));
    const RotationMatrix3d eyeRotation(-upDown_rad, 0.f, leftRight_rad);
    eyePose.SetRotation(eyePose.GetRotation() * eyeRotation);
    face.SetEyePose(eyePose);

    if(kKeepUndistortedFaceFeatures)
    {
      // Set face's eyes to their undistorted locations
      face.SetEyeCenters(Point2f(undistortedPoints[PT_POINT_LEFT_EYE].x,
                                 undistortedPoints[PT_POINT_LEFT_EYE].y),
                         Point2f(undistortedPoints[PT_POINT_RIGHT_EYE].x,
                                 undistortedPoints[PT_POINT_RIGHT_EYE].y));

      // Set other facial features to their undistorted locations
      SetFeatureHelper(undistortedPoints.data(),
                       face.GetFeatureConfidence(TrackedFace::FeatureName::LeftEye).data(),
      {
        PT_POINT_LEFT_EYE_OUT, PT_POINT_LEFT_EYE, PT_POINT_LEFT_EYE_IN
      }, TrackedFace::FeatureName::LeftEye, face);

      SetFeatureHelper(undistortedPoints.data(),
                       face.GetFeatureConfidence(TrackedFace::FeatureName::RightEye).data(),
      {
        PT_POINT_RIGHT_EYE_IN, PT_POINT_RIGHT_EYE, PT_POINT_RIGHT_EYE_OUT
      }, TrackedFace::FeatureName::RightEye, face);

      SetFeatureHelper(undistortedPoints.data(),
                       face.GetFeatureConfidence(TrackedFace::FeatureName::Nose).data(),
      {
        PT_POINT_NOSE_LEFT, PT_POINT_NOSE_RIGHT
      }, TrackedFace::FeatureName::Nose, face);

      SetFeatureHelper(undistortedPoints.data(),
                       face.GetFeatureConfidence(TrackedFace::FeatureName::UpperLip).data(),
      {
        PT_POINT_MOUTH_LEFT, PT_POINT_MOUTH_UP, PT_POINT_MOUTH_RIGHT,
        PT_POINT_MOUTH, PT_POINT_MOUTH_LEFT,
      }, TrackedFace::FeatureName::UpperLip, face);
    }

    return RESULT_OK;
  }

  void FaceTracker::Impl::SetCroppingMask(const INT32 nWidth,
                                          const INT32 nHeight,
                                          const float cropFactor)
  {
    DEV_ASSERT(Util::IsFltGTZero(cropFactor), "FaceTrackerImpl.SetCroppingMask.ZeroCropFactor");
    
    INT32 okaoResult = OKAO_NORMAL;
    
    RECT rcEdgeMask;
    rcEdgeMask.left   = -1;
    rcEdgeMask.top    = -1;
    rcEdgeMask.bottom = -1;
    rcEdgeMask.right  = -1;
    if(Util::IsFltLT(cropFactor, 1.f))
    {
      rcEdgeMask.top = 0;
      rcEdgeMask.bottom = nHeight-1;
      rcEdgeMask.left = std::max(0, (INT32)std::round(0.5*(1.f-cropFactor) * nWidth));
      rcEdgeMask.right = (nWidth-1) - rcEdgeMask.left;
    }
    
    okaoResult = OKAO_DT_SetEdgeMask(_okaoDetectorHandle, rcEdgeMask);
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.SetCroppingMask.FaceLibSetEdgeMaskFail",
                  "FaceLib Result Code=%d, Rect=[%d %d %d %d]",
                  okaoResult, rcEdgeMask.left, rcEdgeMask.top,
                  rcEdgeMask.right, rcEdgeMask.bottom);
    }
    
    if(DetectParams::kDetectionMode == Okao::GetIndex(Okao::DetectionMode::Movie))
    {
      // Tracking edge mask only applies in Movie mode
      okaoResult = OKAO_DT_MV_SetTrackingEdgeMask(_okaoDetectorHandle, rcEdgeMask);
      if(OKAO_NORMAL != okaoResult) {
        LOG_WARNING("FaceTrackerImpl.SetCroppingMask.FaceLibSetTrackingEdgeMaskFail",
                    "FaceLib Result Code=%d, Rect=[%d %d %d %d]",
                    okaoResult, rcEdgeMask.left, rcEdgeMask.top,
                    rcEdgeMask.right, rcEdgeMask.bottom);
      }
    }
  }

  Result FaceTracker::Impl::Update(const Vision::Image& frameOrig,
                                   const float cropFactor,
                                   std::list<TrackedFace>& faces,
                                   std::list<UpdatedFaceID>& updatedIDs,
                                   DebugImageList<CompressedImage>& debugImages)
  {
    if(!_isInitialized) {
      LOG_ERROR("FaceTrackerImpl.Update.NotInitialized", "");
      return RESULT_FAIL;
    }

  #if REMOTE_CONSOLE_ENABLED
    if(kReinitDetector)
    {
      LOG_INFO("FaceTrackerImpl.Update.Reinit",
               "Reinitializing face tracker with current parameters");
      Deinit();
      Init();
      kReinitDetector = false;
    }
  #endif // REMOTE_CONSOLE_ENABLED

    DEV_ASSERT(frameOrig.IsContinuous(), "FaceTrackerImpl.Update.NonContinuousImage");

    const INT32 nWidth  = frameOrig.GetNumCols();
    const INT32 nHeight = frameOrig.GetNumRows();
    
    SetCroppingMask(nWidth, nHeight, cropFactor);
    
    Tic("FaceDetect");
    INT32 okaoResult = OKAO_NORMAL;
    RAWIMAGE* dataPtr = const_cast<UINT8*>(frameOrig.GetDataPointer());
    okaoResult = OKAO_DT_Detect_GRAY(_okaoDetectorHandle, dataPtr, nWidth, nHeight,
                                     GRAY_ORDER_Y0Y1Y2Y3, _okaoDetectionResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.Update.FaceLibDetectFail",
                  "FaceLib Result Code=%d, dataPtr=%p, nWidth=%d, nHeight=%d",
                  okaoResult, dataPtr, nWidth, nHeight);
      return RESULT_FAIL;
    }
    
    INT32 numDetections = 0;
    okaoResult = OKAO_DT_GetResultCount(_okaoDetectionResultHandle, &numDetections);
    if(OKAO_NORMAL != okaoResult) {
      LOG_WARNING("FaceTrackerImpl.Update.FaceLibGetResultCountFail",
                  "FaceLib Result Code=%d", okaoResult);
      return RESULT_FAIL;
    }
    Toc("FaceDetect");

    // Figure out which detected faces we already recognize
    // so that we can choose to run recognition more selectively in the loop below,
    // effectively prioritizing those we don't already recognize
    std::vector<INT32> detectionIndices(numDetections);
    std::set<INT32> skipRecognition;

    for(INT32 detectionIndex=0; detectionIndex<numDetections; ++detectionIndex)
    {
      detectionIndices[detectionIndex] = detectionIndex;

      DETECTION_INFO detectionInfo;
      okaoResult = OKAO_DT_GetRawResultInfo(_okaoDetectionResultHandle, detectionIndex,
                                            &detectionInfo);

      if(OKAO_NORMAL != okaoResult) {
        LOG_WARNING("FaceTrackerImpl.Update.FaceLibGetResultInfoFail1",
                    "Detection index %d of %d. FaceLib Result Code=%d",
                    detectionIndex, numDetections, okaoResult);
        return RESULT_FAIL;
      }
      
      // Don't re-recognize faces we're tracking whose IDs we already know.
      // In this context, a face must be named to be "known" because it's possible
      // (and common!) that we first see a face without matching it to someone
      // already enrolled (e.g. due to difficult pose), and then _later_ realize
      // it's someone we knew, in which case we notify about a updated ID.
      // Note that we don't consider the face currently being enrolled to be
      // "known" because we're in the process of updating it and _want_ to run
      // recognition on it.
      const bool isKnown = _recognizer.HasName(detectionInfo.nID);
      const bool isEnrollmentTrackID = (_recognizer.GetEnrollmentTrackID() == detectionInfo.nID);
      if(isKnown && !isEnrollmentTrackID)
      {
        skipRecognition.insert(detectionInfo.nID);
      }
    }
    
    // Shuffle the set of unrecognized faces so we don't always try the same one.
    // If we know everyone, no need to random shuffle (skip all)
    if(numDetections > 1 && skipRecognition.size() != numDetections)
    {
      std::random_shuffle(detectionIndices.begin(), detectionIndices.end(),
                          [this](int i) { return _rng->RandInt(i); });
    }
    
    for(auto const& detectionIndex : detectionIndices)
    {
      DETECTION_INFO detectionInfo;
      okaoResult = OKAO_DT_GetRawResultInfo(_okaoDetectionResultHandle, detectionIndex,
                                            &detectionInfo);
      
      if(OKAO_NORMAL != okaoResult) {
        LOG_WARNING("FaceTrackerImpl.Update.FaceLibGetResultInfoFail2",
                    "Detection index %d of %d. FaceLib Result Code=%d",
                    detectionIndex, numDetections, okaoResult);
        return RESULT_FAIL;
      }

      if (HaveAllowedTrackedFaces())
      {
        FaceID_t faceID;
        if (_recognizer.GetFaceIDFromTrackingID(detectionInfo.nID, faceID))
        {
          if (_allowedTrackedFaceID.count(faceID) == 0)
          {
            continue; 
          }
        }
      }
      
      // Add a new face to the list
      faces.emplace_back();

      TrackedFace& face = faces.back();

      face.SetIsBeingTracked(detectionInfo.nDetectionMethod != DET_METHOD_DETECTED_HIGH);

      POINT ptLeftTop, ptRightTop, ptLeftBottom, ptRightBottom;
      okaoResult = OKAO_CO_ConvertCenterToSquare(detectionInfo.ptCenter,
                                                 detectionInfo.nHeight,
                                                 0, &ptLeftTop, &ptRightTop,
                                                 &ptLeftBottom, &ptRightBottom);
      if(OKAO_NORMAL != okaoResult) {
        LOG_WARNING("FaceTrackerImpl.Update.FaceLibCenterToSquareFail",
                    "Detection index %d of %d. FaceLib Result Code=%d",
                    detectionIndex, numDetections, okaoResult);
        return RESULT_FAIL;
      }

      face.SetRect(Rectangle<f32>(ptLeftTop.x, ptLeftTop.y,
                                  ptRightBottom.x-ptLeftTop.x,
                                  ptRightBottom.y-ptLeftTop.y));

      face.SetTimeStamp(frameOrig.GetTimestamp());

      // Do we need to find parts?
      const bool doRecognition = _isRecognitionEnabled && !(skipRecognition.count(detectionInfo.nID)>0);
      const bool doPartDetection = (_detectEmotion ||
                                    _detectSmiling ||
                                    _detectGaze ||
                                    _detectBlinks ||
                                    doRecognition);
      
      // Try finding face parts
      bool facePartsFound = false;
      if(doPartDetection)
      {
        Tic("FacePartDetection");
        facePartsFound = DetectFaceParts(nWidth, nHeight, dataPtr, detectionIndex, face);
        Toc("FacePartDetection");
      }

      // Will be computed from detected eyes if face parts are found, or "faked" using
      // face detection rectangle otherwise;
      f32 intraEyeDist = -1.f;

      if(facePartsFound)
      {

        //LOG_INFO("FaceTrackerImpl.Update.HeadOrientation",
        //                 "Roll=%ddeg, Pitch=%ddeg, Yaw=%ddeg",
        //                 roll_deg, pitch_deg, yaw_deg);

        if(_detectEmotion)
        {
          // Expression detection
          Tic("ExpressionRecognition");
          Result expResult = EstimateExpression(nWidth, nHeight, dataPtr, face);
          Toc("ExpressionRecognition");
          if(RESULT_OK != expResult) {
            LOG_WARNING("FaceTrackerImpl.Update.EstimateExpressionFailed",
                        "Detection index %d of %d.",
                        detectionIndex, numDetections);
          }
        } // if(_detectEmotion)
        
        if(_detectSmiling)
        {
          Tic("SmileDetection");
          Result smileResult = DetectSmile(nWidth, nHeight, dataPtr, face);
          Toc("SmileDetection");
          
          if(RESULT_OK != smileResult) {
            LOG_WARNING("FaceTrackerImpl.Update.DetectSmileFailed",
                        "Detection index %d of %d.",
                        detectionIndex, numDetections);
          }
        }
        
        if(_detectGaze || _detectBlinks) // In OKAO, gaze and blink are part of the same detector
        {
          Tic("GazeAndBlinkDetection");
          Result gbResult = DetectGazeAndBlink(nWidth, nHeight, dataPtr, face);
          Toc("GazeAndBlinkDetection");
          
          if(RESULT_OK != gbResult) {
            LOG_WARNING("FaceTrackerImpl.Update.DetectGazeAndBlinkFailed",
                        "Detection index %d of %d.",
                        detectionIndex, numDetections);
          }
        }

        // This needs to happen after we set the gaze, otherwise
        // the eye pose will have the default gaze values
        SetFacePoseFromParts(nHeight, nWidth, face, intraEyeDist);

        if(_detectGaze)
        {
          // This needs to happen after setting the pose.
          // There is a assert in there that should catch if the pose is uninitialized but
          // won't catch on going cases of the dependence.
          face.SetEyeContact(DetectEyeContact(face, frameOrig.GetTimestamp()));
        }

        //
        // Face Recognition:
        //
        

        // Very Verbose:
        //        LOG_DEBUG("FaceTrackerImpl.Update.IsEnrollable",
        //                          "TrackerID:%d EnableEnrollment:%d",
        //                          -detectionInfo.nID, enableEnrollment);
        if(doRecognition)
        {
          const bool enrollable = IsEnrollable(detectionInfo, face, intraEyeDist);
          bool enableEnrollment = enrollable;
          
          // If we have allowed tracked faces we should only enable enrollment
          // in two cases. First if the current face matches the face id returned
          // by GetEnrollmentID. This should only happen in MeetVictor currently.
          // Second if we don't have the tracking id in the recognizer yet, indicating
          // we haven't recognized the face yet. If we don't have any allowed tracked
          // faces we don't need to worry about this and can just use the result from
          // IsEnrollable.
          if(enableEnrollment && HaveAllowedTrackedFaces())
          {
            FaceID_t faceID;
            if (_recognizer.GetFaceIDFromTrackingID(detectionInfo.nID, faceID))
            {
              enableEnrollment &= (faceID == _recognizer.GetEnrollmentID());
            }
          }
          
          _recognizer.SetNextFaceToRecognize(frameOrig,
                                             detectionInfo,
                                             _facialParts,
                                             _facialPartConfs,
                                             enableEnrollment);

          // Very verbose:
          //        else
          //        {
          //          LOG_DEBUG("FaceTrackerImpl.Update.SkipRecognitionForAlreadyKnown",
          //                            "TrackingID %d already known and there are %d faces detected",
          //                            -detectionInfo.nID, numDetections);
          //        }
        }

      }
      else
      {
        // NOTE: Without parts, we do not do eye contact, gaze, face recognition, etc.

        SetFacePoseWithoutParts(nHeight, nWidth, face, intraEyeDist);
      }

      // Get whatever is the latest recognition information for the current tracker ID
      s32 enrollmentCompleted = 0;
      auto recognitionData = _recognizer.GetRecognitionData(detectionInfo.nID, enrollmentCompleted, debugImages);
    
      face.SetBestGuessName(_recognizer.GetBestGuessNameForTrackingID(detectionInfo.nID));

      if(recognitionData.WasFaceIDJustUpdated())
      {
        // We either just assigned a recognition ID to a tracker ID or we updated
        // the recognition ID (e.g. due to merging)
        UpdatedFaceID update{
          .oldID   = (recognitionData.GetPreviousFaceID() == UnknownFaceID ?
                      -detectionInfo.nID : recognitionData.GetPreviousFaceID()),
          .newID   = recognitionData.GetFaceID(),
          .newName = recognitionData.GetName()
        };

        // Update allowed tracked face IDs if one in there just changed
        if(_allowedTrackedFaceID.count(update.oldID)>0)
        {
          LOG_DEBUG("FaceTrackerImpl.Update.UpdatingAllowedTrackedFaceIDs",
                    "Remove %d, Add %d", update.oldID, update.newID);
          _allowedTrackedFaceID.erase(update.oldID);
          _allowedTrackedFaceID.insert(update.newID);
        }
        
        updatedIDs.push_back(std::move(update));
      }

      if(recognitionData.GetFaceID() != UnknownFaceID &&
         recognitionData.GetTrackingID() != recognitionData.GetPreviousTrackingID())
      {
        // We just updated the track ID for a recognized face.
        // So we should notify listeners that tracking ID is now
        // associated with this recognized ID.
        UpdatedFaceID update{
          .oldID   = -recognitionData.GetTrackingID(),
          .newID   = recognitionData.GetFaceID(),
          .newName = recognitionData.GetName()
        };

        // Don't send this update if it turns out to contain the same info as
        // the last one (even if for different reasons)
        if(updatedIDs.empty() ||
           (update.oldID != updatedIDs.back().oldID &&
            update.newID != updatedIDs.back().newID))
        {
          updatedIDs.push_back(std::move(update));
        }
      }

      face.SetScore(recognitionData.GetScore()); // could still be zero!
      if(UnknownFaceID == recognitionData.GetFaceID()) {
        // No recognition ID: use the tracker ID as the face's handle/ID
        DEV_ASSERT(detectionInfo.nID > 0, "FaceTrackerImpl.Update.InvalidTrackerID");
        face.SetID(-detectionInfo.nID);
      } else {
        face.SetID(recognitionData.GetFaceID());
        face.SetName(recognitionData.GetName()); // Could be empty!
        face.SetNumEnrollments(enrollmentCompleted);

        face.SetRecognitionDebugInfo(recognitionData.GetDebugMatchingInfo());
      }

    } // FOR each face

    return RESULT_OK;
  } // Update()

  bool FaceTracker::Impl::CanAddNamedFace() const
  {
    return _recognizer.CanAddNamedFace();
  }

  Result FaceTracker::Impl::AssignNameToID(FaceID_t faceID, const std::string& name, FaceID_t mergeWithID)
  {
    return _recognizer.AssignNameToID(faceID, name, mergeWithID);
  }

  Result FaceTracker::Impl::EraseFace(FaceID_t faceID)
  {
    return _recognizer.EraseFace(faceID);
  }

  void FaceTracker::Impl::EraseAllFaces()
  {
    _recognizer.EraseAllFaces();
  }

  std::vector<Vision::LoadedKnownFace> FaceTracker::Impl::GetEnrolledNames() const
  {
    return _recognizer.GetEnrolledNames();
  }

  Result FaceTracker::Impl::SaveAlbum(const std::string& albumName)
  {
    return _recognizer.SaveAlbum(albumName);
  }

  Result FaceTracker::Impl::RenameFace(FaceID_t faceID, const std::string& oldName, const std::string& newName,
                                       Vision::RobotRenamedEnrolledFace& renamedFace)
  {
    return _recognizer.RenameFace(faceID, oldName, newName, renamedFace);
  }

  Result FaceTracker::Impl::LoadAlbum(const std::string& albumName, std::list<LoadedKnownFace>& loadedFaces)
  {
    if(!_isInitialized) {
      LOG_ERROR("FaceTrackerImpl.LoadAlbum.NotInitialized", "");
      return RESULT_FAIL;
    }

    if(NULL == _okaoCommonHandle) {
      LOG_ERROR("FaceTrackerImpl.LoadAlbum.NullFaceLibCommonHandle", "");
      return RESULT_FAIL;
    }

    return _recognizer.LoadAlbum(albumName, loadedFaces);
  }

  float FaceTracker::Impl::GetMinEyeDistanceForEnrollment()
  {
    return FaceEnrollParams::kFarDistanceBetweenEyesMin;
  }

  void FaceTracker::Impl::SetFaceEnrollmentMode(Vision::FaceID_t forFaceID,
                                                s32 numEnrollments,
                                                bool forceNewID)
  {
    _recognizer.SetAllowedEnrollments(numEnrollments, forFaceID, forceNewID);
  }


  bool FaceTracker::Impl::IsEnrollable(const DETECTION_INFO& detectionInfo, const TrackedFace& face, const f32 intraEyeDist)
  {
#   define DEBUG_ENROLLABILITY 0
    
    if(detectionInfo.nConfidence > FaceEnrollParams::kMinDetectionConfidence &&
       detectionInfo.nPose == POSE_YAW_FRONT &&
       face.IsFacingCamera() &&
       intraEyeDist >= FaceEnrollParams::kFarDistanceBetweenEyesMin)
    {
      return true;
    }
    else if(DEBUG_ENROLLABILITY)
    {
      LOG_DEBUG("FaceTrackerImpl.IsEnrollable.NotLookingStraight",
                "EyeDist=%.1f (vs. %.1f)",
                intraEyeDist, FaceEnrollParams::kFarDistanceBetweenEyesMin);
    }
    
    return false;

  } // IsEnrollable()

  Result FaceTracker::Impl::GetSerializedData(std::vector<u8>& albumData,
                                              std::vector<u8>& enrollData)
  {
    return _recognizer.GetSerializedData(albumData, enrollData);
  }

  Result FaceTracker::Impl::SetSerializedData(const std::vector<u8>& albumData,
                                              const std::vector<u8>& enrollData,
                                              std::list<LoadedKnownFace>& loadedFaces)
  {
    return _recognizer.SetSerializedData(albumData, enrollData, loadedFaces);
  }

#if ANKI_DEVELOPER_CODE
  Result FaceTracker::Impl::DevAddFaceToAlbum(const Image& img, const TrackedFace& face, int albumEntry)
  {
    return _recognizer.DevAddFaceToAlbum(img, face, albumEntry);
  }
  
  Result FaceTracker::Impl::DevFindFaceInAlbum(const Image& img, const TrackedFace& face,
                                            int& albumEntry, float& score) const
  {
    return _recognizer.DevFindFaceInAlbum(img, face, albumEntry, score);
  }
  
  Result FaceTracker::Impl::DevFindFaceInAlbum(const Image& img, const TrackedFace& face, const int maxMatches,
                                            std::vector<std::pair<int, float>>& matches) const
  {
    return _recognizer.DevFindFaceInAlbum(img, face, maxMatches, matches);
  }
  
  float FaceTracker::Impl::DevComputePairwiseMatchScore(int faceID1, int faceID2) const
  {
    return _recognizer.DevComputePairwiseMatchScore(faceID1, faceID2);
  }
  
  float FaceTracker::Impl::DevComputePairwiseMatchScore(int faceID1, const Image& img2, const TrackedFace& face2) const
  {
    return _recognizer.DevComputePairwiseMatchScore(faceID1, img2, face2);
  }
#endif /* ANKI_DEVELOPER_CODE */

#if ANKI_DEV_CHEATS
  void FaceTracker::Impl::SaveAllRecognitionImages(const std::string& imagePathPrefix)
  {
    _recognizer.SaveAllRecognitionImages(imagePathPrefix);
  }

  void FaceTracker::Impl::DeleteAllRecognitionImages()
  {
    _recognizer.DeleteAllRecognitionImages();
  }
#endif // ANKI_DEV_CHEATS

} // namespace Vision
} // namespace Anki

#endif // #if FACE_TRACKER_PROVIDER == FACE_TRACKER_OKAO
