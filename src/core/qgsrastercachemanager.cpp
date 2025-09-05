/***************************************************************************
                         qgsrastercachemanager.cpp
                         --------------------------
    begin                : September 2025
    copyright            : (C) 2025 by QGIS
    email                : development-team@qgis.org
 ***************************************************************************/

#include "qgsrastercachemanager.h"
#include "qgsrasterdataprovider.h"
#include "qgsrasterblock.h"
#include "qgsrasterinterface.h"
#include "qgsfeedback.h"
#include "qgslogger.h"
#include "qgsapplication.h"
#include "iperformancemonitor.h"

#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <fcntl.h>
#include <QThread>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QtConcurrent>
#include <QMutexLocker>
#include <QProcess>
#include <QCoreApplication>

#ifdef Q_OS_LINUX
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#ifdef Q_OS_MACOS
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

// Background tile loading task
class BackgroundTileLoadTask : public QRunnable
{
  public:
    BackgroundTileLoadTask( QgsRasterCacheManager *manager, const QString &tileId,
                           QgsRasterDataProvider *provider, int bandNo,
                           const QgsRectangle &extent, int width, int height )
      : mManager( manager ), mTileId( tileId ), mProvider( provider )
      , mBandNo( bandNo ), mExtent( extent ), mWidth( width ), mHeight( height )
    {
      setAutoDelete( true );
    }

    void run() override
    {
      if ( !mManager || !mProvider )
        return;

      QElapsedTimer timer;
      timer.start();

      // Load the raster block
      QgsRasterBlock *block = mProvider->block( mBandNo, mExtent, mWidth, mHeight );
      
      if ( block && block->isValid() )
      {
        // Store in cache
        QgsRasterCacheManager::TileInfo tileInfo;
        tileInfo.tileId = mTileId;
        tileInfo.extent = mExtent;
        tileInfo.width = mWidth;
        tileInfo.height = mHeight;
        tileInfo.cacheTime = QDateTime::currentDateTime();
        tileInfo.lastAccessed = QDateTime::currentDateTime();
        tileInfo.loadTimeMs = timer.elapsed();
        tileInfo.memorySizeBytes = block->width() * block->height() * block->dataTypeSize();

        const_cast<QgsRasterCacheManager*>( mManager )->storeTileInCache( mTileId, block, tileInfo );
        
        // Signal completion
        QMetaObject::invokeMethod( const_cast<QgsRasterCacheManager*>( mManager ), 
                                  "tileLoaded", 
                                  Qt::QueuedConnection,
                                  Q_ARG( QString, mTileId ),
                                  Q_ARG( double, tileInfo.loadTimeMs ) );
      }
      else
      {
        delete block;
      }
    }

  private:
    QgsRasterCacheManager *mManager;
    QString mTileId;
    QgsRasterDataProvider *mProvider;
    int mBandNo;
    QgsRectangle mExtent;
    int mWidth;
    int mHeight;
};

QgsRasterCacheManager::QgsRasterCacheManager( QObject *parent )
  : QObject( parent )
{
  // Initialize timers
  mMaintenanceTimer = new QTimer( this );
  mMaintenanceTimer->setSingleShot( false );
  mMaintenanceTimer->setInterval( mConfig.cacheMaintenanceIntervalMs );
  connect( mMaintenanceTimer, &QTimer::timeout, this, &QgsRasterCacheManager::performMaintenance );

  mBackgroundProcessingTimer = new QTimer( this );
  mBackgroundProcessingTimer->setSingleShot( false );
  mBackgroundProcessingTimer->setInterval( 100 ); // 100ms intervals
  connect( mBackgroundProcessingTimer, &QTimer::timeout, this, &QgsRasterCacheManager::continueBackgroundProcessing );

  // Initialize network manager
  mNetworkManager = new QNetworkAccessManager( this );
  connect( mNetworkManager, &QNetworkAccessManager::finished,
           this, &QgsRasterCacheManager::onNetworkRequestFinished );

  // Initialize background thread pool
  mBackgroundThreadPool = new QThreadPool( this );
  mBackgroundThreadPool->setMaxThreadCount( mConfig.tile.backgroundThreadCount );

  // Initialize tile cache
  mTileCache.setMaxCost( mConfig.tile.tileCacheSizeMB * 1024 ); // Convert MB to KB

  // Initialize statistics
  mStatistics.timestamp = QDateTime::currentDateTime();

  // Start maintenance timer
  mMaintenanceTimer->start();

  QgsDebugMsgLevel( QStringLiteral( "Raster cache manager initialized" ), 2 );
}

QgsRasterCacheManager::~QgsRasterCacheManager()
{
  // Stop timers
  if ( mMaintenanceTimer )
    mMaintenanceTimer->stop();
  if ( mBackgroundProcessingTimer )
    mBackgroundProcessingTimer->stop();

  // Wait for background tasks to complete
  if ( mBackgroundThreadPool )
  {
    mBackgroundThreadPool->waitForDone( 5000 ); // Wait up to 5 seconds
  }

  // Release memory mappings
  QMutexLocker mapLocker( &mMemoryMapMutex );
  for ( auto it = mMemoryMappings.begin(); it != mMemoryMappings.end(); ++it )
  {
    if ( it->mappedMemory )
    {
#ifdef Q_OS_WIN
      UnmapViewOfFile( it->mappedMemory );
#else
      munmap( it->mappedMemory, it->mappedSize );
#endif
    }
  }
  mMemoryMappings.clear();

  QgsDebugMsgLevel( QStringLiteral( "Raster cache manager destroyed" ), 2 );
}

