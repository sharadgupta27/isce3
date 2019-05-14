//-*- C++ -*-
//-*- coding: utf-8 -*-

#include <complex>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <string>
#include <cstdio>
#include <fstream>
#include <complex>
#include <ctime>
#include <cstring>

#include <isce/core/Constants.h>
#include <isce/core/Cartesian.h>
#include <isce/core/DateTime.h>
#include <isce/core/Ellipsoid.h>
#include <isce/core/LinAlg.h>
#include <isce/core/Peg.h>
#include <isce/core/Pegtrans.h>

#include <isce/geometry/geometry.h>
#include <isce/geometry/RTC.h>
#include <isce/geometry/Topo.h>

using isce::core::cartesian_t;

double computeUpsamplingFactor(const isce::geometry::DEMInterpolator& dem_interp,
                               const isce::product::RadarGridParameters & radarGrid,
                               const isce::core::Ellipsoid& ellps) {
    // Create a projection object from the DEM interpolator
    isce::core::ProjectionBase * proj = isce::core::createProj(dem_interp.epsgCode());

    // Get middle XY coordinate in DEM coords, lat/lon, and ECEF XYZ
    isce::core::cartesian_t demXY{dem_interp.midX(), dem_interp.midY(), 0.0}, llh;
    proj->inverse(demXY, llh);
    isce::core::cartesian_t xyz0;
    ellps.lonLatToXyz(llh, xyz0);

    // Repeat for middle coordinate + deltaX
    demXY[0] += dem_interp.deltaX();
    proj->inverse(demXY, llh);
    isce::core::cartesian_t xyz1;
    ellps.lonLatToXyz(llh, xyz1);

    // Repeat for middle coordinate + deltaX + deltaY
    demXY[1] += dem_interp.deltaY();
    proj->inverse(demXY, llh);
    isce::core::cartesian_t xyz2;
    ellps.lonLatToXyz(llh, xyz2);

    // Estimate width of DEM pixel
    isce::core::cartesian_t delta;
    isce::core::LinAlg::linComb(1., xyz1, -1., xyz0, delta);
    const double dx = isce::core::LinAlg::norm(delta);

    // Estimate length of DEM pixel
    isce::core::LinAlg::linComb(1., xyz2, -1., xyz1, delta);
    const double dy = isce::core::LinAlg::norm(delta);

    // Compute area of DEM pixel
    const double demArea = dx * dy;

    // Compute area of radar pixel (for now, just use spacing in range direction)
    const double radarArea = radarGrid.rangePixelSpacing() * radarGrid.rangePixelSpacing();

    // Upsampling factor is the ratio
    return std::sqrt(demArea / radarArea);
}

