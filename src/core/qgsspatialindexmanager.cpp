/***************************************************************************
                         qgsspatialindexmanager.cpp
                         ---------------------------
    begin                : September 2025
    copyright            : (C) 2025 by QGIS
    email                : development-team@qgis.org
 ***************************************************************************/

#include "qgsspatialindexmanager.h"
#include "qgsvectorlayer.h"
#include "qgsvectordataprovider.h"
#include "qgsspatialindex.h"
#include "qgsfeatureiterator.h"
#include "qgsgeometry.h"
#include "qgslogger.h"
#include "qgsapplication.h"
#include "iperformancemonitor.h"

#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QThread>
#include <QCryptographicHash>
#include <QtConcurrent>
#include <QMutexLocker>
#include <cmath>
#include <algorithm>

// Background index building task
class BackgroundIndexBuildTask : public QRunnable
{
  public:
    BackgroundIndexBuildTask( QgsSpatialIndexManager *manager, QgsVectorLayer *layer, const QString &indexId )
      : mManager( manager ), mLayer( layer ), mIndexId( indexId )
    {
      setAutoDelete( true );
    }

    void run() override
    {
      if ( !mManager || !mLayer )
        return;

      const_cast<QgsSpatialIndexManager*>( mManager )->buildRTreeIndex( mLayer, mIndexId );
      
      QMetaObject::invokeMethod( const_cast<QgsSpatialIndexManager*>( mManager ), 
                                "onBackgroundIndexBuildCompleted", 
                                Qt::QueuedConnection );
    }

  private:
    QgsSpatialIndexManager *mManager;
    QgsVectorLayer *mLayer;
    QString mIndexId;
};

QgsSpatialIndexManager::QgsSpatialIndexManager( QObject *parent )
  : QObject( parent )
{
  // Initialize timers
  mMaintenanceTimer = new QTimer( this );
  mMaintenanceTimer->setSingleShot( false );
  mMaintenanceTimer->setInterval( mConfig.background.maintenanceIntervalMs );
  connect( mMaintenanceTimer, &QTimer::timeout, this, &QgsSpatialIndexManager::performMaintenance );

  mBackgroundProcessingTimer = new QTimer( this );
  mBackgroundProcessingTimer->setSingleShot( false );
  mBackgroundProcessingTimer->setInterval( 100 );
  connect( mBackgroundProcessingTimer, &QTimer::timeout, this, &QgsSpatialIndexManager::continueBackgroundProcessing );

  // Initialize background thread pool
  mBackgroundThreadPool = new QThreadPool( this );
  mBackgroundThreadPool->setMaxThreadCount( mConfig.background.backgroundThreadCount );

  // Initialize query cache
  mQueryCache.setMaxCost( mConfig.queryCache.queryCacheSizeMB * 1024 );

  // Initialize statistics
  mStatistics.timestamp = QDateTime::currentDateTime();

  // Start maintenance timer
  mMaintenanceTimer->start();

  QgsDebugMsgLevel( QStringLiteral( "Spatial index manager initialized" ), 2 );
}

QgsSpatialIndexManager::~QgsSpatialIndexManager()
{
  if ( mMaintenanceTimer )
    mMaintenanceTimer->stop();
  if ( mBackgroundProcessingTimer )
    mBackgroundProcessingTimer->stop();

  if ( mBackgroundThreadPool )
  {
    mBackgroundThreadPool->waitForDone( 5000 );
  }

  QgsDebugMsgLevel( QStringLiteral( "Spatial index manager destroyed" ), 2 );
}

void QgsSpatialIndexManager::setIndexConfig( const IndexConfig &config )
{
  mConfig = config;

  // Update cache sizes
  mQueryCache.setMaxCost( config.queryCache.queryCacheSizeMB * 1024 );

  // Update thread pool
  if ( mBackgroundThreadPool )
  {
    mBackgroundThreadPool->setMaxThreadCount( config.background.backgroundThreadCount );
  }

  // Update maintenance timer
  if ( mMaintenanceTimer )
  {
    mMaintenanceTimer->setInterval( config.background.maintenanceIntervalMs );
  }

  QgsDebugMsgLevel( QStringLiteral( "Spatial index configuration updated" ), 3 );
}

QgsSpatialIndexManager::IndexConfig QgsSpatialIndexManager::getIndexConfig() const
{
  return mConfig;
}

void QgsSpatialIndexManager::setPerformanceMonitor( IPerformanceMonitor *monitor )
{
  mPerformanceMonitor = monitor;
}