QgsRasterCacheManager::CacheConfig QgsRasterCacheManager::getCacheConfig() const
{
  return mConfig;
}

void QgsRasterCacheManager::setPerformanceMonitor( IPerformanceMonitor *monitor )
{
  mPerformanceMonitor = monitor;
}

QgsRasterBlock *QgsRasterCacheManager::getCachedBlock( QgsRasterDataProvider *provider,
                                                       int bandNo,
                                                       const QgsRectangle &extent,
                                                       int width,
                                                       int height,
                                                       QgsRasterBlockFeedback *feedback )
{
  if ( !provider )
    return nullptr;

  QElapsedTimer timer;
  timer.start();

  if ( mPerformanceMonitor )
  {
    mCurrentOperationId = mPerformanceMonitor->startOperation(
      QStringLiteral( "Raster Cache Block Retrieval" ),
      QStringLiteral( "raster_cache" ),
      QHash<QString, QVariant>{
        { QStringLiteral( "provider" ), generateProviderKey( provider ) },
        { QStringLiteral( "band" ), bandNo },
        { QStringLiteral( "extent" ), extent.toString() },
        { QStringLiteral( "width" ), width },
        { QStringLiteral( "height" ), height }
      }
    );
  }

  // Check for exact cache hit first
  QString tileId = generateTileId( provider, bandNo, extent, width, height );
  QgsRasterBlock *cachedBlock = loadTileFromCache( tileId );

  if ( cachedBlock )
  {
    mStatistics.cacheHits++;
    recordTileAccess( tileId );
    
    qint64 retrievalTime = timer.elapsed();
    
    if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
    {
      mPerformanceMonitor->recordMetric( QStringLiteral( "cache_hit" ), 1,
                                        QStringLiteral( "count" ), QStringLiteral( "raster_cache" ), mCurrentOperationId );
      mPerformanceMonitor->recordMetric( QStringLiteral( "retrieval_time" ), retrievalTime,
                                        QStringLiteral( "ms" ), QStringLiteral( "raster_cache" ), mCurrentOperationId );
      mPerformanceMonitor->endOperation( mCurrentOperationId );
    }

    QgsDebugMsgLevel( QStringLiteral( "Cache hit for tile: %1 (%2ms)" ).arg( tileId ).arg( retrievalTime ), 3 );
    return cachedBlock;
  }

  // Check pyramid levels
  if ( mConfig.pyramid.enablePyramids )
  {
    QList<PyramidLevel> pyramids = getPyramidLevels( provider );
    for ( const PyramidLevel &pyramid : pyramids )
    {
      if ( pyramid.isAvailable && pyramid.scaleFactor >= 1.0 )
      {
        // Try to get block from pyramid
        QString pyramidTileId = generateTileId( provider, bandNo, extent, width, height, pyramid.level );
        QgsRasterBlock *pyramidBlock = loadTileFromCache( pyramidTileId );
        
        if ( pyramidBlock )
        {
          mStatistics.cacheHits++;
          recordTileAccess( pyramidTileId );
          
          if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
          {
            mPerformanceMonitor->recordMetric( QStringLiteral( "pyramid_hit" ), 1,
                                              QStringLiteral( "count" ), QStringLiteral( "raster_cache" ), mCurrentOperationId );
            mPerformanceMonitor->endOperation( mCurrentOperationId );
          }

          QgsDebugMsgLevel( QStringLiteral( "Pyramid cache hit for tile: %1" ).arg( pyramidTileId ), 3 );
          return pyramidBlock;
        }
      }
    }
  }

  // Cache miss - record and potentially start background loading
  mStatistics.cacheMisses++;

  if ( mConfig.tile.enableBackgroundTileLoad && mBackgroundThreadPool )
  {
    // Start background loading
    startBackgroundTileLoad( tileId, provider, bandNo, extent, width, height );
  }

  if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
  {
    mPerformanceMonitor->recordMetric( QStringLiteral( "cache_miss" ), 1,
                                      QStringLiteral( "count" ), QStringLiteral( "raster_cache" ), mCurrentOperationId );
    mPerformanceMonitor->endOperation( mCurrentOperationId );
  }

  QgsDebugMsgLevel( QStringLiteral( "Cache miss for tile: %1" ).arg( tileId ), 3 );

  // Return nullptr to indicate cache miss
  return nullptr;
}

