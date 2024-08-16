/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#ifndef PACKED_STRUCT
#ifdef __GNUC__
#define PACKED_STRUCT( __Declaration__ ) __Declaration__ __attribute__((packed))
#else
#define PACKED_STRUCT( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop) )
#endif
#endif

/// The data blocks used within ArduPilot SRTM *.DAT files, as defined in:
///     https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_Terrain/AP_Terrain.h
///
/// ArduPilot SRTM *.DAT files available at:
///     https://terrain.ardupilot.org/
namespace AP_SRTM_Grid {

    // MAVLink sends 4x4 grids
    static constexpr size_t MAVLINK_GRID_SIZE = 4;

    // a 2k grid_block on disk contains 8x7 of the mavlink grids.  Each
    // grid block overlaps by one with its neighbour. This ensures that
    // the altitude at any point can be calculated from a single grid
    // block
    static constexpr size_t BLOCK_MUL_X = 7;
    static constexpr size_t BLOCK_MUL_Y = 8;

    // this is the spacing between 32x28 grid blocks, in grid_spacing units
    static constexpr size_t BLOCK_SPACING_X = ((BLOCK_MUL_X - 1) * MAVLINK_GRID_SIZE);
    static constexpr size_t BLOCK_SPACING_Y = ((BLOCK_MUL_Y - 1) * MAVLINK_GRID_SIZE);

    // giving a total grid size of a disk grid_block of 32x28
    static constexpr size_t BLOCK_SIZE_X = (MAVLINK_GRID_SIZE * BLOCK_MUL_X);
    static constexpr size_t BLOCK_SIZE_Y = (MAVLINK_GRID_SIZE * BLOCK_MUL_Y);

    /*
      a grid block is a structure in a local file containing height
      information. Each grid block is 2048 in size, to keep file IO to
      block oriented SD cards efficient
     */
    PACKED_STRUCT(
        typedef struct Block {
            // bitmap of 4x4 grids filled in from GCS (56 bits are used)
            uint64_t bitmap;

            // south west corner of block in degrees*10^7
            int32_t lat;
            int32_t lon;

            // crc of whole block, taken with crc=0
            uint16_t crc;

            // format version number
            uint16_t version;

            // grid spacing in meters
            uint16_t spacing;

            // heights in meters over a 32*28 grid
            int16_t height[BLOCK_SIZE_X][BLOCK_SIZE_Y];

            // indices info 32x28 grids for this degree reference
            uint16_t grid_idx_x;
            uint16_t grid_idx_y;

            // rounded latitude/longitude in degrees.
            int16_t lon_degrees;
            int8_t lat_degrees;
        }
    ) Block;

    /*
      Block for disk IO, aligned on 2048 byte boundaries
     */
    PACKED_STRUCT(
        typedef struct BlockIO {
            Block block;
            uint8_t _padding[227];
        }
    ) BlockIO;
    static_assert(sizeof(BlockIO) == 2048);

};
