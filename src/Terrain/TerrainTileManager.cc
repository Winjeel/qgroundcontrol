/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TerrainTileManager.h"

#include "TerrainQuery.h"
#include "QGCMapEngine.h"
#include "QGeoMapReplyQGC.h"

#include <QtLocation/private/qgeotilespec_p.h>

#include <qmath.h>

QGC_LOGGING_CATEGORY(TerrainTileManagerLog, "TerrainTileManagerLog")
QGC_LOGGING_CATEGORY(TerrainTileManagerVerboseLog, "TerrainTileManagerVerboseLog")

Q_GLOBAL_STATIC(TerrainTileManager, s_terrainTileManager)

static const auto kMapType = UrlFactory::kCopernicusElevationProviderKey;


TerrainTileManager* TerrainTileManager::instance(void)
{
    return s_terrainTileManager();
}

TerrainTileManager::TerrainTileManager(void)
{

}

void TerrainTileManager::addCoordinateQuery(TerrainQueryInterface* terrainQueryInterface, const QList<QGeoCoordinate>& coordinates)
{
    qCDebug(TerrainTileManagerLog) << "TerrainTileManager::addCoordinateQuery count" << coordinates.count();

    if (coordinates.length() > 0) {
        bool error;
        QList<double> altitudes;

        if (!getAltitudesForCoordinates(coordinates, altitudes, error)) {
            qCDebug(TerrainTileManagerLog) << "TerrainTileManager::addPathQuery queue count" << _requestQueue.count();
            QueuedRequestInfo_t queuedRequestInfo = { terrainQueryInterface, QueryMode::QueryModeCoordinates, 0, 0, coordinates };
            _requestQueue.append(queuedRequestInfo);
            return;
        }

        if (error) {
            QList<double> noAltitudes;
            qCWarning(TerrainTileManagerLog) << "addCoordinateQuery: signalling failure due to internal error";
            emit terrainQueryInterface->coordinateHeightsReceived(false, noAltitudes);
        } else {
            qCDebug(TerrainTileManagerLog) << "addCoordinateQuery: All altitudes taken from cached data";
            emit terrainQueryInterface->coordinateHeightsReceived(coordinates.count() == altitudes.count(), altitudes);
        }
    }
}

/// Returns a list of individual coordinates along the requested path spaced according to the terrain tile value spacing
QList<QGeoCoordinate> TerrainTileManager::pathQueryToCoords(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord, double& distanceBetween, double& finalDistanceBetween)
{
    QList<QGeoCoordinate> coordinates;

    double lat      = fromCoord.latitude();
    double lon      = fromCoord.longitude();
    double steps    = qCeil(toCoord.distanceTo(fromCoord) / TerrainTile::tileValueSpacingMeters);
    double latDiff  = toCoord.latitude() - lat;
    double lonDiff  = toCoord.longitude() - lon;

    if (steps == 0) {
        coordinates.append(fromCoord);
        coordinates.append(toCoord);
        distanceBetween = finalDistanceBetween = coordinates[0].distanceTo(coordinates[1]);
    } else {
        for (double i = 0.0; i <= steps; i = i + 1) {
            coordinates.append(QGeoCoordinate(lat + latDiff * i / steps, lon + lonDiff * i / steps));
        }
        // We always have one too many and we always want the last one to be the endpoint
        coordinates.last() = toCoord;
        distanceBetween = coordinates[0].distanceTo(coordinates[1]);
        finalDistanceBetween = coordinates[coordinates.count() - 2].distanceTo(coordinates.last());
    }

    //qDebug() << "terrain" << startPoint.distanceTo(endPoint) << coordinates.count() << distanceBetween;

    qCDebug(TerrainTileManagerLog) << "TerrainTileManager::pathQueryToCoords fromCoord:toCoord:distanceBetween:finalDisanceBetween:coordCount" << fromCoord << toCoord << distanceBetween << finalDistanceBetween << coordinates.count();

    return coordinates;
}

