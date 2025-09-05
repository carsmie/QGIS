/***************************************************************************
                         qgsspatialindexmanager.h
                         -------------------------
    begin                : September 2025
    copyright            : (C) 2025 by QGIS
    email                : development-team@qgis.org
 ***************************************************************************/

#ifndef QGSSPATIALINDEXMANAGER_H
#define QGSSPATIALINDEXMANAGER_H

#include "qgis_core.h"
#include "qgsrectangle.h"
#include "qgsgeometry.h"
#include "qgsfeature.h"
#include "qgsfeatureid.h"
#include "qgspointxy.h"

#include <QObject>
#include <QHash>
#include <QCache>
#include <QMutex>
#include <QTimer>
#include <QDateTime>
#include <QThreadPool>
#include <QRunnable>
#include <QSharedPointer>
#include <QWeakPointer>
#include <memory>

class QgsVectorLayer;
class QgsVectorDataProvider;
class QgsSpatialIndex;
class QgsFeatureIterator;
class IPerformanceMonitor;
class QgsFeedback;

/**
 * \ingroup core
 * \brief Advanced spatial indexing optimization manager for improved spatial query performance
 * 
 * This class implements high-performance spatial indexing with features including:
 * - Multi-level R-tree indexing with adaptive splitting
 * - Spatial hashing for fast point-in-polygon queries
 * - Query result caching with spatial locality awareness
 * - Background index building and maintenance
 * - Memory-optimized index structures
 * - Cross-layer spatial relationship caching
 * - Performance monitoring integration
 * 
 * The spatial index manager is designed to significantly improve spatial query performance
 * by implementing advanced indexing strategies and intelligent caching mechanisms.
 * 
 * \since QGIS 3.40
 */
class CORE_EXPORT QgsSpatialIndexManager : public QObject
{
    Q_OBJECT

  public:

    /**
     * Configuration for R-tree indexing
     */
    struct RTreeConfig
    {
      int maxEntriesPerNode = 16;              //!< Maximum entries per R-tree node
      int minEntriesPerNode = 8;               //!< Minimum entries per R-tree node
      int maxTreeDepth = 12;                   //!< Maximum tree depth
      bool enableAdaptiveSplitting = true;     //!< Enable adaptive node splitting strategies
      bool enableBulkLoading = true;           //!< Enable bulk loading for initial construction
      bool enableNodeCompression = true;       //!< Enable node compression to save memory
      double splitRatio = 0.4;                 //!< Split ratio for R-tree nodes (0.0-0.5)
      bool enableSpatialSorting = true;        //!< Enable spatial sorting for better clustering
      int bulkLoadBatchSize = 1000;            //!< Batch size for bulk loading operations
    };

    /**
     * Configuration for spatial hashing
     */
    struct SpatialHashConfig
    {
      bool enableSpatialHashing = true;        //!< Enable spatial hashing optimization
      int hashGridSize = 256;                  //!< Grid size for spatial hashing
      double hashCellSize = 0.0;               //!< Cell size (0 = auto-calculate)
      int maxFeaturesPerCell = 50;             //!< Maximum features per hash cell
      bool enableAdaptiveGrid = true;          //!< Enable adaptive grid sizing
      bool enableMultiLevelHashing = true;     //!< Enable multi-level hash grids
      int hashLevels = 3;                      //!< Number of hash levels
      bool enableDynamicRehashing = true;      //!< Enable dynamic rehashing on updates
    };

    /**
     * Configuration for query result caching
     */
    struct QueryCacheConfig
    {
      bool enableQueryCaching = true;          //!< Enable query result caching
      int maxCachedQueries = 500;              //!< Maximum number of cached queries
      qint64 queryCacheSizeMB = 100;           //!< Query cache size in MB
      int cacheExpiryMinutes = 30;             //!< Cache expiry time in minutes
      bool enableSpatialLocalityOptimization = true; //!< Optimize for spatial locality
      double spatialLocalityThreshold = 0.1;  //!< Threshold for spatial locality (0.1 = 10% overlap)
      bool enableQueryCompression = true;     //!< Compress cached query results
      bool enablePartialQueryCaching = true;  //!< Cache partial query results
    };

