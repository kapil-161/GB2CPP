#include "MetricsCalculator.h"
#include <QDebug>
#include <algorithm>
#include <numeric>

double MetricsCalculator::dStat(const QVector<double>& measured, const QVector<double>& simulated)
{
    try {
        qDebug() << "[DEBUG] MetricsCalculator::dStat - ENTRY. measured size:" << measured.size() << ", simulated size:" << simulated.size();

        if (measured.isEmpty() || simulated.isEmpty()) {
            qWarning() << "Empty input arrays for d-stat calculation";
            return 0.0;
        }
        
        if (measured.size() != simulated.size()) {
            qWarning() << "Mismatched array sizes for d-stat calculation";
            return 0.0;
        }
        
        // Filter out NaN values
        auto filteredPair = filterPairs(measured, simulated);
        const auto& M = filteredPair.first;
        const auto& S = filteredPair.second;

        qDebug() << "[DEBUG] MetricsCalculator::dStat - After filterPairs: valid pairs n =" << M.size()
                 << "(dropped" << (measured.size() - M.size()) << ")";

        if (M.isEmpty()) {
            qWarning() << "No valid data pairs for d-stat calculation";
            return 0.0;
        }
        
        double M_mean = mean(M);
        qDebug() << "[DEBUG] MetricsCalculator::dStat - M_mean (mean of measured) =" << M_mean;
        
        // Calculate numerator: sum((M - S)^2)
        double numerator = 0.0;
        for (int i = 0; i < M.size(); ++i) {
            double diff = M[i] - S[i];
            numerator += diff * diff;
        }
        
        // Calculate denominator: sum((|M - M_mean| + |S - M_mean|)^2)
        double denominator = 0.0;
        for (int i = 0; i < M.size(); ++i) {
            double term = std::abs(M[i] - M_mean) + std::abs(S[i] - M_mean);
            denominator += term * term;
        }

        qDebug() << "[DEBUG] MetricsCalculator::dStat - numerator sum((M-S)^2) =" << numerator
                 << ", denominator sum((|M-M_mean|+|S-M_mean|)^2) =" << denominator;
        
        if (denominator == 0.0) {
            qDebug() << "[DEBUG] MetricsCalculator::dStat - denominator 0, returning 0";
            return 0.0;
        }
        
        double d = 1.0 - (numerator / denominator);
        qDebug() << "[DEBUG] MetricsCalculator::dStat - d-stat = 1 - (num/den) =" << d;
        if (!M.isEmpty()) {
            QString pairs;
            int n = std::min(3, static_cast<int>(M.size()));
            for (int i = 0; i < n; ++i)
                pairs += QString(" (%1,%2)").arg(M[i], 0, 'f', 4).arg(S[i], 0, 'f', 4);
            qDebug() << "[DEBUG] MetricsCalculator::dStat - First 3 pairs (M=measured, S=simulated):" << pairs;
        }
        return d;
        
    } catch (const std::exception& e) {
        qWarning() << "Error calculating d-stat:" << e.what();
        return 0.0;
    }
}

double MetricsCalculator::rmse(const QVector<double>& observed, const QVector<double>& simulated)
{
    try {
        if (observed.isEmpty() || simulated.isEmpty()) {
            qWarning() << "Empty input arrays for RMSE calculation";
            return 0.0;
        }
        
        if (observed.size() != simulated.size()) {
            qWarning() << "Mismatched array sizes for RMSE calculation";
            return 0.0;
        }
        
        // Filter out NaN values
        auto filteredPair = filterPairs(observed, simulated);
        const auto& obs = filteredPair.first;
        const auto& sim = filteredPair.second;
        
        if (obs.isEmpty()) {
            qWarning() << "No valid data pairs for RMSE calculation";
            return 0.0;
        }
        
        // Calculate sum of squared differences
        double sumSquaredDiff = 0.0;
        for (int i = 0; i < obs.size(); ++i) {
            double diff = obs[i] - sim[i];
            sumSquaredDiff += diff * diff;
        }
        
        return std::sqrt(sumSquaredDiff / obs.size());
        
    } catch (const std::exception& e) {
        qWarning() << "Error calculating RMSE:" << e.what();
        return 0.0;
    }
}

