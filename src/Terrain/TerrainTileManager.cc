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

#include <qmath.h>

QGC_LOGGING_CATEGORY(TerrainTileManagerLog, "TerrainTileManagerLog")
QGC_LOGGING_CATEGORY(TerrainTileManagerVerboseLog, "TerrainTileManagerVerboseLog")

Q_GLOBAL_STATIC(TerrainTileManager, s_terrainTileManager)


TerrainTileManager* TerrainTileManager::instance(void)
{
    return s_terrainTileManager();
}

TerrainQueryInterface* TerrainTileManager::newQueryProvider(QObject* parent)
{
    return new TerrainQueryAirMap(parent);
}

TerrainTileManager::TerrainTileManager(void)
{
    qRegisterMetaType<TerrainTile>();
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
                auto request = newQueryProvider(this);
                // These need to use a QueuedConnection so that the _tilesMutex accesses occur on the correct thread.
                connect(request, &TerrainQueryInterface::fetchComplete, this, &TerrainTileManager::tileFetchComplete, Qt::ConnectionType::QueuedConnection);
                connect(request, &TerrainQueryInterface::fetchFailed,   this, &TerrainTileManager::tileFetchFailed,   Qt::ConnectionType::QueuedConnection);
                request->fetchTerrainHeight(coordinate);
                _state = State::Downloading;
            }
            _tilesMutex.unlock();

            return false;
        }
        _tilesMutex.unlock();
    }

    return true;
}

void TerrainTileManager::tileFetchFailed(void)
{
    TerrainQueryInterface* reply = qobject_cast<TerrainQueryInterface*>(QObject::sender());
    _state = State::Idle;

    if (reply) {
        reply->deleteLater();
    } else {
        qCWarning(TerrainTileManagerLog) << "Elevation tile fetch failed but invalid reply data type.";
    }

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

void TerrainTileManager::tileFetchComplete(TerrainTile tile, QString hash)
{
    TerrainQueryInterface* reply = qobject_cast<TerrainQueryInterface*>(QObject::sender());
    _state = State::Idle;

    if (reply) {
        reply->deleteLater();
    } else {
        qCWarning(TerrainTileManagerLog) << "Elevation tile fetched but invalid reply data type.";
    }

    if (tile.isValid()) {
        _tilesMutex.lock();
        if (!_tiles.contains(hash)) {
            _tiles.insert(hash, tile);
        }
        _tilesMutex.unlock();
    } else {
        qCWarning(TerrainTileManagerLog) << "Received invalid tile";
    }

    // now try to query the data again
    for (int i = _requestQueue.count() - 1; i >= 0; i--) {
        bool error;
        QList<double> altitudes;
        QueuedRequestInfo_t& requestInfo = _requestQueue[i];

        if (getAltitudesForCoordinates(requestInfo.coordinates, altitudes, error)) {
            if (requestInfo.queryMode == QueryMode::QueryModeCoordinates) {
                if (error) {
                    QList<double> noAltitudes;
                    qCWarning(TerrainTileManagerLog) << "tileFetchComplete(coordinateQuery): signalling failure due to internal error";
                    emit requestInfo.terrainQueryInterface->coordinateHeightsReceived(false, noAltitudes);
                } else {
                    qCDebug(TerrainTileManagerLog) << "tileFetchComplete(coordinateQuery): All altitudes taken from cached data";
                    emit requestInfo.terrainQueryInterface->coordinateHeightsReceived(requestInfo.coordinates.count() == altitudes.count(), altitudes);
                }
            } else if (requestInfo.queryMode == QueryMode::QueryModePath) {
                if (error) {
                    QList<double> noAltitudes;
                    qCWarning(TerrainTileManagerLog) << "tileFetchComplete(pathQuery): signalling failure due to internal error";
                    emit requestInfo.terrainQueryInterface->pathHeightsReceived(false, requestInfo.distanceBetween, requestInfo.finalDistanceBetween, noAltitudes);
                } else {
                    qCDebug(TerrainTileManagerLog) << "tileFetchComplete(pathQuery): All altitudes taken from cached data";
                    emit requestInfo.terrainQueryInterface->pathHeightsReceived(requestInfo.coordinates.count() == altitudes.count(), requestInfo.distanceBetween, requestInfo.finalDistanceBetween, altitudes);
                }
            }
            _requestQueue.removeAt(i);
        }
    }
}

QString TerrainTileManager::_getTileHash(const QGeoCoordinate& coordinate)
{
    const auto prov = newQueryProvider(this);
    const auto hash = prov->getTileHash(coordinate);
    delete prov;
    return hash;
}