    /**
     * Configuration for background processing
     */
    struct BackgroundConfig
    {
      bool enableBackgroundIndexing = true;   //!< Enable background index building
      bool enableBackgroundMaintenance = true; //!< Enable background index maintenance
      int backgroundThreadCount = 2;          //!< Number of background threads
      int maintenanceIntervalMs = 15000;      //!< Maintenance interval in milliseconds
      bool enableProgressiveIndexing = true;  //!< Enable progressive index building
      int progressiveIndexBatchSize = 500;    //!< Batch size for progressive indexing
      bool enableIdleTimeProcessing = true;   //!< Process during idle time only
      int idleThresholdMs = 100;               //!< Idle time threshold in milliseconds
    };

    /**
     * Configuration for memory optimization
     */
    struct MemoryConfig
    {
      bool enableMemoryOptimization = true;   //!< Enable memory optimization
      qint64 maxIndexMemoryMB = 512;           //!< Maximum index memory usage in MB
      double memoryPressureThreshold = 0.8;   //!< Memory pressure threshold (0.8 = 80%)
      bool enableIndexCompression = true;     //!< Enable index structure compression
      bool enableLazyIndexLoading = true;     //!< Enable lazy loading of index data
      bool enableIndexPaging = true;          //!< Enable index paging for large datasets
      int indexPageSize = 1024;               //!< Index page size in entries
      bool enableMemoryMapping = true;        //!< Enable memory mapping for index files
    };

    /**
     * Complete spatial index configuration
     */
    struct IndexConfig
    {
      RTreeConfig rtree;                       //!< R-tree configuration
      SpatialHashConfig spatialHash;           //!< Spatial hashing configuration
      QueryCacheConfig queryCache;             //!< Query caching configuration
      BackgroundConfig background;             //!< Background processing configuration
      MemoryConfig memory;                     //!< Memory optimization configuration
      bool enablePerformanceMonitoring = true; //!< Enable performance monitoring
      bool enableIndexPersistence = true;     //!< Enable index persistence to disk
      QString indexCacheDir;                   //!< Directory for index cache files
    };

    /**
     * Information about a spatial index
     */
    struct IndexInfo
    {
      QString indexId;                         //!< Unique index identifier
      QString layerId;                         //!< Associated layer ID
      QgsRectangle extent;                     //!< Index spatial extent
      int featureCount = 0;                    //!< Number of indexed features
      int nodeCount = 0;                       //!< Number of index nodes
      int treeDepth = 0;                       //!< Current tree depth
      QDateTime buildTime;                     //!< When index was built
      QDateTime lastAccessed;                  //!< When index was last accessed
      QDateTime lastMaintenance;               //!< When index was last maintained
      qint64 memorySizeBytes = 0;              //!< Memory size of index
      qint64 diskSizeBytes = 0;                //!< Disk size of index (if persisted)
      double buildTimeMs = 0.0;                //!< Time taken to build index
      double averageQueryTimeMs = 0.0;        //!< Average query time
      int queryCount = 0;                      //!< Number of queries performed
      bool isLoaded = false;                   //!< Whether index is loaded in memory
      bool isOptimized = false;                //!< Whether index is optimized
      bool needsRebuild = false;               //!< Whether index needs rebuilding
    };

    /**
     * Spatial hash cell information
     */
    struct HashCellInfo
    {
      int cellX = 0;                           //!< Cell X coordinate
      int cellY = 0;                           //!< Cell Y coordinate
      int level = 0;                           //!< Hash level
      QgsRectangle bounds;                     //!< Cell bounds
      QList<QgsFeatureId> featureIds;          //!< Features in this cell
      QDateTime lastUpdated;                   //!< When cell was last updated
      int accessCount = 0;                     //!< Number of times cell was accessed
    };

