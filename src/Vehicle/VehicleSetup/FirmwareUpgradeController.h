﻿/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtGui/QPixmap>
#include <QtQmlIntegration/QtQmlIntegration>
#include <QtQuick/QQuickItem>

#include "QGCSerialPortInfo.h"

class PX4FirmwareUpgradeThread;
class PX4FirmwareUpgradeThreadController;
class FirmwareImage;
class Fact;

/// Supported firmware types. If you modify these you will need to update the qml file as well.

// Firmware Upgrade MVC Controller for FirmwareUpgrade.qml.
class FirmwareUpgradeController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
public:
        typedef enum {
            AutoPilotStackPX4 = 0,
            AutoPilotStackAPM,
            SiKRadio,
            SingleFirmwareMode
        } AutoPilotStackType_t;

        typedef enum {
            StableFirmware = 0,
            BetaFirmware,
            DeveloperFirmware,
            CustomFirmware
        } FirmwareBuildType_t;

        typedef enum {
            CopterFirmware = 0,
            HeliFirmware,
            PlaneFirmware,
            RoverFirmware,
            SubFirmware,
            DefaultVehicleFirmware
        } FirmwareVehicleType_t;

        Q_ENUM(AutoPilotStackType_t)
        Q_ENUM(FirmwareBuildType_t)
        Q_ENUM(FirmwareVehicleType_t)

    class FirmwareIdentifier
    {
    public:
        FirmwareIdentifier(AutoPilotStackType_t stack = AutoPilotStackPX4,
                           FirmwareBuildType_t firmware = StableFirmware,
                           FirmwareVehicleType_t vehicle = DefaultVehicleFirmware)
            : autopilotStackType(stack), firmwareType(firmware), firmwareVehicleType(vehicle) {}

        bool operator==(const FirmwareIdentifier& firmwareId) const
        {
            return (firmwareId.autopilotStackType == autopilotStackType &&
                    firmwareId.firmwareType == firmwareType &&
                    firmwareId.firmwareVehicleType == firmwareVehicleType);
        }

        // members
        AutoPilotStackType_t    autopilotStackType;
        FirmwareBuildType_t     firmwareType;
        FirmwareVehicleType_t   firmwareVehicleType;
    };

    FirmwareUpgradeController(void);
    ~FirmwareUpgradeController();

    Q_PROPERTY(bool                 downloadingFirmwareList     MEMBER _downloadingFirmwareList                                     NOTIFY downloadingFirmwareListChanged)
    Q_PROPERTY(QString              boardPort                   READ boardPort                                                      NOTIFY boardFound)
    Q_PROPERTY(QString              boardDescription            READ boardDescription                                               NOTIFY boardFound)
    Q_PROPERTY(QString              boardType                   MEMBER _boardTypeName                                               NOTIFY boardFound)
    Q_PROPERTY(bool                 pixhawkBoard                READ pixhawkBoard                                                   NOTIFY boardFound)
    Q_PROPERTY(FirmwareBuildType_t  selectedFirmwareBuildType   READ selectedFirmwareBuildType  WRITE setSelectedFirmwareBuildType  NOTIFY selectedFirmwareBuildTypeChanged)
    Q_PROPERTY(QStringList          apmFirmwareNames            MEMBER _apmFirmwareNames                                            NOTIFY apmFirmwareNamesChanged)
    Q_PROPERTY(int                  apmFirmwareNamesBestIndex   MEMBER _apmFirmwareNamesBestIndex                                   NOTIFY apmFirmwareNamesChanged)
    Q_PROPERTY(QStringList          apmFirmwareUrls             MEMBER _apmFirmwareUrls                                             NOTIFY apmFirmwareNamesChanged)
    Q_PROPERTY(QString              px4StableVersion            READ px4StableVersion                                               NOTIFY px4StableVersionChanged)
    Q_PROPERTY(QString              px4BetaVersion              READ px4BetaVersion                                                 NOTIFY px4BetaVersionChanged)

    /// TextArea for log output
    Q_PROPERTY(QQuickItem* statusLog READ statusLog WRITE setStatusLog)
    
    /// Progress bar for you know what
    Q_PROPERTY(QQuickItem* progressBar READ progressBar WRITE setProgressBar)

    /// Starts searching for boards on the background thread
    Q_INVOKABLE void startBoardSearch(void);
    
    /// Cancels whatever state the upgrade worker thread is in
    Q_INVOKABLE void cancel(void);
    
    /// Called when the firmware type has been selected by the user to continue the flash process.
    Q_INVOKABLE void flash(AutoPilotStackType_t stackType,
                           FirmwareBuildType_t firmwareType = StableFirmware,
                           FirmwareVehicleType_t vehicleType = DefaultVehicleFirmware );

    Q_INVOKABLE void flashFirmwareUrl(QString firmwareUrl);

    /// Called to flash when upgrade is running in singleFirmwareMode
    Q_INVOKABLE void flashSingleFirmwareMode(FirmwareBuildType_t firmwareType);

    Q_INVOKABLE FirmwareVehicleType_t vehicleTypeFromFirmwareSelectionIndex(int index);
    
    // overload, not exposed to qml side
    void flash(const FirmwareIdentifier& firmwareId);

    // Property accessors
    
    QQuickItem* progressBar(void) { return _progressBar; }
    void setProgressBar(QQuickItem* progressBar) { _progressBar = progressBar; }
    
    QQuickItem* statusLog(void) { return _statusLog; }
    void setStatusLog(QQuickItem* statusLog) { _statusLog = statusLog; }
    
    QString boardPort(void) { return _boardInfo.portName(); }
    QString boardDescription(void) { return _boardInfo.description(); }

    FirmwareBuildType_t selectedFirmwareBuildType(void) { return _selectedFirmwareBuildType; }
    void setSelectedFirmwareBuildType(FirmwareBuildType_t firmwareType);
    QString firmwareTypeAsString(FirmwareBuildType_t type) const;

    QString     px4StableVersion    (void) { return _px4StableVersion; }
    QString     px4BetaVersion  (void) { return _px4BetaVersion; }

    bool pixhawkBoard(void) const { return _boardType == QGCSerialPortInfo::BoardTypePixhawk; }

    /**
     * @brief Return a human friendly string of available boards
     *
     * @return availableBoardNames
     */
    Q_INVOKABLE QStringList availableBoardsName(void);

