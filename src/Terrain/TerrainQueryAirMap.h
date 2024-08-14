/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "TerrainQueryInterface.h"

#include <QObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>

Q_DECLARE_LOGGING_CATEGORY(TerrainQueryAirMapLog)

class QGeoCoordinate;

/// AirMap offline cachable implementation of terrain queries
class TerrainQueryAirMap : public TerrainQueryInterface {
    Q_OBJECT

public:
    TerrainQueryAirMap(QObject* parent = nullptr);

    // Overrides from TerrainQueryInterface
    void fetchTerrainHeight(const QGeoCoordinate& coordinate) override;
    void requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates) override;
    void requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord) override;
#if TERRAIN_CARPET_HEIGHTS_ENABLED
    void requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly) override;
#endif // TERRAIN_CARPET_HEIGHTS_ENABLED

    QString getTileHash(const QGeoCoordinate& coordinate) const final;

private:
    QString _getTileHash(const int x, const int y, const int z) const;

private slots:
    void _fetchDone(QByteArray responseBytes, QNetworkReply::NetworkError error);

private:
    QNetworkAccessManager _networkManager;
};
