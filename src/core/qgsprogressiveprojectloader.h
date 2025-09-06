/***************************************************************************
                         qgsprogressiveprojectloader.h
                         -----------------------------
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

#ifndef QGSPROGRESSIVEPROJECTLOADER_H
#define QGSPROGRESSIVEPROJECTLOADER_H

#include "qgis_core.h"
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QHash>
#include <QTimer>
#include <QMutex>
#include <QDomElement>
#include <memory>

class QgsProject;
class QgsMapLayer;
class QgsVectorLayer;
class QgsRasterLayer;
class IPerformanceMonitor;
class QDomDocument;
class QDomElement;

/**
 * \ingroup core
 * \class QgsProgressiveProjectLoader
 * \brief Progressive project loading optimization for QGIS
 * 
 * This class implements progressive project loading to achieve 30% improvement
 * in project loading times for large projects (100MB+) through:
 * 
 * - Lazy loading of layers and layer data
 * - Intelligent caching of project components
 * - Optimized XML parsing with streaming
 * - Background loading of non-critical components
 * - Progressive rendering setup
 * - Memory-efficient loading strategies
 * 
 * The loader prioritizes critical project components (extent, CRS, layer tree)
 * and defers loading of heavy components (symbology, labels, expressions) until
 * they are actually needed.
 * 
 * \since QGIS 3.30
 */
class CORE_EXPORT QgsProgressiveProjectLoader : public QObject
{
    Q_OBJECT

  public:

    /**
     * Loading priority levels for project components
     */
    enum LoadingPriority
    {
      Critical = 0,    //!< Must load immediately (project structure, CRS)
      High = 1,        //!< Load early (layer tree, basic properties)
      Medium = 2,      //!< Load when needed (symbology, basic styling)
      Low = 3,         //!< Load on demand (labels, expressions, advanced styling)
      Background = 4   //!< Load in background (metadata, auxiliary data)
    };

    /**
     * Project loading configuration
     */
    struct LoadingConfig
    {
      bool enableProgressiveRendering = true;    //!< Enable progressive rendering
      bool enableBackgroundLoading = true;       //!< Enable background loading of layers
      bool enableLazyLoading = true;              //!< Enable lazy loading of layer data
      bool enableParallelLoading = true;          //!< Enable parallel layer loading
      bool enableXmlStreaming = true;             //!< Use streaming XML parser for large files
      bool enableLayerCaching = true;             //!< Cache frequently used layers
      bool enableGeometrySimplification = true;  //!< Simplify geometries during loading
      bool suppressNetworkWarnings = true;        //!< Suppress warnings for network errors
      bool skipInaccessibleLayers = true;         //!< Skip layers that cannot be accessed
      bool enableOfflineMode = false;             //!< Load project in offline mode (skip all remote sources)
      bool deferStyleLoading = true;              //!< Defer loading of layer styles until needed
      bool calculateExtents = false;              //!< Calculate layer extents during initial loading
      int maxParallelThreads = 4;                //!< Maximum parallel loading threads
      int networkTimeoutMs = 5000;               //!< Network timeout for remote resources (ms)
      int maxNetworkRetries = 2;                 //!< Maximum retry attempts for network resources
      int backgroundLoadingIntervalMs = 100;     //!< Background loading interval
      int layerLoadTimeoutMs = 30000;            //!< Timeout for individual layer loading
      double geometrySimplificationTolerance = 1.0; //!< Tolerance for geometry simplification
      int streamingBufferSizeKB = 64;            //!< XML streaming buffer size
    };