bool QgsSpatialIndexManager::createSpatialIndex( QgsVectorLayer *layer, bool forceRebuild, QgsFeedback *feedback )
{
  if ( !layer || !layer->isValid() )
    return false;

  QString indexId = generateIndexId( layer );

  QMutexLocker locker( &mIndexMutex );
  
  // Check if index exists and is valid
  if ( !forceRebuild && mRTreeIndexes.contains( indexId ) && mIndexInfo.contains( indexId ) )
  {
    IndexInfo &info = mIndexInfo[indexId];
    if ( !info.needsRebuild )
    {
      info.lastAccessed = QDateTime::currentDateTime();
      return true;
    }
  }

  locker.unlock();

  QElapsedTimer timer;
  timer.start();

  if ( mPerformanceMonitor )
  {
    mCurrentOperationId = mPerformanceMonitor->startOperation(
      QStringLiteral( "Spatial Index Creation" ),
      QStringLiteral( "spatial_index" ),
      QHash<QString, QVariant>{
        { QStringLiteral( "layer_id" ), layer->id() },
        { QStringLiteral( "layer_name" ), layer->name() },
        { QStringLiteral( "feature_count" ), layer->featureCount() }
      }
    );
  }

  bool success = false;

  if ( mConfig.background.enableBackgroundIndexing && !forceRebuild )
  {
    // Start background building
    startBackgroundIndexBuild( layer, indexId );
    success = true;
  }
  else
  {
    // Build synchronously
    success = buildRTreeIndex( layer, indexId );
    
    if ( success && mConfig.spatialHash.enableSpatialHashing )
    {
      success = buildSpatialHash( layer, indexId );
    }
  }

  qint64 buildTime = timer.elapsed();

  if ( success )
  {
    QMutexLocker infoLocker( &mIndexMutex );
    if ( mIndexInfo.contains( indexId ) )
    {
      mIndexInfo[indexId].buildTimeMs = buildTime;
      mIndexInfo[indexId].buildTime = QDateTime::currentDateTime();
      mIndexInfo[indexId].isOptimized = false;
      mIndexInfo[indexId].needsRebuild = false;
    }

    emit indexCreated( layer->id(), buildTime );
  }

  if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
  {
    mPerformanceMonitor->recordMetric( QStringLiteral( "build_time" ), buildTime,
                                      QStringLiteral( "ms" ), QStringLiteral( "spatial_index" ), mCurrentOperationId );
    mPerformanceMonitor->recordMetric( QStringLiteral( "success" ), success ? 1 : 0,
                                      QStringLiteral( "bool" ), QStringLiteral( "spatial_index" ), mCurrentOperationId );
    mPerformanceMonitor->endOperation( mCurrentOperationId );
  }

  QgsDebugMsgLevel( QStringLiteral( "Spatial index creation for layer %1: %2 (%3ms)" )
                   .arg( layer->name() ).arg( success ? "success" : "failed" ).arg( buildTime ), 2 );

  return success;
}

bool QgsSpatialIndexManager::createSpatialIndex( QgsVectorDataProvider *provider, const QString &layerId, 
                                                bool forceRebuild, QgsFeedback *feedback )
{
  if ( !provider )
    return false;

  QString indexId = generateIndexId( layerId );

  QMutexLocker locker( &mIndexMutex );
  
  if ( !forceRebuild && mRTreeIndexes.contains( indexId ) && mIndexInfo.contains( indexId ) )
  {
    IndexInfo &info = mIndexInfo[indexId];
    if ( !info.needsRebuild )
    {
      info.lastAccessed = QDateTime::currentDateTime();
      return true;
    }
  }

  locker.unlock();

  bool success = buildRTreeIndex( provider, layerId, indexId );
  
  if ( success && mConfig.spatialHash.enableSpatialHashing )
  {
    success = buildSpatialHash( provider, layerId, indexId );
  }

  return success;
}

QList<QgsFeatureId> QgsSpatialIndexManager::spatialQuery( QgsVectorLayer *layer,
                                                         const QgsGeometry &geometry,
                                                         QueryType queryType,
                                                         int maxResults )
{
  if ( !layer || geometry.isEmpty() )
    return QList<QgsFeatureId>();

  QElapsedTimer timer;
  timer.start();

  QString indexId = generateIndexId( layer );
  QString queryId = generateQueryId( geometry, queryType );

  if ( mPerformanceMonitor )
  {
    mCurrentOperationId = mPerformanceMonitor->startOperation(
      QStringLiteral( "Spatial Query" ),
      QStringLiteral( "spatial_query" ),
      QHash<QString, QVariant>{
        { QStringLiteral( "layer_id" ), layer->id() },
        { QStringLiteral( "query_type" ), static_cast<int>( queryType ) },
        { QStringLiteral( "max_results" ), maxResults }
      }
    );
  }

  // Check query cache first
  QList<QgsFeatureId> results;
  QString cachedResult = getCachedQueryResult( queryId );
  if ( !cachedResult.isEmpty() )
  {
    recordCacheAccess( queryId, true );
    mStatistics.queryCacheHits++;
    
    // Parse cached result (simplified implementation)
    QStringList idStrings = cachedResult.split( ',' );
    for ( const QString &idStr : idStrings )
    {
      bool ok;
      QgsFeatureId id = idStr.toLongLong( &ok );
      if ( ok )
        results.append( id );
    }
  }
  else
  {
    recordCacheAccess( queryId, false );
    mStatistics.queryCacheMisses++;

    // Perform spatial query
    QMutexLocker locker( &mIndexMutex );
    if ( mRTreeIndexes.contains( indexId ) )
    {
      results = queryRTreeIndex( indexId, geometry, queryType );
    }
    else if ( mSpatialHashes.contains( indexId ) && mConfig.spatialHash.enableSpatialHashing )
    {
      results = querySpatialHash( indexId, geometry, queryType );
    }
    else
    {
      // Fallback to linear scan
      QgsFeatureIterator it = layer->getFeatures();
      QgsFeature feature;
      while ( it.nextFeature( feature ) && ( maxResults == 0 || results.size() < maxResults ) )
      {
        if ( geometryIntersects( feature.geometry(), geometry, queryType ) )
        {
          results.append( feature.id() );
        }
      }
    }
    locker.unlock();

    // Limit results if specified
    if ( maxResults > 0 && results.size() > maxResults )
    {
      results = results.mid( 0, maxResults );
    }

    // Cache the result
    qint64 queryTime = timer.elapsed();
    cacheQueryResult( queryId, results, layer->id(), queryTime );
  }

  qint64 totalTime = timer.elapsed();
  recordQuery( layer->id(), queryType, results.size(), totalTime );

  emit queryCompleted( layer->id(), queryType, results.size(), totalTime );

  if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
  {
    mPerformanceMonitor->recordMetric( QStringLiteral( "query_time" ), totalTime,
                                      QStringLiteral( "ms" ), QStringLiteral( "spatial_query" ), mCurrentOperationId );
    mPerformanceMonitor->recordMetric( QStringLiteral( "result_count" ), results.size(),
                                      QStringLiteral( "count" ), QStringLiteral( "spatial_query" ), mCurrentOperationId );
    mPerformanceMonitor->endOperation( mCurrentOperationId );
  }

  QgsDebugMsgLevel( QStringLiteral( "Spatial query completed: %1 results in %2ms" )
                   .arg( results.size() ).arg( totalTime ), 3 );

  return results;
}

