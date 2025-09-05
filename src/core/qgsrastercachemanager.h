/***************************************************************************
                         qgsrastercachemanager.h
                         ------------------------
    begin                : September 2025
    copyright            : (C) 2025 by QGIS
    email                : development-team@qgis.org
 ***************************************************************************/

#ifndef QGSRASTERCACHEMANAGER_H
#define QGSRASTERCACHEMANAGER_H

#include "qgis_core.h"
#include "qgsrectangle.h"
#include "qgswkbtypes.h"
#include "qgsmaptopixel.h"
#include "qgsmaplayer.h"

#include <QObject>
#include <QCache>
#include <QHash>
#include <QMutex>
#include <QTimer>
#include <QDateTime>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QThreadPool>
#include <QRunnable>
#include <QSharedPointer>
#include <QWeakPointer>
#include <QQueue>
#include <memory>

class QgsRasterDataProvider;
class QgsRasterBlock;
class QgsRasterBlockFeedback;
class IPerformanceMonitor;
class QgsFeedback;

/**
 * \ingroup core
 * \brief Advanced raster cache manager providing optimized raster data handling
 * 
 * This class implements high-performance raster caching with features including:
 * - Tile-based caching with configurable tile sizes
 * - Pyramid generation and management
 * - Memory-mapped file access for large rasters
 * - Background tile prefetching
 * - LRU eviction with memory pressure awareness
 * - Network-aware caching for remote rasters
 * - Performance monitoring integration
 * 
 * The cache manager is designed to significantly improve raster rendering performance
 * by implementing intelligent caching strategies and optimized data access patterns.
 * 
 * \since QGIS 3.40
 */
class CORE_EXPORT QgsRasterCacheManager : public QObject
{
    Q_OBJECT

    friend class BackgroundTileLoadTask;

  public:

    /**
     * Configuration for tile-based caching
     */
    struct TileConfig
    {
      int tileWidth = 256;                    //!< Tile width in pixels
      int tileHeight = 256;                   //!< Tile height in pixels
      int maxTilesCached = 1000;              //!< Maximum number of tiles to cache
      int tileCacheSizeMB = 500;              //!< Total tile cache size in MB
      double tileBufferFactor = 0.1;          //!< Buffer factor for tile prefetching (0.1 = 10% buffer)
      bool enableTileCompression = true;      //!< Enable tile compression to save memory
      bool enableBackgroundTileLoad = true;   //!< Enable background tile loading
      int backgroundThreadCount = 2;          //!< Number of background loading threads
    };

    /**
     * Configuration for pyramid generation and management
     */
    struct PyramidConfig
    {
      bool enablePyramids = true;              //!< Enable pyramid generation
      bool autogeneratePyramids = true;        //!< Automatically generate pyramids for large rasters
      int pyramidThresholdPixels = 1000000;   //!< Threshold in pixels for automatic pyramid generation
      int maxPyramidLevels = 8;                //!< Maximum number of pyramid levels
      double pyramidScaleFactor = 2.0;        //!< Scale factor between pyramid levels
      QString pyramidCacheDir;                 //!< Directory for pyramid cache files
      bool persistPyramids = true;            //!< Persist pyramids to disk
      int pyramidTileSize = 256;               //!< Pyramid tile size
    };

    /**
     * Configuration for memory-mapped file access
     */
    struct MemoryMapConfig
    {
      bool enableMemoryMapping = true;         //!< Enable memory-mapped file access
      qint64 memoryMapThresholdMB = 100;       //!< File size threshold for memory mapping (MB)
      qint64 maxMemoryMappedSizeMB = 2048;     //!< Maximum total memory mapped size (MB)
      bool prefaultMemoryMap = false;          //!< Prefault memory mapped pages
      bool useAdviseRandomAccess = true;       //!< Use madvise for random access patterns
      int memoryMapPageSize = 4096;            //!< Memory map page size
    };

