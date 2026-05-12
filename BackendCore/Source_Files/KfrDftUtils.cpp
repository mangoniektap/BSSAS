/**
 * @file KfrDftUtils.cpp
 * @brief 基于KFR库的DFT/FFT实用工具模块，提供实频谱幅值计算、复频谱正逆变换等功能，内部含加窗、幂次对齐、计划缓存优化。
 */

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

/** @brief 计算不小于给定值的下一个2的幂。 */
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

/** @brief 根据采样点数计算实数FFT的尺寸（对齐到2的幂，至少为2）。 */
int computeRealFftSize(int sampleCount)
{
    return std::max(2, nextPowerOfTwo(sampleCount));
}

/**
 * @brief 构建窗函数系数。
 * @param sampleCount 采样点数。
 * @param windowFunction 窗函数类型（矩形窗或汉宁窗）。
 * @returns 归一化的窗系数向量，矩形窗全部返回1.0。
 */
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

/**
 * @brief 计算窗函数的相干增益补偿因子。
 * @param window 输入窗系数。
 * @returns 补偿因子（相干增益倒数），空窗返回1.0。
 */
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

/** @brief 按尺寸从缓存获取或创建FFT计划（带互斥锁保护）。 */
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

/**
 * @brief 计算实数信号的FFT频谱幅值。
 * @param samples 输入采样信号。
 * @param windowFunction 窗函数类型。
 * @param removeDcOffset 是否先去除直流偏置。
 * @returns 包含FFT尺寸和归一化幅值谱的结果结构体。
 */
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

/**
 * @brief 计算复值离散傅里叶变换（DFT）。
 * @param signal 输入实数信号。
 * @returns 复数频谱向量，长度与输入相同。
 */
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

/**
 * @brief 计算复频谱的逆DFT，返回实部信号。
 * @param spectrum 复数频谱向量。
 * @returns 重建的实信号，已按频谱长度归一化。
 */
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
