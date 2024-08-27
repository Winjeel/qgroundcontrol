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

#include "QGCFileDownload.h"
#include "QGCLoggingCategory.h"
#include "QGCMapEngine.h"
#include "QGeoMapReplyQGC.h"

#include <QtLocation/private/qgeotilespec_p.h>

#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

QGC_LOGGING_CATEGORY(TerrainQueryAirMapLog, "TerrainQueryAirMapLog")

static const auto kMapType = UrlFactory::kCopernicusElevationProviderKey;

TerrainQueryAirMap::TerrainQueryAirMap(QObject* parent)
    : TerrainQueryInterface(parent)
{
    qCDebug(TerrainQueryAirMapLog) << "supportsSsl" << QSslSocket::supportsSsl() << "sslLibraryBuildVersionString" << QSslSocket::sslLibraryBuildVersionString();
}

void TerrainQueryAirMap::fetchTerrainHeight(const QGeoCoordinate& coordinate)
{
    qCDebug(TerrainQueryAirMapLog) << "TerrainQueryAirMap::fetch" << coordinate;

    QNetworkRequest request = getQGCMapEngine()->urlFactory()->getTileURL(kMapType, getQGCMapEngine()->urlFactory()->long2tileX(kMapType,coordinate.longitude(), 1), getQGCMapEngine()->urlFactory()->lat2tileY(kMapType, coordinate.latitude(), 1), 1, &_networkManager);

    QGeoTileSpec spec;
    spec.setX(getQGCMapEngine()->urlFactory()->long2tileX(kMapType, coordinate.longitude(), 1));
    spec.setY(getQGCMapEngine()->urlFactory()->lat2tileY(kMapType, coordinate.latitude(), 1));
    spec.setZoom(1);
    spec.setMapId(getQGCMapEngine()->urlFactory()->getIdFromType(kMapType));

    QGeoTiledMapReplyQGC* reply = new QGeoTiledMapReplyQGC(&_networkManager, request, spec);
    connect(reply, &QGeoTiledMapReplyQGC::terrainDone, this, &TerrainQueryAirMap::_fetchDone);
}

void TerrainQueryAirMap::_fetchDone(QByteArray responseBytes, QNetworkReply::NetworkError error)
{
    QGeoTiledMapReplyQGC* reply = qobject_cast<QGeoTiledMapReplyQGC*>(QObject::sender());

    if (!reply) {
        qCWarning(TerrainQueryAirMapLog) << "Elevation tile fetched but invalid reply data type.";
        emit fetchFailed(FetchError::InvalidDataType);
        return;
    }

    // remove from download queue
    QGeoTileSpec spec = reply->tileSpec();
    QString hash = _getTileHash(spec.x(), spec.y(), spec.zoom());

    // handle potential errors
    if (error != QNetworkReply::NoError) {
        qCWarning(TerrainQueryAirMapLog) << "Elevation tile fetching returned error (" << error << ")";
        emit fetchFailed(FetchError::NetworkError);
        reply->deleteLater();
        return;
    }
    if (responseBytes.isEmpty()) {
        qCWarning(TerrainQueryAirMapLog) << "Error in fetching elevation tile. Empty response.";
        emit fetchFailed(FetchError::EmptyResponse);
        reply->deleteLater();
        return;
    }

    qCDebug(TerrainQueryAirMapLog) << "Received some bytes of terrain data: " << responseBytes.size();

    TerrainTile* terrainTile = new TerrainTile(responseBytes);
    emit fetchComplete(terrainTile, hash);
    reply->deleteLater();
}

void TerrainQueryAirMap::requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates)
{
    TerrainTileManager::instance()->addCoordinateQuery(this, coordinates);
}

void TerrainQueryAirMap::requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord)
{
    TerrainTileManager::instance()->addPathQuery(this, fromCoord, toCoord);
}

#if TERRAIN_CARPET_HEIGHTS_ENABLED
void TerrainQueryAirMap::requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly)
{
    // TODO
    Q_UNUSED(swCoord);
    Q_UNUSED(neCoord);
    Q_UNUSED(statsOnly);
    qWarning() << "Carpet queries are currently not supported from offline air map data";
}
#endif // TERRAIN_CARPET_HEIGHTS_ENABLED

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
    return QGCMapEngine::getTileHash(kMapType, x, y, z);
}
