#pragma once

#include "isce/product/Product.h"
#include "isce/io/Raster.h"

namespace isce {
    namespace geometry {
        void facetRTC(isce::product::Product& product,
                      isce::io::Raster& dem,
                      isce::io::Raster& out_raster);
    }
}