/***************************************************************************
                         qgsflatgeobufoptimizer.h
                         -------------------------
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

#ifndef QGSFLATGEOBUFOPTIMIZER_H
#define QGSFLATGEOBUFOPTIMIZER_H

#include "qgis_core.h"
#include "qgsgeometry.h"
#include "qgsrectangle.h"
#include "qgsfeature.h"
#include "qgsfields.h"
#include "qgswkbtypes.h"

#include <QObject>
#include <QCache>
#include <QMutex>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QQueue>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <memory>

class QgsVectorLayer;
class IPerformanceMonitor;

/**
 * \ingroup core
 * \class QgsFlatGeobufOptimizer
 * \brief FlatGeobuf-specific performance optimizer with spatial indexing, chunk-based loading, and schema caching
 * 
 * This optimizer provides significant performance improvements specifically for FlatGeobuf (.fgb) files
 * through format-specific optimizations including:
 * - Spatial indexing with R-tree acceleration
 * - Chunk-based progressive loading
 * - Schema and metadata caching
 * - Background data prefetching
 * - HTTP range request optimization for remote files
 * - Memory-efficient streaming
 * 
 * Target: Additional 5% improvement on top of base vector rendering optimizations
 * 
 * \since QGIS 3.x
 */
class CORE_EXPORT QgsFlatGeobufOptimizer : public QObject
{
    Q_OBJECT

  public:

    /**
     * Spatial indexing configuration
     */
    struct SpatialIndexConfig
    {
      //! Enable spatial indexing
      bool enableSpatialIndex = true;
      
      //! R-tree node capacity
      int rtreeNodeCapacity = 16;
      
      //! Enable spatial filtering
      bool enableSpatialFiltering = true;
      
      //! Spatial index cache size in MB
      int indexCacheSizeMB = 64;
      
      //! Enable index persistence
      bool enableIndexPersistence = true;
      
      //! Index rebuild threshold (features)
      int rebuildThreshold = 10000;
    };

    /**
     * Chunk loading configuration
     */
    struct ChunkLoadingConfig
    {
      //! Enable chunk-based loading
      bool enableChunkLoading = true;
      
      //! Chunk size in bytes
      qint64 chunkSizeBytes = 1024 * 1024; // 1MB
      
      //! Maximum concurrent chunks
      int maxConcurrentChunks = 4;
      
      //! Enable background chunk loading
      bool enableBackgroundLoading = true;
      
      //! Chunk cache size in MB
      int chunkCacheSizeMB = 128;
      
      //! Enable chunk compression
      bool enableCompression = true;
    };

    /**
     * Schema caching configuration
     */
    struct SchemaCacheConfig
    {
      //! Enable schema caching
      bool enableSchemaCache = true;
      
      //! Schema cache size in MB
      int schemaCacheSizeMB = 32;
      
      //! Enable metadata caching
      bool enableMetadataCache = true;
      
      //! Cache expiry time in minutes
      int cacheExpiryMinutes = 60;
      
      //! Enable persistent cache
      bool enablePersistentCache = true;
    };

    /**
     * Network optimization configuration
     */
    struct NetworkConfig
    {
      //! Enable HTTP range requests
      bool enableRangeRequests = true;
      
      //! Enable connection pooling
      bool enableConnectionPooling = true;
      
      //! Request timeout in milliseconds
      int requestTimeoutMs = 30000;
      
      //! Maximum concurrent requests
      int maxConcurrentRequests = 6;
      
      //! Enable request compression
      bool enableRequestCompression = true;
      
      //! Prefetch buffer size in bytes
      qint64 prefetchBufferSize = 512 * 1024; // 512KB
    };

    /**
     * Optimization configuration
     */
    struct OptimizationConfig
    {
      SpatialIndexConfig spatialIndex;
      ChunkLoadingConfig chunkLoading;
      SchemaCacheConfig schemaCache;
      NetworkConfig network;
      
      //! Enable memory streaming
      bool enableMemoryStreaming = true;
      
      //! Enable feature filtering
      bool enableFeatureFiltering = true;
      
      //! Enable geometry simplification
      bool enableGeometrySimplification = true;
      
      //! Memory limit in MB
      int memoryLimitMB = 512;
      
      //! Enable background processing
      bool enableBackgroundProcessing = true;
    };