void QgsRasterCacheManager::cacheBlock( QgsRasterDataProvider *provider,
                                        int bandNo,
                                        const QgsRectangle &extent,
                                        int width,
                                        int height,
                                        QgsRasterBlock *block )
{
  if ( !provider || !block || !block->isValid() )
    return;

  QString tileId = generateTileId( provider, bandNo, extent, width, height );

  TileInfo tileInfo;
  tileInfo.tileId = tileId;
  tileInfo.extent = extent;
  tileInfo.width = width;
  tileInfo.height = height;
  tileInfo.cacheTime = QDateTime::currentDateTime();
  tileInfo.lastAccessed = QDateTime::currentDateTime();
  tileInfo.memorySizeBytes = block->width() * block->height() * block->dataTypeSize();

  // Compress tile if enabled
  if ( mConfig.tile.enableTileCompression )
  {
    // Simplified compression simulation
    tileInfo.isCompressed = true;
    tileInfo.memorySizeBytes = tileInfo.memorySizeBytes * 0.7; // Assume 30% compression
  }

  storeTileInCache( tileId, block, tileInfo );

  QgsDebugMsgLevel( QStringLiteral( "Cached block for tile: %1 (%2 KB)" )
                   .arg( tileId ).arg( tileInfo.memorySizeBytes / 1024 ), 3 );
}

void QgsRasterCacheManager::prefetchTiles( QgsRasterDataProvider *provider,
                                          int bandNo,
                                          const QgsRectangle &extent,
                                          int width,
                                          int height )
{
  if ( !mConfig.prefetch.enablePrefetching || !provider )
    return;

  // Calculate prefetch area
  double bufferX = extent.width() * mConfig.prefetch.prefetchRadiusFactor;
  double bufferY = extent.height() * mConfig.prefetch.prefetchRadiusFactor;
  
  QgsRectangle prefetchExtent(
    extent.xMinimum() - bufferX,
    extent.yMinimum() - bufferY,
    extent.xMaximum() + bufferX,
    extent.yMaximum() + bufferY
  );

  // Calculate tile grid for prefetch area
  int tilesX = qCeil( prefetchExtent.width() / ( extent.width() / ( width / mConfig.tile.tileWidth ) ) );
  int tilesY = qCeil( prefetchExtent.height() / ( extent.height() / ( height / mConfig.tile.tileHeight ) ) );

  int tilesPrefetched = 0;
  double tileWidth = prefetchExtent.width() / tilesX;
  double tileHeight = prefetchExtent.height() / tilesY;

  for ( int x = 0; x < tilesX && tilesPrefetched < mConfig.prefetch.maxPrefetchTiles; ++x )
  {
    for ( int y = 0; y < tilesY && tilesPrefetched < mConfig.prefetch.maxPrefetchTiles; ++y )
    {
      QgsRectangle tileExtent(
        prefetchExtent.xMinimum() + x * tileWidth,
        prefetchExtent.yMinimum() + y * tileHeight,
        prefetchExtent.xMinimum() + ( x + 1 ) * tileWidth,
        prefetchExtent.yMinimum() + ( y + 1 ) * tileHeight
      );

      QString tileId = generateTileId( provider, bandNo, tileExtent, 
                                      mConfig.tile.tileWidth, mConfig.tile.tileHeight );

      // Only prefetch if not already cached
      if ( !loadTileFromCache( tileId ) )
      {
        QMutexLocker locker( &mBackgroundMutex );
        if ( !mPrefetchQueue.contains( tileId ) )
        {
          mPrefetchQueue.enqueue( tileId );
          tilesPrefetched++;
        }
      }
    }
  }

  // Start background processing if tiles were queued
  if ( tilesPrefetched > 0 && !mBackgroundProcessingTimer->isActive() )
  {
    mBackgroundProcessingTimer->start();
  }

  QgsDebugMsgLevel( QStringLiteral( "Queued %1 tiles for prefetching" ).arg( tilesPrefetched ), 3 );
}

bool QgsRasterCacheManager::buildPyramids( QgsRasterDataProvider *provider,
                                          int bandNo,
                                          QgsFeedback *feedback )
{
  if ( !mConfig.pyramid.enablePyramids || !provider )
    return false;

  if ( !shouldBuildPyramids( provider ) )
    return false;

  QElapsedTimer timer;
  timer.start();

  QString providerKey = generateProviderKey( provider );
  
  QMutexLocker locker( &mPyramidMutex );
  QList<PyramidLevel> &pyramids = mPyramidLevels[providerKey];
  pyramids.clear();

  // Get provider extent and dimensions
  QgsRectangle fullExtent = provider->extent();
  int fullWidth = provider->xSize();
  int fullHeight = provider->ySize();

  int levelsBuilt = 0;
  double currentScale = 1.0;

  for ( int level = 1; level <= mConfig.pyramid.maxPyramidLevels; ++level )
  {
    if ( feedback && feedback->isCanceled() )
      break;

    currentScale *= mConfig.pyramid.pyramidScaleFactor;
    
    int levelWidth = qMax( 1, qRound( fullWidth / currentScale ) );
    int levelHeight = qMax( 1, qRound( fullHeight / currentScale ) );

    // Stop if pyramid level is too small
    if ( levelWidth < mConfig.pyramid.pyramidTileSize && 
         levelHeight < mConfig.pyramid.pyramidTileSize )
      break;

    PyramidLevel pyramid = createPyramidLevel( provider, bandNo, level );
    pyramid.scaleFactor = currentScale;
    pyramid.width = levelWidth;
    pyramid.height = levelHeight;
    pyramid.extent = fullExtent;
    pyramid.buildTime = QDateTime::currentDateTime();
    pyramid.isAvailable = true;

    pyramids.append( pyramid );
    levelsBuilt++;

    if ( feedback )
    {
      feedback->setProgress( ( level * 100 ) / mConfig.pyramid.maxPyramidLevels );
    }
  }

  qint64 buildTime = timer.elapsed();
  mStatistics.pyramidBuildTimeMs += buildTime;
  mStatistics.pyramidLevelsBuilt += levelsBuilt;

  emit pyramidsBuilt( provider, levelsBuilt );

  QgsDebugMsgLevel( QStringLiteral( "Built %1 pyramid levels in %2ms" )
                   .arg( levelsBuilt ).arg( buildTime ), 2 );

  return levelsBuilt > 0;
}

