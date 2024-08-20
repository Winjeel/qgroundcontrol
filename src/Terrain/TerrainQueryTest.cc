/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TerrainQueryTest.h"

typedef struct {
    double lat;
    double lon;
    double elevation;
    QString description;
} TestLocation;

// These elevations sourced from Google Earth.
static const TestLocation sTestLocations[] = {
    { -35.363261,  149.165230,  586, "Canberra Model Aircraft Club (CMAC)", },

    { -35.500000,  149.500000,  953, "Mid-point of S36E149", },

    { -35.000001,  149.000001,  594, "NW Corner of S36E149", },
    { -35.000001,  149.999999,  564, "NE Corner of S36E149", },
    { -35.999999,  149.000001, 1150, "SW Corner of S36E149", },
    { -35.999999,  149.999999,  237, "SE Corner of S36E149", },
};

TerrainQueryTest::TerrainQueryTest(void)
    : UnitTest()
{

}

void TerrainQueryTest::init(void)
{

}

void TerrainQueryTest::cleanup(void)
{
    UnitTest::cleanup();
}

void TerrainQueryTest::_testQuery(TerrainQueryInterface& query, const uint16_t kTolerance) {
    connect(&query, &TerrainQueryInterface::fetchComplete, this, &TerrainQueryTest::_tileFetchComplete);
    connect(&query, &TerrainQueryInterface::fetchFailed,   this, &TerrainQueryTest::_tileFetchFailed);

    for (auto testLoc: sTestLocations) {
        qInfo() << "Testing elevation for location:" << testLoc.description;

        _haveTerrainTile = false;
        const auto coord = QGeoCoordinate { testLoc.lat, testLoc.lon };

        query.fetchTerrainHeight(coord);

        // Wait for the fetch to complete and the callbacks to be triggered.
        QTest::qWait(100);

        QVERIFY(_haveTerrainTile);
        const double elevation = _terrainTile.elevation(coord);
        const double error = testLoc.elevation - elevation;

        const QString errorMessage = QStringLiteral("Error (%1) is greater than tolerance (±%2)")
                                         .arg(error)
                                         .arg(kTolerance);
        QVERIFY2(qAbs(error) < kTolerance, qPrintable(errorMessage));
    }
}

void TerrainQueryTest::_tileFetchFailed(TerrainQueryInterface::FetchError error)
{
    Q_UNUSED(error);
    _haveTerrainTile = false;
}

void TerrainQueryTest::_tileFetchComplete(TerrainTile tile, QString hash)
{
    _haveTerrainTile = true;
    _terrainTile = tile;
}