    /**
     * Configuration for network-aware caching
     */
    struct NetworkConfig
    {
      bool enableNetworkCache = true;          //!< Enable network-specific caching
      int networkTimeoutMs = 30000;            //!< Network request timeout in milliseconds
      int maxConcurrentRequests = 4;           //!< Maximum concurrent network requests
      qint64 networkCacheSizeMB = 200;         //!< Network cache size in MB
      bool enableRangeRequests = true;         //!< Enable HTTP range requests
      bool enableNetworkCompression = true;    //!< Enable network data compression
      int retryCount = 3;                      //!< Number of retry attempts for failed requests
      int retryDelayMs = 1000;                 //!< Delay between retry attempts
    };

    /**
     * Configuration for prefetching optimization
     */
    struct PrefetchConfig
    {
      bool enablePrefetching = true;           //!< Enable background prefetching
      double prefetchRadiusFactor = 2.0;      //!< Prefetch radius as factor of viewport
      int maxPrefetchTiles = 50;               //!< Maximum tiles to prefetch
      int prefetchPriority = 3;                //!< Prefetch thread priority (1-5, lower = higher priority)
      bool adaptivePrefetching = true;         //!< Adapt prefetching based on user behavior
      int prefetchDelayMs = 100;               //!< Delay before starting prefetch
      bool prefetchOnIdle = true;              //!< Only prefetch when system is idle
    };

    /**
     * Complete raster cache configuration
     */
    struct CacheConfig
    {
      TileConfig tile;                         //!< Tile-based caching configuration
      PyramidConfig pyramid;                   //!< Pyramid configuration
      MemoryMapConfig memoryMap;               //!< Memory mapping configuration
      NetworkConfig network;                   //!< Network caching configuration
      PrefetchConfig prefetch;                 //!< Prefetching configuration
      bool enablePerformanceMonitoring = true; //!< Enable performance monitoring
      int cacheMaintenanceIntervalMs = 10000; //!< Cache maintenance interval
      double memoryPressureThreshold = 0.8;   //!< Memory pressure threshold (0.8 = 80%)
    };

    /**
     * Information about a cached tile
     */
    struct TileInfo
    {
      QString tileId;                          //!< Unique tile identifier
      QgsRectangle extent;                     //!< Tile geographic extent
      int level = 0;                           //!< Pyramid level (0 = full resolution)
      int tileX = 0;                           //!< Tile X coordinate
      int tileY = 0;                           //!< Tile Y coordinate
      int width = 0;                           //!< Tile width in pixels
      int height = 0;                          //!< Tile height in pixels
      QDateTime cacheTime;                     //!< When tile was cached
      QDateTime lastAccessed;                  //!< When tile was last accessed
      qint64 memorySizeBytes = 0;              //!< Memory size of cached tile
      bool isCompressed = false;               //!< Whether tile data is compressed
      bool isMemoryMapped = false;             //!< Whether tile uses memory mapping
      bool isLoading = false;                  //!< Whether tile is currently loading
      int accessCount = 0;                     //!< Number of times tile has been accessed
      double loadTimeMs = 0;                   //!< Time taken to load tile (ms)
    };

    /**
     * Pyramid level information
     */
    struct PyramidLevel
    {
      int level = 0;                           //!< Pyramid level number
      double scaleFactor = 1.0;                //!< Scale factor relative to original
      int width = 0;                           //!< Pyramid level width in pixels
      int height = 0;                          //!< Pyramid level height in pixels
      QgsRectangle extent;                     //!< Pyramid level extent
      QString cacheFile;                       //!< Cache file path (if persisted)
      QDateTime buildTime;                     //!< When pyramid was built
      qint64 fileSizeBytes = 0;                //!< File size in bytes
      bool isAvailable = false;                //!< Whether pyramid level is available
    };

    /**
     * Memory mapping information
     */
    struct MemoryMapInfo
    {
      QString filePath;                        //!< Original file path
      void *mappedMemory = nullptr;            //!< Mapped memory pointer
      qint64 mappedSize = 0;                   //!< Size of mapped region
      qint64 fileSize = 0;                     //!< Total file size
      QDateTime mapTime;                       //!< When memory mapping was created
      bool isReadOnly = true;                  //!< Whether mapping is read-only
      int pageSize = 4096;                     //!< Memory page size
      int accessPattern = 0;                   //!< Access pattern hint (for madvise)
    };

    /**
     * Performance statistics for the cache manager
     */
    struct PerformanceStatistics
    {
      QDateTime timestamp;                     //!< Statistics timestamp
      
