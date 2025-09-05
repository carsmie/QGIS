/***************************************************************************
                         iperformancemonitor.h
                         ---------------------
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

#ifndef IPERFORMANCEMONITOR_H
#define IPERFORMANCEMONITOR_H

#include "qgis_core.h"
#include <QObject>
#include <QString>
#include <QHash>
#include <QElapsedTimer>
#include <QDateTime>
#include <QMutex>
#include <QVariant>
#include <memory>

/**
 * \ingroup core
 * \class IPerformanceMonitor
 * \brief Interface for real-time performance monitoring in QGIS
 * 
 * This interface provides comprehensive performance monitoring capabilities
 * for QGIS optimization components including:
 * 
 * - Real-time operation timing and profiling
 * - Memory usage tracking and leak detection
 * - Custom metrics collection and aggregation
 * - Performance baseline establishment and comparison
 * - Error tracking and performance correlation
 * - Thread-safe monitoring for concurrent operations
 * 
 * The interface is designed to be lightweight and non-intrusive, allowing
 * performance monitoring to be integrated throughout QGIS without impacting
 * the performance being measured.
 * 
 * \since QGIS 3.30
 */
class CORE_EXPORT IPerformanceMonitor : public QObject
{
    Q_OBJECT

  public:

    /**
     * Performance operation context information
     */
    struct OperationContext
    {
      QString operationId;          //!< Unique operation identifier
      QString operationName;        //!< Human-readable operation name
      QString category;             //!< Operation category (e.g., "project_loading", "rendering")
      QDateTime startTime;          //!< Operation start timestamp
      QDateTime endTime;            //!< Operation end timestamp (if completed)
      qint64 durationMs = -1;       //!< Operation duration in milliseconds
      bool isActive = false;        //!< Whether operation is currently active
      QString parentOperationId;    //!< Parent operation ID for nested operations
      QHash<QString, QVariant> metadata; //!< Additional operation metadata
    };

    /**
     * Memory usage snapshot
     */
    struct MemorySnapshot
    {
      qint64 totalMemoryMB = 0;     //!< Total system memory in MB
      qint64 usedMemoryMB = 0;      //!< Used memory by current process in MB
      qint64 availableMemoryMB = 0; //!< Available system memory in MB
      qint64 peakMemoryMB = 0;      //!< Peak memory usage since monitoring started
      QDateTime timestamp;          //!< Snapshot timestamp
      QString processName;          //!< Process name for context
    };

    /**
     * Performance metric data point
     */
    struct MetricData
    {
      QString name;                 //!< Metric name
      QVariant value;               //!< Metric value
      QString unit;                 //!< Metric unit (e.g., "ms", "MB", "count")
      QDateTime timestamp;          //!< Measurement timestamp
      QString operationId;          //!< Associated operation ID
      QString category;             //!< Metric category
      QHash<QString, QVariant> tags; //!< Additional metric tags
    };

    /**
     * Performance summary statistics
     */
    struct PerformanceSummary
    {
      QString category;             //!< Performance category
      qint64 totalOperations = 0;   //!< Total number of operations
      qint64 totalDurationMs = 0;   //!< Total duration of all operations
      qint64 averageDurationMs = 0; //!< Average operation duration
      qint64 minDurationMs = 0;     //!< Minimum operation duration
      qint64 maxDurationMs = 0;     //!< Maximum operation duration
      qint64 lastDurationMs = 0;    //!< Last operation duration
      double operationsPerSecond = 0.0; //!< Operations per second rate
      QDateTime firstOperationTime; //!< First operation timestamp
      QDateTime lastOperationTime;  //!< Last operation timestamp
    };

