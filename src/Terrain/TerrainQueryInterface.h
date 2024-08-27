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

#include <QObject>

class QGeoCoordinate;

#define TERRAIN_CARPET_HEIGHTS_ENABLED 0

/// Base class for offline/online terrain queries
class TerrainQueryInterface : public QObject
{
    Q_OBJECT

public:
    TerrainQueryInterface(QObject* parent) : QObject(parent) { }

    /// Request terrain heights for specified coodinates.
    /// Signals: fetchComplete when data is available, or fetchFailed on failure
    virtual void fetchTerrainHeight(const QGeoCoordinate& coordinate) = 0;

    /// Request terrain heights for specified coodinates.
    /// Signals: coordinateHeights when data is available
    virtual void requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates) = 0;

    /// Requests terrain heights along the path specified by the two coordinates.
    /// Signals: pathHeights
    ///     @param coordinates to query
    virtual void requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord) = 0;

#if TERRAIN_CARPET_HEIGHTS_ENABLED
    /// Request terrain heights for the rectangular area specified.
    /// Signals: carpetHeights when data is available
    ///     @param swCoord South-West bound of rectangular area to query
    ///     @param neCoord North-East bound of rectangular area to query
    ///     @param statsOnly true: Return only stats, no carpet data
    virtual void requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly) = 0;
#endif // TERRAIN_CARPET_HEIGHTS_ENABLED

    virtual QString getTileHash(const QGeoCoordinate& coordinate) const = 0;

    enum class FetchError {
        InvalidDataType,
        NetworkError,
        EmptyResponse,
        FileNotFound,
        FileRead,
        CRC,
        UnexpectedData,
    };
    Q_ENUM(FetchError);

signals:
    void fetchComplete(TerrainTile* tile, QString hash);
    void fetchFailed(FetchError error);

    void coordinateHeightsReceived(bool success, QList<double> heights);
    void pathHeightsReceived(bool success, double distanceBetween, double finalDistanceBetween, const QList<double>& heights);
#if TERRAIN_CARPET_HEIGHTS_ENABLED
    void carpetHeightsReceived(bool success, double minHeight, double maxHeight, const QList<QList<double>>& carpet);
#endif // TERRAIN_CARPET_HEIGHTS_ENABLED
};
