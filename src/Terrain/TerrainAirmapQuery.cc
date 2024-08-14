/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TerrainAirmapQuery.h"
#include "TerrainTileManager.h"
#include "TerrainQueryTest.h"

#include "QGCApplication.h"
#include "QGCFileDownload.h"
#include "QGCLoggingCategory.h"

#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

QGC_LOGGING_CATEGORY(TerrainAirmapQueryLog, "TerrainAirmapQueryLog")
QGC_LOGGING_CATEGORY(TerrainAirmapQueryVerboseLog, "TerrainAirmapQueryVerboseLog")

TerrainOfflineAirMapQuery::TerrainOfflineAirMapQuery(QObject* parent)
    : TerrainQueryInterface(parent)
{
    qCDebug(TerrainAirmapQueryVerboseLog) << "supportsSsl" << QSslSocket::supportsSsl() << "sslLibraryBuildVersionString" << QSslSocket::sslLibraryBuildVersionString();
}

void TerrainOfflineAirMapQuery::requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestCoordinateHeights(coordinates);
        return;
    }

    if (coordinates.length() == 0) {
        return;
    }

    TerrainTileManager::instance()->addCoordinateQuery(this, coordinates);
}

void TerrainOfflineAirMapQuery::requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestPathHeights(fromCoord, toCoord);
        return;
    }

    TerrainTileManager::instance()->addPathQuery(this, fromCoord, toCoord);
}

void TerrainOfflineAirMapQuery::requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestCarpetHeights(swCoord, neCoord, statsOnly);
        return;
    }

    // TODO
    Q_UNUSED(swCoord);
    Q_UNUSED(neCoord);
    Q_UNUSED(statsOnly);
    qWarning() << "Carpet queries are currently not supported from offline air map data";
}