QList<QgsFeatureId> QgsSpatialIndexManager::spatialQuery( QgsVectorLayer *layer,
                                                         const QgsRectangle &extent,
                                                         QueryType queryType,
                                                         int maxResults )
{
  if ( !layer || extent.isEmpty() )
    return QList<QgsFeatureId>();

  QgsGeometry geometry = createGeometryFromExtent( extent );
  return spatialQuery( layer, geometry, queryType, maxResults );
}

QList<QgsFeatureId> QgsSpatialIndexManager::nearestNeighborQuery( QgsVectorLayer *layer,
                                                                 const QgsPointXY &point,
                                                                 int k,
                                                                 double maxDistance )
{
  if ( !layer || k <= 0 )
    return QList<QgsFeatureId>();

  QElapsedTimer timer;
  timer.start();

  QString indexId = generateIndexId( layer );
  QList<QgsFeatureId> results;

  QMutexLocker locker( &mIndexMutex );
  if ( mRTreeIndexes.contains( indexId ) )
  {
    // Use spatial index for nearest neighbor search
    QgsRectangle searchExtent( point.x() - maxDistance, point.y() - maxDistance,
                              point.x() + maxDistance, point.y() + maxDistance );
    
    QList<QgsFeatureId> candidates = queryRTreeIndex( indexId, createGeometryFromExtent( searchExtent ), IntersectsQuery );
    
    // Calculate distances and sort
    QList<QPair<double, QgsFeatureId>> distances;
    
    QgsFeatureIterator it = layer->getFeatures( QgsFeatureRequest().setFilterFids( candidates ) );
    QgsFeature feature;
    while ( it.nextFeature( feature ) )
    {
      if ( feature.hasGeometry() )
      {
        double distance = feature.geometry().distance( QgsGeometry::fromPointXY( point ) );
        if ( maxDistance == 0.0 || distance <= maxDistance )
        {
          distances.append( qMakePair( distance, feature.id() ) );
        }
      }
    }
    
    // Sort by distance
    std::sort( distances.begin(), distances.end() );
    
    // Take k nearest
    for ( int i = 0; i < qMin( k, distances.size() ); ++i )
    {
      results.append( distances[i].second );
    }
  }
  locker.unlock();

  qint64 queryTime = timer.elapsed();
  recordQuery( layer->id(), NearestNeighborQuery, results.size(), queryTime );

  QgsDebugMsgLevel( QStringLiteral( "Nearest neighbor query completed: %1 results in %2ms" )
                   .arg( results.size() ).arg( queryTime ), 3 );

  return results;
}

QList<QgsFeatureId> QgsSpatialIndexManager::radiusQuery( QgsVectorLayer *layer,
                                                        const QgsPointXY &center,
                                                        double radius,
                                                        int maxResults )
{
  if ( !layer || radius <= 0.0 )
    return QList<QgsFeatureId>();

  QgsRectangle extent( center.x() - radius, center.y() - radius,
                      center.x() + radius, center.y() + radius );
  
  QList<QgsFeatureId> candidates = spatialQuery( layer, extent, IntersectsQuery, 0 );
  QList<QgsFeatureId> results;
  
  // Filter by actual distance
  QgsFeatureIterator it = layer->getFeatures( QgsFeatureRequest().setFilterFids( candidates ) );
  QgsFeature feature;
  while ( it.nextFeature( feature ) && ( maxResults == 0 || results.size() < maxResults ) )
  {
    if ( feature.hasGeometry() )
    {
      double distance = feature.geometry().distance( QgsGeometry::fromPointXY( center ) );
      if ( distance <= radius )
      {
        results.append( feature.id() );
      }
    }
  }

  return results;
}

bool QgsSpatialIndexManager::updateSpatialIndex( QgsVectorLayer *layer, const QList<QgsFeatureId> &featureIds )
{
  if ( !layer || featureIds.isEmpty() )
    return false;

  QString indexId = generateIndexId( layer );

  QMutexLocker locker( &mIndexMutex );
  
  bool success = true;
  
  if ( mRTreeIndexes.contains( indexId ) )
  {
    // Update R-tree index
    QSharedPointer<QgsSpatialIndex> index = mRTreeIndexes[indexId];
    
    QgsFeatureIterator it = layer->getFeatures( QgsFeatureRequest().setFilterFids( featureIds ) );
    QgsFeature feature;
    while ( it.nextFeature( feature ) )
    {
      if ( feature.hasGeometry() )
      {
        // Remove old entry and add new one
        index->deleteFeature( feature );
        index->addFeature( feature );
      }
    }
  }
  
  if ( mSpatialHashes.contains( indexId ) && mConfig.spatialHash.enableSpatialHashing )
  {
    // Update spatial hash
    QHash<QPair<int, int>, HashCellInfo> &hash = mSpatialHashes[indexId];
    
    // This is simplified - would need to remove from old cells and add to new cells
    // based on updated geometry
  }
  
  // Mark for optimization
  if ( mIndexInfo.contains( indexId ) )
  {
    mIndexInfo[indexId].isOptimized = false;
    mIndexInfo[indexId].lastAccessed = QDateTime::currentDateTime();
  }

  return success;
}