    /**
     * Cached query information
     */
    struct CachedQuery
    {
      QString queryId;                         //!< Unique query identifier
      QString layerId;                         //!< Associated layer ID
      QgsRectangle queryExtent;                //!< Query spatial extent
      QString geometryFilter;                  //!< Geometry filter (if any)
      QList<QgsFeatureId> resultIds;           //!< Cached result feature IDs
      QDateTime cacheTime;                     //!< When query was cached
      QDateTime lastAccessed;                  //!< When query was last accessed
      qint64 memorySizeBytes = 0;              //!< Memory size of cached query
      double queryTimeMs = 0.0;                //!< Original query time
      int accessCount = 0;                     //!< Number of times query was accessed
      bool isCompressed = false;               //!< Whether result is compressed
    };

    /**
     * Performance statistics for spatial indexing
     */
    struct PerformanceStatistics
    {
      QDateTime timestamp;                     //!< Statistics timestamp
      
      // Index statistics
      int totalIndexes = 0;                    //!< Total number of indexes
      int loadedIndexes = 0;                   //!< Number of loaded indexes
      qint64 totalIndexMemoryMB = 0;           //!< Total index memory usage
      qint64 totalIndexDiskMB = 0;             //!< Total index disk usage
      
      // Query performance
      int totalQueries = 0;                    //!< Total queries performed
      int queryCacheHits = 0;                  //!< Query cache hits
      int queryCacheMisses = 0;                //!< Query cache misses
      double averageQueryTimeMs = 0.0;         //!< Average query time
      double averageIndexedQueryTimeMs = 0.0;  //!< Average indexed query time
      double averageNonIndexedQueryTimeMs = 0.0; //!< Average non-indexed query time
      
      // R-tree statistics
      double averageRTreeDepth = 0.0;          //!< Average R-tree depth
      int rtreeNodeAccesses = 0;               //!< R-tree node accesses
      double rtreeBuildTimeMs = 0.0;           //!< R-tree build time
      
      // Spatial hash statistics
      int hashCells = 0;                       //!< Number of hash cells
      double averageFeaturesPerCell = 0.0;     //!< Average features per hash cell
      int hashCollisions = 0;                  //!< Hash collisions
      double hashQueryTimeMs = 0.0;            //!< Hash query time
      
      // Cache statistics
      qint64 queryCacheMemoryMB = 0;           //!< Query cache memory usage
      double queryCacheHitRate = 0.0;          //!< Query cache hit rate
      int expiredCacheEntries = 0;             //!< Expired cache entries
      
      // Background processing
      int backgroundTasksActive = 0;           //!< Active background tasks
      int backgroundTasksCompleted = 0;        //!< Completed background tasks
      int indexesRebuilt = 0;                  //!< Indexes rebuilt in background
      
      // Performance improvements
      double querySpeedupFactor = 0.0;         //!< Query speedup factor vs non-indexed
      double memoryEfficiencyRatio = 0.0;      //!< Memory efficiency vs naive indexing
    };

    /**
     * Spatial query types
     */
    enum QueryType
    {
      IntersectsQuery,                         //!< Geometry intersects query
      ContainsQuery,                           //!< Geometry contains query
      WithinQuery,                             //!< Geometry within query
      TouchesQuery,                            //!< Geometry touches query
      OverlapsQuery,                           //!< Geometry overlaps query
      NearestNeighborQuery,                    //!< Nearest neighbor query
      KNearestQuery,                           //!< K-nearest neighbors query
      RadiusQuery                              //!< Radius query
    };

    /**
     * Constructor
     * \param parent parent object
     */
    explicit QgsSpatialIndexManager( QObject *parent = nullptr );

    /**
     * Destructor
     */
    ~QgsSpatialIndexManager() override;

    /**
     * Sets the index configuration
     * \param config index configuration
     */
    void setIndexConfig( const IndexConfig &config );

    /**
     * Returns the current index configuration
     */
    IndexConfig getIndexConfig() const;

