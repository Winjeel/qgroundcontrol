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

#include "AP_Location.h"
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

    QDir terrainDir;
    if (qgcApp()->runningUnitTests()) {
        terrainDir = QDir(":/unittest/Terrain");
    } else {
        terrainDir = QDir(qgcApp()->toolbox()->settingsManager()->appSettings()->terrainSavePath());
    }

    QFile srtmFile(terrainDir.absoluteFilePath(filename));
    if (!srtmFile.exists()) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): SRTM file not found! " << srtmFile;
        emit fetchFailed(FetchError::FileNotFound);
        return;
    }

    AP_SRTM_Grid::Block block;

    srtmFile.open(QIODevice::ReadOnly);

    const uint64_t fileSize = srtmFile.size();
    if (fileSize < sizeof(block)) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad file size";
        srtmFile.close();
        emit fetchFailed(FetchError::FileRead);
        return;
    }

    // Read the first block to get the spacing
    auto bytesRead = srtmFile.read((char *)&block, sizeof(block));
    if (bytesRead != sizeof(block)) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad block initial read";
        srtmFile.close();
        emit fetchFailed(FetchError::FileRead);
        return;
    }

    // CRC check
    if (_getBlockCrc(block) != block.crc) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad initial CRC";
        emit fetchFailed(FetchError::CRC);
        return;
    }

    const auto gridOffset = _calcGridOffset(coordinate, block.spacing);
    const auto fileOffset = _calcFileOffset(gridOffset);

    if (fileOffset > (fileSize - sizeof(block))) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad file offset";
        srtmFile.close();
        emit fetchFailed(FetchError::FileRead);
        return;
    }

    // Check if we need another block, otherwise the current block already has the coordinate we want
    if (fileOffset) {
        // Seek to the desired block
        if (!srtmFile.seek(fileOffset)) {
            qCWarning(TerrainQuerySRTMLog) << "fetch(): bad block seek";
            srtmFile.close();
            emit fetchFailed(FetchError::FileRead);
            return;
        }

        // Read the desired block
        bytesRead = srtmFile.read((char *)&block, sizeof(block));
        srtmFile.close();
        if (bytesRead != sizeof(block)) {
            qCWarning(TerrainQuerySRTMLog) << "fetch(): bad block read";
            emit fetchFailed(FetchError::FileRead);
            return;
        }

        // CRC check
        if (_getBlockCrc(block) != block.crc) {
            qCWarning(TerrainQuerySRTMLog) << "fetch(): bad CRC";
            emit fetchFailed(FetchError::CRC);
            return;
        }
    }

    const bool gotExpectedBlock = (block.grid_idx_x == gridOffset.x) && (block.grid_idx_y == gridOffset.y);
    if (!gotExpectedBlock) {
        qCWarning(TerrainQuerySRTMLog) << "fetch(): bad block index";
        emit fetchFailed(FetchError::UnexpectedData);
        return;
    }

    // Generate TerrainTile
    TerrainTile* tile = new TerrainTile(block);
    sSpacingCache->insert(filename, block.spacing);
    const QString hash = _getTileHash(filename, gridOffset);

    emit fetchComplete(tile, hash);
}

void TerrainQuerySRTM::requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates)
{
    TerrainTileManager::instance()->addCoordinateQuery(this, coordinates);
}

void TerrainQuerySRTM::requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord)
{
    TerrainTileManager::instance()->addPathQuery(this, fromCoord, toCoord);
}

#if TERRAIN_CARPET_HEIGHTS_ENABLED
void TerrainQuerySRTM::requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly)
{
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
    const double latitude = coordinate.latitude();
    const double longitude = coordinate.longitude();

    const double swLatitude = qFloor(latitude);
    const double swLongitude = qFloor(longitude);

    // Note: we have to use the ArduPilot location functions that were used to generate the terrain
    // files, as the Qt functions use different algorithms which results in incorrect offsets.

    // Calculate number of east-west blocks
    //
    // Each grid overlaps it's neighbours by two blocks.
    const double eastOverlap = 2 * spacing * AP_SRTM_Grid::BLOCK_SIZE_Y;
    const auto swLoc = AP::Location { swLatitude, swLongitude     };
    auto seLoc =       AP::Location { swLatitude, swLongitude + 1 };
    seLoc.offset(0, eastOverlap);

    const auto seOffset = swLoc.get_distance_NE(seLoc);
    const int32_t numEastBlocks = qFloor(seOffset.y / spacing / AP_SRTM_Grid::BLOCK_SPACING_Y);

    // Calculate the offset into the tile of the requested coordinate.
    //
    const auto coordLoc = AP::Location { latitude, longitude };
    const auto coordOffset = swLoc.get_distance_NE(coordLoc);

    const uint16_t x = coordOffset.x / spacing / AP_SRTM_Grid::BLOCK_SPACING_X;
    const uint16_t y = coordOffset.y / spacing / AP_SRTM_Grid::BLOCK_SPACING_Y;

    return GridOffset{ x, y, numEastBlocks, };
}

// Calculate the byte offset into the file of the desired block
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