void TerrainTileManager::addPathQuery(TerrainQueryInterface* terrainQueryInterface, const QGeoCoordinate &startPoint, const QGeoCoordinate &endPoint)
{
    QList<QGeoCoordinate> coordinates;
    double distanceBetween;
    double finalDistanceBetween;

    coordinates = pathQueryToCoords(startPoint, endPoint, distanceBetween, finalDistanceBetween);

    bool error;
    QList<double> altitudes;
    if (!getAltitudesForCoordinates(coordinates, altitudes, error)) {
        qCDebug(TerrainTileManagerLog) << "TerrainTileManager::addPathQuery queue count" << _requestQueue.count();
        QueuedRequestInfo_t queuedRequestInfo = { terrainQueryInterface, QueryMode::QueryModePath, distanceBetween, finalDistanceBetween, coordinates };
        _requestQueue.append(queuedRequestInfo);
        return;
    }

    if (error) {
        QList<double> noAltitudes;
        qCWarning(TerrainTileManagerLog) << "addPathQuery: signalling failure due to internal error";
        emit terrainQueryInterface->pathHeightsReceived(false, distanceBetween, finalDistanceBetween, noAltitudes);
    } else {
        qCDebug(TerrainTileManagerLog) << "addPathQuery: All altitudes taken from cached data";
        emit terrainQueryInterface->pathHeightsReceived(coordinates.count() == altitudes.count(), distanceBetween, finalDistanceBetween, altitudes);
    }
}

/// Either returns altitudes from cache or queues database request
///     @param[out] error true: altitude not returned due to error, false: altitudes returned
/// @return true: altitude returned (check error as well), false: database query queued (altitudes not returned)
bool TerrainTileManager::getAltitudesForCoordinates(const QList<QGeoCoordinate>& coordinates, QList<double>& altitudes, bool& error)
{
    error = false;

    for (const QGeoCoordinate& coordinate: coordinates) {
        QString tileHash = _getTileHash(coordinate);
        qCDebug(TerrainTileManagerLog) << "TerrainTileManager::getAltitudesForCoordinates hash:coordinate" << tileHash << coordinate;

        _tilesMutex.lock();
        if (_tiles.contains(tileHash)) {
            double elevation = _tiles[tileHash].elevation(coordinate);
            if (qIsNaN(elevation)) {
                error = true;
                qCWarning(TerrainTileManagerLog) << "TerrainTileManager::getAltitudesForCoordinates Internal Error: missing elevation in tile cache";
            } else {
                qCDebug(TerrainTileManagerLog) << "TerrainTileManager::getAltitudesForCoordinates returning elevation from tile cache" << elevation;
            }
            altitudes.push_back(elevation);
        } else {
            if (_state != State::Downloading) {
                QNetworkRequest request = getQGCMapEngine()->urlFactory()->getTileURL(
                    kMapType, getQGCMapEngine()->urlFactory()->long2tileX(kMapType, coordinate.longitude(), 1),
                    getQGCMapEngine()->urlFactory()->lat2tileY(kMapType, coordinate.latitude(), 1),
                    1,
                    &_networkManager);
                qCDebug(TerrainTileManagerLog) << "TerrainTileManager::getAltitudesForCoordinates query from database" << request.url();
                QGeoTileSpec spec;
                spec.setX(getQGCMapEngine()->urlFactory()->long2tileX(kMapType, coordinate.longitude(), 1));
                spec.setY(getQGCMapEngine()->urlFactory()->lat2tileY(kMapType, coordinate.latitude(), 1));
                spec.setZoom(1);
                spec.setMapId(getQGCMapEngine()->urlFactory()->getIdFromType(kMapType));
                QGeoTiledMapReplyQGC* reply = new QGeoTiledMapReplyQGC(&_networkManager, request, spec);
                connect(reply, &QGeoTiledMapReplyQGC::terrainDone, this, &TerrainTileManager::_terrainDone);
                _state = State::Downloading;
            }
            _tilesMutex.unlock();

            return false;
        }
        _tilesMutex.unlock();
    }

    return true;
}

void TerrainTileManager::_tileFailed(void)
{
    QList<double> noAltitudes;

    for (const QueuedRequestInfo_t& requestInfo: _requestQueue) {
        if (requestInfo.queryMode == QueryMode::QueryModeCoordinates) {
            emit requestInfo.terrainQueryInterface->coordinateHeightsReceived(false, noAltitudes);
        } else if (requestInfo.queryMode == QueryMode::QueryModePath) {
            emit requestInfo.terrainQueryInterface->pathHeightsReceived(false, requestInfo.distanceBetween, requestInfo.finalDistanceBetween, noAltitudes);
        }
    }
    _requestQueue.clear();
}

