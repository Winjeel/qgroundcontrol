/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

/*!
 * @file
 *   @brief Camera Controller
 *   @author Gus Grubba <gus@auterion.com>
 *
 */

#include "VehicleCameraControl.h"
#include "QGCCameraIO.h"
#include "QGCApplication.h"
#include "SettingsManager.h"
#include "AppSettings.h"
#include "VideoManager.h"
#include "QGCCameraManager.h"
#include "FTPManager.h"
#include "QGCLZMA.h"
#include "QGCCorePlugin.h"
#include "Vehicle.h"
#include "LinkInterface.h"
#include "MAVLinkProtocol.h"
#include "QGCVideoStreamInfo.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtXml/QDomDocument>
#include <QtXml/QDomNodeList>
#include <QtQml/QQmlEngine>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkReply>

//-----------------------------------------------------------------------------
QGCCameraOptionExclusion::QGCCameraOptionExclusion(QObject* parent, QString param_, QString value_, QStringList exclusions_)
    : QObject(parent)
    , param(param_)
    , value(value_)
    , exclusions(exclusions_)
{
}

//-----------------------------------------------------------------------------
QGCCameraOptionRange::QGCCameraOptionRange(QObject* parent, QString param_, QString value_, QString targetParam_, QString condition_, QStringList optNames_, QStringList optValues_)
    : QObject(parent)
    , param(param_)
    , value(value_)
    , targetParam(targetParam_)
    , condition(condition_)
    , optNames(optNames_)
    , optValues(optValues_)
{
}

//-----------------------------------------------------------------------------
static bool
read_attribute(QDomNode& node, const char* tagName, bool& target)
{
    QDomNamedNodeMap attrs = node.attributes();
    if(!attrs.count()) {
        return false;
    }
    QDomNode subNode = attrs.namedItem(tagName);
    if(subNode.isNull()) {
        return false;
    }
    target = subNode.nodeValue() != "0";
    return true;
}

//-----------------------------------------------------------------------------
static bool
read_attribute(QDomNode& node, const char* tagName, int& target)
{
    QDomNamedNodeMap attrs = node.attributes();
    if(!attrs.count()) {
        return false;
    }
    QDomNode subNode = attrs.namedItem(tagName);
    if(subNode.isNull()) {
        return false;
    }
    target = subNode.nodeValue().toInt();
    return true;
}

//-----------------------------------------------------------------------------
static bool
read_attribute(QDomNode& node, const char* tagName, QString& target)
{
    QDomNamedNodeMap attrs = node.attributes();
    if(!attrs.count()) {
        return false;
    }
    QDomNode subNode = attrs.namedItem(tagName);
    if(subNode.isNull()) {
        return false;
    }
    target = subNode.nodeValue();
    return true;
}

//-----------------------------------------------------------------------------
static bool
read_value(QDomNode& element, const char* tagName, QString& target)
{
    QDomElement de = element.firstChildElement(tagName);
    if(de.isNull()) {
        return false;
    }
    target = de.text();
    return true;
}

//-----------------------------------------------------------------------------
VehicleCameraControl::VehicleCameraControl(const mavlink_camera_information_t *info, Vehicle* vehicle, int compID, QObject* parent)
    : MavlinkCameraControl(vehicle, parent)
    , _compID(compID)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    memcpy(&_info, info, sizeof(mavlink_camera_information_t));
    connect(this, &VehicleCameraControl::dataReady, this, &VehicleCameraControl::_dataReady);
    _vendor = QString(reinterpret_cast<const char*>(info->vendor_name));
    _modelName = QString(reinterpret_cast<const char*>(info->model_name));
    int ver = static_cast<int>(_info.cam_definition_version);
    _cacheFile = QString::asprintf("%s/%s_%s_%03d.xml",
        SettingsManager::instance()->appSettings()->parameterSavePath().toStdString().c_str(),
        _vendor.toStdString().c_str(),
        _modelName.toStdString().c_str(),
        ver);
    if(info->cam_definition_uri[0] != 0) {
        //-- Process camera definition file
        _handleDefinitionFile(info->cam_definition_uri);
    } else {
        _initWhenReady();
    }
    QSettings settings;
    _photoCaptureMode       = static_cast<PhotoCaptureMode>(settings.value(kPhotoMode, static_cast<int>(PHOTO_CAPTURE_SINGLE)).toInt());
    _photoLapse      = settings.value(kPhotoLapse, 1.0).toDouble();
    _photoLapseCount = settings.value(kPhotoLapseCount, 0).toInt();
    _thermalOpacity  = settings.value(kThermalOpacity, 85.0).toDouble();
    _thermalMode     = static_cast<ThermalViewMode>(settings.value(kThermalMode, static_cast<uint32_t>(THERMAL_BLEND)).toUInt());
    _videoRecordTimeUpdateTimer.setSingleShot(false);
    _videoRecordTimeUpdateTimer.setInterval(333);
    connect(&_videoRecordTimeUpdateTimer, &QTimer::timeout, this, &VehicleCameraControl::_recTimerHandler);
    //-- Tracking
    if(_info.flags & CAMERA_CAP_FLAGS_HAS_TRACKING_RECTANGLE) {
        _trackingStatus = static_cast<TrackingStatus>(_trackingStatus | TRACKING_RECTANGLE);
        _trackingStatus = static_cast<TrackingStatus>(_trackingStatus | TRACKING_SUPPORTED);
    }
    if(_info.flags & CAMERA_CAP_FLAGS_HAS_TRACKING_POINT) {
        _trackingStatus = static_cast<TrackingStatus>(_trackingStatus | TRACKING_POINT);
        _trackingStatus = static_cast<TrackingStatus>(_trackingStatus | TRACKING_SUPPORTED);
    }
}