signals:
    void boardFound                     (void);
    void showFirmwareSelectDlg          (void);
    void noBoardFound                   (void);
    void boardGone                      (void);
    void flashComplete                  (void);
    void flashCancelled                 (void);
    void error                          (void);
    void selectedFirmwareBuildTypeChanged(FirmwareBuildType_t firmwareType);
    void apmFirmwareNamesChanged        (void);
    void px4StableVersionChanged        (const QString& px4StableVersion);
    void px4BetaVersionChanged          (const QString& px4BetaVersion);
    void downloadingFirmwareListChanged (bool downloadingFirmwareList);

private slots:
    void _firmwareDownloadProgress          (qint64 curr, qint64 total);
    void _firmwareDownloadComplete          (QString remoteFile, QString localFile, QString errorMsg);
    void _foundBoard                        (bool firstAttempt, const QSerialPortInfo& portInfo, int boardType, QString boardName);
    void _noBoardFound                      (void);
    void _boardGone                         (void);
    void _foundBoardInfo                    (int bootloaderVersion, int boardID, int flashSize);
    void _error                             (const QString& errorString);
    void _status                            (const QString& statusString);
    void _bootloaderSyncFailed              (void);
    void _flashComplete                     (void);
    void _updateProgress                    (int curr, int total);
    void _eraseStarted                      (void);
    void _eraseComplete                     (void);
    void _eraseProgressTick                 (void);
    void _px4ReleasesGithubDownloadComplete (QString remoteFile, QString localFile, QString errorMsg);
    void _ardupilotManifestDownloadComplete (QString remoteFile, QString localFile, QString errorMsg);
    void _buildAPMFirmwareNames             (void);