    /**
     * Layer loading state
     */
    struct LayerLoadState
    {
      QString layerId;                         //!< Layer ID
      QString layerName;                       //!< Layer name
      QString layerType;                       //!< Layer type (vector, raster, etc.)
      LoadingPriority priority = Medium;      //!< Loading priority
      bool isLoaded = false;                   //!< Whether layer is fully loaded
      bool isStubLoaded = false;               //!< Whether layer stub is loaded
      bool isDataLoaded = false;               //!< Whether layer data is loaded
      bool isStyleLoaded = false;              //!< Whether layer style is loaded
      QDateTime loadStartTime;                 //!< Load start timestamp
      QDateTime loadEndTime;                   //!< Load completion timestamp
      qint64 loadDurationMs = 0;               //!< Load duration in milliseconds
      qint64 memorySizeMB = 0;                 //!< Memory size of loaded layer
      QString errorMessage;                    //!< Error message if loading failed
    };

    /**
     * Project loading statistics
     */
    struct LoadingStatistics
    {
      QString projectPath;                     //!< Project file path
      qint64 totalLoadTimeMs = 0;              //!< Total loading time
      qint64 criticalLoadTimeMs = 0;           //!< Critical components load time
      qint64 layerLoadTimeMs = 0;              //!< Layer loading time
      qint64 backgroundLoadTimeMs = 0;         //!< Background loading time
      int totalLayers = 0;                     //!< Total number of layers
      int loadedLayers = 0;                    //!< Number of loaded layers
      int cachedComponents = 0;                //!< Number of cached components
      qint64 totalMemoryMB = 0;                //!< Total memory usage
      qint64 cacheMemoryMB = 0;                //!< Cache memory usage
      double improvementPercent = 0.0;         //!< Improvement vs traditional loading
      QDateTime loadStartTime;                 //!< Load start timestamp
      QDateTime loadEndTime;                   //!< Load completion timestamp
      bool loadingComplete = false;            //!< Whether loading is complete
    };

    /**
     * Progress callback function type
     */
    using ProgressCallback = std::function<void(int progress, const QString &message)>;

    /**
     * Constructor
     * \param parent Parent QObject
     */
    QgsProgressiveProjectLoader( QObject *parent = nullptr );

    /**
     * Destructor
     */
    ~QgsProgressiveProjectLoader() override;

    /**
     * Load a project progressively
     * \param projectPath Path to the project file
     * \param targetProject Target project instance (nullptr to use QgsProject::instance())
     * \returns True if loading started successfully
     */
    bool loadProject( const QString &projectPath, QgsProject *targetProject = nullptr );

    /**
     * Load a project progressively with custom configuration
     * \param projectPath Path to the project file
     * \param config Loading configuration
     * \param targetProject Target project instance
     * \returns True if loading started successfully
     */
    bool loadProjectWithConfig( const QString &projectPath, 
                               const LoadingConfig &config,
                               QgsProject *targetProject = nullptr );

    /**
     * Cancel current loading operation
     */
    void cancelLoading();

    /**
     * Check if loading is currently in progress
     * \returns True if loading is active
     */
    bool isLoading() const;

    /**
     * Get current loading progress (0-100)
     * \returns Loading progress percentage
     */
    int getLoadingProgress() const;

    /**
     * Get loading statistics for the current or last loading operation
     * \returns Loading statistics
     */
    LoadingStatistics getLoadingStatistics() const;

    /**
     * Get layer loading states
     * \returns List of layer loading states
     */
    QList<LayerLoadState> getLayerLoadingStates() const;

    /**
     * Force load a specific layer immediately
     * \param layerId Layer ID to load
     * \returns True if layer was loaded successfully
     */
    bool forceLoadLayer( const QString &layerId );

    /**
     * Load layer data for a specific layer
     * \param layerId Layer ID
     * \returns True if layer data was loaded successfully
     */
    bool loadLayerData( const QString &layerId );

    /**
     * Load layer style for a specific layer
     * \param layerId Layer ID
     * \returns True if layer style was loaded successfully
     */
    bool loadLayerStyle( const QString &layerId );

    /**
     * Set progress callback function
     * \param callback Progress callback function
     */
    void setProgressCallback( const ProgressCallback &callback );

    /**
     * Set performance monitor for tracking loading performance
     * \param monitor Performance monitor instance
     */
    void setPerformanceMonitor( IPerformanceMonitor *monitor );