    /**
     * Performance monitoring configuration
     */
    struct MonitoringConfig
    {
      bool enableMetricCollection = true;    //!< Enable metric collection
      bool enableMemoryTracking = true;      //!< Enable memory usage tracking
      bool enableOperationTiming = true;     //!< Enable operation timing
      bool enableErrorTracking = true;       //!< Enable error tracking
      qint64 memorySnapshotIntervalMs = 1000; //!< Memory snapshot interval
      qint64 maxMetricHistorySize = 10000;   //!< Maximum metrics to store
      QStringList enabledCategories;         //!< Enabled operation categories
      QStringList disabledCategories;        //!< Disabled operation categories
      bool threadSafe = true;                //!< Enable thread-safe monitoring
    };

    /**
     * Constructor
     */
    IPerformanceMonitor( QObject *parent = nullptr );

    /**
     * Destructor - ensures proper cleanup of monitoring resources
     */
    virtual ~IPerformanceMonitor();

    // Core monitoring operations

    /**
     * Start monitoring a performance operation
     * \param operationName Human-readable operation name
     * \param category Operation category for grouping
     * \param metadata Additional operation metadata
     * \returns Unique operation ID for tracking
     */
    virtual QString startOperation( const QString &operationName,
                                   const QString &category = QString(),
                                   const QHash<QString, QVariant> &metadata = QHash<QString, QVariant>() ) = 0;

    /**
     * End monitoring for an operation
     * \param operationId Operation ID returned by startOperation()
     * \returns True if operation was successfully ended
     */
    virtual bool endOperation( const QString &operationId = QString() ) = 0;

    /**
     * Record a custom performance metric
     * \param name Metric name
     * \param value Metric value
     * \param unit Metric unit (e.g., "ms", "MB", "count")
     * \param category Metric category for grouping
     * \param operationId Associated operation ID (optional)
     */
    virtual void recordMetric( const QString &name,
                              const QVariant &value,
                              const QString &unit = QString(),
                              const QString &category = QString(),
                              const QString &operationId = QString() ) = 0;

    /**
     * Record current memory usage
     * \param operationId Associated operation ID (optional)
     * \returns Current memory snapshot
     */
    virtual MemorySnapshot recordMemoryUsage( const QString &operationId = QString() ) = 0;

    /**
     * Record an error or warning
     * \param errorMessage Error message
     * \param severity Error severity (e.g., "warning", "error", "critical")
     * \param operationId Associated operation ID (optional)
     */
    virtual void recordError( const QString &errorMessage,
                             const QString &severity = QStringLiteral( "error" ),
                             const QString &operationId = QString() ) = 0;

    // Data retrieval and analysis

    /**
     * Get all recorded metrics
     * \param category Filter by category (optional)
     * \param since Filter metrics after this timestamp (optional)
     * \returns List of metric data points
     */
    virtual QList<MetricData> getMetrics( const QString &category = QString(),
                                         const QDateTime &since = QDateTime() ) const = 0;

    /**
     * Get operation context information
     * \param operationId Operation ID to query
     * \returns Operation context or invalid context if not found
     */
    virtual OperationContext getOperationContext( const QString &operationId ) const = 0;

    /**
     * Get all active operations
     * \returns List of currently active operation contexts
     */
    virtual QList<OperationContext> getActiveOperations() const = 0;

    /**
     * Get performance summary for a category
     * \param category Performance category to summarize
     * \param since Calculate summary from this timestamp (optional)
     * \returns Performance summary statistics
     */
    virtual PerformanceSummary getPerformanceSummary( const QString &category,
                                                     const QDateTime &since = QDateTime() ) const = 0;

    /**
     * Get memory usage history
     * \param since Get memory snapshots after this timestamp (optional)
     * \returns List of memory snapshots
     */
    virtual QList<MemorySnapshot> getMemoryHistory( const QDateTime &since = QDateTime() ) const = 0;

    // Configuration and control

    /**
     * Set monitoring configuration
     * \param config Monitoring configuration
     */
    virtual void setConfiguration( const MonitoringConfig &config ) = 0;

    /**
     * Get current monitoring configuration
     * \returns Current monitoring configuration
     */
    virtual MonitoringConfig getConfiguration() const = 0;