void TerrainTileManager::_terrainDone(QByteArray responseBytes, QNetworkReply::NetworkError error)
{
    QGeoTiledMapReplyQGC* reply = qobject_cast<QGeoTiledMapReplyQGC*>(QObject::sender());
    _state = State::Idle;

    if (!reply) {
        qCWarning(TerrainTileManagerLog) << "Elevation tile fetched but invalid reply data type.";
        return;
    }

    // remove from download queue
    QGeoTileSpec spec = reply->tileSpec();
    QString hash = QGCMapEngine::getTileHash(kMapType, spec.x(), spec.y(), spec.zoom());

    // handle potential errors
    if (error != QNetworkReply::NoError) {
        qCWarning(TerrainTileManagerLog) << "Elevation tile fetching returned error (" << error << ")";
        _tileFailed();
        reply->deleteLater();
        return;
    }
    if (responseBytes.isEmpty()) {
        qCWarning(TerrainTileManagerLog) << "Error in fetching elevation tile. Empty response.";
        _tileFailed();
        reply->deleteLater();
        return;
    }

    qCDebug(TerrainTileManagerLog) << "Received some bytes of terrain data: " << responseBytes.size();

    TerrainTile* terrainTile = new TerrainTile(responseBytes);
    if (terrainTile->isValid()) {
        _tilesMutex.lock();
        if (!_tiles.contains(hash)) {
            _tiles.insert(hash, *terrainTile);
        } else {
            delete terrainTile;
        }
        _tilesMutex.unlock();
    } else {
        delete terrainTile;
        qCWarning(TerrainTileManagerLog) << "Received invalid tile";
    }
    reply->deleteLater();

    // now try to query the data again
    for (int i = _requestQueue.count() - 1; i >= 0; i--) {
        bool error;
        QList<double> altitudes;
        QueuedRequestInfo_t& requestInfo = _requestQueue[i];

        if (getAltitudesForCoordinates(requestInfo.coordinates, altitudes, error)) {
            if (requestInfo.queryMode == QueryMode::QueryModeCoordinates) {
                if (error) {
                    QList<double> noAltitudes;
                    qCWarning(TerrainTileManagerLog) << "_terrainDone(coordinateQuery): signalling failure due to internal error";
                    emit requestInfo.terrainQueryInterface->coordinateHeightsReceived(false, noAltitudes);
                } else {
                    qCDebug(TerrainTileManagerLog) << "_terrainDone(coordinateQuery): All altitudes taken from cached data";
                    emit requestInfo.terrainQueryInterface->coordinateHeightsReceived(requestInfo.coordinates.count() == altitudes.count(), altitudes);
                }
            } else if (requestInfo.queryMode == QueryMode::QueryModePath) {
                if (error) {
                    QList<double> noAltitudes;
                    qCWarning(TerrainTileManagerLog) << "_terrainDone(coordinateQuery): signalling failure due to internal error";
                    emit requestInfo.terrainQueryInterface->pathHeightsReceived(false, requestInfo.distanceBetween, requestInfo.finalDistanceBetween, noAltitudes);
                } else {
                    qCDebug(TerrainTileManagerLog) << "_terrainDone(coordinateQuery): All altitudes taken from cached data";
                    emit requestInfo.terrainQueryInterface->pathHeightsReceived(requestInfo.coordinates.count() == altitudes.count(), requestInfo.distanceBetween, requestInfo.finalDistanceBetween, altitudes);
                }
            }
            _requestQueue.removeAt(i);
        }
    }
}

QString TerrainTileManager::_getTileHash(const QGeoCoordinate& coordinate)
{
    QString ret = QGCMapEngine::getTileHash(
        kMapType,
        getQGCMapEngine()->urlFactory()->long2tileX(kMapType, coordinate.longitude(), 1),
        getQGCMapEngine()->urlFactory()->lat2tileY(kMapType, coordinate.latitude(), 1),
        1);
    qCDebug(TerrainTileManagerVerboseLog) << "Computing unique tile hash for " << coordinate << ret;

    return ret;
}

