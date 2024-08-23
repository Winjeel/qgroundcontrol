/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "AP_Location.h"


namespace AP {
    // scaling factor from 1e-7 degrees to meters at equator
    // == 1.0e-7 * DEG_TO_RAD * RADIUS_OF_EARTH
    static constexpr float LOCATION_SCALING_FACTOR = 0.011131884502145034f;
    // inverse of LOCATION_SCALING_FACTOR
    static constexpr float LOCATION_SCALING_FACTOR_INV = 89.83204953368922f;

    Location::Location(const int32_t lat, const int32_t lon)
        : _lat(lat)
        , _lon(lon)
    {}

    Location::Location(const double lat, const double lon)
        : _lat(lat * COORD_DOUBLE_TO_INT32)
        , _lon(lon * COORD_DOUBLE_TO_INT32)
    {}

    float Location::longitude_scale(int32_t lat)
    {
        static constexpr float DEG_TO_RAD = ((float)M_PI / 180.0f);
        float scale = cos(lat * (1.0e-7 * DEG_TO_RAD));
        return fmaxf(scale, 0.01f);
    }

    int32_t Location::wrap_longitude(int64_t lon)
    {
        if (lon > 1800000000L) {
            lon = int32_t(lon-3600000000LL);
        } else if (lon < -1800000000L) {
            lon = int32_t(lon+3600000000LL);
        }
        return int32_t(lon);
    }

    int32_t Location::limit_lattitude(int32_t lat)
    {
        if (lat > 900000000L) {
            lat = 1800000000LL - lat;
        } else if (lat < -900000000L) {
            lat = -(1800000000LL + lat);
        }
        return lat;
    }

    int32_t Location::diff_longitude(int32_t lon1, int32_t lon2)
    {
        if ((lon1 & 0x80000000) == (lon2 & 0x80000000)) {
            // common case of same sign
            return lon1 - lon2;
        }
        int64_t dlon = int64_t(lon1) - int64_t(lon2);
        if (dlon > 1800000000LL) {
            dlon -= 3600000000LL;
        } else if (dlon < -1800000000LL) {
            dlon += 3600000000LL;
        }
        return int32_t(dlon);
    }

    Vector2f Location::get_distance_NE(const Location &loc2) const
    {
        const float x = (loc2.lat() - _lat) * LOCATION_SCALING_FACTOR;
        const float y = diff_longitude(loc2.lon(), _lon) * LOCATION_SCALING_FACTOR * longitude_scale((loc2.lat() + _lat)/2);
        return Vector2f { x, y };
    }

    void Location::offset(float ofs_north, float ofs_east)
    {
        const int32_t dlat = ofs_north * LOCATION_SCALING_FACTOR_INV;
        const int64_t dlng = (ofs_east * LOCATION_SCALING_FACTOR_INV) / longitude_scale(_lat + (dlat / 2));
        _lat += dlat;
        _lat = limit_lattitude(_lat);
        _lon = wrap_longitude(dlng + _lon);
    }
};