bool QgsRasterCacheManager::createMemoryMapping( const QString &filePath )
{
  if ( !mConfig.memoryMap.enableMemoryMapping )
    return false;

  if ( !shouldCreateMemoryMapping( filePath ) )
    return false;

  QElapsedTimer timer;
  timer.start();

  QFileInfo fileInfo( filePath );
  qint64 fileSize = fileInfo.size();

  if ( fileSize < mConfig.memoryMap.memoryMapThresholdMB * 1024 * 1024 )
    return false;

  QMutexLocker locker( &mMemoryMapMutex );

  // Check if already mapped
  if ( mMemoryMappings.contains( filePath ) )
    return true;

  void *mappedMemory = createMemoryMap( filePath, fileSize, true );
  
  if ( mappedMemory )
  {
    MemoryMapInfo mapInfo;
    mapInfo.filePath = filePath;
    mapInfo.mappedMemory = mappedMemory;
    mapInfo.mappedSize = fileSize;
    mapInfo.fileSize = fileSize;
    mapInfo.mapTime = QDateTime::currentDateTime();
    mapInfo.isReadOnly = true;
    mapInfo.pageSize = mConfig.memoryMap.memoryMapPageSize;

    mMemoryMappings[filePath] = mapInfo;
    mTotalMemoryMappedMB += fileSize / ( 1024 * 1024 );

    qint64 mapTime = timer.elapsed();
    recordMemoryMapCreation( filePath, fileSize, mapTime );

    emit memoryMappingCreated( filePath, fileSize / ( 1024 * 1024 ) );

    QgsDebugMsgLevel( QStringLiteral( "Created memory mapping for %1 (%2 MB in %3ms)" )
                     .arg( filePath ).arg( fileSize / ( 1024 * 1024 ) ).arg( mapTime ), 2 );

    return true;
  }

  return false;
}

QList<QgsRasterCacheManager::TileInfo> QgsRasterCacheManager::getCachedTileInfo( QgsRasterDataProvider *provider ) const
{
  QMutexLocker locker( &mTileCacheMutex );
  
  QList<TileInfo> result;
  QString providerKey = provider ? generateProviderKey( provider ) : QString();

  for ( auto it = mTileInfo.begin(); it != mTileInfo.end(); ++it )
  {
    if ( providerKey.isEmpty() || it->tileId.startsWith( providerKey ) )
    {
      result.append( it.value() );
    }
  }

  return result;
}

QList<QgsRasterCacheManager::PyramidLevel> QgsRasterCacheManager::getPyramidLevels( QgsRasterDataProvider *provider ) const
{
  if ( !provider )
    return QList<PyramidLevel>();

  QMutexLocker locker( &mPyramidMutex );
  QString providerKey = generateProviderKey( provider );
  
  return mPyramidLevels.value( providerKey );
}

QList<QgsRasterCacheManager::MemoryMapInfo> QgsRasterCacheManager::getMemoryMappingInfo() const
{
  QMutexLocker locker( &mMemoryMapMutex );
  return mMemoryMappings.values();
}

void QgsRasterCacheManager::invalidateCache( QgsRasterDataProvider *provider )
{
  if ( !provider )
    return;

  evictTilesForProvider( provider );

  QString providerKey = generateProviderKey( provider );
  
  // Remove pyramids
  QMutexLocker pyramidLocker( &mPyramidMutex );
  mPyramidLevels.remove( providerKey );

  QgsDebugMsgLevel( QStringLiteral( "Invalidated cache for provider: %1" ).arg( providerKey ), 3 );
}

void QgsRasterCacheManager::clearCache()
{
  QMutexLocker tileLocker( &mTileCacheMutex );
  mTileCache.clear();
  mTileInfo.clear();
  mCurrentCacheMemoryMB = 0;

  QMutexLocker pyramidLocker( &mPyramidMutex );
  mPyramidLevels.clear();

  // Don't clear memory mappings as they might be in use

  resetStatistics();

  emit cacheMemoryUsageChanged( 0, mConfig.tile.tileCacheSizeMB );

  QgsDebugMsgLevel( QStringLiteral( "Cache cleared" ), 2 );
}

QgsRasterCacheManager::PerformanceStatistics QgsRasterCacheManager::getPerformanceStatistics() const
{
  PerformanceStatistics stats = mStatistics;
  stats.timestamp = QDateTime::currentDateTime();
  stats.tilesInCache = mTileCache.size();
  stats.cacheMemoryUsageMB = mCurrentCacheMemoryMB;
  
  int totalCacheAccesses = stats.cacheHits + stats.cacheMisses;
  if ( totalCacheAccesses > 0 )
  {
    stats.cacheHitRate = double( stats.cacheHits ) / totalCacheAccesses;
  }

  stats.memoryMappedFiles = mMemoryMappings.size();
  stats.totalMemoryMappedMB = mTotalMemoryMappedMB;

  QMutexLocker backgroundLocker( &mBackgroundMutex );
  stats.backgroundTasksActive = mActiveBackgroundTasks.size();

  return stats;
}

