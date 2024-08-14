/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "TerrainTile.h"
#include "TerrainQueryInterface.h"

#include <QObject>
#include <QGeoCoordinate>
#include <QNetworkReply>
#include <QMutex>

Q_DECLARE_METATYPE(TerrainTile)

Q_DECLARE_LOGGING_CATEGORY(TerrainTileManagerLog)

/// Used internally by TerrainQueryAirMap to manage terrain tiles
class TerrainTileManager : public QObject {
    Q_OBJECT

public:
    TerrainTileManager(void);

    void addCoordinateQuery         (TerrainQueryInterface* terrainQueryInterface, const QList<QGeoCoordinate>& coordinates);
    void addPathQuery               (TerrainQueryInterface* terrainQueryInterface, const QGeoCoordinate& startPoint, const QGeoCoordinate& endPoint);
    bool getAltitudesForCoordinates (const QList<QGeoCoordinate>& coordinates, QList<double>& altitudes, bool& error);

    static TerrainTileManager* instance();

    // Returns a new instance of a TerrainQueryInterface.
    // Caller is responsible for deleting instance.
    static TerrainQueryInterface* newQueryProvider(QObject* parent);
    static QList<QGeoCoordinate> pathQueryToCoords(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord, double& distanceBetween, double& finalDistanceBetween);

public slots:
    void tileFetchComplete(TerrainTile tile, QString hash);
    void tileFetchFailed(void);

private:
    enum class State {
        Idle,
        Downloading,
    };

    enum QueryMode {
        QueryModeCoordinates,
        QueryModePath,
#if TERRAIN_CARPET_HEIGHTS_ENABLED
        QueryModeCarpet
#endif // TERRAIN_CARPET_HEIGHTS_ENABLED
    };

    typedef struct {
        TerrainQueryInterface*      terrainQueryInterface;
        QueryMode                   queryMode;
        double                      distanceBetween;        // Distance between each returned height
        double                      finalDistanceBetween;   // Distance between for final height
        QList<QGeoCoordinate>       coordinates;
    } QueuedRequestInfo_t;

    QString _getTileHash                        (const QGeoCoordinate& coordinate);

    QList<QueuedRequestInfo_t>  _requestQueue;
    State                       _state = State::Idle;
    QNetworkAccessManager       _networkManager;

    QMutex                      _tilesMutex;
    QHash<QString, TerrainTile> _tiles;
};
