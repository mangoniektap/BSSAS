#include "KfrDftUtils.h"

#include <kfr/dft.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {
using RealPlan = kfr::dft_plan_real<float>;
using ComplexPlan = kfr::dft_plan<double>;

constexpr double kPi = 3.14159265358979323846;

int nextPowerOfTwo(int value)
{
    if (value <= 1) {
        return 1;
    }

    int power = 1;
    while (power < value) {
        power <<= 1;
    }
    return power;
}

int computeRealFftSize(int sampleCount)
{
    return std::max(2, nextPowerOfTwo(sampleCount));
}

QVector<float> buildWindow(int sampleCount, KfrDftUtils::WindowFunction windowFunction)
{
    QVector<float> window(sampleCount, 1.0f);
    if (sampleCount <= 0 || windowFunction == KfrDftUtils::WindowFunction::Rectangular) {
        return window;
    }

    if (sampleCount == 1) {
        window[0] = 1.0f;
        return window;
    }

    for (int index = 0; index < sampleCount; ++index) {
        window[index] = static_cast<float>(
            0.5 * (1.0 - std::cos(
                2.0 * kPi * static_cast<double>(index) /
                static_cast<double>(sampleCount - 1))));
    }
    return window;
}

float windowCompensation(const QVector<float>& window)
{
    if (window.isEmpty()) {
        return 1.0f;
    }

    double sum = 0.0;
    for (float coefficient : window) {
        sum += static_cast<double>(coefficient);
    }

    const double coherentGain = sum / static_cast<double>(window.size());
    if (coherentGain <= 0.0) {
        return 1.0f;
    }

    return static_cast<float>(1.0 / coherentGain);
}

template <typename Plan>
std::shared_ptr<const Plan> acquirePlan(
    int size,
    std::unordered_map<int, std::shared_ptr<const Plan>>& cache,
    std::mutex& cacheMutex)
{
    std::lock_guard<std::mutex> lock(cacheMutex);
    const auto it = cache.find(size);
    if (it != cache.end()) {
        return it->second;
    }

    auto plan = std::make_shared<Plan>(static_cast<size_t>(size));
    cache.emplace(size, plan);
    return plan;
}

std::shared_ptr<const RealPlan> acquireRealPlan(int size)
{
    static std::mutex cacheMutex;
    static std::unordered_map<int, std::shared_ptr<const RealPlan>> cache;
    return acquirePlan(size, cache, cacheMutex);
}

std::shared_ptr<const ComplexPlan> acquireComplexPlan(int size)
{
    static std::mutex cacheMutex;
    static std::unordered_map<int, std::shared_ptr<const ComplexPlan>> cache;
    return acquirePlan(size, cache, cacheMutex);
}

kfr::u8* tempPointer(std::vector<kfr::u8>& temp)
{
    return temp.empty() ? nullptr : temp.data();
}
} // namespace

namespace KfrDftUtils {

RealDftSpectrumResult computeRealSpectrumMagnitudes(
    const QVector<float>& samples,
    WindowFunction windowFunction,
    bool removeDcOffset)
{
    RealDftSpectrumResult result;
    if (samples.isEmpty()) {
        return result;
    }

    const int sampleCount = samples.size();
    const int fftSize = computeRealFftSize(sampleCount);
    const QVector<float> window = buildWindow(sampleCount, windowFunction);
    const float compensation = windowCompensation(window);

    QVector<float> timeDomain(fftSize, 0.0f);

    double mean = 0.0;
    if (removeDcOffset) {
        int finiteSampleCount = 0;
        for (float sample : samples) {
            if (!std::isfinite(sample)) {
                continue;
            }

            mean += static_cast<double>(sample);
            ++finiteSampleCount;
        }
        mean = finiteSampleCount > 0
            ? mean / static_cast<double>(finiteSampleCount)
            : 0.0;
    }

    for (int index = 0; index < sampleCount; ++index) {
        const double sample = std::isfinite(samples[index])
            ? static_cast<double>(samples[index])
            : 0.0;
        const double centered = sample - mean;
        timeDomain[index] =
            static_cast<float>(centered * static_cast<double>(window[index]));
    }

    const auto plan = acquireRealPlan(fftSize);
    QVector<kfr::complex<float>> spectrum(static_cast<int>(plan->complex_size()));
    std::vector<kfr::u8> temp(plan->temp_size);
    plan->execute(
        spectrum.data(),
        timeDomain.constData(),
        tempPointer(temp),
        kfr::cdirect_t{});

    result.fftSize = fftSize;
    result.magnitudes.resize(spectrum.size());
    for (int index = 0; index < spectrum.size(); ++index) {
        result.magnitudes[index] =
            std::hypot(spectrum[index].real(), spectrum[index].imag()) * compensation;
    }

    return result;
}

QVector<std::complex<double>> computeComplexDft(const QVector<float>& signal)
{
    QVector<std::complex<double>> spectrum(signal.size(), std::complex<double>(0.0, 0.0));
    if (signal.isEmpty()) {
        return spectrum;
    }

    QVector<std::complex<double>> timeDomain(signal.size(), std::complex<double>(0.0, 0.0));
    for (int index = 0; index < signal.size(); ++index) {
        timeDomain[index] = std::complex<double>(static_cast<double>(signal[index]), 0.0);
    }

    const auto plan = acquireComplexPlan(signal.size());
    std::vector<kfr::u8> temp(plan->temp_size);
    plan->execute(spectrum.data(), timeDomain.constData(), tempPointer(temp), false);
    return spectrum;
}

QVector<double> computeInverseComplexDftReal(
    const QVector<std::complex<double>>& spectrum)
{
    QVector<double> signal(spectrum.size(), 0.0);
    if (spectrum.isEmpty()) {
        return signal;
    }

    QVector<std::complex<double>> timeDomain(spectrum.size(), std::complex<double>(0.0, 0.0));
    const auto plan = acquireComplexPlan(spectrum.size());
    std::vector<kfr::u8> temp(plan->temp_size);
    plan->execute(timeDomain.data(), spectrum.constData(), tempPointer(temp), true);

    const double scale = 1.0 / static_cast<double>(spectrum.size());
    for (int index = 0; index < timeDomain.size(); ++index) {
        signal[index] = timeDomain[index].real() * scale;
    }

    return signal;
}

} // namespace KfrDftUtils
