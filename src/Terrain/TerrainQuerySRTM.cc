/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TerrainQuerySRTM.h"
#include "TerrainTileManager.h"
#include "TerrainQueryTest.h"

#include "AppSettings.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include "QGCMapEngine.h"
#include "SettingsManager.h"

QGC_LOGGING_CATEGORY(TerrainQuerySRTMLog, "TerrainQuerySRTMLog")

// We need a typedef here because the comma in the QHash breaks the Q_GLOBAL_STATIC #define
typedef QHash<QString, uint16_t> SpacingCache;
Q_GLOBAL_STATIC(SpacingCache, sSpacingCache)

TerrainQuerySRTM::TerrainQuerySRTM(QObject* parent)
    : TerrainQueryInterface(parent)
{

}

void TerrainQuerySRTM::fetchTerrainHeight(const QGeoCoordinate& coordinate)
{
    qCDebug(TerrainQuerySRTMLog) << "fetch():" << coordinate;

    const auto filename = _calcFilename(coordinate);
    QDir terrainDir(qgcApp()->toolbox()->settingsManager()->appSettings()->terrainSavePath());
    QFile srtmFile(terrainDir.absoluteFilePath(filename));
    if (!srtmFile.exists()) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): SRTM file not found! " << srtmFile;
        emit fetchFailed();
        return;
    }

    AP_SRTM_Grid::Block block;

    srtmFile.open(QIODevice::ReadOnly);

    // Read the first block to get the spacing
    auto bytesRead = srtmFile.read((char *)&block, sizeof(block));
    if (bytesRead != sizeof(block)) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad block initial read";
        srtmFile.close();
        emit fetchFailed();
        return;
    }

    // CRC check
    if (_getBlockCrc(block) != block.crc) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad initial CRC";
        emit fetchFailed();
        return;
    }

    const auto gridOffset = _calcGridOffset(coordinate, block.spacing);
    const auto fileOffset = _calcFileOffset(gridOffset);

    const bool seekError = fileOffset && !srtmFile.seek(fileOffset);
    if (seekError) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad block seek";
        srtmFile.close();
        emit fetchFailed();
        return;
    }

    // Read the desired block
    bytesRead = srtmFile.read((char *)&block, sizeof(block));
    srtmFile.close();
    if (bytesRead != sizeof(block)) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad block read";
        emit fetchFailed();
        return;
    }

    // CRC check
    if (_getBlockCrc(block) != block.crc) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad CRC";
        emit fetchFailed();
        return;
    }

    const bool gotExpectedBlock = (block.grid_idx_x == gridOffset.x) && (block.grid_idx_y == gridOffset.y);
    if (!gotExpectedBlock) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad block index";
        emit fetchFailed();
        return;
    }

    // TODO: bitmap check

    // Generate TerrainTile
    TerrainTile tile = TerrainTile(block);
    sSpacingCache->insert(filename, block.spacing);
    const QString hash = _getTileHash(filename, gridOffset);

    emit fetchComplete(tile, hash);
}

void TerrainQuerySRTM::requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates)
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

void TerrainQuerySRTM::requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestPathHeights(fromCoord, toCoord);
        return;
    }

    TerrainTileManager::instance()->addPathQuery(this, fromCoord, toCoord);
}

#if TERRAIN_CARPET_HEIGHTS_ENABLED
void TerrainQuerySRTM::requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestCarpetHeights(swCoord, neCoord, statsOnly);
        return;
    }

    Q_UNUSED(swCoord);
    Q_UNUSED(neCoord);
    Q_UNUSED(statsOnly);
    qWarning() << "Carpet queries are currently not supported from SRTM data";
}
#endif // TERRAIN_CARPET_HEIGHTS_ENABLED

QString TerrainQuerySRTM::getTileHash(const QGeoCoordinate& coordinate) const
{
    const auto filename = _calcFilename(coordinate);
    // Unless we've read an SRTM file, assume it is SRTM3 data with 100m spacing.
    const uint16_t kDefaultSpacing = 100;
    const auto spacing = sSpacingCache->value(filename, kDefaultSpacing);
    const auto gridOffset = _calcGridOffset(coordinate, spacing);

    const QString ret = _getTileHash(filename, gridOffset);
    qCDebug(TerrainQuerySRTMLog) << "Computing unique tile hash for" << coordinate << "=>" << ret;
    return ret;
}

QString TerrainQuerySRTM::_getTileHash(const QString& filename, const GridOffset& gridOffset)
{
    return QGCMapEngine::getTileHash(filename, gridOffset.x, gridOffset.y, gridOffset.numEastBlocks);
}

QString TerrainQuerySRTM::_calcFilename(const QGeoCoordinate& coordinate)
{
    const auto latitude = coordinate.latitude();
    const auto longitude = coordinate.longitude();

    return QStringLiteral("%1%2%3%4.DAT")
               .arg((latitude >= 0) ? 'N' : 'S')
               .arg(abs(qFloor(latitude)))
               .arg((longitude >= 0) ? 'E' : 'W')
               .arg(abs(qFloor(longitude)));
}

TerrainQuerySRTM::GridOffset TerrainQuerySRTM::_calcGridOffset(const QGeoCoordinate& coordinate, uint16_t spacing)
{
    const auto latitude = coordinate.latitude();
    const auto longitude = coordinate.longitude();

    // Calculate corners
    const auto swCorner = QGeoCoordinate(qFloor(latitude), qFloor(longitude));
    const auto east_overlap = 2 * spacing * AP_SRTM_Grid::BLOCK_SIZE_Y;
    const auto seCorner = QGeoCoordinate(qFloor(latitude), qFloor(longitude + 1))
                              .atDistanceAndAzimuth(east_overlap, 90.0);

    // Calculate the offset of the desired grid
    const auto distance_gridunits = swCorner.distanceTo(coordinate) / spacing;
    const auto azimuth_rad = qDegreesToRadians(swCorner.azimuthTo(coordinate));

    const uint16_t x = qFloor((distance_gridunits * cos(azimuth_rad)) / AP_SRTM_Grid::BLOCK_SPACING_X);
    const uint16_t y = qFloor((distance_gridunits * sin(azimuth_rad)) / AP_SRTM_Grid::BLOCK_SPACING_Y);

    // Calculate number of east-west blocks
    const auto grid_width_gridunits = swCorner.distanceTo(seCorner) / spacing;
    const int32_t numEastBlocks = qFloor(grid_width_gridunits / AP_SRTM_Grid::BLOCK_SPACING_Y);

    return GridOffset{ x, y, numEastBlocks, };
}

// Calculate the offset into the file of the desired block
uint64_t TerrainQuerySRTM::_calcFileOffset(const GridOffset& gridOffset)
{
    const uint64_t numGrids = (gridOffset.numEastBlocks * gridOffset.x) + gridOffset.y;
    return numGrids * sizeof(AP_SRTM_Grid::BlockIO);
}

uint16_t TerrainQuerySRTM::_getBlockCrc(AP_SRTM_Grid::Block& block)
{
    uint16_t saved_crc = block.crc;
    block.crc = 0;
    uint16_t ret = QGC::crc16_ccitt((const uint8_t *)&block, sizeof(block), 0);
    block.crc = saved_crc;
    return ret;
}