private:
    QHash<FirmwareIdentifier, QString>* _firmwareHashForBoardId(int boardId);
    void _getFirmwareFile           (FirmwareIdentifier firmwareId);
    void _downloadFirmware          (void);
    void _appendStatusLog           (const QString& text, bool critical = false);
    void _errorCancel               (const QString& msg);
    void _determinePX4StableVersion (void);
    void _downloadArduPilotManifest (void);

    QString _singleFirmwareURL;
    bool    _singleFirmwareMode;
    bool    _downloadingFirmwareList;
    QString _portName;
    QString _portDescription;

    // Firmware hashes
    QHash<FirmwareIdentifier, QString> _rgSiKRadioFirmware;

    // Hash map for ArduPilot ChibiOS lookup by board name
    QHash<FirmwareIdentifier, QString> _rgAPMChibiosReplaceNamedBoardFirmware;
    QHash<FirmwareIdentifier, QString> _rgFirmwareDynamic;

    QMap<FirmwareBuildType_t, QMap<FirmwareVehicleType_t, QString> > _apmVersionMap;
    QList<FirmwareVehicleType_t>                                _apmVehicleTypeFromCurrentVersionList;

    /// Information which comes back from the bootloader
    bool        _bootloaderFound;           ///< true: we have received the foundBootloader signals
    uint32_t    _bootloaderVersion;         ///< Bootloader version
    uint32_t    _bootloaderBoardID;         ///< Board ID
    uint32_t    _bootloaderBoardFlashSize;  ///< Flash size in bytes of board
    
    bool                 _startFlashWhenBootloaderFound;
    FirmwareIdentifier   _startFlashWhenBootloaderFoundFirmwareIdentity;

    QPixmap _boardIcon;             ///< Icon used to display image of board
    
    QString _firmwareFilename;      ///< Image which we are going to flash to the board
    
    /// @brief Thread controller which is used to run bootloader commands on separate thread
    PX4FirmwareUpgradeThreadController* _threadController;
    
    static const int    _eraseTickMsec = 500;       ///< Progress bar update tick time for erase
    static const int    _eraseTotalMsec = 15000;    ///< Estimated amount of time erase takes
    int                 _eraseTickCount;            ///< Number of ticks for erase progress update
    QTimer              _eraseTimer;                ///< Timer used to update progress bar for erase

    static const int    _findBoardTimeoutMsec = 30000;      ///< Amount of time for user to plug in USB
    static const int    _findBootloaderTimeoutMsec = 5000;  ///< Amount time to look for bootloader
    
    QQuickItem*     _statusLog;         ///< Status log TextArea Qml control
    QQuickItem*     _progressBar;
    
    bool _searchingForBoard;    ///< true: searching for board, false: search for bootloader
    
    QSerialPortInfo                 _boardInfo;
    QGCSerialPortInfo::BoardType_t  _boardType;
    QString                         _boardTypeName;

    FirmwareBuildType_t             _selectedFirmwareBuildType;

    FirmwareImage*                  _image;

    QString _px4StableVersion;  // Version strange for latest PX4 stable
    QString _px4BetaVersion;    // Version strange for latest PX4 beta

    const QString _apmBoardDescriptionReplaceText;

    static constexpr const char* _manifestFirmwareJsonKey =               "firmware";
    static constexpr const char* _manifestBoardIdJsonKey =                "board_id";
    static constexpr const char* _manifestMavTypeJsonKey =                "mav-type";
    static constexpr const char* _manifestFormatJsonKey =                 "format";
    static constexpr const char* _manifestUrlJsonKey =                    "url";
    static constexpr const char* _manifestMavFirmwareVersionTypeJsonKey = "mav-firmware-version-type";
    static constexpr const char* _manifestUSBIDJsonKey =                  "USBID";
    static constexpr const char* _manifestMavFirmwareVersionJsonKey =     "mav-firmware-version";
    static constexpr const char* _manifestBootloaderStrJsonKey =          "bootloader_str";
    static constexpr const char* _manifestLatestKey =                     "latest";
    static constexpr const char* _manifestPlatformKey =                   "platform";
    static constexpr const char* _manifestBrandNameKey =                  "brand_name";

    typedef struct {
        uint32_t                boardId;
        FirmwareBuildType_t     firmwareBuildType;
        FirmwareVehicleType_t   vehicleType;
        QString                 url;
        QString                 version;
        QStringList             rgBootloaderPortString;
        QList<int>              rgVID;
        QList<int>              rgPID;
        QString                 friendlyName;
        bool                    chibios;
        bool                    fmuv2;
    } ManifestFirmwareInfo_t;


    QList<ManifestFirmwareInfo_t>           _rgManifestFirmwareInfo;
    QMap<QString, FirmwareBuildType_t>      _manifestMavFirmwareVersionTypeToFirmwareBuildTypeMap;
    QMap<QString, FirmwareVehicleType_t>    _manifestMavTypeToFirmwareVehicleTypeMap;
    QStringList                             _apmFirmwareNames;
    int                                     _apmFirmwareNamesBestIndex = 0;
    QStringList                             _apmFirmwareUrls;
    Fact*                                   _apmChibiOSSetting;
    Fact*                                   _apmVehicleTypeSetting;

    FirmwareBuildType_t     _manifestMavFirmwareVersionTypeToFirmwareBuildType  (const QString& manifestMavFirmwareVersionType);
    FirmwareVehicleType_t   _manifestMavTypeToFirmwareVehicleType               (const QString& manifestMavType);
};

// global hashing function
uint qHash(const FirmwareUpgradeController::FirmwareIdentifier& firmwareId);