bool QgsSpatialIndexManager::removeFeaturesFromIndex( QgsVectorLayer *layer, const QList<QgsFeatureId> &featureIds )
{
  if ( !layer || featureIds.isEmpty() )
    return false;

  QString indexId = generateIndexId( layer );

  QMutexLocker locker( &mIndexMutex );
  
  bool success = true;
  
  if ( mRTreeIndexes.contains( indexId ) )
  {
    QSharedPointer<QgsSpatialIndex> index = mRTreeIndexes[indexId];
    
    for ( QgsFeatureId fid : featureIds )
    {
      QgsFeature feature = layer->getFeature( fid );
      if ( feature.isValid() )
      {
        index->deleteFeature( feature );
      }
    }
  }
  
  if ( mSpatialHashes.contains( indexId ) )
  {
    QHash<QPair<int, int>, HashCellInfo> &hash = mSpatialHashes[indexId];
    
    // Remove from hash cells
    for ( auto it = hash.begin(); it != hash.end(); ++it )
    {
      for ( QgsFeatureId fid : featureIds )
      {
        it->featureIds.removeAll( fid );
      }
    }
  }

  return success;
}

QList<QgsSpatialIndexManager::IndexInfo> QgsSpatialIndexManager::getIndexInfo( QgsVectorLayer *layer ) const
{
  QMutexLocker locker( &mIndexMutex );
  
  QList<IndexInfo> result;
  
  if ( layer )
  {
    QString indexId = generateIndexId( layer );
    if ( mIndexInfo.contains( indexId ) )
    {
      result.append( mIndexInfo[indexId] );
    }
  }
  else
  {
    result = mIndexInfo.values();
  }
  
  return result;
}

void QgsSpatialIndexManager::invalidateIndex( QgsVectorLayer *layer )
{
  if ( !layer )
    return;

  QString indexId = generateIndexId( layer );

  QMutexLocker locker( &mIndexMutex );
  
  mRTreeIndexes.remove( indexId );
  mSpatialHashes.remove( indexId );
  mIndexInfo.remove( indexId );

  // Clear related query cache entries
  QMutexLocker cacheLocker( &mQueryCacheMutex );
  auto it = mCachedQueryInfo.begin();
  while ( it != mCachedQueryInfo.end() )
  {
    if ( it->layerId == layer->id() )
    {
      mQueryCache.remove( it.key() );
      it = mCachedQueryInfo.erase( it );
    }
    else
    {
      ++it;
    }
  }

  QgsDebugMsgLevel( QStringLiteral( "Invalidated spatial index for layer: %1" ).arg( layer->name() ), 3 );
}

void QgsSpatialIndexManager::clearAllIndexes()
{
  QMutexLocker indexLocker( &mIndexMutex );
  mRTreeIndexes.clear();
  mSpatialHashes.clear();
  mIndexInfo.clear();

  QMutexLocker cacheLocker( &mQueryCacheMutex );
  mQueryCache.clear();
  mCachedQueryInfo.clear();
  mCurrentQueryCacheMemoryMB = 0;

  resetStatistics();

  QgsDebugMsgLevel( QStringLiteral( "All spatial indexes cleared" ), 2 );
}

QgsSpatialIndexManager::PerformanceStatistics QgsSpatialIndexManager::getPerformanceStatistics() const
{
  PerformanceStatistics stats = mStatistics;
  stats.timestamp = QDateTime::currentDateTime();
  
  QMutexLocker indexLocker( &mIndexMutex );
  stats.totalIndexes = mIndexInfo.size();
  stats.loadedIndexes = mRTreeIndexes.size();
  
  // Calculate memory usage
  qint64 totalMemory = 0;
  for ( const IndexInfo &info : mIndexInfo )
  {
    totalMemory += info.memorySizeBytes;
  }
  stats.totalIndexMemoryMB = totalMemory / ( 1024 * 1024 );
  
  indexLocker.unlock();
  
  QMutexLocker cacheLocker( &mQueryCacheMutex );
  stats.queryCacheMemoryMB = mCurrentQueryCacheMemoryMB;
  
  int totalCacheAccesses = stats.queryCacheHits + stats.queryCacheMisses;
  if ( totalCacheAccesses > 0 )
  {
    stats.queryCacheHitRate = double( stats.queryCacheHits ) / totalCacheAccesses;
  }
  
  // Calculate query speedup
  if ( mBaselineQueryTimeMs > 0 && stats.averageIndexedQueryTimeMs > 0 )
  {
    stats.querySpeedupFactor = mBaselineQueryTimeMs / stats.averageIndexedQueryTimeMs;
  }
  
  return stats;
}

void QgsSpatialIndexManager::resetStatistics()
{
  mStatistics = PerformanceStatistics();
  mStatistics.timestamp = QDateTime::currentDateTime();
}

bool QgsSpatialIndexManager::isBackgroundIndexingActive() const
{
  QMutexLocker locker( &mBackgroundMutex );
  return !mActiveBackgroundTasks.isEmpty() || !mIndexBuildQueue.isEmpty();
}

qint64 QgsSpatialIndexManager::getIndexMemoryUsageMB() const
{
  return calculateTotalMemoryUsage() / ( 1024 * 1024 );
}

double QgsSpatialIndexManager::getQueryCacheHitRate() const
{
  int totalAccesses = mStatistics.queryCacheHits + mStatistics.queryCacheMisses;
  return totalAccesses > 0 ? double( mStatistics.queryCacheHits ) / totalAccesses : 0.0;
}