      // Cache statistics
      int tilesInCache = 0;                    //!< Number of tiles currently cached
      qint64 cacheMemoryUsageMB = 0;           //!< Cache memory usage in MB
      double cacheHitRate = 0.0;               //!< Cache hit rate (0.0-1.0)
      int cacheHits = 0;                       //!< Total cache hits
      int cacheMisses = 0;                     //!< Total cache misses
      int tilesEvicted = 0;                    //!< Number of tiles evicted
      
      // Performance metrics
      double averageTileLoadTimeMs = 0.0;      //!< Average tile load time
      double averageMemoryMapTimeMs = 0.0;     //!< Average memory mapping time
      qint64 totalBytesLoaded = 0;             //!< Total bytes loaded from disk/network
      qint64 totalBytesEvicted = 0;            //!< Total bytes evicted from cache
      
      // Network statistics
      int networkRequests = 0;                 //!< Number of network requests
      int networkRequestsSuccessful = 0;       //!< Successful network requests
      double averageNetworkTimeMs = 0.0;       //!< Average network request time
      qint64 networkBytesTransferred = 0;      //!< Bytes transferred over network
      
      // Pyramid statistics
      int pyramidLevelsBuilt = 0;              //!< Number of pyramid levels built
      double pyramidBuildTimeMs = 0.0;         //!< Total pyramid build time
      qint64 pyramidCacheSizeMB = 0;           //!< Pyramid cache size
      
      // Memory mapping statistics
      int memoryMappedFiles = 0;               //!< Number of memory mapped files
      qint64 totalMemoryMappedMB = 0;          //!< Total memory mapped size
      double averageMapCreationTimeMs = 0.0;   //!< Average memory map creation time
      
      // Background processing
      int backgroundTasksActive = 0;           //!< Active background tasks
      int backgroundTasksCompleted = 0;        //!< Completed background tasks
      int prefetchedTiles = 0;                 //!< Number of prefetched tiles
    };

    /**
     * Constructor
     * \param parent parent object
     */
    explicit QgsRasterCacheManager( QObject *parent = nullptr );

    /**
     * Destructor
     */
    ~QgsRasterCacheManager() override;

    /**
     * Sets the cache configuration
     * \param config cache configuration
     */
    void setCacheConfig( const CacheConfig &config );

    /**
     * Returns the current cache configuration
     */
    CacheConfig getCacheConfig() const;

    /**
     * Sets the performance monitor for metrics collection
     * \param monitor performance monitor instance
     */
    void setPerformanceMonitor( IPerformanceMonitor *monitor );

    /**
     * Gets a cached raster block for the specified parameters
     * \param provider raster data provider
     * \param bandNo band number
     * \param extent geographic extent
     * \param width width in pixels
     * \param height height in pixels
     * \param feedback optional feedback object
     * \returns cached raster block or nullptr if not available
     */
    QgsRasterBlock *getCachedBlock( QgsRasterDataProvider *provider,
                                    int bandNo,
                                    const QgsRectangle &extent,
                                    int width,
                                    int height,
                                    QgsRasterBlockFeedback *feedback = nullptr );

    /**
     * Caches a raster block with the specified parameters
     * \param provider raster data provider
     * \param bandNo band number
     * \param extent geographic extent
     * \param width width in pixels
     * \param height height in pixels
     * \param block raster block to cache
     */
    void cacheBlock( QgsRasterDataProvider *provider,
                     int bandNo,
                     const QgsRectangle &extent,
                     int width,
                     int height,
                     QgsRasterBlock *block );

    /**
     * Prefetches tiles for the specified extent
     * \param provider raster data provider
     * \param bandNo band number
     * \param extent geographic extent to prefetch around
     * \param width width in pixels
     * \param height height in pixels
     */
    void prefetchTiles( QgsRasterDataProvider *provider,
                        int bandNo,
                        const QgsRectangle &extent,
                        int width,
                        int height );

    /**
     * Builds pyramid levels for the specified provider
     * \param provider raster data provider
     * \param bandNo band number
     * \param feedback optional feedback object
     * \returns true if pyramids were built successfully
     */
    bool buildPyramids( QgsRasterDataProvider *provider,
                        int bandNo,
                        QgsFeedback *feedback = nullptr );

