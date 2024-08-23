/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <cmath>


namespace AP {

    typedef struct {
        float x;
        float y;
    } Vector2f;

    /// Implements a subset of the ArduPilot Location class defined at:
    ///     https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_Common/Location.h
    class Location {

    public:
        // lat and lon in 1e7 degrees
        Location(const int32_t lat, const int32_t lon);
        // lat and lon in degrees
        Location(const double lat, const double lon);

        // constants to convert between coordinate represnentations:
        //   - int32 (in 1e7 degrees); and
        //   - double (in degrees).
        static constexpr double COORD_DOUBLE_TO_INT32 = 1e7;
        static constexpr double COORD_INT32_TO_DOUBLE = 1e-7;

        int32_t lat() const { return _lat; }
        int32_t lon() const { return _lon; }

        // return the distance in meters in North/East plane as a N/E vector to loc2
        Vector2f get_distance_NE(const Location &loc2) const;

        // extrapolate latitude/longitude given distances (in meters) north and east
        void offset(float north_m, float east_m);

    private:
        // longitude_scale - returns the scaler to compensate for
        // shrinking longitude as you move north or south from the equator
        // Note: this does not include the scaling to convert
        // longitude/latitude points to meters or centimeters
        static float longitude_scale(int32_t lat);

        // wrap longitude for -180e7 to 180e7
        static int32_t wrap_longitude(int64_t lon);

        // get lon1-lon2, wrapping at -180e7 to 180e7
        static int32_t diff_longitude(int32_t lon1, int32_t lon2);

        // limit latitude to -90e7 to 90e7
        static int32_t limit_lattitude(int32_t lat);

        int32_t _lat; // in 1e7 degrees
        int32_t _lon; // in 1e7 degrees
    };
};