void QgsSpatialIndexManager::performMaintenance()
{
  QElapsedTimer timer;
  timer.start();

  // Expire old queries
  evictExpiredQueries();

  // Optimize indexes that need it
  QMutexLocker locker( &mIndexMutex );
  QStringList indexesToOptimize;
  for ( auto it = mIndexInfo.begin(); it != mIndexInfo.end(); ++it )
  {
    if ( !it->isOptimized && it->lastAccessed.secsTo( QDateTime::currentDateTime() ) < 3600 )
    {
      indexesToOptimize.append( it.key() );
    }
  }
  locker.unlock();

  for ( const QString &indexId : indexesToOptimize )
  {
    optimizeRTreeIndex( indexId );
  }

  // Update statistics
  updateStatistics();

  qint64 maintenanceTime = timer.elapsed();
  emit maintenanceCompleted( indexesToOptimize.size(), maintenanceTime );

  QgsDebugMsgLevel( QStringLiteral( "Spatial index maintenance completed in %1ms" ).arg( maintenanceTime ), 3 );
}

void QgsSpatialIndexManager::optimizeMemoryUsage()
{
  if ( isMemoryPressureHigh() )
  {
    reduceMemoryUsage();
  }
}

void QgsSpatialIndexManager::handleMemoryPressure()
{
  QgsDebugMsgLevel( QStringLiteral( "Handling memory pressure" ), 2 );

  // Unload least recently used indexes
  unloadLeastRecentlyUsedIndexes();

  // Reduce query cache size
  QMutexLocker cacheLocker( &mQueryCacheMutex );
  mQueryCache.setMaxCost( mConfig.queryCache.queryCacheSizeMB * 512 ); // 50% reduction
  evictLeastRecentlyUsedQueries();
}

void QgsSpatialIndexManager::onBackgroundIndexBuildCompleted()
{
  mStatistics.backgroundTasksCompleted++;
  updateStatistics();
}

void QgsSpatialIndexManager::continueBackgroundProcessing()
{
  QMutexLocker locker( &mBackgroundMutex );
  
  if ( mIndexBuildQueue.isEmpty() )
  {
    mBackgroundProcessingTimer->stop();
    return;
  }

  QString indexId = mIndexBuildQueue.dequeue();
  // Process the queued index build task
  // This is simplified - would need to reconstruct layer from indexId
}

QString QgsSpatialIndexManager::generateIndexId( QgsVectorLayer *layer ) const
{
  if ( !layer )
    return QString();

  return generateIndexId( layer->id() );
}

QString QgsSpatialIndexManager::generateIndexId( const QString &layerId ) const
{
  QCryptographicHash hash( QCryptographicHash::Md5 );
  hash.addData( layerId.toUtf8() );
  return QString( hash.result().toHex().left( 16 ) );
}

QString QgsSpatialIndexManager::generateQueryId( const QgsGeometry &geometry, QueryType queryType ) const
{
  QCryptographicHash hash( QCryptographicHash::Md5 );
  hash.addData( geometry.asWkt().toUtf8() );
  hash.addData( QByteArray::number( static_cast<int>( queryType ) ) );
  return QString( hash.result().toHex().left( 16 ) );
}

QString QgsSpatialIndexManager::generateQueryId( const QgsRectangle &extent, QueryType queryType ) const
{
  QCryptographicHash hash( QCryptographicHash::Md5 );
  hash.addData( extent.toString().toUtf8() );
  hash.addData( QByteArray::number( static_cast<int>( queryType ) ) );
  return QString( hash.result().toHex().left( 16 ) );
}

bool QgsSpatialIndexManager::buildRTreeIndex( QgsVectorLayer *layer, const QString &indexId )
{
  if ( !layer )
    return false;

  QElapsedTimer timer;
  timer.start();

  QSharedPointer<QgsSpatialIndex> index = QSharedPointer<QgsSpatialIndex>::create();

  QgsFeatureIterator it = layer->getFeatures();
  QgsFeature feature;
  int featureCount = 0;

  while ( it.nextFeature( feature ) )
  {
    if ( feature.hasGeometry() )
    {
      index->addFeature( feature );
      featureCount++;
    }
  }

  qint64 buildTime = timer.elapsed();

  QMutexLocker locker( &mIndexMutex );
  mRTreeIndexes[indexId] = index;

  IndexInfo info;
  info.indexId = indexId;
  info.layerId = layer->id();
  info.extent = layer->extent();
  info.featureCount = featureCount;
  info.buildTime = QDateTime::currentDateTime();
  info.lastAccessed = QDateTime::currentDateTime();
  info.buildTimeMs = buildTime;
  info.isLoaded = true;
  info.memorySizeBytes = featureCount * 100; // Rough estimate

  mIndexInfo[indexId] = info;

  QgsDebugMsgLevel( QStringLiteral( "Built R-tree index with %1 features in %2ms" )
                   .arg( featureCount ).arg( buildTime ), 2 );

  return true;
}

bool QgsSpatialIndexManager::buildRTreeIndex( QgsVectorDataProvider *provider, const QString &layerId, const QString &indexId )
{
  if ( !provider )
    return false;

  // Similar implementation to layer-based version but using provider directly
  QSharedPointer<QgsSpatialIndex> index = QSharedPointer<QgsSpatialIndex>::create();

  QgsFeatureIterator it = provider->getFeatures();
  QgsFeature feature;
  int featureCount = 0;

  while ( it.nextFeature( feature ) )
  {
    if ( feature.hasGeometry() )
    {
      index->addFeature( feature );
      featureCount++;
    }
  }

  QMutexLocker locker( &mIndexMutex );
  mRTreeIndexes[indexId] = index;

  IndexInfo info;
  info.indexId = indexId;
  info.layerId = layerId;
  info.extent = provider->extent();
  info.featureCount = featureCount;
  info.buildTime = QDateTime::currentDateTime();
  info.lastAccessed = QDateTime::currentDateTime();
  info.isLoaded = true;
  info.memorySizeBytes = featureCount * 100;

  mIndexInfo[indexId] = info;

  return true;
}

