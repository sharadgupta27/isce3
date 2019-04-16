#include <cmath> // cos, sin, sqrt, fmod
#include <complex> // std::complex, std::arg
#include <cstdint> // uint8_t
#include <gtest/gtest.h> // TEST, ASSERT_EQ, ASSERT_TRUE, testing::InitGoogleTest, RUN_ALL_TE  STS
#include <valarray> // std::valarray, std::abs

#include "isce/unwrap/phass/Phass.h" // isce::unwrap::phass::Phass
#include "isce/io/Raster.h" // isce::io::Raster

TEST(Phass, GetSetters)
{

    isce::unwrap::phass::Phass phassObj;

    phassObj.correlationThreshold(0.5);
    ASSERT_EQ(phassObj.correlationThreshold(), 0.5);

    phassObj.goodCorrelation(0.7);
    ASSERT_EQ(phassObj.goodCorrelation(), 0.7);

    phassObj.minPixelsPerRegion(100);
    ASSERT_EQ(phassObj.minPixelsPerRegion(), 100);

}

TEST(ICU, RunPhass)
{
    constexpr size_t l = 1100;
    constexpr size_t w = 256;

    // Wrapped phase with simple vertical phase gradient
    std::valarray<float> phase(l*w);
    for (size_t j = 0; j < l; ++j)
    {
        float y = float(j) / float(l) * 50.f;
        std::complex<float> z{cos(y), sin(y)};
        for (size_t i = 0; i < w; ++i) { phase[j * w + i] = std::arg(z); }
    }

    // Correlation (results in one tall connected component with "U" shape and
    // a separate rectangular component)
    std::valarray<float> corr(l*w);
    for (size_t j = 100; j < 900; ++j)
    {
        for (size_t i = 50; i < 100; ++i) { corr[j * w + i] = 1.f; }
        for (size_t i = 150; i < 200; ++i) { corr[j * w + i] = 1.f; }
    }
    for (size_t j = 900; j < 950; ++j)
    {
        for (size_t i = 50; i < 200; ++i) { corr[j * w + i] = 1.f; }
    }

    for (size_t j = 1000; j < 1050; ++j)
    {
        for (size_t i = 50; i < 200; ++i) { corr[j * w + i] = 1.f; }
    }

    // Create wrapped phase and correlation rasters
    isce::io::Raster wrappedPhaseRaster("./intf", w, l, 1, GDT_Float32, "ENVI");
    wrappedPhaseRaster.setBlock(phase, 0, 0, w, l);
    isce::io::Raster corrRaster("./corr", w, l, 1, GDT_Float32, "ENVI");
    corrRaster.setBlock(corr, 0, 0, w, l);

    // Init output unwrapped phase, connected component labels rasters
    isce::io::Raster unwRaster("./unw", w, l, 1, GDT_Float32, "ENVI");
    isce::io::Raster labelsRaster("./labels", w, l, 1, GDT_Int32, "ENVI");

    // Configure Phass.
    isce::unwrap::phass::Phass phassObj;

    //unwrap the interferogram
    phassObj.unwrap(wrappedPhaseRaster, corrRaster, unwRaster, labelsRaster);

    // Read unwrapped phase from prior test.
    isce::io::Raster outputUnwRaster("./unw");
    ASSERT_TRUE(outputUnwRaster.length() == l && outputUnwRaster.width() == w);
    std::valarray<float> unw(l*w);
    outputUnwRaster.getBlock(unw, 0, 0, w, l);

    // Read connected component labels from prior test.
    isce::io::Raster cclRaster("./labels");
    ASSERT_TRUE(cclRaster.length() == l && cclRaster.width() == w);
    std::valarray<uint8_t> ccl(l*w);
    cclRaster.getBlock(ccl, 0, 0, w, l);

    // Check sine distance between unwrapped and wrapped phase within connected
    // component(s).
    std::valarray<float> sindist(l*w);
    for (size_t i = 0; i < l*w; ++i)
    {
        if (ccl[i] != 0) { sindist[i] = std::abs(sin(unw[i] - phase[i])); }
    }
    std::cout << "err" << sindist.max() << std::endl;

    ASSERT_TRUE(sindist.max() < 1e-5);
}

TEST(Phass, CheckConnCompLabels)
{
    constexpr size_t l = 1100;
    constexpr size_t w = 256;

    // Get reference labels.
    std::valarray<int> refccl(l*w);
    for (size_t j = 100; j < 900; ++j)
    {
        for (size_t i = 50; i < 100; ++i) { refccl[j * w + i] = 1; }
        for (size_t i = 150; i < 200; ++i) { refccl[j * w + i] = 1; }
    }
    for (size_t j = 900; j < 950; ++j)
    {
        for (size_t i = 50; i < 200; ++i) { refccl[j * w + i] = 1; }
    }

    for (size_t j = 1000; j < 1050; ++j)
    {
        for (size_t i = 50; i < 200; ++i) { refccl[j * w + i] = 2; }
    }

    // Read connected component labels from prior test.
    isce::io::Raster cclRaster("./labels");
    ASSERT_TRUE(cclRaster.length() == l && cclRaster.width() == w);

    // Read the connected components
    std::valarray<int> ccl(l*w);
    cclRaster.getBlock(ccl, 0, 0, w, l);

    // Check the connected components
    ASSERT_TRUE((ccl == refccl).min());

}


int main(int argc, char * argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

