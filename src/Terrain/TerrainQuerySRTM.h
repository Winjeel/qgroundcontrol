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
#include "AP_SRTM_Grid.h"

#include <QObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>

Q_DECLARE_LOGGING_CATEGORY(TerrainQuerySRTMLog)

/// SRTM offline cachable implementation of terrain queries
class TerrainQuerySRTM : public TerrainQueryInterface {
    Q_OBJECT

    friend class TerrainQueryTest;

public:
    TerrainQuerySRTM(QObject* parent = nullptr);

    // Overrides from TerrainQueryInterface
    void fetchTerrainHeight(const QGeoCoordinate& coordinate) override;
    void requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates) override;
    void requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord) override;
#if TERRAIN_CARPET_HEIGHTS_ENABLED
    void requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly) override;
#endif // TERRAIN_CARPET_HEIGHTS_ENABLED

    QString getTileHash(const QGeoCoordinate& coordinate) const final;

private:
    typedef struct {
        uint16_t x;
        uint16_t y;
        int32_t numEastBlocks;
    } GridOffset;

    static QString    _calcFilename(const QGeoCoordinate& coordinate);
    static GridOffset _calcGridOffset(const QGeoCoordinate& coordinate, uint16_t spacing);
    static uint64_t   _calcFileOffset(const GridOffset& gridOffset);

    static QString  _getTileHash(const QString& filename, const GridOffset& gridOffset);
    static uint16_t _getBlockCrc(AP_SRTM_Grid::Block& block);
};