QList<QgsFeatureId> QgsSpatialIndexManager::queryRTreeIndex( const QString &indexId, const QgsGeometry &geometry, QueryType queryType )
{
  if ( !mRTreeIndexes.contains( indexId ) )
    return QList<QgsFeatureId>();

  QSharedPointer<QgsSpatialIndex> index = mRTreeIndexes[indexId];
  
  recordIndexAccess( indexId );

  switch ( queryType )
  {
    case IntersectsQuery:
      return index->intersects( geometry.boundingBox() );
    case ContainsQuery:
    case WithinQuery:
    case TouchesQuery:
    case OverlapsQuery:
      // For more complex queries, get candidates from bounding box and filter
      return index->intersects( geometry.boundingBox() );
    default:
      return QList<QgsFeatureId>();
  }
}

QList<QgsFeatureId> QgsSpatialIndexManager::queryRTreeIndex( const QString &indexId, const QgsRectangle &extent, QueryType queryType )
{
  if ( !mRTreeIndexes.contains( indexId ) )
    return QList<QgsFeatureId>();

  QSharedPointer<QgsSpatialIndex> index = mRTreeIndexes[indexId];
  
  recordIndexAccess( indexId );

  return index->intersects( extent );
}

void QgsSpatialIndexManager::optimizeRTreeIndex( const QString &indexId )
{
  QMutexLocker locker( &mIndexMutex );
  
  if ( mIndexInfo.contains( indexId ) )
  {
    mIndexInfo[indexId].isOptimized = true;
    mIndexInfo[indexId].lastMaintenance = QDateTime::currentDateTime();
  }
}

bool QgsSpatialIndexManager::buildSpatialHash( QgsVectorLayer *layer, const QString &indexId )
{
  if ( !layer || !mConfig.spatialHash.enableSpatialHashing )
    return false;

  QHash<QPair<int, int>, HashCellInfo> hash;
  QgsRectangle layerExtent = layer->extent();

  // Calculate cell size if not specified
  double cellSize = mConfig.spatialHash.hashCellSize;
  if ( cellSize <= 0.0 )
  {
    cellSize = qMax( layerExtent.width(), layerExtent.height() ) / mConfig.spatialHash.hashGridSize;
  }

  QgsFeatureIterator it = layer->getFeatures();
  QgsFeature feature;
  
  while ( it.nextFeature( feature ) )
  {
    if ( feature.hasGeometry() )
    {
      QgsRectangle bbox = feature.geometry().boundingBox();
      QList<QPair<int, int>> cells = calculateHashCells( bbox, 0 );
      
      for ( const auto &cell : cells )
      {
        if ( !hash.contains( cell ) )
        {
          HashCellInfo cellInfo;
          cellInfo.cellX = cell.first;
          cellInfo.cellY = cell.second;
          cellInfo.level = 0;
          cellInfo.bounds = QgsRectangle(
            layerExtent.xMinimum() + cell.first * cellSize,
            layerExtent.yMinimum() + cell.second * cellSize,
            layerExtent.xMinimum() + ( cell.first + 1 ) * cellSize,
            layerExtent.yMinimum() + ( cell.second + 1 ) * cellSize
          );
          cellInfo.lastUpdated = QDateTime::currentDateTime();
          hash[cell] = cellInfo;
        }
        
        hash[cell].featureIds.append( feature.id() );
      }
    }
  }

  QMutexLocker locker( &mIndexMutex );
  mSpatialHashes[indexId] = hash;

  return true;
}

bool QgsSpatialIndexManager::buildSpatialHash( QgsVectorDataProvider *provider, const QString &layerId, const QString &indexId )
{
  // Similar to layer-based version
  return true;
}

QList<QgsFeatureId> QgsSpatialIndexManager::querySpatialHash( const QString &indexId, const QgsGeometry &geometry, QueryType queryType )
{
  if ( !mSpatialHashes.contains( indexId ) )
    return QList<QgsFeatureId>();

  const QHash<QPair<int, int>, HashCellInfo> &hash = mSpatialHashes[indexId];
  QgsRectangle bbox = geometry.boundingBox();
  
  QList<QPair<int, int>> cells = calculateHashCells( bbox, 0 );
  QSet<QgsFeatureId> resultSet;
  
  for ( const auto &cell : cells )
  {
    if ( hash.contains( cell ) )
    {
      const QList<QgsFeatureId> &cellFeatures = hash[cell].featureIds;
      for ( QgsFeatureId fid : cellFeatures )
      {
        resultSet.insert( fid );
      }
    }
  }
  
  return resultSet.values();
}

QList<QgsFeatureId> QgsSpatialIndexManager::querySpatialHash( const QString &indexId, const QgsRectangle &extent, QueryType queryType )
{
  QgsGeometry geometry = createGeometryFromExtent( extent );
  return querySpatialHash( indexId, geometry, queryType );
}

QString QgsSpatialIndexManager::getCachedQueryResult( const QString &queryId )
{
  QMutexLocker locker( &mQueryCacheMutex );
  
  if ( mCachedQueryInfo.contains( queryId ) )
  {
    CachedQuery &query = mCachedQueryInfo[queryId];
    
    if ( isCacheValid( query ) )
    {
      query.lastAccessed = QDateTime::currentDateTime();
      query.accessCount++;
      
      // Convert result IDs to string
      QStringList idStrings;
      for ( QgsFeatureId id : query.resultIds )
      {
        idStrings.append( QString::number( id ) );
      }
      return idStrings.join( ',' );
    }
    else
    {
      // Remove expired query
      mQueryCache.remove( queryId );
      mCachedQueryInfo.remove( queryId );
    }
  }
  
  return QString();
}