    /**
     * Data chunk for efficient loading
     */
    struct DataChunk
    {
      //! Chunk identifier
      QString chunkId;
      
      //! Chunk offset in file
      qint64 fileOffset;
      
      //! Chunk size in bytes
      qint64 sizeBytes;
      
      //! Spatial bounds of chunk
      QgsRectangle bounds;
      
      //! Number of features in chunk
      int featureCount;
      
      //! Chunk data buffer
      QByteArray data;
      
      //! Compressed data flag
      bool isCompressed = false;
      
      //! Loading status
      bool isLoaded = false;
      
      //! Background loading status
      bool isLoadingInBackground = false;
      
      //! Last access time
      QDateTime lastAccessed;
      
      //! Memory size in bytes
      qint64 memorySizeBytes = 0;
      
      //! Features in this chunk
      QList<QgsFeature> features;
    };

    /**
     * Schema cache entry
     */
    struct SchemaEntry
    {
      //! File path or URL
      QString filePath;
      
      //! Cached fields
      QgsFields fields;
      
      //! Cached metadata
      QHash<QString, QVariant> metadata;
      
      //! Geometry type
      Qgis::GeometryType geometryType;
      
      //! Feature count
      qint64 featureCount;
      
      //! File size
      qint64 fileSize;
      
      //! CRS information
      QString crsWkt;
      
      //! Cache timestamp
      QDateTime cacheTime;
      
      //! File modification time
      QDateTime fileModTime;
      
      //! Cache entry size in bytes
      qint64 entrySizeBytes = 0;
    };

    /**
     * Spatial index entry
     */
    struct SpatialIndexEntry
    {
      //! Feature ID
      QgsFeatureId featureId;
      
      //! Feature bounds
      QgsRectangle bounds;
      
      //! Chunk ID containing this feature
      QString chunkId;
      
      //! Feature offset within chunk
      qint64 chunkOffset;
      
      //! Feature size in bytes
      qint64 featureSize;
    };

    /**
     * Performance statistics
     */
    struct PerformanceStatistics
    {
      //! Features loaded
      int featuresLoaded = 0;
      
      //! Chunks loaded
      int chunksLoaded = 0;
      
      //! Chunks cached
      int chunksCached = 0;
      
      //! Schema cache hits
      int schemaCacheHits = 0;
      
      //! Schema cache misses
      int schemaCacheMisses = 0;
      
      //! Spatial index queries
      int spatialQueries = 0;
      
      //! Features filtered by spatial index
      int featuresFilteredBySpatialIndex = 0;
      
      //! Network requests made
      int networkRequests = 0;
      
      //! Bytes downloaded
      qint64 bytesDownloaded = 0;
      
      //! Total loading time in ms
      qint64 totalLoadTimeMs = 0;
      
      //! Chunk loading time in ms
      qint64 chunkLoadTimeMs = 0;
      
      //! Index query time in ms
      qint64 indexQueryTimeMs = 0;
      
      //! Network time in ms
      qint64 networkTimeMs = 0;
      
      //! Memory usage in MB
      double memoryUsageMB = 0.0;
      
      //! Cache hit rate (0.0 - 1.0)
      double cacheHitRate = 0.0;
      
      //! Performance improvement percentage
      double improvementPercent = 0.0;
      
      //! Statistics timestamp
      QDateTime timestamp;
    };

    /**
     * Background loading task
     */
    struct LoadingTask
    {
      //! Task identifier
      QString taskId;
      
      //! File path or URL
      QString filePath;
      
      //! Spatial filter extent
      QgsRectangle filterExtent;
      
      //! Chunk IDs to load
      QStringList chunkIds;
      
      //! Task priority (higher = more important)
      int priority = 0;
      
      //! Task creation time
      QDateTime createdTime;
      
      //! Task completion status
      bool isCompleted = false;
      
      //! Task progress (0.0 - 1.0)
      double progress = 0.0;
    };

    /**
     * Constructor
     */
    QgsFlatGeobufOptimizer( QObject *parent = nullptr );

    /**
     * Destructor
     */
    ~QgsFlatGeobufOptimizer();

    // Configuration methods

    /**
     * Set optimization configuration
     */
    void setOptimizationConfig( const OptimizationConfig &config );

    /**
     * Get optimization configuration
     */
    OptimizationConfig getOptimizationConfig() const;