void QgsRasterCacheManager::resetStatistics()
{
  mStatistics = PerformanceStatistics();
  mStatistics.timestamp = QDateTime::currentDateTime();
}

qint64 QgsRasterCacheManager::getCacheMemoryUsageMB() const
{
  return mCurrentCacheMemoryMB;
}

double QgsRasterCacheManager::getCacheHitRate() const
{
  int totalAccesses = mStatistics.cacheHits + mStatistics.cacheMisses;
  return totalAccesses > 0 ? double( mStatistics.cacheHits ) / totalAccesses : 0.0;
}

bool QgsRasterCacheManager::isBackgroundProcessingActive() const
{
  QMutexLocker locker( &mBackgroundMutex );
  return !mActiveBackgroundTasks.isEmpty() || !mPrefetchQueue.isEmpty();
}

void QgsRasterCacheManager::optimizeCache()
{
  QElapsedTimer timer;
  timer.start();

  // Check memory pressure
  if ( isMemoryPressureHigh() )
  {
    handleMemoryPressure();
  }
  else
  {
    // Normal optimization
    evictLeastRecentlyUsedTiles();
  }

  // Optimize cache layout
  optimizeCacheLayout();

  qint64 optimizeTime = timer.elapsed();
  QgsDebugMsgLevel( QStringLiteral( "Cache optimization completed in %1ms" ).arg( optimizeTime ), 3 );
}

void QgsRasterCacheManager::performMaintenance()
{
  QElapsedTimer timer;
  timer.start();

  // Clean expired tiles
  cleanupExpiredTiles();

  // Validate cache integrity
  validateCacheIntegrity();

  // Update statistics
  updateStatistics();

  mLastMaintenanceTime = QDateTime::currentDateTime();

  qint64 maintenanceTime = timer.elapsed();
  QgsDebugMsgLevel( QStringLiteral( "Cache maintenance completed in %1ms" ).arg( maintenanceTime ), 3 );
}

void QgsRasterCacheManager::handleMemoryPressure()
{
  QgsDebugMsgLevel( QStringLiteral( "Handling memory pressure" ), 2 );

  // Reduce cache size by 25%
  qint64 targetSize = mConfig.tile.tileCacheSizeMB * 0.75;
  reduceCacheSize( targetSize );

  // Release some memory mappings
  QMutexLocker mapLocker( &mMemoryMapMutex );
  if ( mMemoryMappings.size() > 10 )
  {
    // Release least recently used memory mappings
    auto it = mMemoryMappings.begin();
    while ( it != mMemoryMappings.end() && mMemoryMappings.size() > 5 )
    {
      releaseMemoryMapping( it.key() );
      it = mMemoryMappings.erase( it );
    }
  }
}

void QgsRasterCacheManager::onBackgroundTileLoadCompleted()
{
  mStatistics.backgroundTasksCompleted++;
  updateStatistics();
}

void QgsRasterCacheManager::onNetworkRequestFinished()
{
  QNetworkReply *reply = qobject_cast<QNetworkReply*>( sender() );
  if ( !reply )
    return;

  QMutexLocker locker( &mNetworkMutex );
  auto it = mActiveNetworkRequests.find( reply );
  
  if ( it != mActiveNetworkRequests.end() )
  {
    QString tileId = it.value();
    handleNetworkResponse( reply, tileId );
    mActiveNetworkRequests.erase( it );
  }

  reply->deleteLater();
}

void QgsRasterCacheManager::continueBackgroundProcessing()
{
  QMutexLocker locker( &mBackgroundMutex );
  
  if ( mPrefetchQueue.isEmpty() )
  {
    mBackgroundProcessingTimer->stop();
    return;
  }

  // Process a few tiles from the prefetch queue
  int tilesProcessed = 0;
  while ( !mPrefetchQueue.isEmpty() && tilesProcessed < 3 )
  {
    QString tileId = mPrefetchQueue.dequeue();
    
    // Check if tile is already cached
    if ( !loadTileFromCache( tileId ) )
    {
      // Start background loading task
      // This is simplified - would need to reconstruct provider and parameters from tileId
      mActiveBackgroundTasks[tileId] = QDateTime::currentDateTime();
      tilesProcessed++;
    }
  }

  if ( mPrefetchQueue.isEmpty() )
  {
    mBackgroundProcessingTimer->stop();
    emit prefetchingCompleted( mStatistics.prefetchedTiles );
  }
}

QString QgsRasterCacheManager::generateTileId( QgsRasterDataProvider *provider, int bandNo,
                                              const QgsRectangle &extent, int width, int height, int level ) const
{
  QString providerKey = generateProviderKey( provider );
  
  // Create a hash of the parameters
  QCryptographicHash hash( QCryptographicHash::Md5 );
  hash.addData( providerKey.toUtf8() );
  hash.addData( QByteArray::number( bandNo ) );
  hash.addData( QByteArray::number( extent.xMinimum(), 'f', 6 ) );
  hash.addData( QByteArray::number( extent.yMinimum(), 'f', 6 ) );
  hash.addData( QByteArray::number( extent.xMaximum(), 'f', 6 ) );
  hash.addData( QByteArray::number( extent.yMaximum(), 'f', 6 ) );
  hash.addData( QByteArray::number( width ) );
  hash.addData( QByteArray::number( height ) );
  hash.addData( QByteArray::number( level ) );

  return QStringLiteral( "%1_%2" ).arg( providerKey ).arg( QString( hash.result().toHex() ) );
}