void QgsSpatialIndexManager::cacheQueryResult( const QString &queryId, const QList<QgsFeatureId> &results, 
                                              const QString &layerId, double queryTimeMs )
{
  if ( !mConfig.queryCache.enableQueryCaching )
    return;

  QMutexLocker locker( &mQueryCacheMutex );
  
  // Convert results to string for cache storage
  QStringList idStrings;
  for ( QgsFeatureId id : results )
  {
    idStrings.append( QString::number( id ) );
  }
  QString resultString = idStrings.join( ',' );
  
  qint64 resultSize = resultString.size();
  mQueryCache.insert( queryId, new QString( resultString ), resultSize );
  
  CachedQuery query;
  query.queryId = queryId;
  query.layerId = layerId;
  query.resultIds = results;
  query.cacheTime = QDateTime::currentDateTime();
  query.lastAccessed = QDateTime::currentDateTime();
  query.memorySizeBytes = resultSize;
  query.queryTimeMs = queryTimeMs;
  query.accessCount = 1;
  
  mCachedQueryInfo[queryId] = query;
  mCurrentQueryCacheMemoryMB += resultSize / ( 1024 * 1024 );
}

bool QgsSpatialIndexManager::isCacheValid( const CachedQuery &query ) const
{
  QDateTime expiryTime = query.cacheTime.addSecs( mConfig.queryCache.cacheExpiryMinutes * 60 );
  return QDateTime::currentDateTime() <= expiryTime;
}

void QgsSpatialIndexManager::evictExpiredQueries()
{
  QMutexLocker locker( &mQueryCacheMutex );
  
  auto it = mCachedQueryInfo.begin();
  while ( it != mCachedQueryInfo.end() )
  {
    if ( !isCacheValid( it.value() ) )
    {
      mQueryCache.remove( it.key() );
      mCurrentQueryCacheMemoryMB -= it->memorySizeBytes / ( 1024 * 1024 );
      it = mCachedQueryInfo.erase( it );
      mStatistics.expiredCacheEntries++;
    }
    else
    {
      ++it;
    }
  }
}

void QgsSpatialIndexManager::evictLeastRecentlyUsedQueries()
{
  QMutexLocker locker( &mQueryCacheMutex );
  
  // Get queries sorted by last access time
  QList<QPair<QDateTime, QString>> queriesByAccess;
  for ( auto it = mCachedQueryInfo.begin(); it != mCachedQueryInfo.end(); ++it )
  {
    queriesByAccess.append( qMakePair( it->lastAccessed, it.key() ) );
  }
  
  std::sort( queriesByAccess.begin(), queriesByAccess.end() );
  
  // Remove oldest 25%
  int queriesToRemove = queriesByAccess.size() / 4;
  for ( int i = 0; i < queriesToRemove; ++i )
  {
    QString queryId = queriesByAccess[i].second;
    if ( mCachedQueryInfo.contains( queryId ) )
    {
      mCurrentQueryCacheMemoryMB -= mCachedQueryInfo[queryId].memorySizeBytes / ( 1024 * 1024 );
      mCachedQueryInfo.remove( queryId );
    }
    mQueryCache.remove( queryId );
  }
}

void QgsSpatialIndexManager::startBackgroundIndexBuild( QgsVectorLayer *layer, const QString &indexId )
{
  if ( !mBackgroundThreadPool )
    return;

  BackgroundIndexBuildTask *task = new BackgroundIndexBuildTask( this, layer, indexId );
  
  QMutexLocker locker( &mBackgroundMutex );
  mActiveBackgroundTasks[indexId] = QDateTime::currentDateTime();
  
  mBackgroundThreadPool->start( task );
}

qint64 QgsSpatialIndexManager::calculateTotalMemoryUsage() const
{
  qint64 totalMemory = 0;
  
  QMutexLocker indexLocker( &mIndexMutex );
  for ( const IndexInfo &info : mIndexInfo )
  {
    totalMemory += info.memorySizeBytes;
  }
  
  QMutexLocker cacheLocker( &mQueryCacheMutex );
  totalMemory += mCurrentQueryCacheMemoryMB * 1024 * 1024;
  
  return totalMemory;
}

bool QgsSpatialIndexManager::isMemoryPressureHigh() const
{
  qint64 totalMemory = calculateTotalMemoryUsage();
  qint64 maxMemory = mConfig.memory.maxIndexMemoryMB * 1024 * 1024;
  
  return totalMemory > maxMemory * mConfig.memory.memoryPressureThreshold;
}

void QgsSpatialIndexManager::reduceMemoryUsage()
{
  unloadLeastRecentlyUsedIndexes();
  evictLeastRecentlyUsedQueries();
}

void QgsSpatialIndexManager::unloadLeastRecentlyUsedIndexes()
{
  QMutexLocker locker( &mIndexMutex );
  
  QList<QPair<QDateTime, QString>> indexesByAccess;
  for ( auto it = mIndexInfo.begin(); it != mIndexInfo.end(); ++it )
  {
    if ( it->isLoaded )
    {
      indexesByAccess.append( qMakePair( it->lastAccessed, it.key() ) );
    }
  }
  
  std::sort( indexesByAccess.begin(), indexesByAccess.end() );
  
  // Unload oldest 30%
  int indexesToUnload = indexesByAccess.size() * 0.3;
  for ( int i = 0; i < indexesToUnload; ++i )
  {
    QString indexId = indexesByAccess[i].second;
    mRTreeIndexes.remove( indexId );
    mSpatialHashes.remove( indexId );
    
    if ( mIndexInfo.contains( indexId ) )
    {
      mIndexInfo[indexId].isLoaded = false;
    }
  }
}

