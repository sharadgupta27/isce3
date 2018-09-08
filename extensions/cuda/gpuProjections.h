/**
 * Source Author: Paulo Penteado, based on Projections.h by Piyush Agram / Joshua Cohen
 * Copyright 2018
 */

#ifndef __ISCE_CUDA_CORE_PROJECTIONS_H__
#define __ISCE_CUDA_CORE_PROJECTIONS_H__

#ifdef __CUDACC__
#define CUDA_HOSTDEV __host__ __device__
#define CUDA_DEV __device__
#define CUDA_HOST __host__
#define CUDA_GLOBAL __global__
#else
#define CUDA_HOSTDEV
#define CUDA_DEV
#define CUDA_HOST
#define CUDA_GLOBAL
#endif

#include <cmath>
#include <iostream>
#include <vector>
#include "Constants.h"
#include "gpuEllipsoid.h"
#include "Projections.h"
using isce::core::cartesian_t;

namespace isce { 
    namespace cuda { 
        namespace core {

    /** Abstract base class for individual projections
     *
     *Internally, every derived class is expected to provide two functions.
     * forward - To convert llh (radians) to expected projection system 
     * inverse - To convert expected projection system to llh (radians)
     */
    struct ProjectionBase {
    	
    	
        /** Ellipsoid object for projections - currently only WGS84 */
        gpuEllipsoid ellipse;
        /** Type of projection system. This can be used to check if projection systems are equal
         * Private member and should not be modified after initialization*/
        int _epsgcode;

        /** Value constructor with EPSG code as input. Ellipsoid is always initialized to standard WGS84 ellipse.*/
        CUDA_HOSTDEV ProjectionBase(int code) : ellipse(6378137.,.0066943799901), _epsgcode(code) {}

        /** \brief Function for transforming from LLH. This is similar to fwd or fwd3d in PROJ.4 
         * 
         * @param[in] llh Lon/Lat/Height - Lon and Lat are in radians
         * @param[out] xyz Coordinates in specified projection system */
        CUDA_HOST int forward_h(const cartesian_t& llh, cartesian_t& xyz) const;
        CUDA_DEV virtual int forward(const double *llh, double *xyz) const = 0 ;

        /** Function for transforming to LLH. This is similar to inv or inv3d in PROJ.4
         *
         * @param[in] xyz Coordinates in specified projection system 
         * @param[out] llh Lat/Lon/Height - Lon and Lat are in radians */
        CUDA_HOST int inverse_h(const cartesian_t& xyz, cartesian_t& llh) const;
        CUDA_DEV virtual int inverse(const double *xyz, double *llh) const = 0 ;
    };

    //Geodetic Lon/Lat projection system
    struct LonLat: public ProjectionBase {
        //Constructor
        CUDA_HOSTDEV LonLat():ProjectionBase(4326){};
        //Radians to Degrees pass through
        CUDA_DEV int forward(const double*, double*) const;
        //Degrees to Radians pass through
        CUDA_DEV int inverse(const double*, double*) const;
    };

    //Geodetic Lon/Lat projection system
    struct Geocent: public ProjectionBase {
        //Constructor
        CUDA_HOSTDEV Geocent():ProjectionBase(4978){};
        //Radians to ECEF pass through
        CUDA_DEV int forward(const double* in, double* out) const;
        //ECEF to Radians pass through
        CUDA_DEV int inverse(const double* in, double* out) const;
    };

    /** UTM coordinate extension of ProjBase
     *
     * EPSG 32601-32660 for Northern Hemisphere
     * EPSG 32701-32760 for Southern Hemisphere*/
    struct UTM : public ProjectionBase {
        // Constants related to the projection system
        double lon0;
        int zone;
        bool isnorth;
        // Parameters from Proj.4
        double cgb[6], cbg[6], utg[6], gtu[6];
        double Qn, Zb;

        // Value constructor
        CUDA_HOSTDEV UTM(int);

        /** Transform from llh (rad) to UTM (m)*/
        CUDA_DEV int forward(const double*, double*) const;

        /** Transform from UTM(m) to llh (rad)*/
        CUDA_DEV int inverse(const double*, double*) const;
    };
    
    // Polar stereographic coordinate system
    struct PolarStereo: public ProjectionBase {
    	
        // Constants related to projection system
        double lon0, lat_ts, akm1, e;
        bool isnorth;

        // Value constructor
        CUDA_HOSTDEV PolarStereo(int);
        // Transfrom from LLH to Polar Stereo
        CUDA_DEV int forward(const double*, double*) const;
        // Transform from Polar Stereo to LLH
        CUDA_DEV int inverse(const double*, double*) const;
    };

    /** Equal Area Projection extension of ProjBase
     *
     * EPSG:6933 for EASE2 grid*/
    struct CEA: public ProjectionBase {
        // Constants related to projection system
        double apa[3];
        double lat_ts, k0, e, one_es, qp;

        // Value constructor
        CUDA_HOSTDEV CEA();

        /** Transform from llh (rad) to CEA (m)*/
        CUDA_DEV int forward(const double* llh, double* xyz) const;

        /** Transform from CEA (m) to LLH (rad)*/
        CUDA_DEV int inverse(const double* llh, double* xyz) const;
    };
 
    // This is to transform a point from one coordinate system to another
    CUDA_DEV int projTransform(ProjectionBase* in, 
                               ProjectionBase *out,
                               const double *inpts,
                               double *outpts);

    //Projection Factory using EPSG code
    CUDA_HOSTDEV ProjectionBase* createProj(int epsg);
        }
    }
}

#endif