    /**
     * Sets the performance monitor for metrics collection
     * \param monitor performance monitor instance
     */
    void setPerformanceMonitor( IPerformanceMonitor *monitor );

    /**
     * Creates or rebuilds a spatial index for the specified layer
     * \param layer vector layer
     * \param forceRebuild force rebuild even if index exists
     * \param feedback optional feedback object
     * \returns true if index was created successfully
     */
    bool createSpatialIndex( QgsVectorLayer *layer, bool forceRebuild = false, QgsFeedback *feedback = nullptr );

    /**
     * Creates a spatial index for a data provider
     * \param provider vector data provider
     * \param layerId layer identifier
     * \param forceRebuild force rebuild even if index exists
     * \param feedback optional feedback object
     * \returns true if index was created successfully
     */
    bool createSpatialIndex( QgsVectorDataProvider *provider, const QString &layerId, 
                            bool forceRebuild = false, QgsFeedback *feedback = nullptr );

    /**
     * Performs a spatial query using optimized indexing
     * \param layer vector layer
     * \param geometry query geometry
     * \param queryType type of spatial query
     * \param maxResults maximum number of results (0 = no limit)
     * \returns list of matching feature IDs
     */
    QList<QgsFeatureId> spatialQuery( QgsVectorLayer *layer,
                                     const QgsGeometry &geometry,
                                     QueryType queryType,
                                     int maxResults = 0 );

    /**
     * Performs a spatial query within a rectangular extent
     * \param layer vector layer
     * \param extent query extent
     * \param queryType type of spatial query
     * \param maxResults maximum number of results (0 = no limit)
     * \returns list of matching feature IDs
     */
    QList<QgsFeatureId> spatialQuery( QgsVectorLayer *layer,
                                     const QgsRectangle &extent,
                                     QueryType queryType,
                                     int maxResults = 0 );

    /**
     * Performs a nearest neighbor query
     * \param layer vector layer
     * \param point query point
     * \param k number of nearest neighbors
     * \param maxDistance maximum search distance (0 = no limit)
     * \returns list of nearest feature IDs ordered by distance
     */
    QList<QgsFeatureId> nearestNeighborQuery( QgsVectorLayer *layer,
                                             const QgsPointXY &point,
                                             int k = 1,
                                             double maxDistance = 0.0 );

    /**
     * Performs a radius query around a point
     * \param layer vector layer
     * \param center center point
     * \param radius search radius
     * \param maxResults maximum number of results (0 = no limit)
     * \returns list of feature IDs within radius
     */
    QList<QgsFeatureId> radiusQuery( QgsVectorLayer *layer,
                                    const QgsPointXY &center,
                                    double radius,
                                    int maxResults = 0 );

    /**
     * Updates the spatial index with new/modified features
     * \param layer vector layer
     * \param featureIds list of feature IDs to update
     * \returns true if update was successful
     */
    bool updateSpatialIndex( QgsVectorLayer *layer, const QList<QgsFeatureId> &featureIds );

    /**
     * Removes features from the spatial index
     * \param layer vector layer
     * \param featureIds list of feature IDs to remove
     * \returns true if removal was successful
     */
    bool removeFeaturesFromIndex( QgsVectorLayer *layer, const QList<QgsFeatureId> &featureIds );

    /**
     * Returns information about existing spatial indexes
     * \param layer optional layer to filter indexes
     * \returns list of index information
     */
    QList<IndexInfo> getIndexInfo( QgsVectorLayer *layer = nullptr ) const;

    /**
     * Returns information about spatial hash cells
     * \param layer vector layer
     * \param extent optional extent to filter cells
     * \returns list of hash cell information
     */
    QList<HashCellInfo> getHashCellInfo( QgsVectorLayer *layer, const QgsRectangle &extent = QgsRectangle() ) const;

    /**
     * Returns information about cached queries
     * \param layer optional layer to filter queries
     * \returns list of cached query information
     */
    QList<CachedQuery> getCachedQueryInfo( QgsVectorLayer *layer = nullptr ) const;

