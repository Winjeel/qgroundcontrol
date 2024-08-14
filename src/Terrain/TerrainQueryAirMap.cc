/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TerrainQueryAirMap.h"
#include "TerrainTileManager.h"
#include "TerrainQueryTest.h"

#include "QGCApplication.h"
#include "QGCFileDownload.h"
#include "QGCLoggingCategory.h"
#include "QGCMapEngine.h"

#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

QGC_LOGGING_CATEGORY(TerrainQueryAirMapLog, "TerrainQueryAirMapLog")
QGC_LOGGING_CATEGORY(TerrainAirmapQueryVerboseLog, "TerrainAirmapQueryVerboseLog")

static const auto kMapType = UrlFactory::kCopernicusElevationProviderKey;

TerrainQueryAirMap::TerrainQueryAirMap(QObject* parent)
    : TerrainQueryInterface(parent)
{
    qCDebug(TerrainAirmapQueryVerboseLog) << "supportsSsl" << QSslSocket::supportsSsl() << "sslLibraryBuildVersionString" << QSslSocket::sslLibraryBuildVersionString();
}

void TerrainQueryAirMap::requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates)
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

void TerrainQueryAirMap::requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestPathHeights(fromCoord, toCoord);
        return;
    }

    TerrainTileManager::instance()->addPathQuery(this, fromCoord, toCoord);
}

void TerrainQueryAirMap::requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly)
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

QString TerrainQueryAirMap::getTileHash(const QGeoCoordinate& coordinate) const
{
    const int z = 1;
    const int x = getQGCMapEngine()->urlFactory()->long2tileX(kMapType, coordinate.longitude(), z);
    const int y = getQGCMapEngine()->urlFactory()->lat2tileY(kMapType, coordinate.latitude(), z);

    const QString ret = _getTileHash(x, y, z);
    qCDebug(TerrainQueryAirMapLog) << "Computing unique tile hash for " << coordinate << "=>" << ret;
    return ret;
}

QString TerrainQueryAirMap::_getTileHash(const int x, const int y, const int z) const
{
    return QGCMapEngine::getTileHash("Airmap Elevation", x, y, z);
}
