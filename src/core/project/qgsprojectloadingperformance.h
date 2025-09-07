/***************************************************************************
                         qgsprojectloadingperformance.h
                         ------------------------------
    begin                : September 2025
    copyright            : (C) 2025 by QGIS
    email                : development-team@qgis.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSPROJECTLOADINGPERFORMANCE_H
#define QGSPROJECTLOADINGPERFORMANCE_H

#include "qgis_core.h"
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <memory>

/**
 * \ingroup core
 * \class QgsProjectLoadingPerformance
 * \brief Performance monitoring and metrics collection for QGIS project loading
 * 
 * This class provides detailed performance monitoring capabilities for project
 * loading operations, including:
 * 
 * - Loading time breakdown by component (layers, layouts, styles, etc.)
 * - Memory usage tracking during loading
 * - Comparison with baseline performance metrics
 * - Automated performance regression detection
 * - Export capabilities for performance analysis
 * 
 * \since QGIS 3.41
 */
class CORE_EXPORT QgsProjectLoadingPerformance : public QObject
{
    Q_OBJECT

  public:

    /**
     * Performance metrics for a project loading operation
     */
    struct LoadingMetrics
    {
      QString projectPath;                    //!< Path to the project file
      qint64 fileSizeBytes = 0;              //!< Project file size in bytes
      QDateTime startTime;                   //!< Loading start timestamp
      QDateTime endTime;                     //!< Loading end timestamp
      qint64 totalLoadTimeMs = 0;            //!< Total loading time in milliseconds
      
      // Component timing breakdown
      qint64 xmlParsingTimeMs = 0;           //!< Time spent parsing XML
      qint64 layerLoadingTimeMs = 0;         //!< Time spent loading layers
      qint64 layoutLoadingTimeMs = 0;        //!< Time spent loading layouts
      qint64 styleLoadingTimeMs = 0;         //!< Time spent loading styles
      qint64 dependencyResolutionTimeMs = 0; //!< Time spent resolving dependencies
      
      // Layer-specific metrics
      int totalLayers = 0;                   //!< Total number of layers
      int layersLoadedInParallel = 0;        //!< Number of layers loaded in parallel
      int layersLoadedSequentially = 0;     //!< Number of layers loaded sequentially
      QHash<QString, qint64> layerLoadTimes; //!< Per-layer loading times (layerId -> time in ms)
      
      // Memory usage
      qint64 peakMemoryUsageMB = 0;          //!< Peak memory usage during loading
      qint64 initialMemoryUsageMB = 0;       //!< Memory usage before loading
      qint64 finalMemoryUsageMB = 0;         //!< Memory usage after loading
      
      // Performance comparison
      qint64 baselineLoadTimeMs = 0;         //!< Baseline loading time for comparison
      double improvementPercent = 0.0;       //!< Performance improvement percentage
      
      // Loading strategy used
      QString loadingStrategy;               //!< Strategy used (Traditional, Progressive, Streaming)
      bool progressiveLoaderUsed = false;    //!< Whether progressive loader was used
      int parallelThreadsUsed = 0;          //!< Number of parallel threads used
      
      // Error tracking
      QStringList errors;                    //!< Any errors encountered during loading
      QStringList warnings;                 //!< Any warnings generated during loading
    };

    /**
     * Constructor for QgsProjectLoadingPerformance
     */
    QgsProjectLoadingPerformance( QObject *parent = nullptr );

    /**
     * Starts performance monitoring for a project loading operation
     * \param projectPath path to the project file being loaded
     * \param expectedStrategy the loading strategy that will be used
     */
    void startMonitoring( const QString &projectPath, const QString &expectedStrategy = QString() );

    /**
     * Stops performance monitoring and finalizes metrics
     */
    void stopMonitoring();

    /**
     * Records the start time for a specific component
     * \param component the component name (e.g., "xml_parsing", "layer_loading")
     */
    void startComponent( const QString &component );

    /**
     * Records the end time for a specific component
     * \param component the component name
     */
    void endComponent( const QString &component );

    /**
     * Records layer loading time
     * \param layerId the layer identifier
     * \param loadTimeMs the loading time in milliseconds
     */
    void recordLayerLoadTime( const QString &layerId, qint64 loadTimeMs );

    /**
     * Updates memory usage statistics
     */
    void updateMemoryUsage();

    /**
     * Sets the baseline loading time for comparison
     * \param baselineTimeMs baseline time in milliseconds
     */
    void setBaselineTime( qint64 baselineTimeMs );

    /**
     * Adds an error message to the metrics
     * \param error the error message
     */
    void addError( const QString &error );

    /**
     * Adds a warning message to the metrics
     * \param warning the warning message
     */
    void addWarning( const QString &warning );

    /**
     * Returns the current loading metrics
     */
    LoadingMetrics getMetrics() const { return mMetrics; }

    /**
     * Sets whether progressive loader was used
     * \param used true if progressive loader was used
     */
    void setProgressiveLoaderUsed( bool used );

    /**
     * Sets the number of parallel threads used
     * \param threads number of parallel threads
     */
    void setParallelThreadsUsed( int threads );

    /**
     * Exports metrics to JSON format
     */
    QJsonObject exportToJson() const;

    /**
     * Saves metrics to a file for later analysis
     * \param filePath path where to save the metrics
     */
    bool saveToFile( const QString &filePath ) const;

    /**
     * Loads baseline metrics from a file
     * \param filePath path to the baseline metrics file
     */
    bool loadBaseline( const QString &filePath );

    /**
     * Returns TRUE if monitoring is currently active
     */
    bool isMonitoring() const { return mIsMonitoring; }

    /**
     * Returns TRUE if the current performance represents a regression
     * compared to baseline (performance decreased by more than 10%)
     */
    bool isPerformanceRegression() const;

    /**
     * Returns a human-readable performance summary
     */
    QString getPerformanceSummary() const;

  signals:

    /**
     * Emitted when monitoring starts
     */
    void monitoringStarted( const QString &projectPath );

    /**
     * Emitted when monitoring completes
     */
    void monitoringCompleted( const QgsProjectLoadingPerformance::LoadingMetrics &metrics );

    /**
     * Emitted when a performance regression is detected
     */
    void performanceRegression( double regressionPercent );

    /**
     * Emitted when a significant performance improvement is detected
     */
    void performanceImprovement( double improvementPercent );

  private:

    bool mIsMonitoring = false;
    LoadingMetrics mMetrics;
    QHash<QString, QDateTime> mComponentStartTimes;
    QDateTime mOverallStartTime;

    /**
     * Calculates current memory usage in MB
     */
    qint64 getCurrentMemoryUsageMB() const;

    /**
     * Calculates performance improvement percentage
     */
    void calculateImprovement();
};

#endif // QGSPROJECTLOADINGPERFORMANCE_H