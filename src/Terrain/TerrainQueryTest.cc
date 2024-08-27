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
    { -35.36326100, +149.16523000,  586, "Canberra Model Aircraft Club (CMAC)", },

    { -35.50000000, +149.50000000,  953, "Mid-point of S36E149", },

    { -36.00000000, +149.00000000, 1150, "SW Corner of S36E149", },
    { -36.00000000, +149.99999999,  238, "SE Corner of S36E149", },
    { -35.00000001, +149.00000000,  594, "NW Corner of S36E149", },
    { -35.00000001, +149.99999999,  564, "NE Corner of S36E149", },
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


void TerrainQueryTest::_testSRTMGridOffset(void)
{
    typedef struct {
        double lat;
        double lon;
        uint16_t spacing;

        uint16_t x;
        uint16_t y;
        QString description;
    } TestLocation;
    // SRTM1 30m spacing
    static const TestLocation sTestGridLocations[] = {
        // Check the corners of the S36E149 Grid
        { -36.00000000, +149.00000000,  30,   0,   0, "SW Corner of S36E149", },
        { -36.00000000, +149.99999999,  30,   0, 107, "SE Corner of S36E149", },
        { -35.00000001, +149.00000000,  30, 154,   0, "NW Corner of S36E149", },
        { -35.00000001, +149.99999999,  30, 154, 107, "NE Corner of S36E149", },

        // Check the borders of a Block within the S36E149 Grid
        //   - These two points are ~15m apart (east-west)
        { -35.37260000, +149.36230000,  30,  97,  38, "Block 97,38 max longitude" },
        { -35.37260000, +149.36245000,  30,  97,  39, "Block 97,39 min longitude" },
        //   - These two points are ~15m apart (north-south)
        { -35.50840000, +149.50250000,  30,  76,  54, "Block 76,54 min latitude" },
        { -35.50855000, +149.50250000,  30,  75,  54, "Block 75,54 max latitude" },
    };

    for (auto testLoc: sTestGridLocations) {
        qInfo() << "Testing grid offset for location:" << testLoc.description;

        const auto coord = QGeoCoordinate { testLoc.lat, testLoc.lon };
        const auto gridOffset = TerrainQuerySRTM::_calcGridOffset(coord, testLoc.spacing);

        QVERIFY(gridOffset.x == testLoc.x);
        QVERIFY(gridOffset.y == testLoc.y);
    }
};


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

        const QString errorMessage = QStringLiteral("Elevation (%1) error (%2) is greater than tolerance (±%3)")
                                         .arg(elevation)
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

void TerrainQueryTest::_tileFetchComplete(TerrainTile* tile, QString hash)
{
    _haveTerrainTile = true;
    _terrainTile = *tile;
}
