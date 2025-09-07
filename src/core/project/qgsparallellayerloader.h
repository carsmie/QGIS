/***************************************************************************
                         qgsparallellayerloader.h
                         ------------------------
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

#ifndef QGSPARALLELLAYERLOADER_H
#define QGSPARALLELLAYERLOADER_H

#include "qgis_core.h"
#include <QObject>
#include <QThreadPool>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <QDomElement>
#include <QSet>
#include <QQueue>
#include <QFuture>
#include <QFutureWatcher>

class QgsProject;
class QgsMapLayer;
class QgsLayerTreeLayer;
class QgsReadWriteContext;
class QgsLayerStyleCache;

/**
 * \ingroup core
 * \brief Parallel layer loading system for QGIS projects
 *
 * The QgsParallelLayerLoader extends parallelization beyond just data provider
 * creation to include the complete layer setup process. This provides significant
 * performance improvements for projects with many layers by:
 *
 * - Analyzing layer dependencies to determine safe parallel loading order
 * - Loading independent layers simultaneously in separate threads
 * - Batch processing of layer properties and styling
 * - Intelligent prioritization of visible/critical layers
 * - Progress reporting and cancellation support
 * - Memory-aware loading to prevent system overload
 *
 * \since QGIS 3.34
 */
class CORE_EXPORT QgsParallelLayerLoader : public QObject
{
    Q_OBJECT

  public:

    /**
     * Layer loading priority levels
     */
    enum class LoadingPriority
    {
      Critical = 0,     //!< Base maps, always visible layers
      High = 1,         //!< Currently visible layers in map canvas
      Normal = 2,       //!< Layers in layer tree but not visible
      Low = 3,          //!< Disabled or auxiliary layers
      Background = 4    //!< Memory layers, temporary layers
    };

    /**
     * Loading strategies for different scenarios
     */
    enum class LoadingStrategy
    {
      Aggressive,       //!< Maximum parallelization, may use more memory
      Balanced,         //!< Balance between speed and memory usage
      Conservative,     //!< Minimize memory usage, fewer parallel threads
      Sequential        //!< Load layers one by one (fallback mode)
    };

    /**
     * Configuration for parallel layer loading
     */
    struct LoadingConfig
    {
      LoadingStrategy strategy = LoadingStrategy::Balanced;
      int maxParallelLayers = 4;           //!< Maximum layers to load simultaneously
      int maxMemoryUsageMB = 512;          //!< Memory limit for parallel loading
      bool enableDependencyAnalysis = true; //!< Analyze layer dependencies
      bool enablePriorityLoading = true;   //!< Load high priority layers first
      bool enableStyleCaching = true;      //!< Cache and reuse styles
      bool enableProgressReporting = true; //!< Report loading progress
      QStringList priorityLayerIds;        //!< Layer IDs to load with high priority
      QStringList deferredLayerIds;        //!< Layer IDs to defer until later
    };

    /**
     * Information about a layer loading task
     */
    struct LayerLoadingTask
    {
      QString layerId;                  //!< Layer ID
      QString layerName;                //!< Layer name for display
      QDomElement layerElement;         //!< Layer XML element
      LoadingPriority priority;         //!< Loading priority
      QStringList dependencies;         //!< Layer IDs this layer depends on
      qint64 estimatedMemoryMB = 0;     //!< Estimated memory usage
      bool isLoaded = false;            //!< Whether layer has been loaded
      bool hasError = false;            //!< Whether loading failed
      QString errorMessage;             //!< Error message if loading failed
      QgsMapLayer *layer = nullptr;     //!< Loaded layer (if successful)
    };

    /**
     * Statistics about the loading process
     */
    struct LoadingStatistics
    {
      int totalLayers = 0;              //!< Total number of layers
      int layersLoadedInParallel = 0;   //!< Layers loaded simultaneously
      int layersLoadedSequentially = 0; //!< Layers loaded one by one
      int layersWithErrors = 0;         //!< Layers that failed to load
      qint64 totalLoadingTimeMs = 0;    //!< Total loading time
      qint64 parallelLoadingTimeMs = 0; //!< Time spent in parallel loading
      qint64 dependencyAnalysisTimeMs = 0; //!< Time spent analyzing dependencies
      qint64 peakMemoryUsageMB = 0;     //!< Peak memory usage during loading
      QHash<QString, qint64> layerLoadTimes; //!< Individual layer load times
    };

    /**
     * Constructor
     * \param parent Parent object
     */
    explicit QgsParallelLayerLoader( QObject *parent = nullptr );

    /**
     * Destructor
     */
    ~QgsParallelLayerLoader() override;

    /**
     * Set the loading configuration
     * \param config The configuration to use
     */
    void setLoadingConfig( const LoadingConfig &config );

    /**
     * Get the current loading configuration
     * \returns The current configuration
     */
    LoadingConfig loadingConfig() const { return mConfig; }

    /**
     * Load layers from XML elements in parallel
     * \param layerElements List of layer XML elements to load
     * \param project The project to load layers into
     * \param context Read/write context for layer loading
     * \returns true if loading was successful
     */
    bool loadLayers( const QList<QDomElement> &layerElements, 
                     QgsProject *project, 
                     const QgsReadWriteContext &context );