double MetricsCalculator::rSquared(const QVector<double>& x, const QVector<double>& y)
{
    try {
        if (x.isEmpty() || y.isEmpty()) {
            qWarning() << "Empty input arrays for R-squared calculation";
            return 0.0;
        }
        
        if (x.size() != y.size()) {
            qWarning() << "Mismatched array sizes for R-squared calculation";
            return 0.0;
        }
        
        if (x.size() < 2) {
            qWarning() << "Insufficient data points for R-squared calculation";
            return 0.0;
        }
        
        // Filter out NaN values
        auto filteredPair = filterPairs(x, y);
        const auto& x_arr = filteredPair.first;
        const auto& y_arr = filteredPair.second;
        
        if (x_arr.size() < 2) {
            qWarning() << "Insufficient valid data pairs for R-squared calculation";
            return 0.0;
        }
        
        // Calculate means
        double x_mean = mean(x_arr);
        double y_mean = mean(y_arr);
        
        // Calculate correlation coefficient
        double numerator = 0.0;
        double sum_x_sq = 0.0;
        double sum_y_sq = 0.0;
        
        for (int i = 0; i < x_arr.size(); ++i) {
            double x_diff = x_arr[i] - x_mean;
            double y_diff = y_arr[i] - y_mean;
            
            numerator += x_diff * y_diff;
            sum_x_sq += x_diff * x_diff;
            sum_y_sq += y_diff * y_diff;
        }
        
        double denominator = std::sqrt(sum_x_sq * sum_y_sq);
        if (denominator == 0.0) {
            return 0.0;
        }
        
        double correlation = numerator / denominator;
        return correlation * correlation;  // R-squared
        
    } catch (const std::exception& e) {
        qWarning() << "Error calculating R-squared:" << e.what();
        return 0.0;
    }
}

QVariantMap MetricsCalculator::calculateMetrics(const QVector<double>& simValues, 
                                              const QVector<double>& obsValues, 
                                              int treatmentNumber)
{
    QVariantMap result;
    
    try {
        qDebug() << "[DEBUG] MetricsCalculator::calculateMetrics - ENTRY. TRT:" << treatmentNumber
                 << ", simValues size:" << simValues.size() << ", obsValues size:" << obsValues.size();

        if (simValues.isEmpty() || obsValues.isEmpty()) {
            qWarning() << "Empty input arrays for metrics calculation";
            return result;
        }
        
        // Get minimum length and filter valid pairs
        int minLength = std::min(simValues.size(), obsValues.size());
        QVector<double> sim_subset = simValues.mid(0, minLength);
        QVector<double> obs_subset = obsValues.mid(0, minLength);
        qDebug() << "[DEBUG] MetricsCalculator::calculateMetrics - minLength:" << minLength
                 << ", using sim[0.." << (minLength - 1) << "], obs[0.." << (minLength - 1) << "]";
        
        auto filteredPair = filterPairs(obs_subset, sim_subset);
        const auto& obs = filteredPair.first;   // observed
        const auto& sim = filteredPair.second;  // simulated

        qDebug() << "[DEBUG] MetricsCalculator::calculateMetrics - After filterPairs: obs size:" << obs.size()
                 << ", sim size:" << sim.size();
        
        if (obs.isEmpty()) {
            qWarning() << "No valid pairs after filtering for metrics calculation";
            return result;
        }
        
        // Calculate metrics
        double mean_obs = mean(obs);
        int n = obs.size();
        double rmse_value = rmse(obs, sim);
        double nrmse = (mean_obs != 0.0) ? (rmse_value / mean_obs) * 100.0 : 0.0;
        qDebug() << "[DEBUG] MetricsCalculator::calculateMetrics - n:" << n << ", mean_obs:" << mean_obs
                 << ", RMSE:" << rmse_value << ", NRMSE%:" << nrmse << "; calling dStat(obs, sim)...";
        double d_stat = dStat(obs, sim);
        // R² not calculated for time series data - leave as "-"
        // double r_squared = rSquared(obs, sim);
        
        // Build result map
        result["TRT"] = treatmentNumber;
        result["n"] = n;
        result["RMSE"] = rmse_value;
        result["NRMSE"] = nrmse;
        result["Willmott's d-stat"] = d_stat;
        result["R²"] = "-";  // Not calculated for time series data

        qDebug() << "[DEBUG] MetricsCalculator::calculateMetrics - RESULT TRT:" << treatmentNumber
                 << "n:" << n << "RMSE:" << rmse_value << "d-stat:" << d_stat;
        
    } catch (const std::exception& e) {
        qWarning() << "Error calculating metrics:" << e.what();
    }
    
    return result;
}

// Helper functions
double MetricsCalculator::mean(const QVector<double>& values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    
    double sum = 0.0;
    for (double value : values) {
        sum += value;
    }
    
    return sum / values.size();
}

bool MetricsCalculator::isValidData(const QVector<double>& data)
{
    for (double value : data) {
        if (std::isnan(value) || std::isinf(value)) {
            return false;
        }
    }
    return true;
}

QVector<double> MetricsCalculator::filterNaN(const QVector<double>& data)
{
    QVector<double> filtered;
    for (double value : data) {
        if (!std::isnan(value) && !std::isinf(value)) {
            filtered.append(value);
        }
    }
    return filtered;
}

QPair<QVector<double>, QVector<double>> MetricsCalculator::filterPairs(const QVector<double>& x, 
                                                                      const QVector<double>& y)
{
    QVector<double> x_filtered;
    QVector<double> y_filtered;
    
    int minSize = std::min(x.size(), y.size());
    
    for (int i = 0; i < minSize; ++i) {
        if (!std::isnan(x[i]) && !std::isinf(x[i]) && !std::isnan(y[i]) && !std::isinf(y[i])) {
            x_filtered.append(x[i]);
            y_filtered.append(y[i]);
        }
    }
    
    return qMakePair(x_filtered, y_filtered);
}