    /**
     * Set performance monitor
     */
    void setPerformanceMonitor( IPerformanceMonitor *monitor );

    // File operations

    /**
     * Initialize optimizer for a FlatGeobuf file
     */
    bool initializeFile( const QString &filePath );

    /**
     * Load features from spatial extent
     */
    QList<QgsFeature> loadFeatures( const QgsRectangle &extent = QgsRectangle() );

    /**
     * Load specific features by IDs
     */
    QList<QgsFeature> loadFeatures( const QList<QgsFeatureId> &featureIds );

    /**
     * Get file schema information
     */
    SchemaEntry getSchemaInfo( const QString &filePath );

    /**
     * Preload chunks for given extent
     */
    void preloadChunks( const QgsRectangle &extent );

    // Spatial indexing

    /**
     * Build spatial index for file
     */
    bool buildSpatialIndex( const QString &filePath );

    /**
     * Query spatial index
     */
    QList<SpatialIndexEntry> querySpatialIndex( const QgsRectangle &extent );

    /**
     * Check if spatial index exists
     */
    bool hasSpatialIndex( const QString &filePath ) const;

    /**
     * Clear spatial index
     */
    void clearSpatialIndex( const QString &filePath );

    // Chunk management

    /**
     * Get data chunks for extent
     */
    QList<DataChunk> getChunksForExtent( const QgsRectangle &extent );

    /**
     * Load specific chunk
     */
    bool loadChunk( const QString &chunkId );

    /**
     * Get chunk by ID
     */
    DataChunk *getChunk( const QString &chunkId );

    /**
     * Remove chunk from cache
     */
    void removeChunk( const QString &chunkId );

    /**
     * Get all chunk IDs
     */
    QStringList getChunkIds() const;

    // Cache management

    /**
     * Clear all caches
     */
    void clearAllCaches();

    /**
     * Clear schema cache
     */
    void clearSchemaCache();

    /**
     * Clear chunk cache
     */
    void clearChunkCache();

    /**
     * Get cache memory usage in MB
     */
    qint64 getCacheMemoryUsageMB() const;

    // Statistics and monitoring

    /**
     * Get performance statistics
     */
    PerformanceStatistics getPerformanceStatistics() const;

    /**
     * Reset performance statistics
     */
    void resetStatistics();

    /**
     * Get estimated improvement percentage
     */
    double getEstimatedImprovement() const;

    // Background processing

    /**
     * Start background loading task
     */
    QString startBackgroundLoading( const QString &filePath, const QgsRectangle &extent );

    /**
     * Cancel background loading task
     */
    void cancelBackgroundLoading( const QString &taskId );

    /**
     * Check if background loading is active
     */
    bool isBackgroundLoadingActive() const;

    /**
     * Get background loading progress
     */
    double getBackgroundLoadingProgress( const QString &taskId ) const;

  public slots:

    /**
     * Handle cache maintenance
     */
    void performCacheMaintenance();

    /**
     * Handle background loading completion
     */
    void onBackgroundLoadingComplete( const QString &taskId );

  signals:

    /**
     * Emitted when schema is loaded
     */
    void schemaLoaded( const QString &filePath, const SchemaEntry &schema );

    /**
     * Emitted when chunk loading starts
     */
    void chunkLoadingStarted( const QString &chunkId );

    /**
     * Emitted when chunk loading finishes
     */
    void chunkLoadingFinished( const QString &chunkId, bool success );

    /**
     * Emitted when spatial index is built
     */
    void spatialIndexBuilt( const QString &filePath, int featureCount );

    /**
     * Emitted when background loading starts
     */
    void backgroundLoadingStarted( const QString &taskId );

    /**
     * Emitted when background loading finishes
     */
    void backgroundLoadingFinished( const QString &taskId, bool success );

    /**
     * Emitted when background loading progress changes
     */
    void backgroundLoadingProgress( const QString &taskId, double progress );

    /**
     * Emitted when cache is updated
     */
    void cacheUpdated( qint64 totalSizeMB, int itemCount );

    /**
     * Emitted when performance statistics are updated
     */
    void statisticsUpdated( const PerformanceStatistics &statistics );

  private slots:

    /**
     * Continue background processing
     */
    void continueBackgroundProcessing();

    /**
     * Handle network request completion
     */
    void onNetworkRequestFinished();