    /**
     * Load a specific set of layers by ID
     * \param layerIds List of layer IDs to load
     * \param project The project containing the layers
     * \returns true if loading was successful
     */
    bool loadLayersById( const QStringList &layerIds, QgsProject *project );

    /**
     * Cancel the current loading operation
     */
    void cancelLoading();

    /**
     * Check if loading was cancelled
     * \returns true if loading was cancelled
     */
    bool isCancelled() const { return mCancelled; }

    /**
     * Get current loading progress (0-100)
     * \returns Progress percentage
     */
    int progress() const { return mProgress; }

    /**
     * Get loading statistics
     * \returns Statistics about the loading process
     */
    LoadingStatistics getStatistics() const { return mStatistics; }

    /**
     * Set the layer style cache for optimization
     * \param styleCache The cache to use for layer styles
     */
    void setLayerStyleCache( QgsLayerStyleCache *styleCache );

    /**
     * Get the current layer style cache
     * \returns The current style cache or nullptr if not set
     */
    QgsLayerStyleCache *layerStyleCache() const { return mStyleCache; }

    /**
     * Retry loading failed layers
     * \param project The project to load layers into
     * \param context Read/write context for layer loading
     * \returns true if retry was successful
     */
    bool retryFailedLayers( QgsProject *project, const QgsReadWriteContext &context );

  signals:

    /**
     * Emitted when loading progress changes
     * \param progress Progress percentage (0-100)
     * \param currentLayerName Name of currently loading layer
     */
    void progressChanged( int progress, const QString &currentLayerName );

    /**
     * Emitted when a layer is successfully loaded
     * \param layerId ID of the loaded layer
     * \param layer The loaded layer
     */
    void layerLoaded( const QString &layerId, QgsMapLayer *layer );

    /**
     * Emitted when a layer fails to load
     * \param layerId ID of the failed layer
     * \param errorMessage Error description
     */
    void layerLoadFailed( const QString &layerId, const QString &errorMessage );

    /**
     * Emitted when all layers have been processed
     * \param success Whether all layers loaded successfully
     * \param statistics Loading statistics
     */
    void loadingCompleted( bool success, const QgsParallelLayerLoader::LoadingStatistics &statistics );

    /**
     * Emitted when memory usage exceeds threshold
     * \param currentUsageMB Current memory usage in MB
     * \param thresholdMB Memory threshold that was exceeded
     */
    void memoryThresholdExceeded( qint64 currentUsageMB, qint64 thresholdMB );

  public slots:

    /**
     * Slot called when a layer loading task completes
     */
    void onLayerLoadingFinished();

  private slots:

    /**
     * Update loading progress and check for completion
     */
    void updateProgress();

    /**
     * Check memory usage and throttle loading if necessary
     */
    void checkMemoryUsage();

  private:

    //! Analyze layer dependencies to determine loading order
    void analyzeDependencies();

    //! Determine the priority of a layer based on various factors
    LoadingPriority calculateLayerPriority( const LayerLoadingTask &task ) const;

    //! Check if a layer can be loaded (all dependencies satisfied)
    bool canLoadLayer( const LayerLoadingTask &task ) const;

    //! Get the next batch of layers that can be loaded in parallel
    QList<LayerLoadingTask*> getNextLoadingBatch();

    //! Load a single layer in a separate thread
    void loadLayerAsync( LayerLoadingTask *task, QgsProject *project, const QgsReadWriteContext &context );

    //! Create a layer from XML element (called in worker thread)
    QgsMapLayer *createLayerFromElement( const QDomElement &element, 
                                         const QgsReadWriteContext &context,
                                         QString *errorMessage = nullptr ) const;

    //! Process layer style with caching optimization
    void processLayerStyleWithCache( QgsMapLayer *layer, const QDomElement &styleElement ) const;

    //! Get current memory usage in MB
    qint64 getCurrentMemoryUsageMB() const;

    //! Check if we should throttle loading due to memory constraints
    bool shouldThrottleLoading() const;

    //! Initialize loading statistics
    void initializeStatistics();

    //! Update loading statistics
    void updateStatistics();

    LoadingConfig mConfig;
    QList<LayerLoadingTask> mLoadingTasks;
    QSet<QString> mLoadedLayerIds;
    QSet<QString> mFailedLayerIds;
    
    // Threading
    QThreadPool *mThreadPool = nullptr;
    QList<QFutureWatcher<void>*> mActiveWatchers;
    QMutex mTaskMutex;
    QWaitCondition mTaskCondition;
    
    // Progress tracking
    bool mCancelled = false;
    int mProgress = 0;
    QString mCurrentLayerName;
    QTimer *mProgressTimer = nullptr;
    QTimer *mMemoryTimer = nullptr;
    
    // Statistics
    LoadingStatistics mStatistics;
    QDateTime mLoadingStartTime;
    qint64 mInitialMemoryUsage = 0;
    
    // Memory management
    qint64 mCurrentMemoryUsage = 0;
    bool mMemoryThrottled = false;
    
    // Style caching
    QgsLayerStyleCache *mStyleCache = nullptr;
};

#endif // QGSPARALLELLAYERLOADER_H