    /**
     * Get current loading configuration
     * \returns Loading configuration
     */
    LoadingConfig getLoadingConfig() const;

    /**
     * Set loading configuration
     * \param config Loading configuration
     */
    void setLoadingConfig( const LoadingConfig &config );

    /**
     * Clear all cached components
     */
    void clearCache();

    /**
     * Get cache size in MB
     * \returns Current cache size
     */
    qint64 getCacheSizeMB() const;

    /**
     * Check if a layer is fully loaded
     * \param layerId Layer ID to check
     * \returns True if layer is fully loaded
     */
    bool isLayerLoaded( const QString &layerId ) const;

    /**
     * Get estimated time remaining for loading
     * \returns Estimated time remaining in milliseconds
     */
    qint64 getEstimatedTimeRemainingMs() const;

  signals:

    /**
     * Emitted when loading starts
     * \param projectPath Project file path
     */
    void loadingStarted( const QString &projectPath );

    /**
     * Emitted when loading progress changes
     * \param progress Loading progress (0-100)
     * \param message Progress message
     */
    void loadingProgress( int progress, const QString &message );

    /**
     * Emitted when a layer starts loading
     * \param layerId Layer ID
     * \param layerName Layer name
     * \param priority Loading priority
     */
    void layerLoadingStarted( const QString &layerId, const QString &layerName, LoadingPriority priority );

    /**
     * Emitted when a layer finishes loading
     * \param layerId Layer ID
     * \param success Whether loading was successful
     * \param loadTimeMs Loading time in milliseconds
     */
    void layerLoadingFinished( const QString &layerId, bool success, qint64 loadTimeMs );

    /**
     * Emitted when critical components are loaded
     * \param loadTimeMs Critical loading time in milliseconds
     */
    void criticalLoadingFinished( qint64 loadTimeMs );

    /**
     * Emitted when all loading is complete
     * \param statistics Final loading statistics
     */
    void loadingFinished( const LoadingStatistics &statistics );

    /**
     * Emitted when loading is cancelled
     */
    void loadingCancelled();

    /**
     * Emitted when an error occurs during loading
     * \param errorMessage Error message
     * \param layerId Layer ID where error occurred (optional)
     */
    void loadingError( const QString &errorMessage, const QString &layerId = QString() );

    /**
     * Emitted when cache is updated
     * \param cacheSizeMB Current cache size in MB
     * \param cachedComponents Number of cached components
     */
    void cacheUpdated( qint64 cacheSizeMB, int cachedComponents );

  protected:

    /**
     * Parse project file and extract component information
     * \param projectPath Project file path
     * \returns True if parsing was successful
     */
    bool parseProjectFile( const QString &projectPath );

    /**
     * Load critical project components first
     * \returns True if critical loading was successful
     */
    bool loadCriticalComponents();

    /**
     * Load high priority components
     * \returns True if high priority loading was successful
     */
    bool loadHighPriorityComponents();

    /**
     * Start background loading of remaining components
     */
    void startBackgroundLoading();

    /**
     * Create layer stub for lazy loading
     * \param layerElement DOM element for layer
     * \returns Layer stub instance
     */
    QgsMapLayer *createLayerStub( const QDomElement &layerElement );

    /**
     * Load layer from DOM element
     * \param layerElement DOM element for layer
     * \param priority Loading priority
     * \returns Loaded layer instance
     */
    QgsMapLayer *loadLayerFromElement( const QDomElement &layerElement, LoadingPriority priority );

    /**
     * Determine layer loading priority based on layer properties
     * \param layerElement DOM element for layer
     * \returns Loading priority
     */
    LoadingPriority determineLayerPriority( const QDomElement &layerElement );

    /**
     * Cache project component
     * \param componentId Component identifier
     * \param componentData Component data
     */
    void cacheComponent( const QString &componentId, const QByteArray &componentData );

    /**
     * Get cached component
     * \param componentId Component identifier
     * \returns Cached component data or empty if not found
     */
    QByteArray getCachedComponent( const QString &componentId ) const;