    /**
     * Creates a memory mapping for the specified file
     * \param filePath path to raster file
     * \returns true if memory mapping was created successfully
     */
    bool createMemoryMapping( const QString &filePath );

    /**
     * Returns information about cached tiles
     * \param provider optional provider to filter tiles
     * \returns list of tile information
     */
    QList<TileInfo> getCachedTileInfo( QgsRasterDataProvider *provider = nullptr ) const;

    /**
     * Returns information about available pyramid levels
     * \param provider raster data provider
     * \returns list of pyramid level information
     */
    QList<PyramidLevel> getPyramidLevels( QgsRasterDataProvider *provider ) const;

    /**
     * Returns information about memory mapped files
     * \returns list of memory mapping information
     */
    QList<MemoryMapInfo> getMemoryMappingInfo() const;

    /**
     * Invalidates cache entries for the specified provider
     * \param provider raster data provider
     */
    void invalidateCache( QgsRasterDataProvider *provider );

    /**
     * Clears all cached data
     */
    void clearCache();

    /**
     * Returns current performance statistics
     */
    PerformanceStatistics getPerformanceStatistics() const;

    /**
     * Resets performance statistics
     */
    void resetStatistics();

    /**
     * Returns current cache memory usage in MB
     */
    qint64 getCacheMemoryUsageMB() const;

    /**
     * Returns cache hit rate (0.0-1.0)
     */
    double getCacheHitRate() const;

    /**
     * Returns whether background processing is active
     */
    bool isBackgroundProcessingActive() const;

  public slots:

    /**
     * Optimizes cache by removing least recently used entries
     */
    void optimizeCache();

    /**
     * Performs cache maintenance tasks
     */
    void performMaintenance();

    /**
     * Handles memory pressure by reducing cache size
     */
    void handleMemoryPressure();

  signals:

    /**
     * Emitted when a tile is loaded and cached
     * \param tileId tile identifier
     * \param loadTimeMs load time in milliseconds
     */
    void tileLoaded( const QString &tileId, double loadTimeMs );

    /**
     * Emitted when pyramid building is completed
     * \param provider raster data provider
     * \param levelsBuilt number of pyramid levels built
     */
    void pyramidsBuilt( QgsRasterDataProvider *provider, int levelsBuilt );

    /**
     * Emitted when a memory mapping is created
     * \param filePath file path
     * \param mappedSizeMB mapped size in MB
     */
    void memoryMappingCreated( const QString &filePath, qint64 mappedSizeMB );

    /**
     * Emitted when cache statistics are updated
     * \param statistics current statistics
     */
    void statisticsUpdated( const PerformanceStatistics &statistics );

    /**
     * Emitted when cache memory usage changes significantly
     * \param usageMB current usage in MB
     * \param maxSizeMB maximum cache size in MB
     */
    void cacheMemoryUsageChanged( qint64 usageMB, qint64 maxSizeMB );

    /**
     * Emitted when background prefetching completes
     * \param tilesPrefetched number of tiles prefetched
     */
    void prefetchingCompleted( int tilesPrefetched );

  private slots:

    /**
     * Handles completion of background tile loading
     */
    void onBackgroundTileLoadCompleted();

    /**
     * Handles network request finished
     */
    void onNetworkRequestFinished();

    /**
     * Continues background processing
     */
    void continueBackgroundProcessing();

  private:

    // Core functionality
    QString generateTileId( QgsRasterDataProvider *provider, int bandNo, 
                           const QgsRectangle &extent, int width, int height, int level = 0 ) const;
    QString generateProviderKey( QgsRasterDataProvider *provider ) const;
    QgsRectangle calculateTileExtent( const QgsRectangle &requestExtent, int tileX, int tileY, 
                                     int tileWidth, int tileHeight, int totalWidth, int totalHeight ) const;
    
    // Tile management
    QgsRasterBlock *loadTileFromCache( const QString &tileId );
    void storeTileInCache( const QString &tileId, QgsRasterBlock *block, const TileInfo &info );
    void evictLeastRecentlyUsedTiles();
    void evictTilesForProvider( QgsRasterDataProvider *provider );
    QList<QString> getTileIdsForExtent( QgsRasterDataProvider *provider, int bandNo, 
                                       const QgsRectangle &extent, int width, int height ) const;
    
