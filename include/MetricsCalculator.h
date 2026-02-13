#ifndef METRICSCALCULATOR_H
#define METRICSCALCULATOR_H

#include <QVector>
#include <QVariant>
#include <QVariantMap>
#include <QString>
#include <cmath>
#include <limits>

class MetricsCalculator
{
public:
    // Static methods for calculating various metrics
    static double dStat(const QVector<double>& measured, const QVector<double>& simulated);
    static double rmse(const QVector<double>& observed, const QVector<double>& simulated);
    static double rSquared(const QVector<double>& x, const QVector<double>& y);
    
    // Decomposition helpers (mainly for scatter-plot evaluation)
    // Returns mean error (sim - obs)
    static double meanError(const QVector<double>& observed, const QVector<double>& simulated);
    // Returns systematic and unsystematic components of MSE (Willmott decomposition)
    static void mseDecomposition(const QVector<double>& observed,
                                 const QVector<double>& simulated,
                                 double& mseSystematic,
                                 double& mseUnsystematic);
    
    // Main metrics calculation function
    static QVariantMap calculateMetrics(const QVector<double>& simValues, 
                                       const QVector<double>& obsValues, 
                                       int treatmentNumber);

private:
    // Helper functions
    static double mean(const QVector<double>& values);
    static bool isValidData(const QVector<double>& data);
    static QVector<double> filterNaN(const QVector<double>& data);
    static QPair<QVector<double>, QVector<double>> filterPairs(const QVector<double>& x, 
                                                               const QVector<double>& y);
};

#endif // METRICSCALCULATOR_H