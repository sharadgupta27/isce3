//-*- coding: utf-8 -*-
//
// Author: Bryan V. Riel
// Copyright: 2017-2018

#ifndef ISCE_CUDA_GEOMETRY_GPUGEO2RDR_H
#define ISCE_CUDA_GEOMETRY_GPUGEO2RDR_H

// isce::core
#include "isce/core/Ellipsoid.h"
#include "isce/core/Orbit.h"
#include "isce/core/Poly2d.h"
// isce::product
#include  "isce/product/ImageMode.h"

namespace isce {
    namespace cuda {
        namespace geometry {
            // C++ interface for running geo2rdr for a block of data on GPU
            void runGPUGeo2rdr(const isce::core::Ellipsoid & ellipsoid,
                               const isce::core::Orbit & orbit,
                               const isce::core::Poly2d & doppler,
                               const isce::product::ImageMode & mode,
                               const std::valarray<double> & x,
                               const std::valarray<double> & y,
                               const std::valarray<double> & hgt,
                               std::valarray<float> & azoff,
                               std::valarray<float> & rgoff,
                               int topoEPSG, size_t lineStart, size_t blockWidth,
                               double t0, double r0,
                               double threshold, double numiter,
                               unsigned int & totalconv); 
        }
    }
}

#endif

// end of file