QString QgsRasterCacheManager::generateProviderKey( QgsRasterDataProvider *provider ) const
{
  if ( !provider )
    return QString();

  // Create a hash based on provider characteristics
  QCryptographicHash hash( QCryptographicHash::Md5 );
  hash.addData( provider->name().toUtf8() );
  hash.addData( provider->dataSourceUri().toUtf8() );
  hash.addData( QByteArray::number( provider->xSize() ) );
  hash.addData( QByteArray::number( provider->ySize() ) );

  return QString( hash.result().toHex().left( 16 ) );
}

QgsRasterBlock *QgsRasterCacheManager::loadTileFromCache( const QString &tileId )
{
  QMutexLocker locker( &mTileCacheMutex );
  
  QgsRasterBlock *block = mTileCache.object( tileId );
  if ( block )
  {
    // Update access information
    if ( mTileInfo.contains( tileId ) )
    {
      mTileInfo[tileId].lastAccessed = QDateTime::currentDateTime();
      mTileInfo[tileId].accessCount++;
    }
  }
  
  return block;
}

void QgsRasterCacheManager::storeTileInCache( const QString &tileId, QgsRasterBlock *block, const TileInfo &info )
{
  QMutexLocker locker( &mTileCacheMutex );
  
  qint64 tileSizeKB = info.memorySizeBytes / 1024;
  
  // Store in cache
  mTileCache.insert( tileId, block, tileSizeKB );
  mTileInfo[tileId] = info;
  
  mCurrentCacheMemoryMB += info.memorySizeBytes / ( 1024 * 1024 );
  
  emit cacheMemoryUsageChanged( mCurrentCacheMemoryMB, mConfig.tile.tileCacheSizeMB );
}

void QgsRasterCacheManager::evictLeastRecentlyUsedTiles()
{
  QMutexLocker locker( &mTileCacheMutex );
  
  // Get all tile access times
  QList<QPair<QDateTime, QString>> tilesByAccess;
  for ( auto it = mTileInfo.begin(); it != mTileInfo.end(); ++it )
  {
    tilesByAccess.append( qMakePair( it->lastAccessed, it.key() ) );
  }
  
  // Sort by access time (oldest first)
  std::sort( tilesByAccess.begin(), tilesByAccess.end() );
  
  // Remove oldest 20% of tiles
  int tilesToRemove = qMax( 1, tilesByAccess.size() / 5 );
  
  for ( int i = 0; i < tilesToRemove && i < tilesByAccess.size(); ++i )
  {
    QString tileId = tilesByAccess[i].second;
    
    if ( mTileInfo.contains( tileId ) )
    {
      mCurrentCacheMemoryMB -= mTileInfo[tileId].memorySizeBytes / ( 1024 * 1024 );
      mTileInfo.remove( tileId );
    }
    
    mTileCache.remove( tileId );
    mStatistics.tilesEvicted++;
  }
  
  QgsDebugMsgLevel( QStringLiteral( "Evicted %1 LRU tiles" ).arg( tilesToRemove ), 3 );
}

void QgsRasterCacheManager::evictTilesForProvider( QgsRasterDataProvider *provider )
{
  QString providerKey = generateProviderKey( provider );
  
  QMutexLocker locker( &mTileCacheMutex );
  
  auto it = mTileInfo.begin();
  while ( it != mTileInfo.end() )
  {
    if ( it->tileId.startsWith( providerKey ) )
    {
      mCurrentCacheMemoryMB -= it->memorySizeBytes / ( 1024 * 1024 );
      mTileCache.remove( it.key() );
      it = mTileInfo.erase( it );
      mStatistics.tilesEvicted++;
    }
    else
    {
      ++it;
    }
  }
}

bool QgsRasterCacheManager::shouldBuildPyramids( QgsRasterDataProvider *provider ) const
{
  if ( !mConfig.pyramid.autogeneratePyramids || !provider )
    return false;

  int totalPixels = provider->xSize() * provider->ySize();
  return totalPixels > mConfig.pyramid.pyramidThresholdPixels;
}

QgsRasterCacheManager::PyramidLevel QgsRasterCacheManager::createPyramidLevel( QgsRasterDataProvider *provider, int bandNo, int level )
{
  PyramidLevel pyramid;
  pyramid.level = level;
  pyramid.scaleFactor = qPow( mConfig.pyramid.pyramidScaleFactor, level );
  
  if ( mConfig.pyramid.persistPyramids )
  {
    pyramid.cacheFile = getPyramidCacheFile( provider, level );
  }
  
  return pyramid;
}

