/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "UnitTest.h"

#include "TerrainQueryInterface.h"
#include "TerrainQueryAirMap.h"
#include "TerrainQuerySRTM.h"
#include "TerrainTile.h"


/// Unit test for the TerrainQuery Object
class TerrainQueryTest : public UnitTest
{
    Q_OBJECT

public:
    TerrainQueryTest(void);

protected slots:
    void init(void) override;
    void cleanup(void) override;

private slots:
    void _testSRTMQuery  (void) { auto q = TerrainQuerySRTM(this);   _testQuery(q, 10); }
    // Disable until I can get SSL working again with Qt v5.15 on Ubuntu 22.04
    // void _testAirmapQuery(void) { auto q = TerrainQueryAirMap(this); _testQuery(q, 10); }

    void _testSRTMGridOffset(void);

    void _tileFetchFailed(TerrainQueryInterface::FetchError error);
    void _tileFetchComplete(TerrainTile tile, QString hash);

private:
    bool        _haveTerrainTile;
    TerrainTile _terrainTile;

    void _testQuery(TerrainQueryInterface& query, const uint16_t kTolerance);

};