void isce::geometry::facetRTC(isce::product::Product& product,
                              isce::io::Raster& dem,
                              isce::io::Raster& out_raster,
                              char frequency) {
    using isce::core::LinAlg;

    isce::core::Ellipsoid ellps(isce::core::EarthSemiMajorAxis,
                                isce::core::EarthEccentricitySquared);
    isce::core::Orbit orbit = product.metadata().orbit();
    isce::product::RadarGridParameters radarGrid(product, frequency, 1, 1);
    isce::geometry::Topo topo(product, frequency, true);
    topo.orbitMethod(isce::core::orbitInterpMethod::HERMITE_METHOD);
    int lookSide = product.lookSide();

    // Get a copy of the Doppler LUT; allow for out-of-bounds extrapolation
    isce::core::LUT2d<double> dop = product.metadata().procInfo().dopplerCentroid(frequency);
    dop.boundsError(false);

    const double start = radarGrid.sensingStart();
    const double   end = radarGrid.sensingStop();
    const double pixazm = (end - start) / radarGrid.length(); // azimuth difference per pixel

    const double r0 = radarGrid.startingRange();
    const double dr = radarGrid.rangePixelSpacing();

    // Initialize other ISCE objects
    isce::core::Peg peg;
    isce::core::Pegtrans ptm;
    ptm.radarToXYZ(ellps, peg);

    // Bounds for valid RDC coordinates
    double xbound = radarGrid.width()  - 1.0;
    double ybound = radarGrid.length() - 1.0;

    // Output raster
    float* out = new float[radarGrid.length() * radarGrid.width()]();

    // ------------------------------------------------------------------------
    // Main code: decompose DEM into facets, compute RDC coordinates
    // ------------------------------------------------------------------------

    isce::geometry::DEMInterpolator dem_interp(0, isce::core::dataInterpMethod::BIQUINTIC_METHOD);

    // Determine DEM bounds
    topo.computeDEMBounds(dem, dem_interp, 0, radarGrid.length());

    // Enter loop to read in SLC range/azimuth coordinates and compute area
    std::cout << std::endl;

    const float upsample_factor = computeUpsamplingFactor(dem_interp, radarGrid, ellps);

    const size_t imax = dem_interp.length() * upsample_factor;
    const size_t jmax = dem_interp.width()  * upsample_factor;

    const size_t progress_block = imax*jmax/100;
    size_t numdone = 0;

    // Loop over DEM facets
    #pragma omp parallel for schedule(dynamic) collapse(2)
    for (size_t ii = 0; ii < imax; ++ii) {
        for (size_t jj = 0; jj < jmax; ++jj) {

            #pragma omp atomic
            numdone++;

            if (numdone % progress_block == 0)
                #pragma omp critical
                printf("\rRTC progress: %d%%", (int) (numdone * 1e2 / (imax * jmax))),
                    fflush(stdout);

            isce::core::cartesian_t dem00, dem01, dem10, dem11,
                                    llh00, llh01, llh10, llh11, inputLLH,
                                    xyz00, xyz01, xyz10, xyz11, xyz_mid,
                                    P00_01, P00_10, P10_01, P11_01, P11_10,
                                    lookXYZ, normalFacet1, normalFacet2;

            // Central DEM coordinates of facets
            const double dem_ymid = dem_interp.yStart() + dem_interp.deltaY() * (ii + 0.5) / upsample_factor;
            const double dem_xmid = dem_interp.xStart() + dem_interp.deltaX() * (jj + 0.5) / upsample_factor;

            double a, r;
            isce::core::ProjectionBase* proj = isce::core::createProj(dem_interp.epsgCode());
            isce::core::cartesian_t inputDEM{dem_xmid, dem_ymid,
                dem_interp.interpolateXY(dem_xmid, dem_ymid)};
            // Compute facet-central LLH vector
            proj->inverse(inputDEM, inputLLH);
            int geostat = isce::geometry::geo2rdr(inputLLH, ellps, orbit, dop,
                    a, r, radarGrid.wavelength(), 1e-4, 100, 1e-4);
            const float azpix = (a - start) / pixazm;
            const float ranpix = (r - r0) / dr;

            // Establish bounds for bilinear weighting model
            const float x1 = std::floor(ranpix);
            const float x2 = x1 + 1.0;
            const float y1 = std::floor(azpix);
            const float y2 = y1 + 1.0;

            // Check to see if pixel lies in valid RDC range
            if (ranpix < 0.0 or x2 > xbound or azpix < 0.0 or y2 > ybound)
                continue;

            // Current x/y-coords in DEM
            const double dem_y0 = dem_interp.yStart() + ii * dem_interp.deltaY() / upsample_factor;
            const double dem_y1 = dem_y0 + dem_interp.deltaY() / upsample_factor;
            const double dem_x0 = dem_interp.xStart() + dem_interp.deltaX() * jj / upsample_factor;
            const double dem_x1 = dem_x0 + dem_interp.deltaX() / upsample_factor;

            // Set DEM-coordinate corner vectors
            dem00 = {dem_x0, dem_y0,
                dem_interp.interpolateXY(dem_x0, dem_y0)};
            dem01 = {dem_x0, dem_y1,
                dem_interp.interpolateXY(dem_x0, dem_y1)};
            dem10 = {dem_x1, dem_y0,
                dem_interp.interpolateXY(dem_x1, dem_y0)};
            dem11 = {dem_x1, dem_y1,
                dem_interp.interpolateXY(dem_x1, dem_y1)};

            // Get LLH corner vectors
            proj->inverse(dem00, llh00);
            proj->inverse(dem01, llh01);
            proj->inverse(dem10, llh10);
            proj->inverse(dem11, llh11);

            // Convert to XYZ
            ellps.lonLatToXyz(llh00, xyz00);
            ellps.lonLatToXyz(llh01, xyz01);
            ellps.lonLatToXyz(llh10, xyz10);
            ellps.lonLatToXyz(llh11, xyz11);

            // Compute normal vectors for each facet
            normalFacet1 = normalPlane(xyz00, xyz10, xyz01);
            normalFacet2 = normalPlane(xyz01, xyz10, xyz11);

            // Compute vectors associated with facet sides
            LinAlg::linComb(1.0, xyz00, -1.0, xyz01, P00_01);
            LinAlg::linComb(1.0, xyz00, -1.0, xyz10, P00_10);
            LinAlg::linComb(1.0, xyz10, -1.0, xyz01, P10_01);
            LinAlg::linComb(1.0, xyz11, -1.0, xyz01, P11_01);
            LinAlg::linComb(1.0, xyz11, -1.0, xyz10, P11_10);

            // Side lengths
            const double p00_01 = LinAlg::norm(P00_01);
            const double p00_10 = LinAlg::norm(P00_10);
            const double p10_01 = LinAlg::norm(P10_01);
            const double p11_01 = LinAlg::norm(P11_01);
            const double p11_10 = LinAlg::norm(P11_10);

            // Semi-perimeters
            const float h1 = 0.5 * (p00_01 + p00_10 + p10_01);
            const float h2 = 0.5 * (p11_01 + p11_10 + p10_01);

            // Heron's formula to get area of facets in XYZ coordinates
            const float AP1 = std::sqrt(h1 * (h1 - p00_01) * (h1 - p00_10) * (h1 - p10_01));
            const float AP2 = std::sqrt(h2 * (h2 - p11_01) * (h2 - p11_10) * (h2 - p10_01));

            // Compute look angle from sensor to ground
            ellps.lonLatToXyz(inputLLH, xyz_mid);
            isce::core::cartesian_t xyz_plat, vel;
            orbit.interpolateWGS84Orbit(a, xyz_plat, vel);
            lookXYZ = {xyz_plat[0] - xyz_mid[0],
                       xyz_plat[1] - xyz_mid[1],
                       xyz_plat[2] - xyz_mid[2]};
            double norm = LinAlg::norm(lookXYZ);
            lookXYZ[0] /= norm;
            lookXYZ[1] /= norm;
            lookXYZ[2] /= norm;

            // Compute dot product between each facet and look vector
            const double cosIncFacet1 = LinAlg::dot(lookXYZ, normalFacet1);
            const double cosIncFacet2 = LinAlg::dot(lookXYZ, normalFacet2);
            // If facets are not illuminated by radar, skip
            if (cosIncFacet1 < 0. or cosIncFacet2 < 0.) {
                continue;
            }

            // Compute projected area
            const float area = AP1 * cosIncFacet1 + AP2 * cosIncFacet2;

            // Get integer indices of bounds
            const int ix1 = static_cast<int>(x1);
            const int ix2 = static_cast<int>(x2);
            const int iy1 = static_cast<int>(y1);
            const int iy2 = static_cast<int>(y2);

            // Compute fractional weights from indices
            const float Wr = ranpix - x1;
            const float Wa = azpix - y1;
            const float Wrc = 1. - Wr;
            const float Wac = 1. - Wa;

            // Use bilinear weighting to distribute area
            #pragma omp atomic
            out[radarGrid.width() * iy1 + ix1] += area * Wrc * Wac;
            #pragma omp atomic
            out[radarGrid.width() * iy1 + ix2] += area * Wr * Wac;
            #pragma omp atomic
            out[radarGrid.width() * iy2 + ix1] += area * Wrc * Wa;
            #pragma omp atomic
            out[radarGrid.width() * iy2 + ix2] += area * Wr * Wa;
        }
    }

    float max_hgt, avg_hgt;
    pyre::journal::info_t info("facet_calib");
    dem_interp.computeHeightStats(max_hgt, avg_hgt, info);
    isce::geometry::DEMInterpolator flat_interp(avg_hgt);

    // Compute the flat earth incidence angle correction applied by UAVSAR processing
    #pragma omp parallel for schedule(dynamic) collapse(2)
    for (size_t i = 0; i < radarGrid.length(); ++i) {
        for (size_t j = 0; j < radarGrid.width(); ++j) {

            isce::core::cartesian_t xyz_plat, vel;
            orbit.interpolateWGS84Orbit(start + i * pixazm, xyz_plat, vel);

            // Slant range for current pixel
            const double slt_range = r0 + j * dr;

            // Get LLH and XYZ coordinates for this azimuth/range
            isce::core::cartesian_t targetLLH, targetXYZ;
            targetLLH[2] = avg_hgt; // initialize first guess
            isce::geometry::rdr2geo(start + i * pixazm, slt_range, 0, orbit, ellps,
                    flat_interp, targetLLH, radarGrid.wavelength(), lookSide,
                    1e-4, 20, 20, isce::core::HERMITE_METHOD);

            // Computation of ENU coordinates around ground target
            isce::core::cartesian_t satToGround, enu;
            isce::core::cartmat_t enumat, xyz2enu;
            ellps.lonLatToXyz(targetLLH, targetXYZ);
            LinAlg::linComb(1.0, targetXYZ, -1.0, xyz_plat, satToGround);
            LinAlg::enuBasis(targetLLH[1], targetLLH[0], enumat);
            LinAlg::tranMat(enumat, xyz2enu);
            LinAlg::matVec(xyz2enu, satToGround, enu);

            // Compute incidence angle components
            const double costheta = std::abs(enu[2]) / LinAlg::norm(enu);
            const double sintheta = std::sqrt(1. - costheta*costheta);

            out[radarGrid.width() * i + j] *= sintheta;
        }
    }
    std::cout << std::endl;

    out_raster.setBlock(out, 0, 0, radarGrid.width(), radarGrid.length());
    delete[] out;
}