    /**
     * Optimizes spatial indexes for better performance
     * \param layer optional layer to optimize (all if nullptr)
     * \param feedback optional feedback object
     * \returns true if optimization was successful
     */
    bool optimizeIndexes( QgsVectorLayer *layer = nullptr, QgsFeedback *feedback = nullptr );

    /**
     * Invalidates spatial indexes for the specified layer
     * \param layer vector layer
     */
    void invalidateIndex( QgsVectorLayer *layer );

    /**
     * Clears all spatial indexes and caches
     */
    void clearAllIndexes();

    /**
     * Returns current performance statistics
     */
    PerformanceStatistics getPerformanceStatistics() const;

    /**
     * Resets performance statistics
     */
    void resetStatistics();

    /**
     * Returns whether background indexing is active
     */
    bool isBackgroundIndexingActive() const;

    /**
     * Returns current index memory usage in MB
     */
    qint64 getIndexMemoryUsageMB() const;

    /**
     * Returns query cache hit rate (0.0-1.0)
     */
    double getQueryCacheHitRate() const;

  public slots:

    /**
     * Performs maintenance on spatial indexes
     */
    void performMaintenance();

    /**
     * Optimizes memory usage by clearing unused indexes
     */
    void optimizeMemoryUsage();

    /**
     * Handles memory pressure by reducing index cache sizes
     */
    void handleMemoryPressure();

  signals:

    /**
     * Emitted when a spatial index is created or rebuilt
     * \param layerId layer identifier
     * \param buildTimeMs build time in milliseconds
     */
    void indexCreated( const QString &layerId, double buildTimeMs );

    /**
     * Emitted when a spatial query is completed
     * \param layerId layer identifier
     * \param queryType type of query
     * \param resultCount number of results
     * \param queryTimeMs query time in milliseconds
     */
    void queryCompleted( const QString &layerId, QueryType queryType, int resultCount, double queryTimeMs );

    /**
     * Emitted when query cache statistics are updated
     * \param hitRate current hit rate
     * \param cacheSize current cache size in MB
     */
    void queryCacheUpdated( double hitRate, qint64 cacheSize );

    /**
     * Emitted when background index maintenance is completed
     * \param indexesProcessed number of indexes processed
     * \param timeMs time taken in milliseconds
     */
    void maintenanceCompleted( int indexesProcessed, double timeMs );

    /**
     * Emitted when performance statistics are updated
     * \param statistics current statistics
     */
    void statisticsUpdated( const PerformanceStatistics &statistics );

  private slots:

    /**
     * Handles completion of background index building
     */
    void onBackgroundIndexBuildCompleted();

    /**
     * Continues background processing tasks
     */
    void continueBackgroundProcessing();

  private:

    // Core functionality
    QString generateIndexId( QgsVectorLayer *layer ) const;
    QString generateIndexId( const QString &layerId ) const;
    QString generateQueryId( const QgsGeometry &geometry, QueryType queryType ) const;
    QString generateQueryId( const QgsRectangle &extent, QueryType queryType ) const;
    
    // R-tree index management
    bool buildRTreeIndex( QgsVectorLayer *layer, const QString &indexId );
    bool buildRTreeIndex( QgsVectorDataProvider *provider, const QString &layerId, const QString &indexId );
    QList<QgsFeatureId> queryRTreeIndex( const QString &indexId, const QgsGeometry &geometry, QueryType queryType );
    QList<QgsFeatureId> queryRTreeIndex( const QString &indexId, const QgsRectangle &extent, QueryType queryType );
    void optimizeRTreeIndex( const QString &indexId );
    