    /**
     * Reset all monitoring data
     * \param preserveConfig Whether to preserve current configuration
     */
    virtual void reset( bool preserveConfig = true ) = 0;

    /**
     * Enable monitoring for a specific category
     * \param category Category to enable
     */
    virtual void enableCategory( const QString &category ) = 0;

    /**
     * Disable monitoring for a specific category
     * \param category Category to disable
     */
    virtual void disableCategory( const QString &category ) = 0;

    /**
     * Check if monitoring is enabled for a category
     * \param category Category to check
     * \returns True if monitoring is enabled for the category
     */
    virtual bool isCategoryEnabled( const QString &category ) const = 0;

    // Utility methods

    /**
     * Get current memory usage (lightweight call)
     * \returns Current memory usage in MB
     */
    virtual qint64 getCurrentMemoryUsageMB() const = 0;

    /**
     * Check if monitoring is currently active
     * \returns True if monitoring is active
     */
    virtual bool isMonitoringActive() const = 0;

    /**
     * Get monitoring overhead estimation
     * \returns Estimated monitoring overhead in percentage (0.0-100.0)
     */
    virtual double getMonitoringOverhead() const = 0;

    /**
     * Generate a unique operation ID
     * \returns Unique operation identifier
     */
    virtual QString generateOperationId() const = 0;

  signals:

    /**
     * Emitted when a new operation starts
     * \param operationId Operation ID
     * \param operationName Operation name
     * \param category Operation category
     */
    void operationStarted( const QString &operationId, const QString &operationName, const QString &category );

    /**
     * Emitted when an operation completes
     * \param operationId Operation ID
     * \param durationMs Operation duration in milliseconds
     * \param category Operation category
     */
    void operationCompleted( const QString &operationId, qint64 durationMs, const QString &category );

    /**
     * Emitted when a metric is recorded
     * \param name Metric name
     * \param value Metric value
     * \param category Metric category
     */
    void metricRecorded( const QString &name, const QVariant &value, const QString &category );

    /**
     * Emitted when memory usage changes significantly
     * \param currentUsageMB Current memory usage in MB
     * \param changeFromPreviousMB Change from previous measurement
     */
    void memoryUsageChanged( qint64 currentUsageMB, qint64 changeFromPreviousMB );

    /**
     * Emitted when an error is recorded
     * \param errorMessage Error message
     * \param severity Error severity
     * \param operationId Associated operation ID
     */
    void errorRecorded( const QString &errorMessage, const QString &severity, const QString &operationId );

    /**
     * Emitted when performance monitoring configuration changes
     * \param config New configuration
     */
    void configurationChanged( const MonitoringConfig &config );

  protected:

    /**
     * Validate operation ID format
     * \param operationId Operation ID to validate
     * \returns True if operation ID is valid
     */
    bool isValidOperationId( const QString &operationId ) const;

    /**
     * Validate metric name format
     * \param metricName Metric name to validate
     * \returns True if metric name is valid
     */
    bool isValidMetricName( const QString &metricName ) const;

    /**
     * Get platform-specific memory usage
     * \returns Current process memory usage in MB
     */
    qint64 getPlatformMemoryUsageMB() const;

    /**
     * Thread-safe configuration access
     */
    mutable QMutex mConfigMutex;

    /**
     * Current monitoring configuration
     */
    MonitoringConfig mConfig;

    /**
     * Monitoring start time
     */
    QDateTime mMonitoringStartTime;

  private:

    Q_DISABLE_COPY( IPerformanceMonitor )
};

/**
 * Factory function to create a performance monitor instance
 * \param parent Parent QObject
 * \returns Smart pointer to performance monitor instance
 */
CORE_EXPORT std::unique_ptr<IPerformanceMonitor> createPerformanceMonitor( QObject *parent = nullptr );

#endif // IPERFORMANCEMONITOR_H