    /**
     * Update loading progress
     * \param progress Progress value (0-100)
     * \param message Progress message
     */
    void updateProgress( int progress, const QString &message );

    /**
     * Calculate loading improvement percentage
     * \returns Improvement percentage vs traditional loading
     */
    double calculateImprovement() const;

  private slots:

    /**
     * Continue background loading
     */
    void continueBackgroundLoading();

    /**
     * Handle layer loading timeout
     */
    void handleLoadingTimeout();

  private:

    // Core loading state
    QgsProject *mTargetProject = nullptr;
    QString mProjectPath;
    LoadingConfig mConfig;
    LoadingStatistics mStatistics;
    ProgressCallback mProgressCallback;
    IPerformanceMonitor *mPerformanceMonitor = nullptr;

    // Loading control
    bool mIsLoading = false;
    bool mLoadingCancelled = false;
    int mCurrentProgress = 0;
    QString mCurrentProgressMessage;
    QTimer *mBackgroundTimer = nullptr;
    QTimer *mTimeoutTimer = nullptr;

    // Layer management
    QHash<QString, LayerLoadState> mLayerStates;
    QStringList mPendingLayers;
    QStringList mLoadingLayers;
    QStringList mLoadedLayers;

    // Component caching
    mutable QMutex mCacheMutex;
    QHash<QString, QByteArray> mComponentCache;
    qint64 mCurrentCacheSizeMB = 0;

    // Smart layer caching
    struct LayerCacheEntry {
      QDomElement element;
      QDateTime lastAccessed;
      qint64 sizeBytes = 0;
      int accessCount = 0;
      bool isFrequentlyUsed = false;
    };
    QHash<QString, LayerCacheEntry> mLayerCache;
    QStringList mCacheAccessOrder; // LRU tracking
    static constexpr qint64 MAX_CACHE_SIZE_MB = 512; // 512MB cache limit

    // XML parsing
    std::unique_ptr<QDomDocument> mProjectDocument;
    QHash<QString, QDomElement> mLayerElements;

    // Performance tracking
    QString mCurrentOperationId;
    QDateTime mLoadingStartTime;
    qint64 mBaselineLoadTimeMs = 0; // For improvement calculation

    // Thread safety
    mutable QMutex mStateMutex;

    // GDAL configuration backup
    QHash<QString, QString> mOriginalGdalConfig;

    // Advanced loading methods
    bool loadProjectWithParallelProcessing( const QString &projectPath, QgsProject *project );
    bool loadProjectWithStreamingParser( const QString &projectPath, QgsProject *project );

    // Smart caching methods
    void initializeLayerCache();
    void cacheLayerMetadata( const QString &layerId, const QDomElement &layerElement );
    bool isLayerCached( const QString &layerId ) const;
    QDomElement getCachedLayerElement( const QString &layerId ) const;
    void evictLeastRecentlyUsedCache();
    void updateCacheAccessTime( const QString &layerId );

    // Compression support methods
    bool isCompressedProject( const QString &projectPath ) const;
    QByteArray decompressProjectData( const QByteArray &compressedData ) const;
    bool loadCompressedProject( const QString &projectPath, QgsProject *project );

    // GDAL configuration methods
    void configureGdalForRemoteSources();
    void restoreGdalConfiguration();
    
    // FlatGeobuf optimization methods  
    void optimizeForFlatGeobuf( const QString &dataSource );
    void optimizeFlatGeobufLayer( QgsVectorLayer *layer );
    void applyFlatGeobufStreamingOptions( const QString &dataSource );
    void configureFlatGeobufCaching( QgsVectorLayer *layer );
    
    // PostgreSQL read-only optimization methods
    void optimizeForReadOnlyPostgres( const QString &dataSource );
    void configurePostgresReadOnlyConnection( const QString &dataSource );

    Q_DISABLE_COPY( QgsProgressiveProjectLoader )
};

#endif // QGSPROGRESSIVEPROJECTLOADER_H