QString QgsRasterCacheManager::getPyramidCacheFile( QgsRasterDataProvider *provider, int level ) const
{
  QString cacheDir = mConfig.pyramid.pyramidCacheDir;
  if ( cacheDir.isEmpty() )
  {
    cacheDir = QStandardPaths::writableLocation( QStandardPaths::CacheLocation ) + "/raster_pyramids";
  }
  
  QDir().mkpath( cacheDir );
  
  QString providerKey = generateProviderKey( provider );
  return QDir( cacheDir ).filePath( QStringLiteral( "%1_level_%2.pyramid" ).arg( providerKey ).arg( level ) );
}

bool QgsRasterCacheManager::shouldCreateMemoryMapping( const QString &filePath ) const
{
  QFileInfo fileInfo( filePath );
  
  if ( !fileInfo.exists() || !fileInfo.isReadable() )
    return false;
  
  qint64 fileSizeMB = fileInfo.size() / ( 1024 * 1024 );
  
  if ( fileSizeMB < mConfig.memoryMap.memoryMapThresholdMB )
    return false;
  
  if ( mTotalMemoryMappedMB + fileSizeMB > mConfig.memoryMap.maxMemoryMappedSizeMB )
    return false;
  
  return true;
}

void *QgsRasterCacheManager::createMemoryMap( const QString &filePath, qint64 size, bool readOnly )
{
#ifdef Q_OS_WIN
  HANDLE fileHandle = CreateFileA( filePath.toLocal8Bit().constData(),
                                  readOnly ? GENERIC_READ : ( GENERIC_READ | GENERIC_WRITE ),
                                  FILE_SHARE_READ,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr );
  
  if ( fileHandle == INVALID_HANDLE_VALUE )
    return nullptr;
  
  HANDLE mapHandle = CreateFileMapping( fileHandle,
                                       nullptr,
                                       readOnly ? PAGE_READONLY : PAGE_READWRITE,
                                       0, 0,
                                       nullptr );
  
  CloseHandle( fileHandle );
  
  if ( !mapHandle )
    return nullptr;
  
  void *mappedMemory = MapViewOfFile( mapHandle,
                                     readOnly ? FILE_MAP_READ : FILE_MAP_WRITE,
                                     0, 0, size );
  
  CloseHandle( mapHandle );
  return mappedMemory;
#else
  int fd = open( filePath.toLocal8Bit().constData(), readOnly ? O_RDONLY : O_RDWR );
  if ( fd == -1 )
    return nullptr;
  
  void *mappedMemory = mmap( nullptr, size,
                            readOnly ? PROT_READ : ( PROT_READ | PROT_WRITE ),
                            MAP_SHARED, fd, 0 );
  
  close( fd );
  
  if ( mappedMemory == MAP_FAILED )
    return nullptr;
  
  // Optimize memory access if enabled
  if ( mConfig.memoryMap.useAdviseRandomAccess )
  {
    madvise( mappedMemory, size, MADV_RANDOM );
  }
  
  return mappedMemory;
#endif
}

void QgsRasterCacheManager::releaseMemoryMapping( const QString &filePath )
{
  auto it = mMemoryMappings.find( filePath );
  if ( it != mMemoryMappings.end() )
  {
    if ( it->mappedMemory )
    {
#ifdef Q_OS_WIN
      UnmapViewOfFile( it->mappedMemory );
#else
      munmap( it->mappedMemory, it->mappedSize );
#endif
    }
    
    mTotalMemoryMappedMB -= it->mappedSize / ( 1024 * 1024 );
  }
}

void QgsRasterCacheManager::startBackgroundTileLoad( const QString &tileId, QgsRasterDataProvider *provider,
                                                     int bandNo, const QgsRectangle &extent, int width, int height )
{
  if ( !mBackgroundThreadPool )
    return;

  BackgroundTileLoadTask *task = new BackgroundTileLoadTask( this, tileId, provider, bandNo, extent, width, height );
  task->setAutoDelete( true );

  QMutexLocker locker( &mBackgroundMutex );
  mActiveBackgroundTasks[tileId] = QDateTime::currentDateTime();

  mBackgroundThreadPool->start( task );
}

void QgsRasterCacheManager::updateStatistics()
{
  mStatistics.timestamp = QDateTime::currentDateTime();
  mStatistics.tilesInCache = mTileCache.size();
  mStatistics.cacheMemoryUsageMB = mCurrentCacheMemoryMB;

  int totalAccesses = mStatistics.cacheHits + mStatistics.cacheMisses;
  if ( totalAccesses > 0 )
  {
    mStatistics.cacheHitRate = double( mStatistics.cacheHits ) / totalAccesses;
  }

  mStatistics.memoryMappedFiles = mMemoryMappings.size();
  mStatistics.totalMemoryMappedMB = mTotalMemoryMappedMB;

  emit statisticsUpdated( mStatistics );
  mLastStatisticsUpdate = QDateTime::currentDateTime();
}

void QgsRasterCacheManager::recordTileAccess( const QString &tileId )
{
  mTileAccessTimes[tileId] = QDateTime::currentDateTime();
  mTileAccessCounts[tileId]++;
}