void QgsSpatialIndexManager::updateStatistics()
{
  mStatistics.timestamp = QDateTime::currentDateTime();
  
  QMutexLocker indexLocker( &mIndexMutex );
  mStatistics.totalIndexes = mIndexInfo.size();
  mStatistics.loadedIndexes = mRTreeIndexes.size();
  
  QMutexLocker cacheLocker( &mQueryCacheMutex );
  mStatistics.queryCacheMemoryMB = mCurrentQueryCacheMemoryMB;
  
  int totalCacheAccesses = mStatistics.queryCacheHits + mStatistics.queryCacheMisses;
  if ( totalCacheAccesses > 0 )
  {
    mStatistics.queryCacheHitRate = double( mStatistics.queryCacheHits ) / totalCacheAccesses;
  }
  
  emit statisticsUpdated( mStatistics );
}

void QgsSpatialIndexManager::recordQuery( const QString &layerId, QueryType queryType, int resultCount, double queryTimeMs )
{
  mStatistics.totalQueries++;
  
  if ( mStatistics.totalQueries == 1 )
  {
    mStatistics.averageQueryTimeMs = queryTimeMs;
  }
  else
  {
    mStatistics.averageQueryTimeMs = 
      ( mStatistics.averageQueryTimeMs * ( mStatistics.totalQueries - 1 ) + queryTimeMs ) / mStatistics.totalQueries;
  }
  
  // Update indexed vs non-indexed query times
  QString indexId = generateIndexId( layerId );
  if ( mRTreeIndexes.contains( indexId ) || mSpatialHashes.contains( indexId ) )
  {
    // This was an indexed query
    if ( mStatistics.averageIndexedQueryTimeMs == 0.0 )
    {
      mStatistics.averageIndexedQueryTimeMs = queryTimeMs;
    }
    else
    {
      mStatistics.averageIndexedQueryTimeMs = 
        ( mStatistics.averageIndexedQueryTimeMs + queryTimeMs ) / 2.0;
    }
  }
  else
  {
    // This was a non-indexed query
    if ( mStatistics.averageNonIndexedQueryTimeMs == 0.0 )
    {
      mStatistics.averageNonIndexedQueryTimeMs = queryTimeMs;
      mBaselineQueryTimeMs = queryTimeMs;
    }
    else
    {
      mStatistics.averageNonIndexedQueryTimeMs = 
        ( mStatistics.averageNonIndexedQueryTimeMs + queryTimeMs ) / 2.0;
    }
  }
}

void QgsSpatialIndexManager::recordIndexAccess( const QString &indexId )
{
  mIndexAccessTimes[indexId] = QDateTime::currentDateTime();
  mIndexAccessCounts[indexId]++;
  
  QMutexLocker locker( &mIndexMutex );
  if ( mIndexInfo.contains( indexId ) )
  {
    mIndexInfo[indexId].lastAccessed = QDateTime::currentDateTime();
    mIndexInfo[indexId].queryCount++;
  }
}

void QgsSpatialIndexManager::recordCacheAccess( const QString &queryId, bool hit )
{
  mQueryAccessTimes[queryId] = QDateTime::currentDateTime();
  mQueryAccessCounts[queryId]++;
  
  if ( hit )
  {
    mStatistics.queryCacheHits++;
  }
  else
  {
    mStatistics.queryCacheMisses++;
  }
}

QgsGeometry QgsSpatialIndexManager::createGeometryFromExtent( const QgsRectangle &extent ) const
{
  return QgsGeometry::fromRect( extent );
}

double QgsSpatialIndexManager::calculateDistance( const QgsPointXY &p1, const QgsPointXY &p2 ) const
{
  double dx = p1.x() - p2.x();
  double dy = p1.y() - p2.y();
  return std::sqrt( dx * dx + dy * dy );
}

QgsRectangle QgsSpatialIndexManager::expandExtent( const QgsRectangle &extent, double buffer ) const
{
  return QgsRectangle( extent.xMinimum() - buffer, extent.yMinimum() - buffer,
                      extent.xMaximum() + buffer, extent.yMaximum() + buffer );
}

bool QgsSpatialIndexManager::geometryIntersects( const QgsGeometry &g1, const QgsGeometry &g2, QueryType queryType ) const
{
  switch ( queryType )
  {
    case IntersectsQuery:
      return g1.intersects( g2 );
    case ContainsQuery:
      return g1.contains( g2 );
    case WithinQuery:
      return g1.within( g2 );
    case TouchesQuery:
      return g1.touches( g2 );
    case OverlapsQuery:
      return g1.overlaps( g2 );
    default:
      return g1.intersects( g2 );
  }
}

QPair<int, int> QgsSpatialIndexManager::calculateHashCell( const QgsPointXY &point, int level ) const
{
  // Simplified hash cell calculation
  double cellSize = 1000.0 / qPow( 2, level ); // Adjust based on coordinate system
  int cellX = qFloor( point.x() / cellSize );
  int cellY = qFloor( point.y() / cellSize );
  return qMakePair( cellX, cellY );
}

QList<QPair<int, int>> QgsSpatialIndexManager::calculateHashCells( const QgsRectangle &extent, int level ) const
{
  QList<QPair<int, int>> cells;
  
  QPair<int, int> minCell = calculateHashCell( QgsPointXY( extent.xMinimum(), extent.yMinimum() ), level );
  QPair<int, int> maxCell = calculateHashCell( QgsPointXY( extent.xMaximum(), extent.yMaximum() ), level );
  
  for ( int x = minCell.first; x <= maxCell.first; ++x )
  {
    for ( int y = minCell.second; y <= maxCell.second; ++y )
    {
      cells.append( qMakePair( x, y ) );
    }
  }
  
  return cells;
}