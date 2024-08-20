/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "AP_SRTM_Grid.h"

bool AP_SRTM_Grid::checkBitmap(const uint8_t x, const uint8_t y, const uint64_t bitmap)
{
    const uint8_t bitnum = (y / MAVLINK_GRID_SIZE) + (BLOCK_MUL_Y * (x / MAVLINK_GRID_SIZE));
    const uint64_t mask = (uint64_t)1 << bitnum;
    return (bitmap & mask) != 0;
}