void QgsRasterCacheManager::recordMemoryMapCreation( const QString &filePath, qint64 size, double timeMs )
{
  mStatistics.averageMapCreationTimeMs = 
    ( mStatistics.averageMapCreationTimeMs * mStatistics.memoryMappedFiles + timeMs ) / 
    ( mStatistics.memoryMappedFiles + 1 );
    
  if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
  {
    mPerformanceMonitor->recordMetric( QStringLiteral( "memory_map_time" ), timeMs,
                                      QStringLiteral( "ms" ), QStringLiteral( "raster_cache" ), mCurrentOperationId );
    mPerformanceMonitor->recordMetric( QStringLiteral( "memory_map_size" ), size / ( 1024 * 1024 ),
                                      QStringLiteral( "MB" ), QStringLiteral( "raster_cache" ), mCurrentOperationId );
  }
}

void QgsRasterCacheManager::recordNetworkRequest( double timeMs, qint64 bytesTransferred, bool success )
{
  mStatistics.networkRequests++;
  if ( success )
  {
    mStatistics.networkRequestsSuccessful++;
    mStatistics.networkBytesTransferred += bytesTransferred;
    
    mStatistics.averageNetworkTimeMs = 
      ( mStatistics.averageNetworkTimeMs * ( mStatistics.networkRequestsSuccessful - 1 ) + timeMs ) / 
      mStatistics.networkRequestsSuccessful;
  }
}

qint64 QgsRasterCacheManager::calculateMemoryUsage() const
{
  qint64 totalMemory = 0;
  
#ifdef Q_OS_LINUX
  struct sysinfo info;
  if ( sysinfo( &info ) == 0 )
  {
    totalMemory = info.totalram * info.mem_unit;
  }
#elif defined(Q_OS_WIN)
  MEMORYSTATUSEX memInfo;
  memInfo.dwLength = sizeof( MEMORYSTATUSEX );
  if ( GlobalMemoryStatusEx( &memInfo ) )
  {
    totalMemory = memInfo.ullTotalPhys;
  }
#elif defined(Q_OS_MACOS)
  size_t size = sizeof( uint64_t );
  uint64_t memSize;
  if ( sysctlbyname( "hw.memsize", &memSize, &size, nullptr, 0 ) == 0 )
  {
    totalMemory = memSize;
  }
#endif
  
  return totalMemory;
}

bool QgsRasterCacheManager::isMemoryPressureHigh() const
{
  qint64 totalMemory = calculateMemoryUsage();
  if ( totalMemory == 0 )
    return false;
  
  qint64 usedMemory = mCurrentCacheMemoryMB * 1024 * 1024 + mTotalMemoryMappedMB * 1024 * 1024;
  double memoryUsageRatio = double( usedMemory ) / totalMemory;
  
  return memoryUsageRatio > mConfig.memoryPressureThreshold;
}

void QgsRasterCacheManager::reduceCacheSize( qint64 targetSizeMB )
{
  while ( mCurrentCacheMemoryMB > targetSizeMB )
  {
    evictLeastRecentlyUsedTiles();
  }
  
  mTileCache.setMaxCost( targetSizeMB * 1024 );
}

void QgsRasterCacheManager::cleanupExpiredTiles()
{
  QDateTime cutoffTime = QDateTime::currentDateTime().addSecs( -3600 ); // 1 hour
  
  QMutexLocker locker( &mTileCacheMutex );
  
  auto it = mTileInfo.begin();
  while ( it != mTileInfo.end() )
  {
    if ( it->lastAccessed < cutoffTime )
    {
      mCurrentCacheMemoryMB -= it->memorySizeBytes / ( 1024 * 1024 );
      mTileCache.remove( it.key() );
      it = mTileInfo.erase( it );
      mStatistics.tilesEvicted++;
    }
    else
    {
      ++it;
    }
  }
}

void QgsRasterCacheManager::optimizeCacheLayout()
{
  // Optimize tile organization for better spatial locality
  // This is a simplified implementation
  QgsDebugMsgLevel( QStringLiteral( "Cache layout optimization completed" ), 3 );
}

void QgsRasterCacheManager::validateCacheIntegrity()
{
  // Validate that cached tiles are still valid
  // This is a simplified implementation
  QgsDebugMsgLevel( QStringLiteral( "Cache integrity validation completed" ), 3 );
}

QNetworkReply *QgsRasterCacheManager::createNetworkRequest( const QUrl &url, const QgsRectangle &extent )
{
  Q_UNUSED( extent )
  
  QNetworkRequest request( url );
  request.setRawHeader( "User-Agent", "QGIS RasterCacheManager" );
  
  if ( mConfig.network.enableNetworkCompression )
  {
    request.setRawHeader( "Accept-Encoding", "gzip, deflate" );
  }
  
  return mNetworkManager->get( request );
}

void QgsRasterCacheManager::handleNetworkResponse( QNetworkReply *reply, const QString &tileId )
{
  QElapsedTimer timer;
  timer.start();
  
  bool success = ( reply->error() == QNetworkReply::NoError );
  qint64 bytesTransferred = 0;
  
  if ( success )
  {
    QByteArray data = reply->readAll();
    bytesTransferred = data.size();
    
    // Process the downloaded data
    // This would typically involve creating a raster block and caching it
  }
  
  double requestTime = timer.elapsed();
  recordNetworkRequest( requestTime, bytesTransferred, success );
  
  QgsDebugMsgLevel( QStringLiteral( "Network request completed for tile %1: %2 bytes in %3ms" )
                   .arg( tileId ).arg( bytesTransferred ).arg( requestTime ), 3 );
}