    // Spatial hash management
    bool buildSpatialHash( QgsVectorLayer *layer, const QString &indexId );
    bool buildSpatialHash( QgsVectorDataProvider *provider, const QString &layerId, const QString &indexId );
    QList<QgsFeatureId> querySpatialHash( const QString &indexId, const QgsGeometry &geometry, QueryType queryType );
    QList<QgsFeatureId> querySpatialHash( const QString &indexId, const QgsRectangle &extent, QueryType queryType );
    QPair<int, int> calculateHashCell( const QgsPointXY &point, int level ) const;
    QList<QPair<int, int>> calculateHashCells( const QgsRectangle &extent, int level ) const;
    void rehashSpatialIndex( const QString &indexId );
    
    // Query caching
    QString getCachedQueryResult( const QString &queryId );
    void cacheQueryResult( const QString &queryId, const QList<QgsFeatureId> &results, 
                          const QString &layerId, double queryTimeMs );
    bool isCacheValid( const CachedQuery &query ) const;
    void evictExpiredQueries();
    void evictLeastRecentlyUsedQueries();
    
    // Background processing
    void startBackgroundIndexBuild( QgsVectorLayer *layer, const QString &indexId );
    void processBackgroundTasks();
    void scheduleIndexMaintenance( const QString &indexId );
    
    // Memory management
    qint64 calculateIndexMemoryUsage( const QString &indexId ) const;
    qint64 calculateTotalMemoryUsage() const;
    bool isMemoryPressureHigh() const;
    void reduceMemoryUsage();
    void unloadLeastRecentlyUsedIndexes();
    
    // Index persistence
    bool saveIndexToDisk( const QString &indexId );
    bool loadIndexFromDisk( const QString &indexId );
    QString getIndexCacheFile( const QString &indexId ) const;
    void cleanupIndexFiles();
    
    // Performance monitoring
    void updateStatistics();
    void recordQuery( const QString &layerId, QueryType queryType, int resultCount, double queryTimeMs );
    void recordIndexAccess( const QString &indexId );
    void recordCacheAccess( const QString &queryId, bool hit );
    
    // Utility functions
    QgsGeometry createGeometryFromExtent( const QgsRectangle &extent ) const;
    double calculateDistance( const QgsPointXY &p1, const QgsPointXY &p2 ) const;
    QgsRectangle expandExtent( const QgsRectangle &extent, double buffer ) const;
    bool geometryIntersects( const QgsGeometry &g1, const QgsGeometry &g2, QueryType queryType ) const;

    // Configuration and state
    IndexConfig mConfig;
    IPerformanceMonitor *mPerformanceMonitor = nullptr;
    PerformanceStatistics mStatistics;
    QString mCurrentOperationId;
    
    // Index storage
    QHash<QString, QSharedPointer<QgsSpatialIndex>> mRTreeIndexes;
    QHash<QString, QHash<QPair<int, int>, HashCellInfo>> mSpatialHashes;
    QHash<QString, IndexInfo> mIndexInfo;
    mutable QMutex mIndexMutex;
    
    // Query caching
    QCache<QString, QList<QgsFeatureId>> mQueryCache;
    QHash<QString, CachedQuery> mCachedQueryInfo;
    mutable QMutex mQueryCacheMutex;
    qint64 mCurrentQueryCacheMemoryMB = 0;
    
    // Background processing
    QThreadPool *mBackgroundThreadPool = nullptr;
    QQueue<QString> mIndexBuildQueue;
    QHash<QString, QDateTime> mActiveBackgroundTasks;
    mutable QMutex mBackgroundMutex;
    
    // Timers and maintenance
    QTimer *mMaintenanceTimer = nullptr;
    QTimer *mBackgroundProcessingTimer = nullptr;
    QDateTime mLastMaintenanceTime;
    QDateTime mLastStatisticsUpdate;
    
    // Performance tracking
    QHash<QString, QDateTime> mIndexAccessTimes;
    QHash<QString, int> mIndexAccessCounts;
    QHash<QString, QDateTime> mQueryAccessTimes;
    QHash<QString, int> mQueryAccessCounts;
    mutable qint64 mBaselineQueryTimeMs = 0;
};

#endif // QGSSPATIALINDEXMANAGER_H