    // Pyramid management
    bool shouldBuildPyramids( QgsRasterDataProvider *provider ) const;
    PyramidLevel createPyramidLevel( QgsRasterDataProvider *provider, int bandNo, int level );
    void cachePyramidLevel( QgsRasterDataProvider *provider, const PyramidLevel &pyramid );
    QString getPyramidCacheFile( QgsRasterDataProvider *provider, int level ) const;
    
    // Memory mapping
    bool shouldCreateMemoryMapping( const QString &filePath ) const;
    void *createMemoryMap( const QString &filePath, qint64 size, bool readOnly = true );
    void releaseMemoryMapping( const QString &filePath );
    void optimizeMemoryMapAccess( void *mappedMemory, qint64 size );
    
    // Network handling
    QNetworkReply *createNetworkRequest( const QUrl &url, const QgsRectangle &extent );
    void handleNetworkResponse( QNetworkReply *reply, const QString &tileId );
    bool supportsRangeRequests( const QUrl &url ) const;
    
    // Background processing
    void startBackgroundTileLoad( const QString &tileId, QgsRasterDataProvider *provider, 
                                 int bandNo, const QgsRectangle &extent, int width, int height );
    void processBackgroundTasks();
    void updatePrefetchQueue( QgsRasterDataProvider *provider, const QgsRectangle &viewExtent );
    
    // Compression and optimization
    QByteArray compressTileData( const QByteArray &data ) const;
    QByteArray decompressTileData( const QByteArray &compressedData ) const;
    void optimizeTileSize( TileInfo &tile ) const;
    
    // Statistics and monitoring
    void updateStatistics();
    void recordTileAccess( const QString &tileId );
    void recordMemoryMapCreation( const QString &filePath, qint64 size, double timeMs );
    void recordNetworkRequest( double timeMs, qint64 bytesTransferred, bool success );
    
    // Memory management
    qint64 calculateMemoryUsage() const;
    qint64 calculateAvailableMemory() const;
    bool isMemoryPressureHigh() const;
    void reduceCacheSize( qint64 targetSizeMB );
    
    // Cache maintenance
    void cleanupExpiredTiles();
    void optimizeCacheLayout();
    void validateCacheIntegrity();

    // Configuration and state
    CacheConfig mConfig;
    IPerformanceMonitor *mPerformanceMonitor = nullptr;
    PerformanceStatistics mStatistics;
    QString mCurrentOperationId;
    
    // Tile cache
    QCache<QString, QgsRasterBlock> mTileCache;
    QHash<QString, TileInfo> mTileInfo;
    mutable QMutex mTileCacheMutex;
    qint64 mCurrentCacheMemoryMB = 0;
    
    // Pyramid cache
    QHash<QString, QList<PyramidLevel>> mPyramidLevels;
    QHash<QString, QString> mPyramidCacheFiles;
    mutable QMutex mPyramidMutex;
    
    // Memory mapping
    QHash<QString, MemoryMapInfo> mMemoryMappings;
    mutable QMutex mMemoryMapMutex;
    qint64 mTotalMemoryMappedMB = 0;
    
    // Network management
    QNetworkAccessManager *mNetworkManager = nullptr;
    QHash<QNetworkReply*, QString> mActiveNetworkRequests;
    mutable QMutex mNetworkMutex;
    
    // Background processing
    QThreadPool *mBackgroundThreadPool = nullptr;
    QQueue<QString> mPrefetchQueue;
    QHash<QString, QDateTime> mActiveBackgroundTasks;
    mutable QMutex mBackgroundMutex;
    
    // Timers and maintenance
    QTimer *mMaintenanceTimer = nullptr;
    QTimer *mBackgroundProcessingTimer = nullptr;
    QDateTime mLastMaintenanceTime;
    QDateTime mLastStatisticsUpdate;
    
    // Performance tracking
    mutable qint64 mBaselineLoadTimeMs = 0;
    QHash<QString, QDateTime> mTileAccessTimes;
    QHash<QString, int> mTileAccessCounts;
};

#endif // QGSRASTERCACHEMANAGER_H