  private:

    // Core functionality
    bool parseHeader( const QString &filePath );
    bool parseFeatureTable( const QString &filePath );
    QList<QgsFeature> extractFeaturesFromChunk( const DataChunk &chunk, const QgsRectangle &filterExtent = QgsRectangle() );
    
    // Spatial indexing
    void buildRTree( const QString &filePath );
    QList<SpatialIndexEntry> queryRTree( const QgsRectangle &extent );
    void saveIndexToCache( const QString &filePath );
    bool loadIndexFromCache( const QString &filePath );
    
    // Chunk operations
    QString generateChunkId( qint64 offset, qint64 size ) const;
    DataChunk createChunk( qint64 offset, qint64 size, const QgsRectangle &bounds );
    void createSampleChunks();
    void optimizeChunkSize( DataChunk &chunk );
    void compressChunk( DataChunk &chunk );
    void decompressChunk( DataChunk &chunk );
    
    // Schema operations
    void cacheSchema( const QString &filePath, const SchemaEntry &schema );
    SchemaEntry getCachedSchema( const QString &filePath ) const;
    bool isSchemaValid( const SchemaEntry &schema, const QString &filePath ) const;
    
    // Network operations
    QNetworkReply *createRangeRequest( const QString &url, qint64 start, qint64 end );
    void handleRangeRequestResponse( QNetworkReply *reply, const QString &chunkId );
    void optimizeNetworkRequests();
    
    // Cache management
    void evictLeastRecentlyUsedChunks();
    void evictLeastRecentlyUsedSchemas();
    void updateCacheStatistics();
    
    // Background processing
    void processLoadingTask( const LoadingTask &task );
    void prioritizeLoadingTasks();
    
    // Statistics and monitoring
    void updateStatistics();
    void recordChunkLoad( qint64 loadTime, qint64 chunkSize );
    void recordSpatialQuery( qint64 queryTime, int resultCount );
    void recordNetworkRequest( qint64 requestTime, qint64 bytesTransferred );
    
    // Utility methods
    QgsRectangle calculateFileBounds( const QString &filePath );
    qint64 estimateChunkMemoryUsage( const DataChunk &chunk ) const;
    qint64 estimateSchemaMemoryUsage( const SchemaEntry &schema ) const;
    qint64 getCurrentMemoryUsage() const;
    bool isRemoteFile( const QString &filePath ) const;
    QString getIndexCacheKey( const QString &filePath ) const;

    // Configuration
    OptimizationConfig mConfig;
    
    // Performance monitoring
    IPerformanceMonitor *mPerformanceMonitor = nullptr;
    QString mCurrentOperationId;
    
    // File state
    QString mCurrentFilePath;
    bool mIsInitialized = false;
    SchemaEntry mCurrentSchema;
    
    // Spatial indexing
    QHash<QString, QList<SpatialIndexEntry>> mSpatialIndexes;
    mutable QMutex mSpatialIndexMutex;
    
    // Chunk management using raw pointers for Qt5 compatibility
    QHash<QString, DataChunk *> mChunkCache;
    mutable QMutex mChunkCacheMutex;
    qint64 mCurrentChunkCacheSizeMB = 0;
    
    // Schema caching
    QCache<QString, SchemaEntry> mSchemaCache;
    mutable QMutex mSchemaCacheMutex;
    qint64 mCurrentSchemaCacheSizeMB = 0;
    
    // Network management
    QNetworkAccessManager *mNetworkManager = nullptr;
    QHash<QNetworkReply*, QString> mActiveRequests;
    mutable QMutex mNetworkMutex;
    
    // Background processing
    QTimer *mBackgroundTimer = nullptr;
    QQueue<LoadingTask> mLoadingQueue;
    QHash<QString, LoadingTask> mActiveTasks;
    mutable QMutex mTaskMutex;
    
    // Cache maintenance
    QTimer *mCacheMaintenanceTimer = nullptr;
    
    // Statistics
    PerformanceStatistics mStatistics;
    QElapsedTimer mOperationTimer;
    mutable qint64 mBaselineLoadTimeMs = 0;
    
    // Chunk tracking
    QHash<QString, QDateTime> mChunkAccessTimes;
    QHash<QString, QString> mFileToChunks;
};

#endif // QGSFLATGEOBUFOPTIMIZER_H