//-----------------------------------------------------------------------------
VehicleCameraControl::~VehicleCameraControl()
{
    // Stop all timers to prevent them from firing during or after destruction
    _captureStatusTimer.stop();
    _videoRecordTimeUpdateTimer.stop();
    _streamInfoTimer.stop();
    _streamStatusTimer.stop();
    _cameraSettingsTimer.stop();
    _storageInfoTimer.stop();

    delete _netManager;
    _netManager = nullptr;
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_initWhenReady()
{
    qCDebug(CameraControlLog) << "_initWhenReady()";
    if(isBasic()) {
        qCDebug(CameraControlLog) << "Basic, MAVLink only messages, no parameters.";
        //-- Basic cameras have no parameters
        _paramComplete = true;
        emit parametersReady();
    } else {
        _requestAllParameters();
    }

    QTimer::singleShot(500, this, &VehicleCameraControl::_requestCameraSettings);
    connect(&_cameraSettingsTimer, &QTimer::timeout, this, &VehicleCameraControl::_cameraSettingsTimeout);

    QTimer::singleShot(1000, this, &VehicleCameraControl::_checkForVideoStreams);

    connect(_vehicle, &Vehicle::mavCommandResult, this, &VehicleCameraControl::_mavCommandResult);

    connect(&_captureStatusTimer, &QTimer::timeout, this, &VehicleCameraControl::_requestCaptureStatus);
    _captureStatusTimer.setSingleShot(true);
    _captureStatusTimer.start(1500);

    connect(&_storageInfoTimer, &QTimer::timeout, this, &VehicleCameraControl::_storageInfoTimeout);
    QTimer::singleShot(2000, this, &VehicleCameraControl::_requestStorageInfo);

    emit infoChanged();

    delete _netManager;
    _netManager = nullptr;
}

//-----------------------------------------------------------------------------
QString
VehicleCameraControl::firmwareVersion() const
{
    int major = (_info.firmware_version >> 24) & 0xFF;
    int minor = (_info.firmware_version >> 16) & 0xFF;
    int build = _info.firmware_version & 0xFFFF;
    return QString::asprintf("%d.%d.%d", major, minor, build);
}

//-----------------------------------------------------------------------------
QString
VehicleCameraControl::recordTimeStr() const
{
    return QTime(0, 0).addMSecs(static_cast<int>(recordTime())).toString("hh:mm:ss");
}

//-----------------------------------------------------------------------------
QString
VehicleCameraControl::storageFreeStr() const
{
    return qgcApp()->bigSizeMBToString(static_cast<quint64>(_storageFree));
}

//-----------------------------------------------------------------------------
QString
VehicleCameraControl::batteryRemainingStr() const
{
    if(_batteryRemaining >= 0) {
        return qgcApp()->numberToString(static_cast<quint64>(_batteryRemaining)) + " %";
    }
    return "";
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setCameraMode(CameraMode mode)
{
    if(!_resetting) {
        qCDebug(CameraControlLog) << "setCameraMode(" << mode << ")";
        if(mode == CAM_MODE_VIDEO) {
            setCameraModeVideo();
        } else if(mode == CAM_MODE_PHOTO) {
            setCameraModePhoto();
        } else {
            qCDebug(CameraControlLog) << "setCameraMode() Invalid mode:" << mode;
        }
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setPhotoCaptureMode(PhotoCaptureMode mode)
{
    if(!_resetting) {
        _photoCaptureMode = mode;
        QSettings settings;
        settings.setValue(kPhotoMode, static_cast<int>(mode));
        emit photoCaptureModeChanged();
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setPhotoLapse(qreal interval)
{
    _photoLapse = interval;
    QSettings settings;
    settings.setValue(kPhotoLapse, interval);
    emit photoLapseChanged();
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setPhotoLapseCount(int count)
{
    _photoLapseCount = count;
    QSettings settings;
    settings.setValue(kPhotoLapseCount, count);
    emit photoLapseCountChanged();
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_setCameraMode(CameraMode mode)
{
    if(_cameraMode != mode) {
        _cameraMode = mode;
        emit cameraModeChanged();
        //-- Update stream status
        _streamStatusTimer.start(1000);
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::toggleCameraMode()
{
    if(!_resetting) {
        if(cameraMode() == CAM_MODE_PHOTO || cameraMode() == CAM_MODE_SURVEY) {
            setCameraModeVideo();
        } else if(cameraMode() == CAM_MODE_VIDEO) {
            setCameraModePhoto();
        }
    }
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::toggleVideoRecording()
{
    if(!_resetting) {
        if(videoCaptureStatus() == VIDEO_CAPTURE_STATUS_RUNNING) {
            return stopVideoRecording();
        } else {
            return startVideoRecording();
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::takePhoto()
{
    qCDebug(CameraControlLog) << "takePhoto()";
    //-- Check if camera can capture photos or if it can capture it while in Video Mode
    if(!capturesPhotos()) {
        qCWarning(CameraControlLog) << "Camera does not handle image capture";
        return false;
    }
    if(cameraMode() == CAM_MODE_VIDEO && !photosInVideoMode()) {
        qCWarning(CameraControlLog) << "Camera does not handle image capture while in video mode";
        return false;
    }
    if(photoCaptureStatus() != PHOTO_CAPTURE_IDLE) {
        qCWarning(CameraControlLog) << "Camera not idle";
        return false;
    }
    if(!_resetting) {
        if(capturesPhotos()) {
            _vehicle->sendMavCommand(
                _compID,                                                                    // Target component
                MAV_CMD_IMAGE_START_CAPTURE,                                                // Command id
                false,                                                                      // ShowError
                0,                                                                          // Reserved (Set to 0)
                static_cast<float>(_photoCaptureMode == PHOTO_CAPTURE_SINGLE ? 0 : _photoLapse),   // Duration between two consecutive pictures (in seconds--ignored if single image)
                _photoCaptureMode == PHOTO_CAPTURE_SINGLE ? 1 : _photoLapseCount);                 // Number of images to capture total - 0 for unlimited capture
            _setPhotoStatus(PHOTO_CAPTURE_IN_PROGRESS);
            _captureInfoRetries = 0;
            //-- Capture local image as well
            VideoManager::instance()->grabImage();
            return true;
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::stopTakePhoto()
{
    if(!_resetting) {
        qCDebug(CameraControlLog) << "stopTakePhoto()";
        if(photoCaptureStatus() == PHOTO_CAPTURE_IDLE || (photoCaptureStatus() != PHOTO_CAPTURE_INTERVAL_IDLE && photoCaptureStatus() != PHOTO_CAPTURE_INTERVAL_IN_PROGRESS)) {
            return false;
        }
        if(capturesPhotos()) {
            _vehicle->sendMavCommand(
                _compID,                                                    // Target component
                MAV_CMD_IMAGE_STOP_CAPTURE,                                 // Command id
                false,                                                      // ShowError
                0);                                                         // Reserved (Set to 0)
            _setPhotoStatus(PHOTO_CAPTURE_IDLE);
            _captureInfoRetries = 0;
            return true;
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::startVideoRecording()
{
    if(!_resetting) {
        qCDebug(CameraControlLog) << "startVideoRecording()";
        //-- Check if camera can capture videos or if it can capture it while in Photo Mode
        if(!capturesVideo() || (cameraMode() == CAM_MODE_PHOTO && !videoInPhotoMode())) {
            return false;
        }
        if(videoCaptureStatus() != VIDEO_CAPTURE_STATUS_RUNNING) {
            _vehicle->sendMavCommand(
                _compID,                                    // Target component
                MAV_CMD_VIDEO_START_CAPTURE,                // Command id
                false,                                      // Don't Show Error (handle locally)
                0,                                          // All streams
                0);                                         // CAMERA_CAPTURE_STATUS streaming frequency
            return true;
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::stopVideoRecording()
{
    if(!_resetting) {
        qCDebug(CameraControlLog) << "stopVideoRecording()";
        if(videoCaptureStatus() == VIDEO_CAPTURE_STATUS_RUNNING) {
            _vehicle->sendMavCommand(
                _compID,                                    // Target component
                MAV_CMD_VIDEO_STOP_CAPTURE,                 // Command id
                false,                                      // Don't Show Error (handle locally)
                0);                                         // Reserved (Set to 0)
            return true;
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setCameraModeVideo()
{
    if(!_resetting && hasModes()) {
        qCDebug(CameraControlLog) << "setCameraModeVideo()";
        //-- Does it have a mode parameter?
        Fact* pMode = mode();
        if(pMode) {
            if(cameraMode() != CAM_MODE_VIDEO) {
                pMode->setRawValue(CAM_MODE_VIDEO);
                _setCameraMode(CAM_MODE_VIDEO);
            }
        } else {
            //-- Use MAVLink Command
            if(_cameraMode != CAM_MODE_VIDEO) {
                //-- Use basic MAVLink message
                _vehicle->sendMavCommand(
                    _compID,                                // Target component
                    MAV_CMD_SET_CAMERA_MODE,                // Command id
                    true,                                   // ShowError
                    0,                                      // Reserved (Set to 0)
                    CAM_MODE_VIDEO);                        // Camera mode (0: photo, 1: video)
                _setCameraMode(CAM_MODE_VIDEO);
            }
        }
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setCameraModePhoto()
{
    if(!_resetting && hasModes()) {
        qCDebug(CameraControlLog) << "setCameraModePhoto()";
        //-- Does it have a mode parameter?
        Fact* pMode = mode();
        if(pMode) {
            if(cameraMode() != CAM_MODE_PHOTO) {
                pMode->setRawValue(CAM_MODE_PHOTO);
                _setCameraMode(CAM_MODE_PHOTO);
            }
        } else {
            //-- Use MAVLink Command
            if(_cameraMode != CAM_MODE_PHOTO) {
                //-- Use basic MAVLink message
                _vehicle->sendMavCommand(
                    _compID,                                // Target component
                    MAV_CMD_SET_CAMERA_MODE,                // Command id
                    true,                                   // ShowError
                    0,                                      // Reserved (Set to 0)
                    CAM_MODE_PHOTO);                        // Camera mode (0: photo, 1: video)
                _setCameraMode(CAM_MODE_PHOTO);
            }
        }
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setThermalMode(ThermalViewMode mode)
{
    QSettings settings;
    settings.setValue(kThermalMode, static_cast<uint32_t>(mode));
    _thermalMode = mode;
    emit thermalModeChanged();
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setThermalOpacity(double val)
{
    if(val < 0.0) val = 0.0;
    if(val > 100.0) val = 100.0;
    if(fabs(_thermalOpacity - val) > 0.1) {
        _thermalOpacity = val;
        QSettings settings;
        settings.setValue(kThermalOpacity, val);
        emit thermalOpacityChanged();
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setZoomLevel(qreal level)
{
    qCDebug(CameraControlLog) << "setZoomLevel()" << level;
    if(hasZoom()) {
        //-- Limit
        level = std::min(std::max(level, 0.0), 100.0);
        if(_vehicle) {
            _vehicle->sendMavCommand(
                _compID,                                // Target component
                MAV_CMD_SET_CAMERA_ZOOM,                // Command id
                false,                                  // ShowError
                ZOOM_TYPE_RANGE,                        // Zoom type
                static_cast<float>(level));             // Level
        }
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setFocusLevel(qreal level)
{
    qCDebug(CameraControlLog) << "setFocusLevel()" << level;
    if(hasFocus()) {
        //-- Limit
        level = std::min(std::max(level, 0.0), 100.0);
        if(_vehicle) {
            _vehicle->sendMavCommand(
                _compID,                                // Target component
                MAV_CMD_SET_CAMERA_FOCUS,               // Command id
                false,                                  // ShowError
                FOCUS_TYPE_RANGE,                       // Focus type
                static_cast<float>(level));             // Level
        }
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::resetSettings()
{
    if(!_resetting) {
        qCDebug(CameraControlLog) << "resetSettings()";
        _resetting = true;
        _vehicle->sendMavCommand(
            _compID,                                // Target component
            MAV_CMD_RESET_CAMERA_SETTINGS,          // Command id
            true,                                   // ShowError
            1);                                     // Do Reset
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::formatCard(int id)
{
    if(!_resetting) {
        qCDebug(CameraControlLog) << "formatCard()";
        if(_vehicle) {
            _vehicle->sendMavCommand(
                _compID,                                // Target component
                MAV_CMD_STORAGE_FORMAT,                 // Command id
                true,                                   // ShowError
                id,                                     // Storage ID (1 for first, 2 for second, etc.)
                1);                                     // Do Format
        }
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::stepZoom(int direction)
{
    qCDebug(CameraControlLog) << "stepZoom()" << direction;
    if(_vehicle && hasZoom()) {
        _vehicle->sendMavCommand(
            _compID,                                // Target component
            MAV_CMD_SET_CAMERA_ZOOM,                // Command id
            false,                                  // ShowError
            ZOOM_TYPE_STEP,                         // Zoom type
            direction);                             // Direction (-1 wide, 1 tele)
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::startZoom(int direction)
{
    qCDebug(CameraControlLog) << "startZoom()" << direction;
    if(_vehicle && hasZoom()) {
        _vehicle->sendMavCommand(
            _compID,                                // Target component
            MAV_CMD_SET_CAMERA_ZOOM,                // Command id
            false,                                  // ShowError
            ZOOM_TYPE_CONTINUOUS,                   // Zoom type
            direction);                             // Direction (-1 wide, 1 tele)
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::stopZoom()
{
    qCDebug(CameraControlLog) << "stopZoom()";
    if(_vehicle && hasZoom()) {
        _vehicle->sendMavCommand(
            _compID,                                // Target component
            MAV_CMD_SET_CAMERA_ZOOM,                // Command id
            false,                                  // ShowError
            ZOOM_TYPE_CONTINUOUS,                   // Zoom type
            0);                                     // Direction (-1 wide, 1 tele)
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_requestCaptureStatus()
{
    qCDebug(CameraControlLog) << "_requestCaptureStatus() - retries:" << _cameraCaptureStatusRetries;

    if(_cameraCaptureStatusRetries++ % 2 == 0) {
        qCDebug(CameraControlLog) << "_requestCaptureStatus() - using REQUEST_MESSAGE";
        _vehicle->sendMavCommand(
            _compID,                                // target component
            MAV_CMD_REQUEST_MESSAGE,                // command id
            false,                                  // showError
            MAVLINK_MSG_ID_CAMERA_CAPTURE_STATUS);  // msgid
    } else {
        qCDebug(CameraControlLog) << "_requestCaptureStatus() - using legacy MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS";
        _vehicle->sendMavCommand(
            _compID,                                // target component
            MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS,  // command id
            false,                                  // showError
            1);                                     // Do Request
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::factChanged(Fact* pFact)
{
    _updateActiveList();
    _updateRanges(pFact);
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_mavCommandResult(int vehicleId, int component, int command, int result, int failureCode)
{
    Q_UNUSED(failureCode);

    //-- Is this ours?
    if (_vehicle->id() != vehicleId || compID() != component) {
        return;
    }
    if (result == MAV_RESULT_IN_PROGRESS) {
        //-- Do Nothing
        qCDebug(CameraControlLog) << "In progress response for" << command;
    } else if(result == MAV_RESULT_ACCEPTED) {
        switch(command) {
            case MAV_CMD_RESET_CAMERA_SETTINGS:
                _resetting = false;
                if(isBasic()) {
                    _requestCameraSettings();
                } else {
                    QTimer::singleShot(500, this, &VehicleCameraControl::_requestAllParameters);
                    QTimer::singleShot(2500, this, &VehicleCameraControl::_requestCameraSettings);
                }
                break;
            case MAV_CMD_VIDEO_START_CAPTURE:
                _setVideoStatus(VIDEO_CAPTURE_STATUS_RUNNING);
                _captureStatusTimer.start(1000);
                break;
            case MAV_CMD_VIDEO_STOP_CAPTURE:
                _setVideoStatus(VIDEO_CAPTURE_STATUS_STOPPED);
                _captureStatusTimer.start(1000);
                break;
            case MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS:
                _cameraCaptureStatusRetries = 0;
                break;
            case MAV_CMD_REQUEST_STORAGE_INFORMATION:
                _storageInfoRetries = 0;
                break;
            case MAV_CMD_IMAGE_START_CAPTURE:
                _captureStatusTimer.start(1000);
                break;
        }
    } else {
        if ((result == MAV_RESULT_TEMPORARILY_REJECTED) || (result == MAV_RESULT_FAILED)) {
            if (result == MAV_RESULT_TEMPORARILY_REJECTED) {
                qCDebug(CameraControlLog) << "Command temporarily rejected for" << command;
            } else {
                qCDebug(CameraControlLog) << "Command failed for" << command;
            }
            switch(command) {
                case MAV_CMD_RESET_CAMERA_SETTINGS:
                    _resetting = false;
                    qCDebug(CameraControlLog) << "Failed to reset camera settings";
                break;
                case MAV_CMD_IMAGE_START_CAPTURE:
                case MAV_CMD_IMAGE_STOP_CAPTURE:
                    if(++_captureInfoRetries <= 5) {
                        _captureStatusTimer.start(1000);
                    } else {
                        qCDebug(CameraControlLog) << "Giving up start/stop image capture";
                        _setPhotoStatus(PHOTO_CAPTURE_IDLE);
                    }
                    break;
                case MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS:
                    if(++_cameraCaptureStatusRetries <= 5) {
                        _captureStatusTimer.start(1000);
                    } else {
                        qCDebug(CameraControlLog) << "Giving up requesting capture status";
                    }
                    break;
                case MAV_CMD_REQUEST_STORAGE_INFORMATION:
                    if(++_storageInfoRetries <= 5) {
                        QTimer::singleShot(1000, this, &VehicleCameraControl::_requestStorageInfo);
                    } else {
                        qCDebug(CameraControlLog) << "Giving up requesting storage status";
                    }
                    break;
            }
        } else {
            qCDebug(CameraControlLog) << "Bad response for" << command << result;
        }
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_setVideoStatus(VideoCaptureStatus status)
{
    if(_videoCaptureStatus != status) {
        _videoCaptureStatus = status;
        emit videoCaptureStatusChanged();
        if(status == VIDEO_CAPTURE_STATUS_RUNNING) {
             _recordTime = 0;
             _recTime = QTime::currentTime();
             _videoRecordTimeUpdateTimer.start();
        } else {
             _videoRecordTimeUpdateTimer.stop();
             _recordTime = 0;
             emit recordTimeChanged();
        }
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_recTimerHandler()
{
    _recordTime = static_cast<uint32_t>(_recTime.msecsTo(QTime::currentTime()));
    emit recordTimeChanged();
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_setPhotoStatus(PhotoCaptureStatus status)
{
    if(_photoCaptureStatus != status) {
        qCDebug(CameraControlLog) << "Set Photo Status:" << status;
        _photoCaptureStatus = status;
        emit photoCaptureStatusChanged();
    }
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::_loadCameraDefinitionFile(QByteArray& bytes)
{
    QByteArray originalData(bytes);
    //-- Handle localization
    if(!_handleLocalization(bytes)) {
        return false;
    }

    QDomDocument doc;
    const QDomDocument::ParseResult result = doc.setContent(bytes, QDomDocument::ParseOption::Default);
    if (!result) {
        qCCritical(CameraControlLog) << "Unable to parse camera definition file on line:" << result.errorLine;
        qCCritical(CameraControlLog) << result.errorMessage;
        return false;
    }
    //-- Load camera constants
    QDomNodeList defElements = doc.elementsByTagName(kDefnition);
    if(!defElements.size() || !_loadConstants(defElements)) {
        qCWarning(CameraControlLog) <<  "Unable to load camera constants from camera definition";
        return false;
    }
    //-- Load camera parameters
    QDomNodeList paramElements = doc.elementsByTagName(kParameters);
    if(!paramElements.size()) {
        qCDebug(CameraControlLog) <<  "No parameters to load from camera";
        return false;
    }
    if(!_loadSettings(paramElements)) {
        qCWarning(CameraControlLog) <<  "Unable to load camera parameters from camera definition";
        return false;
    }
    //-- If this is new, cache it
    if(!_cached) {
        qCDebug(CameraControlLog) << "Saving camera definition file" << _cacheFile;
        QFile file(_cacheFile);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << QString("Could not save cache file %1. Error: %2").arg(_cacheFile).arg(file.errorString());
        } else {
            file.write(originalData);
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::_loadConstants(const QDomNodeList nodeList)
{
    QDomNode node = nodeList.item(0);
    if(!read_attribute(node, kVersion, _version)) {
        return false;
    }
    if(!read_value(node, kModel, _modelName)) {
        return false;
    }
    if(!read_value(node, kVendor, _vendor)) {
        return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::_loadSettings(const QDomNodeList nodeList)
{
    QDomNode node = nodeList.item(0);
    QDomElement elem = node.toElement();
    QDomNodeList parameters = elem.elementsByTagName(kParameter);
    //-- Pre-process settings (maintain order and skip non-controls)
    for(int i = 0; i < parameters.size(); i++) {
        QDomNode parameterNode = parameters.item(i);
        QString name;
        if(read_attribute(parameterNode, kName, name)) {
            bool control = true;
            read_attribute(parameterNode, kControl, control);
            if(control) {
                _settings << name;
            }
        } else {
            qCritical() << "Parameter entry missing parameter name";
            return false;
        }
    }
    //-- Load parameters
    for(int i = 0; i < parameters.size(); i++) {
        QDomNode parameterNode = parameters.item(i);
        QString factName;
        read_attribute(parameterNode, kName, factName);
        QString type;
        if(!read_attribute(parameterNode, kType, type)) {
            qCritical() << QString("Parameter %1 missing parameter type").arg(factName);
            return false;
        }
        //-- Does it have a control?
        bool control = true;
        read_attribute(parameterNode, kControl, control);
        //-- Is it read only?
        bool readOnly = false;
        read_attribute(parameterNode, kReadOnly, readOnly);
        //-- Is it write only?
        bool writeOnly = false;
        read_attribute(parameterNode, kWriteOnly, writeOnly);
        //-- It can't be both
        if(readOnly && writeOnly) {
            qCritical() << QString("Parameter %1 cannot be both read only and write only").arg(factName);
        }
        //-- Param type
        bool unknownType;
        FactMetaData::ValueType_t factType = FactMetaData::stringToType(type, unknownType);
        if (unknownType) {
            qCritical() << QString("Unknown type for parameter %1").arg(factName);
            return false;
        }
        //-- By definition, custom types do not have control
        if(factType == FactMetaData::valueTypeCustom) {
            control = false;
        }
        //-- Description
        QString description;
        if(!read_value(parameterNode, kDescription, description)) {
            qCritical() << QString("Parameter %1 missing parameter description").arg(factName);
            return false;
        }
        //-- Check for updates
        QStringList updates = _loadUpdates(parameterNode);
        if(updates.size()) {
            qCDebug(CameraControlVerboseLog) << "Parameter" << factName << "requires updates for:" << updates;
            _requestUpdates[factName] = updates;
        }
        //-- Build metadata
        FactMetaData* metaData = new FactMetaData(factType, factName, this);
        QQmlEngine::setObjectOwnership(metaData, QQmlEngine::CppOwnership);
        metaData->setShortDescription(description);
        metaData->setLongDescription(description);
        metaData->setHasControl(control);
        metaData->setReadOnly(readOnly);
        metaData->setWriteOnly(writeOnly);
        //-- Options (enums)
        QDomElement optionElem = parameterNode.toElement();
        QDomNodeList optionsRoot = optionElem.elementsByTagName(kOptions);
        if(optionsRoot.size()) {
            //-- Iterate options
            QDomNode node = optionsRoot.item(0);
            QDomElement elem = node.toElement();
            QDomNodeList options = elem.elementsByTagName(kOption);
            for(int i = 0; i < options.size(); i++) {
                QDomNode option = options.item(i);
                QString optName;
                QString optValue;
                QVariant optVariant;
                if(!_loadNameValue(option, factName, metaData, optName, optValue, optVariant)) {
                    delete metaData;
                    return false;
                }
                metaData->addEnumInfo(optName, optVariant);
                _originalOptNames[factName]  << optName;
                _originalOptValues[factName] << optVariant;
                //-- Check for exclusions
                QStringList exclusions = _loadExclusions(option);
                if(exclusions.size()) {
                    qCDebug(CameraControlVerboseLog) << "New exclusions:" << factName << optValue << exclusions;
                    QGCCameraOptionExclusion* pExc = new QGCCameraOptionExclusion(this, factName, optValue, exclusions);
                    QQmlEngine::setObjectOwnership(pExc, QQmlEngine::CppOwnership);
                    _valueExclusions.append(pExc);
                }
                //-- Check for range rules
                if(!_loadRanges(option, factName, optValue)) {
                    delete metaData;
                    return false;
                }
            }
        }
        QString defaultValue;
        if(read_attribute(parameterNode, kDefault, defaultValue)) {
            QVariant defaultVariant;
            QString  errorString;
            if (metaData->convertAndValidateRaw(defaultValue, false, defaultVariant, errorString)) {
                metaData->setRawDefaultValue(defaultVariant);
            } else {
                qWarning() << "Invalid default value for" << factName
                           << " type:"  << metaData->type()
                           << " value:" << defaultValue
                           << " error:" << errorString;
            }
        }
        //-- Set metadata and Fact
        if (_nameToFactMetaDataMap.contains(factName)) {
            qWarning() << QStringLiteral("Duplicate fact name:") << factName;
            delete metaData;
        } else {
            {
                //-- Check for Min Value
                QString attr;
                if(read_attribute(parameterNode, kMin, attr)) {
                    QVariant typedValue;
                    QString  errorString;
                    if (metaData->convertAndValidateRaw(attr, true /* convertOnly */, typedValue, errorString)) {
                        metaData->setRawMin(typedValue);
                    } else {
                        qWarning() << "Invalid min value for" << factName
                                   << " type:"  << metaData->type()
                                   << " value:" << attr
                                   << " error:" << errorString;
                    }
                }
            }
            {
                //-- Check for Max Value
                QString attr;
                if(read_attribute(parameterNode, kMax, attr)) {
                    QVariant typedValue;
                    QString  errorString;
                    if (metaData->convertAndValidateRaw(attr, true /* convertOnly */, typedValue, errorString)) {
                        metaData->setRawMax(typedValue);
                    } else {
                        qWarning() << "Invalid max value for" << factName
                                   << " type:"  << metaData->type()
                                   << " value:" << attr
                                   << " error:" << errorString;
                    }
                }
            }
            {
                //-- Check for Step Value
                QString attr;
                if(read_attribute(parameterNode, kStep, attr)) {
                    QVariant typedValue;
                    QString  errorString;
                    if (metaData->convertAndValidateRaw(attr, true /* convertOnly */, typedValue, errorString)) {
                        metaData->setRawIncrement(typedValue.toDouble());
                    } else {
                        qWarning() << "Invalid step value for" << factName
                                   << " type:"  << metaData->type()
                                   << " value:" << attr
                                   << " error:" << errorString;
                    }
                }
            }
            {
                //-- Check for Decimal Places
                QString attr;
                if(read_attribute(parameterNode, kDecimalPlaces, attr)) {
                    QVariant typedValue;
                    QString  errorString;
                    if (metaData->convertAndValidateRaw(attr, true /* convertOnly */, typedValue, errorString)) {
                        metaData->setDecimalPlaces(typedValue.toInt());
                    } else {
                        qWarning() << "Invalid decimal places value for" << factName
                                   << " type:"  << metaData->type()
                                   << " value:" << attr
                                   << " error:" << errorString;
                    }
                }
            }
            {
                //-- Check for Units
                QString attr;
                if(read_attribute(parameterNode, kUnit, attr)) {
                    metaData->setRawUnits(attr);
                }
            }
            qCDebug(CameraControlLog) << "New parameter:" << factName << (readOnly ? "ReadOnly" : "Writable") << (writeOnly ? "WriteOnly" : "Readable");
            _nameToFactMetaDataMap[factName] = metaData;
            Fact* pFact = new Fact(_compID, factName, factType, this);
            QQmlEngine::setObjectOwnership(pFact, QQmlEngine::CppOwnership);
            pFact->setMetaData(metaData);
            pFact->containerSetRawValue(metaData->rawDefaultValue());
            QGCCameraParamIO* pIO = new QGCCameraParamIO(this, pFact, _vehicle);
            QQmlEngine::setObjectOwnership(pIO, QQmlEngine::CppOwnership);
            _paramIO[factName] = pIO;
            _addFact(pFact, factName);
        }
    }
    if(_nameToFactMetaDataMap.size() > 0) {
        _addFactGroup(this, "camera");
        _processRanges();
        _activeSettings = _settings;
        emit activeSettingsChanged();
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::_handleLocalization(QByteArray& bytes)
{
    QDomDocument doc;
    const QDomDocument::ParseResult result = doc.setContent(bytes, QDomDocument::ParseOption::Default);
    if (!result) {
        qCritical() << "Unable to parse camera definition file on line:" << result.errorLine;
        qCritical() << result.errorMessage;
        return false;
    }
    //-- Find out where we are
    QLocale locale = QLocale::system();
#if defined (Q_OS_MACOS)
    locale = QLocale(locale.name());
#endif
    QString localeName = locale.name().toLower().replace("-", "_");
    qCDebug(CameraControlLog) << "Current locale:" << localeName;
    if(localeName == "en_us") {
        // Nothing to do
        return true;
    }
    QDomNodeList locRoot = doc.elementsByTagName(kLocalization);
    if(!locRoot.size()) {
        // Nothing to do
        return true;
    }
    //-- Iterate locales
    QDomNode node = locRoot.item(0);
    QDomElement elem = node.toElement();
    QDomNodeList locales = elem.elementsByTagName(kLocale);
    for(int i = 0; i < locales.size(); i++) {
        QDomNode locale = locales.item(i);
        QString name;
        if(!read_attribute(locale, kName, name)) {
            qWarning() << "Localization entry is missing its name attribute";
            continue;
        }
        // If we found a direct match, deal with it now
        if(localeName == name.toLower().replace("-", "_")) {
            return _replaceLocaleStrings(locale, bytes);
        }
    }
    //-- No direct match. Pick first matching language (if any)
    localeName = localeName.left(3);
    for(int i = 0; i < locales.size(); i++) {
        QDomNode locale = locales.item(i);
        QString name;
        read_attribute(locale, kName, name);
        if(name.toLower().startsWith(localeName)) {
            return _replaceLocaleStrings(locale, bytes);
        }
    }
    //-- Could not find a language to use
    qWarning() <<  "No match for" << QLocale::system().name() << "in camera definition file";
    //-- Just use default, en_US
    return true;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::_replaceLocaleStrings(const QDomNode node, QByteArray& bytes)
{
    QDomElement stringElem = node.toElement();
    QDomNodeList strings = stringElem.elementsByTagName(kStrings);
    for(int i = 0; i < strings.size(); i++) {
        QDomNode stringNode = strings.item(i);
        QString original;
        QString translated;
        if(read_attribute(stringNode, kOriginal, original)) {
            if(read_attribute(stringNode, kTranslated, translated)) {
                QString o; o = "\"" + original + "\"";
                QString t; t = "\"" + translated + "\"";
                bytes.replace(o.toUtf8(), t.toUtf8());
                o = ">" + original + "<";
                t = ">" + translated + "<";
                bytes.replace(o.toUtf8(), t.toUtf8());
            }
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_requestAllParameters()
{
    //-- Reset receive list
    for(const QString& paramName: _paramIO.keys()) {
        if(_paramIO[paramName]) {
            _paramIO[paramName]->setParamRequest();
        } else {
            qCritical() << "QGCParamIO is NULL" << paramName;
        }
    }
    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (sharedLink) {
        mavlink_message_t msg;
        mavlink_msg_param_ext_request_list_pack_chan(
                    static_cast<uint8_t>(MAVLinkProtocol::instance()->getSystemId()),
                    static_cast<uint8_t>(MAVLinkProtocol::getComponentId()),
                    sharedLink->mavlinkChannel(),
                    &msg,
                    static_cast<uint8_t>(_vehicle->id()),
                    static_cast<uint8_t>(compID()));
        _vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg);
    }
    qCDebug(CameraControlVerboseLog) << "Request all parameters";
}

//-----------------------------------------------------------------------------
QString
VehicleCameraControl::_getParamName(const char* param_id)
{
    // This will null terminate the name string
    char parameterNameWithNull[MAVLINK_MSG_PARAM_VALUE_FIELD_PARAM_ID_LEN + 1] = {};
    (void) strncpy(parameterNameWithNull, param_id, MAVLINK_MSG_PARAM_VALUE_FIELD_PARAM_ID_LEN);
    const QString parameterName(parameterNameWithNull);
    return parameterName;
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::handleParamAck(const mavlink_param_ext_ack_t& ack)
{
    QString paramName = _getParamName(ack.param_id);
    if(!_paramIO.contains(paramName)) {
        qCWarning(CameraControlLog) << "Received PARAM_EXT_ACK for unknown param:" << paramName;
        return;
    }
    if(_paramIO[paramName]) {
        _paramIO[paramName]->handleParamAck(ack);
    } else {
        qCritical() << "QGCParamIO is NULL" << paramName;
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::handleParamValue(const mavlink_param_ext_value_t& value)
{
    QString paramName = _getParamName(value.param_id);
    if(!_paramIO.contains(paramName)) {
        qCWarning(CameraControlLog) << "Received PARAM_EXT_VALUE for unknown param:" << paramName;
        return;
    }
    if(_paramIO[paramName]) {
        _paramIO[paramName]->handleParamValue(value);
    } else {
        qCritical() << "QGCParamIO is NULL" << paramName;
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_updateActiveList()
{
    //-- Clear out excluded parameters based on exclusion rules
    QStringList exclusionList;
    for(QGCCameraOptionExclusion* param: _valueExclusions) {
        Fact* pFact = getFact(param->param);
        if(pFact) {
            QString option = pFact->rawValueString();
            if(param->value == option) {
                exclusionList << param->exclusions;
            }
        }
    }
    QStringList active;
    for(QString key: _settings) {
        if(!exclusionList.contains(key)) {
            active.append(key);
        }
    }
    if(active != _activeSettings) {
        qCDebug(CameraControlVerboseLog) << "Excluding" << exclusionList;
        _activeSettings = active;
        emit activeSettingsChanged();
        //-- Force validity of "Facts" based on active set
        if(_paramComplete) {
            emit parametersReady();
        }
    }
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::_processConditionTest(const QString conditionTest)
{
    enum {
        TEST_NONE,
        TEST_EQUAL,
        TEST_NOT_EQUAL,
        TEST_GREATER,
        TEST_SMALLER
    };
    qCDebug(CameraControlVerboseLog) << "_processConditionTest(" << conditionTest << ")";
    int op = TEST_NONE;
    QStringList test;

    auto split = [&conditionTest](const QString& sep ) {
        return conditionTest.split(sep, Qt::SkipEmptyParts);
    };

    if(conditionTest.contains("!=")) {
        test = split("!=");
        op = TEST_NOT_EQUAL;
    } else if(conditionTest.contains("=")) {
        test = split("=");
        op = TEST_EQUAL;
    } else if(conditionTest.contains(">")) {
        test = split(">");
        op = TEST_GREATER;
    } else if(conditionTest.contains("<")) {
        test = split("<");
        op = TEST_SMALLER;
    }
    if(test.size() == 2) {
        Fact* pFact = getFact(test[0]);
        if(pFact) {
            switch(op) {
            case TEST_EQUAL:
                return pFact->rawValueString() == test[1];
            case TEST_NOT_EQUAL:
                return pFact->rawValueString() != test[1];
            case TEST_GREATER:
                return pFact->rawValueString() > test[1];
            case TEST_SMALLER:
                return pFact->rawValueString() < test[1];
            case TEST_NONE:
                break;
            }
        } else {
            qWarning() << "Invalid condition parameter:" << test[0] << "in" << conditionTest;
            return false;
        }
    }
    qWarning() << "Invalid condition" << conditionTest;
    return false;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::_processCondition(const QString condition)
{
    qCDebug(CameraControlVerboseLog) << "_processCondition(" << condition << ")";
    bool result = true;
    bool andOp  = true;
    if(!condition.isEmpty()) {
        QStringList scond = condition.split(" ", Qt::SkipEmptyParts);
        while(scond.size()) {
            QString test = scond.first();
            scond.removeFirst();
            if(andOp) {
                result = result && _processConditionTest(test);
            } else {
                result = result || _processConditionTest(test);
            }
            if(!scond.size()) {
                return result;
            }
            andOp = scond.first().toUpper() == "AND";
            scond.removeFirst();
        }
    }
    return result;
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_updateRanges(Fact* pFact)
{
    QMap<Fact*, QGCCameraOptionRange*> rangesSet;
    QMap<Fact*, QString> rangesReset;
    QStringList changedList;
    QStringList resetList;
    QStringList updates;
    //-- Iterate range sets looking for limited ranges
    for(QGCCameraOptionRange* pRange: _optionRanges) {
        //-- If this fact or one of its conditions is part of this range set
        if(!changedList.contains(pRange->targetParam) && (pRange->param == pFact->name() || pRange->condition.contains(pFact->name()))) {
            Fact* pRFact = getFact(pRange->param);          //-- This parameter
            Fact* pTFact = getFact(pRange->targetParam);    //-- The target parameter (the one its range is to change)
            if(pRFact && pTFact) {
                //qCDebug(CameraControlVerboseLog) << "Check new set of options for" << pTFact->name();
                QString option = pRFact->rawValueString();  //-- This parameter value
                //-- If this value (and condition) triggers a change in the target range
                //qCDebug(CameraControlVerboseLog) << "Range value:" << pRange->value << "Current value:" << option << "Condition:" << pRange->condition;
                if(pRange->value == option && _processCondition(pRange->condition)) {
                    if(pTFact->enumStrings() != pRange->optNames) {
                        //-- Set limited range set
                        rangesSet[pTFact] = pRange;
                    }
                    changedList << pRange->targetParam;
                }
            }
        }
    }
    //-- Iterate range sets again looking for resets
    for(QGCCameraOptionRange* pRange: _optionRanges) {
        if(!changedList.contains(pRange->targetParam) && (pRange->param == pFact->name() || pRange->condition.contains(pFact->name()))) {
            Fact* pTFact = getFact(pRange->targetParam);    //-- The target parameter (the one its range is to change)
            if(!resetList.contains(pRange->targetParam)) {
                if(pTFact->enumStrings() != _originalOptNames[pRange->targetParam]) {
                    //-- Restore full option set
                    rangesReset[pTFact] = pRange->targetParam;
                }
                resetList << pRange->targetParam;
            }
        }
    }
    //-- Update limited range set
    for (Fact* f: rangesSet.keys()) {
        f->setEnumInfo(rangesSet[f]->optNames, rangesSet[f]->optVariants);
        if(!updates.contains(f->name())) {
            emit f->enumsChanged();
            qCDebug(CameraControlVerboseLog) << "Limited set of options for:" << f->name() << rangesSet[f]->optNames;;
            updates << f->name();
        }
    }
    //-- Restore full range set
    for (Fact* f: rangesReset.keys()) {
        f->setEnumInfo(_originalOptNames[rangesReset[f]], _originalOptValues[rangesReset[f]]);
        if(!updates.contains(f->name())) {
            emit f->enumsChanged();
            qCDebug(CameraControlVerboseLog) << "Restore full set of options for:" << f->name() << _originalOptNames[f->name()];
            updates << f->name();
        }
    }
    //-- Parameter update requests
    if(_requestUpdates.contains(pFact->name())) {
        for(const QString& param: _requestUpdates[pFact->name()]) {
            if(!_updatesToRequest.contains(param)) {
                _updatesToRequest << param;
            }
        }
    }
    if(_updatesToRequest.size()) {
        QTimer::singleShot(500, this, &VehicleCameraControl::_requestParamUpdates);
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_requestParamUpdates()
{
    for(const QString& param: _updatesToRequest) {
        _paramIO[param]->paramRequest();
    }
    _updatesToRequest.clear();
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_requestCameraSettings()
{
    qCDebug(CameraControlLog) << "_requestCameraSettings() - retries:" << _cameraSettingsRetries << "timer active:" << _cameraSettingsTimer.isActive();
    if(_vehicle) {
        // Use REQUEST_MESSAGE instead of deprecated REQUEST_CAMERA_SETTINGS
        // first time and every other time after that.

        if(_cameraSettingsRetries % 2 == 0) {
            qCDebug(CameraControlLog) << "_requestCameraSettings() - using REQUEST_MESSAGE";
            _vehicle->sendMavCommand(
                _compID,                                 // target component
                MAV_CMD_REQUEST_MESSAGE,                // command id
                false,                                  // showError
                MAVLINK_MSG_ID_CAMERA_SETTINGS);        // msgid
        } else {
            qCDebug(CameraControlLog) << "_requestCameraSettings() - using legacy MAV_CMD_REQUEST_CAMERA_SETTINGS";
            _vehicle->sendMavCommand(
                _compID,                                // Target component
                MAV_CMD_REQUEST_CAMERA_SETTINGS,        // command id
                false,                                  // showError
                1);                                     // Do Request
        }
        if(_cameraSettingsTimer.isActive()) {
            qCDebug(CameraControlLog) << "_requestCameraSettings() - RESTARTING already active timer";
        } else {
            qCDebug(CameraControlLog) << "_requestCameraSettings() - starting timer";
        }
        _cameraSettingsTimer.start(1000);               // Wait up to a second for it
    }

}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_requestStorageInfo()
{
    qCDebug(CameraControlLog) << "_requestStorageInfo() - retries:" << _storageInfoRetries << "timer active:" << _storageInfoTimer.isActive();
    if(_vehicle) {
        // Use REQUEST_MESSAGE instead of deprecated REQUEST_STORAGE_INFORMATION
        // first time and every other time after that.
        if(_storageInfoRetries % 2 == 0) {
            qCDebug(CameraControlLog) << "_requestStorageInfo() - using REQUEST_MESSAGE";
            _vehicle->sendMavCommand(
                _compID,                                 // target component
                MAV_CMD_REQUEST_MESSAGE,                // command id
                false,                                  // showError
                MAVLINK_MSG_ID_STORAGE_INFORMATION,     // msgid
                0);                                     // storage ID
        } else {
            qCDebug(CameraControlLog) << "_requestStorageInfo() - using legacy MAV_CMD_REQUEST_STORAGE_INFORMATION";
            _vehicle->sendMavCommand(
                _compID,                                // Target component
                MAV_CMD_REQUEST_STORAGE_INFORMATION,    // command id
                false,                                  // showError
                0,                                      // Storage ID (0 for all, 1 for first, 2 for second, etc.)
                1);                                     // Do Request
        }
        qCDebug(CameraControlLog) << "_requestStorageInfo() - starting timer";
        _storageInfoTimer.start(1000);                  // Wait up to a second for it
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::handleSettings(const mavlink_camera_settings_t& settings)
{
    qCDebug(CameraControlLog) << "handleSettings() Mode:" << settings.mode_id << "- stopping timer, resetting retries";
    _cameraSettingsTimer.stop();
    _cameraSettingsRetries = 0;
    _setCameraMode(static_cast<CameraMode>(settings.mode_id));
    qreal z = static_cast<qreal>(settings.zoomLevel);
    qreal f = static_cast<qreal>(settings.focusLevel);
    if(std::isfinite(z) && z != _zoomLevel) {
        _zoomLevel = z;
        emit zoomLevelChanged();
    }
    if(std::isfinite(f) && f != _focusLevel) {
        _focusLevel = f;
        emit focusLevelChanged();
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::handleStorageInfo(const mavlink_storage_information_t& st)
{
    qCDebug(CameraControlLog) << "handleStorageInfo() - stopping timer, resetting retries";
    _storageInfoTimer.stop();
    _storageInfoRetries = 0;
    qCDebug(CameraControlLog) << "handleStorageInfo:"
        << "\n\tStorage id:" << st.storage_id
        << "\n\tStorage count:" << st.storage_count
        << "\n\tStatus:"<< storageStatusToStr(st.status)
        << "\n\tTotal capacity:" << st.total_capacity
        << "\n\tUsed capacity:" << st.used_capacity
        << "\n\tAvailable capacity:" << st.available_capacity;

    if(st.status == STORAGE_STATUS_READY) {
        uint32_t t = static_cast<uint32_t>(st.total_capacity);
        if(_storageTotal != t) {
            _storageTotal = t;
            emit storageTotalChanged();
        }
        uint32_t a = static_cast<uint32_t>(st.available_capacity);
        if(_storageFree != a) {
            _storageFree = a;
            emit storageFreeChanged();
        }
    }
    if(_storageStatus != static_cast<StorageStatus>(st.status)) {
        _storageStatus = static_cast<StorageStatus>(st.status);
        emit storageStatusChanged();
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::handleBatteryStatus(const mavlink_battery_status_t& bs)
{
    qCDebug(CameraControlLog) << "handleBatteryStatus:" << bs.battery_remaining;
    if(bs.battery_remaining >= 0 && _batteryRemaining != static_cast<int>(bs.battery_remaining)) {
        _batteryRemaining = static_cast<int>(bs.battery_remaining);
        emit batteryRemainingChanged();
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::handleCaptureStatus(const mavlink_camera_capture_status_t& cap)
{
    //-- This is a response to MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS
    qCDebug(CameraControlLog) << "handleCaptureStatus() - stopping timer, resetting retries";
    _captureStatusTimer.stop();
    _cameraCaptureStatusRetries = 0;
    qCDebug(CameraControlLog).noquote() << "handleCaptureStatus:"
        << "\n\tImage status:" << captureImageStatusToStr(cap.image_status)
        << "\n\tVideo status:" << captureVideoStatusToStr(cap.video_status)
        << "\n\tInterval:" << cap.image_interval
        << "\n\tRecording time (ms):" << cap.recording_time_ms
        << "\n\tCapacity:" << cap.available_capacity;
    //-- Disk Free Space
    uint32_t a = static_cast<uint32_t>(cap.available_capacity);
    if(_storageFree != a) {
        _storageFree = a;
        emit storageFreeChanged();
    }
    //-- Do we have recording time?
    if(cap.recording_time_ms) {
        // Resync our _recTime timer to the time info received from the camera component
        _recordTime = cap.recording_time_ms;
        _recTime = _recTime.addMSecs(_recTime.msecsTo(QTime::currentTime()) - static_cast<int>(cap.recording_time_ms));
        emit recordTimeChanged();
    }
    //-- Video/Image Capture Status
    uint8_t vs = cap.video_status < static_cast<uint8_t>(VIDEO_CAPTURE_STATUS_LAST) ? cap.video_status : static_cast<uint8_t>(VIDEO_CAPTURE_STATUS_UNDEFINED);
    uint8_t ps = cap.image_status < static_cast<uint8_t>(PHOTO_CAPTURE_LAST) ? cap.image_status : static_cast<uint8_t>(PHOTO_CAPTURE_STATUS_UNDEFINED);
    _setVideoStatus(static_cast<VideoCaptureStatus>(vs));
    _setPhotoStatus(static_cast<PhotoCaptureStatus>(ps));
    //-- Keep asking for it once in a while when recording
    if(videoCaptureStatus() == VIDEO_CAPTURE_STATUS_RUNNING) {
        _captureStatusTimer.start(5000);
    //-- Same while (single) image capture is busy
    } else if(photoCaptureStatus() != PHOTO_CAPTURE_IDLE && photoCaptureMode() == PHOTO_CAPTURE_SINGLE) {
        _captureStatusTimer.start(1000);
    }
    //-- Time Lapse
    if(photoCaptureStatus() == PHOTO_CAPTURE_INTERVAL_IDLE || photoCaptureStatus() == PHOTO_CAPTURE_INTERVAL_IN_PROGRESS) {
        //-- Capture local image as well
        QString photoPath = SettingsManager::instance()->appSettings()->savePath()->rawValue().toString() + QStringLiteral("/Photo");
        QDir().mkpath(photoPath);
        photoPath += "/" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hh.mm.ss.zzz") + ".jpg";
        VideoManager::instance()->grabImage(photoPath);
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::handleVideoInfo(const mavlink_video_stream_information_t* vi)
{
    qCDebug(CameraControlLog) << "handleVideoInfo:" << vi->stream_id << vi->uri;
    _expectedCount = vi->count;
    if(!_findStream(vi->stream_id, false)) {
        qCDebug(CameraControlLog) << "Create stream handler for stream ID:" << vi->stream_id;
        QGCVideoStreamInfo* pStream = new QGCVideoStreamInfo(*vi, this);
        QQmlEngine::setObjectOwnership(pStream, QQmlEngine::CppOwnership);
        _streams.append(pStream);
        //-- Thermal is handled separately and not listed
        if(!pStream->isThermal()) {
            _streamLabels.append(pStream->name());
            emit streamsChanged();
            emit streamLabelsChanged();
        } else {
            emit thermalStreamChanged();
        }
    }
    //-- Check for missing count
    if(_streams.count() < _expectedCount) {
        _streamInfoTimer.start(1000);
    } else if (_streamInfoTimer.isActive()) {
        //-- Done
        qCDebug(CameraControlLog) << "All stream handlers done";
        _streamInfoTimer.stop();
        _videoStreamInfoRetries = 0;
        emit autoStreamChanged();
        emit _vehicle->cameraManager()->streamChanged();
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::handleVideoStatus(const mavlink_video_stream_status_t* vs)
{
    qCDebug(CameraControlLog) << "handleVideoStatus() - stopping timer, resetting retries";
    _streamStatusTimer.stop();
    _videoStreamStatusRetries = 0;
    qCDebug(CameraControlLog) << "handleVideoStatus:" << vs->stream_id;
    QGCVideoStreamInfo* pInfo = _findStream(vs->stream_id);
    if(pInfo) {
        pInfo->update(*vs);
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::handleTrackingImageStatus(const mavlink_camera_tracking_image_status_t *tis)
{
    if (tis) {
        _trackingImageStatus = *tis;

        if (_trackingImageStatus.tracking_status == 0 || !trackingEnabled()) {
            _trackingImageRect = {};
            qCDebug(CameraControlLog) << "Tracking off";
        } else {
            if (_trackingImageStatus.tracking_mode == 2) {
                _trackingImageRect = QRectF(QPointF(_trackingImageStatus.rec_top_x, _trackingImageStatus.rec_top_y),
                                            QPointF(_trackingImageStatus.rec_bottom_x, _trackingImageStatus.rec_bottom_y));
            } else {
                float r = _trackingImageStatus.radius;
                if (qIsNaN(r) || r <= 0 ) {
                    r = 0.05f;
                }
                // Bottom is NAN so that we can draw perfect square using video aspect ratio
                _trackingImageRect = QRectF(QPointF(_trackingImageStatus.point_x - r, _trackingImageStatus.point_y - r),
                                            QPointF(_trackingImageStatus.point_x + r, NAN));
            }
            // get rectangle into [0..1] boundaries
            _trackingImageRect.setLeft(std::min(std::max(_trackingImageRect.left(), 0.0), 1.0));
            _trackingImageRect.setTop(std::min(std::max(_trackingImageRect.top(), 0.0), 1.0));
            _trackingImageRect.setRight(std::min(std::max(_trackingImageRect.right(), 0.0), 1.0));
            _trackingImageRect.setBottom(std::min(std::max(_trackingImageRect.bottom(), 0.0), 1.0));

            qCDebug(CameraControlLog) << "Tracking Image Status [left:" << _trackingImageRect.left()
                                      << "top:" << _trackingImageRect.top()
                                      << "right:" << _trackingImageRect.right()
                                      << "bottom:" << _trackingImageRect.bottom() << "]";
        }

        emit trackingImageStatusChanged();
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setCurrentStream(int stream)
{
    if (stream != _currentStream && stream >= 0 && stream < _streamLabels.count()) {
        QGCVideoStreamInfo* pInfo = currentStreamInstance();
        if(pInfo) {
            qCDebug(CameraControlLog) << "Stopping stream:" << pInfo->uri();
            //-- Stop current stream
            _vehicle->sendMavCommand(
                _compID,                                // Target component
                MAV_CMD_VIDEO_STOP_STREAMING,           // Command id
                false,                                  // ShowError
                pInfo->streamID());                     // Stream ID
        }
        _currentStream = stream;
        pInfo = currentStreamInstance();
        if(pInfo) {
            //-- Start new stream
            qCDebug(CameraControlLog) << "Starting stream:" << pInfo->uri();
            _vehicle->sendMavCommand(
                _compID,                                // Target component
                MAV_CMD_VIDEO_START_STREAMING,          // Command id
                false,                                  // ShowError
                pInfo->streamID());                     // Stream ID
            //-- Update stream status
            _requestStreamStatus(static_cast<uint8_t>(pInfo->streamID()));
        }
        emit currentStreamChanged();
        emit _vehicle->cameraManager()->streamChanged();
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::stopStream()
{
    QGCVideoStreamInfo* pInfo = currentStreamInstance();
    if(pInfo) {
        //-- Stop current stream
        _vehicle->sendMavCommand(
            _compID,                                // Target component
            MAV_CMD_VIDEO_STOP_STREAMING,           // Command id
            false,                                  // ShowError
            pInfo->streamID());                     // Stream ID
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::resumeStream()
{
    QGCVideoStreamInfo* pInfo = currentStreamInstance();
    if(pInfo) {
        //-- Start new stream
        _vehicle->sendMavCommand(
            _compID,                                // Target component
            MAV_CMD_VIDEO_START_STREAMING,          // Command id
            false,                                  // ShowError
            pInfo->streamID());                     // Stream ID
    }
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::autoStream() const
{
    if(hasVideoStream()) {
        return _streams.count() > 0;
    }
    return false;
}

//-----------------------------------------------------------------------------
QGCVideoStreamInfo*
VehicleCameraControl::currentStreamInstance()
{
    if(_currentStream < _streamLabels.count() && _streamLabels.count()) {
        QGCVideoStreamInfo* pStream = _findStream(_streamLabels[_currentStream]);
        return pStream;
    }
    return nullptr;
}

//-----------------------------------------------------------------------------
QGCVideoStreamInfo*
VehicleCameraControl::thermalStreamInstance()
{
    //-- For now, it will return the first thermal listed (if any)
    for(int i = 0; i < _streams.count(); i++) {
        if(_streams[i]) {
            QGCVideoStreamInfo* pStream = qobject_cast<QGCVideoStreamInfo*>(_streams[i]);
            if(pStream) {
                if(pStream->isThermal()) {
                    return pStream;
                }
            }
        }
    }
    return nullptr;
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_requestStreamInfo(uint8_t streamID)
{
    qCDebug(CameraControlLog) << "_requestStreamInfo() - stream:" << streamID << "retries:" << _videoStreamInfoRetries;
    // By default, try to use new REQUEST_MESSAGE command instead of
    // deprecated MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION.
    if (_videoStreamInfoRetries % 2 == 0) {
        qCDebug(CameraControlLog) << "_requestStreamInfo() - using REQUEST_MESSAGE";
        _vehicle->sendMavCommand(
            _compID,                                         // target component
            MAV_CMD_REQUEST_MESSAGE,                        // command id
            false,                                          // showError
            MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION,        // msgid
            streamID);                                      // stream ID
    } else {
        qCDebug(CameraControlLog) << "_requestStreamInfo() - using legacy MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION";
        _vehicle->sendMavCommand(
            _compID,                                            // Target component
            MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION,           // Command id
            false,                                              // ShowError
            streamID);                                          // Stream ID
    }
    _streamInfoTimer.start(1000);                           // Wait up to a second for it
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_requestStreamStatus(uint8_t streamID)
{
    qCDebug(CameraControlLog) << "_requestStreamStatus() - stream:" << streamID << "retries:" << _videoStreamStatusRetries;
    // By default, try to use new REQUEST_MESSAGE command instead of
    // deprecated MAV_CMD_REQUEST_VIDEO_STREAM_STATUS.
    if (_videoStreamStatusRetries % 2 == 0) {
        qCDebug(CameraControlLog) << "_requestStreamStatus() - using REQUEST_MESSAGE";
        _vehicle->sendMavCommand(
            _compID,                                         // target component
            MAV_CMD_REQUEST_MESSAGE,                        // command id
            false,                                          // showError
            MAVLINK_MSG_ID_VIDEO_STREAM_STATUS,             // msgid
            streamID);                                      // stream id
    } else {
        qCDebug(CameraControlLog) << "_requestStreamStatus() - using legacy MAV_CMD_REQUEST_VIDEO_STREAM_STATUS";
        _vehicle->sendMavCommand(
            _compID,                                            // Target component
            MAV_CMD_REQUEST_VIDEO_STREAM_STATUS,                // Command id
            false,                                              // ShowError
            streamID);                                          // Stream ID
    }
    _streamStatusTimer.start(1000);                         // Wait up to a second for it
}

//-----------------------------------------------------------------------------
QGCVideoStreamInfo*
VehicleCameraControl::_findStream(uint8_t id, bool report)
{
    for(int i = 0; i < _streams.count(); i++) {
        if(_streams[i]) {
            QGCVideoStreamInfo* pStream = qobject_cast<QGCVideoStreamInfo*>(_streams[i]);
            if(pStream) {
                if(pStream->streamID() == id) {
                    return pStream;
                }
            } else {
                qCritical() << "Null QGCVideoStreamInfo instance";
            }
        }
    }
    if(report) {
        qWarning() << "Stream id not found:" << id;
    }
    return nullptr;
}

//-----------------------------------------------------------------------------
QGCVideoStreamInfo*
VehicleCameraControl::_findStream(const QString name)
{
    for(int i = 0; i < _streams.count(); i++) {
        if(_streams[i]) {
            QGCVideoStreamInfo* pStream = qobject_cast<QGCVideoStreamInfo*>(_streams[i]);
            if(pStream) {
                if(pStream->name() == name) {
                    return pStream;
                }
            }
        }
    }
    return nullptr;
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_streamInfoTimeout()
{
    _videoStreamInfoRetries++;
    int count = _expectedCount * 6;
    if(_videoStreamInfoRetries > count) {
        qCWarning(CameraControlLog) << "Giving up requesting video stream info";
        _streamInfoTimer.stop();
        //-- If we have at least one stream, work with what we have.
        if(_streams.count()) {
            emit autoStreamChanged();
            emit _vehicle->cameraManager()->streamChanged();
        }
        return;
    }
    for(uint8_t i = 0; i < _expectedCount; i++) {
        //-- Stream ID starts at 1
        if(!_findStream(i+1, false)) {
            _requestStreamInfo(i+1);
            return;
        }
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_streamStatusTimeout()
{
    _videoStreamStatusRetries++;
    if(_videoStreamStatusRetries > 5) {
        qCWarning(CameraControlLog) << "Giving up requesting video stream status";
        _streamStatusTimer.stop();
        return;
    }
    QGCVideoStreamInfo* pStream = currentStreamInstance();
    if(pStream) {
        _requestStreamStatus(static_cast<uint8_t>(pStream->streamID()));
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_cameraSettingsTimeout()
{
    _cameraSettingsRetries++;
    qCDebug(CameraControlLog) << "_cameraSettingsTimeout() - retries now:" << _cameraSettingsRetries;
    if(_cameraSettingsRetries > 5) {
        qCWarning(CameraControlLog) << "Giving up requesting camera settings after" << _cameraSettingsRetries << "retries";
        _cameraSettingsTimer.stop();
        return;
    }
    qCDebug(CameraControlLog) << "_cameraSettingsTimeout() - calling _requestCameraSettings()";
    _requestCameraSettings();
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_storageInfoTimeout()
{
    _storageInfoRetries++;
    qCDebug(CameraControlLog) << "_storageInfoTimeout() - retries now:" << _storageInfoRetries;
    if(_storageInfoRetries > 5) {
        qCWarning(CameraControlLog) << "Giving up requesting storage info after" << _storageInfoRetries << "retries";
        _storageInfoTimer.stop();
        return;
    }
    qCDebug(CameraControlLog) << "_storageInfoTimeout() - calling _requestStorageInfo()";
    _requestStorageInfo();
}

//-----------------------------------------------------------------------------
QStringList
VehicleCameraControl::_loadExclusions(QDomNode option)
{
    QStringList exclusionList;
    QDomElement optionElem = option.toElement();
    QDomNodeList excRoot = optionElem.elementsByTagName(kExclusions);
    if(excRoot.size()) {
        //-- Iterate exclusions
        QDomNode node = excRoot.item(0);
        QDomElement elem = node.toElement();
        QDomNodeList exclusions = elem.elementsByTagName(kExclusion);
        for(int i = 0; i < exclusions.size(); i++) {
            QString exclude = exclusions.item(i).toElement().text();
            if(!exclude.isEmpty()) {
                exclusionList << exclude;
            }
        }
    }
    return exclusionList;
}

//-----------------------------------------------------------------------------
QStringList
VehicleCameraControl::_loadUpdates(QDomNode option)
{
    QStringList updateList;
    QDomElement optionElem = option.toElement();
    QDomNodeList updateRoot = optionElem.elementsByTagName(kUpdates);
    if(updateRoot.size()) {
        //-- Iterate updates
        QDomNode node = updateRoot.item(0);
        QDomElement elem = node.toElement();
        QDomNodeList updates = elem.elementsByTagName(kUpdate);
        for(int i = 0; i < updates.size(); i++) {
            QString update = updates.item(i).toElement().text();
            if(!update.isEmpty()) {
                updateList << update;
            }
        }
    }
    return updateList;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::_loadRanges(QDomNode option, const QString factName, QString paramValue)
{
    QDomElement optionElem = option.toElement();
    QDomNodeList rangeRoot = optionElem.elementsByTagName(kParameterranges);
    if(rangeRoot.size()) {
        QDomNode node = rangeRoot.item(0);
        QDomElement elem = node.toElement();
        QDomNodeList parameterRanges = elem.elementsByTagName(kParameterrange);
        //-- Iterate parameter ranges
        for(int i = 0; i < parameterRanges.size(); i++) {
            QString param;
            QString condition;
            QDomNode paramRange = parameterRanges.item(i);
            if(!read_attribute(paramRange, kParameter, param)) {
                qCritical() << QString("Malformed option range for parameter %1").arg(factName);
                return false;
            }
            read_attribute(paramRange, kCondition, condition);
            QDomElement pelem = paramRange.toElement();
            QDomNodeList rangeOptions = pelem.elementsByTagName(kRoption);
            QStringList  optNames;
            QStringList  optValues;
            //-- Iterate options
            for(int i = 0; i < rangeOptions.size(); i++) {
                QString optName;
                QString optValue;
                QDomNode roption = rangeOptions.item(i);
                if(!read_attribute(roption, kName, optName)) {
                    qCritical() << QString("Malformed roption for parameter %1").arg(factName);
                    return false;
                }
                if(!read_attribute(roption, kValue, optValue)) {
                    qCritical() << QString("Malformed rvalue for parameter %1").arg(factName);
                    return false;
                }
                optNames  << optName;
                optValues << optValue;
            }
            if(optNames.size()) {
                QGCCameraOptionRange* pRange = new QGCCameraOptionRange(this, factName, paramValue, param, condition, optNames, optValues);
                _optionRanges.append(pRange);
                qCDebug(CameraControlVerboseLog) << "New range limit:" << factName << paramValue << param << condition << optNames << optValues;
            }
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_processRanges()
{
    //-- After all parameter are loaded, process parameter ranges
    for(QGCCameraOptionRange* pRange: _optionRanges) {
        Fact* pRFact = getFact(pRange->targetParam);
        if(pRFact) {
            for(int i = 0; i < pRange->optNames.size(); i++) {
                QVariant optVariant;
                QString  errorString;
                if (!pRFact->metaData()->convertAndValidateRaw(pRange->optValues[i], false, optVariant, errorString)) {
                    qWarning() << "Invalid roption value, name:" << pRange->targetParam
                               << " type:"  << pRFact->metaData()->type()
                               << " value:" << pRange->optValues[i]
                               << " error:" << errorString;
                } else {
                    pRange->optVariants << optVariant;
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::_loadNameValue(QDomNode option, const QString factName, FactMetaData* metaData, QString& optName, QString& optValue, QVariant& optVariant)
{
    if(!read_attribute(option, kName, optName)) {
        qCritical() << QString("Malformed option for parameter %1").arg(factName);
        return false;
    }
    if(!read_attribute(option, kValue, optValue)) {
        qCritical() << QString("Malformed value for parameter %1").arg(factName);
        return false;
    }
    QString  errorString;
    if (!metaData->convertAndValidateRaw(optValue, false, optVariant, errorString)) {
        qWarning() << "Invalid option value, name:" << factName
                   << " type:"  << metaData->type()
                   << " value:" << optValue
                   << " error:" << errorString;
    }
    return true;
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_handleDefinitionFile(const QString &url)
{
    //-- First check and see if we have it cached
    QFile xmlFile(_cacheFile);

    QString ftpPrefix(QStringLiteral("%1://").arg(FTPManager::mavlinkFTPScheme));
    if (!xmlFile.exists() && url.startsWith(ftpPrefix, Qt::CaseInsensitive)) {
        qCDebug(CameraControlLog) << "No camera definition file cached, attempt ftp download";
        int ver = static_cast<int>(_info.cam_definition_version);
        QString ext = "";
        if (url.endsWith(".lzma", Qt::CaseInsensitive)) { ext = ".lzma"; }
        if (url.endsWith(".xz", Qt::CaseInsensitive)) { ext = ".xz"; }
        QString fileName = QString::asprintf("%s_%s_%03d.xml%s",
            _vendor.toStdString().c_str(),
            _modelName.toStdString().c_str(),
            ver,
            ext.toStdString().c_str());
        connect(_vehicle->ftpManager(), &FTPManager::downloadComplete, this, &VehicleCameraControl::_ftpDownloadComplete);
        _vehicle->ftpManager()->download(_compID, url,
            SettingsManager::instance()->appSettings()->parameterSavePath().toStdString().c_str(),
            fileName);
        return;
    }

    if (!xmlFile.exists()) {
        qCDebug(CameraControlLog) << "No camera definition file cached, attempt http download";
        _httpRequest(url);
        return;
    }
    if (!xmlFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not read cached camera definition file:" << _cacheFile;
        _httpRequest(url);
        return;
    }
    QByteArray bytes = xmlFile.readAll();
    QDomDocument doc;
    const QDomDocument::ParseResult result = doc.setContent(bytes, QDomDocument::ParseOption::Default);
    if (!result) {
        qWarning() << "Could not parse cached camera definition file:" << _cacheFile;
        _httpRequest(url);
        return;
    }
    //-- We have it
    qCDebug(CameraControlLog) << "Using cached camera definition file:" << _cacheFile;
    _cached = true;
    emit dataReady(bytes);
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_httpRequest(const QString &url)
{
    qCDebug(CameraControlLog) << "Request camera definition:" << url;
    if(!_netManager) {
        _netManager = new QNetworkAccessManager(this);
    }
    QNetworkProxy savedProxy = _netManager->proxy();
    QNetworkProxy tempProxy;
    tempProxy.setType(QNetworkProxy::DefaultProxy);
    _netManager->setProxy(tempProxy);
    QNetworkRequest request(QUrl::fromUserInput(url));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, true);
    QSslConfiguration conf = request.sslConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(conf);
    QNetworkReply* reply = _netManager->get(request);
    connect(reply, &QNetworkReply::finished,  this, &VehicleCameraControl::_downloadFinished);
    _netManager->setProxy(savedProxy);
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_downloadFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if(!reply) {
        return;
    }
    int err = reply->error();
    int http_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data = reply->readAll();
    if(err == QNetworkReply::NoError && http_code == 200) {
        data.append("\n");
    } else {
        data.clear();
        qWarning() << QString("Camera Definition (%1) download error: %2 status: %3").arg(
            reply->url().toDisplayString(),
            reply->errorString(),
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toString()
        );
    }
    emit dataReady(data);
    //reply->deleteLater();
}

void VehicleCameraControl::_ftpDownloadComplete(const QString& fileName, const QString& errorMsg)
{
    qCDebug(CameraControlLog) << "FTP Download completed: " << fileName << ", " << errorMsg;

    disconnect(_vehicle->ftpManager(), &FTPManager::downloadComplete, this, &VehicleCameraControl::_ftpDownloadComplete);

    QString outputFileName = fileName;

    if (fileName.endsWith(".lzma", Qt::CaseInsensitive) || fileName.endsWith(".xz", Qt::CaseInsensitive)) {
        outputFileName = fileName.left(fileName.lastIndexOf("."));
        if (QGCLZMA::inflateLZMAFile(fileName, outputFileName)) {
            QFile(fileName).remove();
        } else {
            qCWarning(CameraControlLog) << "Inflate of compressed xml failed" << fileName;
            outputFileName.clear();
        }
    }

    QFile xmlFile(outputFileName);

    if (!xmlFile.exists()) {
        qCDebug(CameraControlLog) << "No camera definition file present after ftp download completed";
        return;
    }
    if (!xmlFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not read downloaded camera definition file: " << fileName;
        return;
    }

    _cached = true;
    QByteArray bytes = xmlFile.readAll();
    emit dataReady(bytes);
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_dataReady(QByteArray data)
{
    if(data.size()) {
        qCDebug(CameraControlLog) << "Parsing camera definition";
        _loadCameraDefinitionFile(data);
    } else {
        qCDebug(CameraControlLog) << "No camera definition received, trying to search on our own...";
        QFile definitionFile;
        if(QGCCorePlugin::instance()->getOfflineCameraDefinitionFile(_modelName, definitionFile)) {
            qCDebug(CameraControlLog) << "Found offline definition file for: " << _modelName << ", loading: " << definitionFile.fileName();
            if (definitionFile.open(QIODevice::ReadOnly)) {
                QByteArray newData = definitionFile.readAll();
                _loadCameraDefinitionFile(newData);
            } else {
                qCDebug(CameraControlLog) << "error opening offline definition file for: " << _modelName;
            }
        } else {
            qCDebug(CameraControlLog) << "No offline camera definition file found";
        }
    }
    _initWhenReady();
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_paramDone()
{
    for(const QString& param: _paramIO.keys()) {
        if(!_paramIO[param]->paramDone()) {
            return;
        }
    }
    //-- All parameters loaded (or timed out)
    _paramComplete = true;
    emit parametersReady();
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_checkForVideoStreams()
{
    if(_info.flags & CAMERA_CAP_FLAGS_HAS_VIDEO_STREAM) {
        connect(&_streamInfoTimer, &QTimer::timeout, this, &VehicleCameraControl::_streamInfoTimeout);
        _streamInfoTimer.setSingleShot(false);
        connect(&_streamStatusTimer, &QTimer::timeout, this, &VehicleCameraControl::_streamStatusTimeout);
        _streamStatusTimer.setSingleShot(true);
        //-- Request all streams
        _requestStreamInfo(0);
        _streamInfoTimer.start(2000);
    }
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::incomingParameter(Fact* pFact, QVariant& newValue)
{
    Q_UNUSED(pFact);
    Q_UNUSED(newValue);
    return true;
}

//-----------------------------------------------------------------------------
bool
VehicleCameraControl::validateParameter(Fact* pFact, QVariant& newValue)
{
    Q_UNUSED(pFact);
    Q_UNUSED(newValue);
    return true;
}

//-----------------------------------------------------------------------------
QStringList
VehicleCameraControl::activeSettings() const
{
    qCDebug(CameraControlLog) << "Active:" << _activeSettings;
    return _activeSettings;
}

//-----------------------------------------------------------------------------
Fact*
VehicleCameraControl::exposureMode()
{
    return (_paramComplete && _activeSettings.contains(kCAM_EXPMODE)) ? getFact(kCAM_EXPMODE) : nullptr;
}

//-----------------------------------------------------------------------------
Fact*
VehicleCameraControl::ev()
{
    return (_paramComplete && _activeSettings.contains(kCAM_EV)) ? getFact(kCAM_EV) : nullptr;
}

//-----------------------------------------------------------------------------
Fact*
VehicleCameraControl::iso()
{
    return (_paramComplete && _activeSettings.contains(kCAM_ISO)) ? getFact(kCAM_ISO) : nullptr;
}

//-----------------------------------------------------------------------------
Fact*
VehicleCameraControl::shutterSpeed()
{
    return (_paramComplete && _activeSettings.contains(kCAM_SHUTTERSPD)) ? getFact(kCAM_SHUTTERSPD) : nullptr;
}

//-----------------------------------------------------------------------------
Fact*
VehicleCameraControl::aperture()
{
    return (_paramComplete && _activeSettings.contains(kCAM_APERTURE)) ? getFact(kCAM_APERTURE) : nullptr;
}

//-----------------------------------------------------------------------------
Fact*
VehicleCameraControl::wb()
{
    return (_paramComplete && _activeSettings.contains(kCAM_WBMODE)) ? getFact(kCAM_WBMODE) : nullptr;
}

//-----------------------------------------------------------------------------
Fact*
VehicleCameraControl::mode()
{
    return _paramComplete && factExists(kCAM_MODE) ? getFact(kCAM_MODE) : nullptr;
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::setTrackingEnabled(bool set)
{
    if(set) {
        _trackingStatus = static_cast<TrackingStatus>(_trackingStatus | TRACKING_ENABLED);
    } else {
        _trackingStatus = static_cast<TrackingStatus>(_trackingStatus & ~TRACKING_ENABLED);
    }
    emit trackingEnabledChanged();
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::startTracking(QRectF rec)
{
    if(_trackingMarquee != rec) {
        _trackingMarquee = rec;

        qCDebug(CameraControlLog) << "Start Tracking (Rectangle: ["
                                  << static_cast<float>(rec.x()) << ", "
                                  << static_cast<float>(rec.y()) << "] - ["
                                  << static_cast<float>(rec.x() + rec.width()) << ", "
                                  << static_cast<float>(rec.y() + rec.height()) << "]";

        _vehicle->sendMavCommand(_compID,
                                 MAV_CMD_CAMERA_TRACK_RECTANGLE,
                                 true,
                                 static_cast<float>(rec.x()),
                                 static_cast<float>(rec.y()),
                                 static_cast<float>(rec.x() + rec.width()),
                                 static_cast<float>(rec.y() + rec.height()));
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::startTracking(QPointF point, double radius)
{
    if(_trackingPoint != point || _trackingRadius != radius) {
        _trackingPoint  = point;
        _trackingRadius = radius;

        qCDebug(CameraControlLog) << "Start Tracking (Point: ["
                                  << static_cast<float>(point.x()) << ", "
                                  << static_cast<float>(point.y()) << "], Radius:  "
                                  << static_cast<float>(radius);

        _vehicle->sendMavCommand(_compID,
                                 MAV_CMD_CAMERA_TRACK_POINT,
                                 true,
                                 static_cast<float>(point.x()),
                                 static_cast<float>(point.y()),
                                 static_cast<float>(radius));
    }
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::stopTracking()
{
    qCDebug(CameraControlLog) << "Stop Tracking";

    //-- Stop Tracking
    _vehicle->sendMavCommand(_compID,
                             MAV_CMD_CAMERA_STOP_TRACKING,
                             true);

    //-- Stop Sending Tracking Status
    _vehicle->sendMavCommand(_compID,
                             MAV_CMD_SET_MESSAGE_INTERVAL,
                             true,
                             MAVLINK_MSG_ID_CAMERA_TRACKING_IMAGE_STATUS,
                             -1);

    // reset tracking image rectangle
    _trackingImageRect = {};
}

//-----------------------------------------------------------------------------
void
VehicleCameraControl::_requestTrackingStatus()
{
    _vehicle->sendMavCommand(_compID,
                             MAV_CMD_SET_MESSAGE_INTERVAL,
                             true,
                             MAVLINK_MSG_ID_CAMERA_TRACKING_IMAGE_STATUS,
                             500000); // Interval (us)
}
