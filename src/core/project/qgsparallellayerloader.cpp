/***************************************************************************
                         qgsparallellayerloader.cpp
                         --------------------------
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

#include "qgsparallellayerloader.h"
#include "qgslayerstylecache.h"
#include "qgsproject.h"
#include "qgsmaplayerfactory.h"
#include "qgslogger.h"
#include "qgslayertree.h"
#include "qgslayertreeutils.h"
#include "qgsmaplayerstore.h"
#include "qgsproviderregistry.h"
#include "qgsvectorlayer.h"
#include "qgsrasterlayer.h"

#include <QThread>
#include <QApplication>
#include <QtConcurrent>
#include <QElapsedTimer>
#include <QMutexLocker>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#include <fstream>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/mach_host.h>
#endif

QgsParallelLayerLoader::QgsParallelLayerLoader( QObject *parent )
  : QObject( parent )
  , mThreadPool( new QThreadPool( this ) )
{
  // Initialize progress timer
  mProgressTimer = new QTimer( this );
  connect( mProgressTimer, &QTimer::timeout, this, &QgsParallelLayerLoader::updateProgress );
  mProgressTimer->setInterval( 100 ); // Update every 100ms
  
  // Initialize memory monitoring timer
  mMemoryTimer = new QTimer( this );
  connect( mMemoryTimer, &QTimer::timeout, this, &QgsParallelLayerLoader::checkMemoryUsage );
  mMemoryTimer->setInterval( 250 ); // Check every 250ms
  
  // Configure thread pool
  mThreadPool->setMaxThreadCount( QThread::idealThreadCount() );
}

QgsParallelLayerLoader::~QgsParallelLayerLoader()
{
  cancelLoading();
  
  // Clean up any remaining watchers
  for ( QFutureWatcher<void> *watcher : std::as_const( mActiveWatchers ) )
  {
    watcher->cancel();
    watcher->waitForFinished();
    delete watcher;
  }
  mActiveWatchers.clear();
}

void QgsParallelLayerLoader::setLoadingConfig( const LoadingConfig &config )
{
  mConfig = config;
  
  // Update thread pool configuration
  int maxThreads = config.maxParallelLayers;
  if ( config.strategy == LoadingStrategy::Aggressive )
  {
    maxThreads = qMin( maxThreads * 2, QThread::idealThreadCount() );
  }
  else if ( config.strategy == LoadingStrategy::Conservative )
  {
    maxThreads = qMax( 1, maxThreads / 2 );
  }
  else if ( config.strategy == LoadingStrategy::Sequential )
  {
    maxThreads = 1;
  }
  
  mThreadPool->setMaxThreadCount( maxThreads );
  
  QgsDebugMsgLevel( QStringLiteral( "Parallel layer loader configured: strategy=%1, maxThreads=%2, maxMemory=%3MB" )
                    .arg( static_cast<int>( config.strategy ) )
                    .arg( maxThreads )
                    .arg( config.maxMemoryUsageMB ), 2 );
}

void QgsParallelLayerLoader::setLayerStyleCache( QgsLayerStyleCache *styleCache )
{
  mStyleCache = styleCache;
}

bool QgsParallelLayerLoader::loadLayers( const QList<QDomElement> &layerElements, 
                                         QgsProject *project, 
                                         const QgsReadWriteContext &context )
{
  if ( layerElements.isEmpty() || !project )
  {
    return false;
  }

  // Initialize loading state
  mCancelled = false;
  mProgress = 0;
  mMemoryThrottled = false;
  mLoadingStartTime = QDateTime::currentDateTime();
  mInitialMemoryUsage = getCurrentMemoryUsageMB();
  
  mLoadingTasks.clear();
  mLoadedLayerIds.clear();
  mFailedLayerIds.clear();
  
  // Create loading tasks from DOM elements
  for ( const QDomElement &element : layerElements )
  {
    LayerLoadingTask task;
    task.layerElement = element;
    task.layerId = element.attribute( QStringLiteral( "id" ) );
    task.layerName = element.firstChildElement( QStringLiteral( "layername" ) ).text();
    
    if ( task.layerName.isEmpty() )
    {
      task.layerName = element.attribute( QStringLiteral( "name" ), tr( "Unknown Layer" ) );
    }
    
    // Calculate priority
    task.priority = calculateLayerPriority( task );
    
    // Estimate memory usage (rough approximation)
    const QString providerType = element.attribute( QStringLiteral( "provider" ) );
    if ( providerType == QLatin1String( "gdal" ) || providerType == QLatin1String( "wms" ) )
    {
      task.estimatedMemoryMB = 20; // Raster layers typically use more memory
    }
    else if ( providerType == QLatin1String( "ogr" ) || providerType == QLatin1String( "postgres" ) )
    {
      task.estimatedMemoryMB = 10; // Vector layers
    }
    else
    {
      task.estimatedMemoryMB = 5; // Default estimate
    }
    
    mLoadingTasks.append( task );
  }
  
  initializeStatistics();
  
  QgsDebugMsgLevel( QStringLiteral( "Starting parallel loading of %1 layers" ).arg( mLoadingTasks.size() ), 2 );
  
  // Analyze dependencies if enabled
  if ( mConfig.enableDependencyAnalysis )
  {
    QElapsedTimer depTimer;
    depTimer.start();
    analyzeDependencies();
    mStatistics.dependencyAnalysisTimeMs = depTimer.elapsed();
  }
  
  // Start progress and memory monitoring
  if ( mConfig.enableProgressReporting )
  {
    mProgressTimer->start();
  }
  mMemoryTimer->start();
  
  // Start the loading process
  const QElapsedTimer loadingTimer;
  loadingTimer.start();
  
  bool success = true;
  
  // Main loading loop
  while ( !mCancelled && mLoadedLayerIds.size() + mFailedLayerIds.size() < mLoadingTasks.size() )
  {
    // Get next batch of layers to load
    QList<LayerLoadingTask*> batch = getNextLoadingBatch();
    
    if ( batch.isEmpty() )
    {
      // No layers ready to load - wait a bit and try again
      QThread::msleep( 50 );
      continue;
    }
    
    // Check memory constraints
    if ( shouldThrottleLoading() )
    {
      if ( !mMemoryThrottled )
      {
        mMemoryThrottled = true;
        emit memoryThresholdExceeded( mCurrentMemoryUsage, mConfig.maxMemoryUsageMB );
        QgsDebugMsgLevel( QStringLiteral( "Memory threshold exceeded, throttling loading" ), 2 );
      }
      
      // Reduce batch size or wait for memory to be freed
      if ( batch.size() > 1 )
      {
        batch = batch.mid( 0, 1 ); // Load only one layer at a time
      }
      else
      {
        QThread::msleep( 100 );
        continue;
      }
    }
    else if ( mMemoryThrottled )
    {
      mMemoryThrottled = false;
      QgsDebugMsgLevel( QStringLiteral( "Memory usage back to normal, resuming parallel loading" ), 2 );
    }
    
    // Launch loading tasks
    for ( LayerLoadingTask *task : batch )
    {
      loadLayerAsync( task, project, context );
    }
    
    // Wait for at least one task to complete before continuing
    while ( !mCancelled && mActiveWatchers.size() >= mThreadPool->maxThreadCount() )
    {
      QApplication::processEvents();
      QThread::msleep( 10 );
    }
  }
  
  // Wait for all remaining tasks to complete
  while ( !mActiveWatchers.isEmpty() && !mCancelled )
  {
    QApplication::processEvents();
    QThread::msleep( 10 );
  }
  
  // Stop timers
  mProgressTimer->stop();
  mMemoryTimer->stop();
  
  // Update final statistics
  mStatistics.totalLoadingTimeMs = loadingTimer.elapsed();
  success = mFailedLayerIds.isEmpty() && !mCancelled;
  
  updateStatistics();
  
  mProgress = mCancelled ? mProgress : 100;
  emit progressChanged( mProgress, QString() );
  emit loadingCompleted( success, mStatistics );
  
  QgsDebugMsgLevel( QStringLiteral( "Parallel loading completed: success=%1, loaded=%2, failed=%3, time=%4ms" )
                    .arg( success )
                    .arg( mLoadedLayerIds.size() )
                    .arg( mFailedLayerIds.size() )
                    .arg( mStatistics.totalLoadingTimeMs ), 2 );
  
  return success;
}

bool QgsParallelLayerLoader::loadLayersById( const QStringList &layerIds, QgsProject *project )
{
  Q_UNUSED( layerIds )
  Q_UNUSED( project )
  
  // This would implement loading of existing layers from the project
  // For now, we focus on loading from XML elements
  QgsDebugMsgLevel( QStringLiteral( "loadLayersById not yet implemented" ), 2 );
  return false;
}

void QgsParallelLayerLoader::cancelLoading()
{
  mCancelled = true;
  
  // Cancel all active futures
  for ( QFutureWatcher<void> *watcher : std::as_const( mActiveWatchers ) )
  {
    watcher->cancel();
  }
  
  QgsDebugMsgLevel( QStringLiteral( "Parallel layer loading cancelled" ), 2 );
}

QList<QgsParallelLayerLoader::LayerLoadingTask> QgsParallelLayerLoader::getFailedLayers() const
{
  QList<LayerLoadingTask> failedTasks;
  for ( const LayerLoadingTask &task : mLoadingTasks )
  {
    if ( task.hasError )
    {
      failedTasks.append( task );
    }
  }
  return failedTasks;
}

bool QgsParallelLayerLoader::retryFailedLayers( QgsProject *project, const QgsReadWriteContext &context )
{
  const QList<LayerLoadingTask> failedTasks = getFailedLayers();
  if ( failedTasks.isEmpty() )
  {
    return true;
  }

  QgsDebugMsgLevel( QStringLiteral( "Retrying %1 failed layers" ).arg( failedTasks.size() ), 2 );
  
  // Reset failed layers and try again with sequential loading
  for ( LayerLoadingTask &task : mLoadingTasks )
  {
    if ( task.hasError )
    {
      task.hasError = false;
      task.errorMessage.clear();
      task.layer = nullptr;
    }
  }
  
  mFailedLayerIds.clear();
  
  // Use conservative strategy for retry
  const LoadingConfig originalConfig = mConfig;
  mConfig.strategy = LoadingStrategy::Sequential;
  mConfig.maxParallelLayers = 1;
  
  // Create element list from failed tasks
  QList<QDomElement> elements;
  for ( const LayerLoadingTask &task : failedTasks )
  {
    elements.append( task.layerElement );
  }
  
  const bool success = loadLayers( elements, project, context );
  
  // Restore original configuration
  mConfig = originalConfig;
  
  return success;
}

void QgsParallelLayerLoader::onLayerLoadingFinished()
{
  QFutureWatcher<void> *watcher = qobject_cast<QFutureWatcher<void>*>( sender() );
  if ( !watcher )
  {
    return;
  }

  // Remove the completed watcher
  mActiveWatchers.removeAll( watcher );
  watcher->deleteLater();
  
  // Update progress
  updateProgress();
}

void QgsParallelLayerLoader::updateProgress()
{
  if ( mLoadingTasks.isEmpty() )
  {
    return;
  }

  const int totalTasks = mLoadingTasks.size();
  const int completedTasks = mLoadedLayerIds.size() + mFailedLayerIds.size();
  const int newProgress = ( completedTasks * 100 ) / totalTasks;
  
  if ( newProgress != mProgress )
  {
    mProgress = newProgress;
    emit progressChanged( mProgress, mCurrentLayerName );
  }
}

void QgsParallelLayerLoader::checkMemoryUsage()
{
  mCurrentMemoryUsage = getCurrentMemoryUsageMB();
  
  if ( mCurrentMemoryUsage > mStatistics.peakMemoryUsageMB )
  {
    mStatistics.peakMemoryUsageMB = mCurrentMemoryUsage;
  }
}

void QgsParallelLayerLoader::analyzeDependencies()
{
  // For now, we implement a simple dependency analysis
  // In a full implementation, this would analyze layer relationships,
  // joined layers, style dependencies, etc.
  
  for ( LayerLoadingTask &task : mLoadingTasks )
  {
    // Check for obvious dependencies (this is simplified)
    const QDomElement element = task.layerElement;
    
    // Look for layer relationships in joins, etc.
    const QDomNodeList joinNodes = element.elementsByTagName( QStringLiteral( "join" ) );
    for ( int i = 0; i < joinNodes.count(); ++i )
    {
      const QDomElement joinElement = joinNodes.at( i ).toElement();
      const QString joinLayerId = joinElement.attribute( QStringLiteral( "joinLayerId" ) );
      if ( !joinLayerId.isEmpty() && joinLayerId != task.layerId )
      {
        task.dependencies.append( joinLayerId );
      }
    }
    
    // Check for style dependencies
    const QDomElement rendererElement = element.firstChildElement( QStringLiteral( "renderer-v2" ) );
    if ( !rendererElement.isNull() )
    {
      // Look for data-defined properties that might reference other layers
      const QDomNodeList ddNodes = rendererElement.elementsByTagName( QStringLiteral( "data-defined-properties" ) );
      for ( int i = 0; i < ddNodes.count(); ++i )
      {
        const QDomElement ddElement = ddNodes.at( i ).toElement();
        const QString expression = ddElement.text();
        // Simple check for layer references in expressions
        if ( expression.contains( QStringLiteral( "layer(" ) ) )
        {
          // This is a very basic implementation - would need proper expression parsing
          // for complete dependency detection
        }
      }
    }
  }
  
  QgsDebugMsgLevel( QStringLiteral( "Dependency analysis completed" ), 3 );
}

QgsParallelLayerLoader::LoadingPriority QgsParallelLayerLoader::calculateLayerPriority( const LayerLoadingTask &task ) const
{
  // Check if layer is in priority list
  if ( mConfig.priorityLayerIds.contains( task.layerId ) )
  {
    return LoadingPriority::Critical;
  }
  
  // Check if layer is deferred
  if ( mConfig.deferredLayerIds.contains( task.layerId ) )
  {
    return LoadingPriority::Background;
  }
  
  const QDomElement element = task.layerElement;
  
  // Check layer type and provider
  const QString layerType = element.attribute( QStringLiteral( "type" ) );
  const QString providerType = element.attribute( QStringLiteral( "provider" ) );
  
  // Base maps and tile layers get high priority
  if ( providerType == QLatin1String( "wms" ) || 
       providerType == QLatin1String( "wmts" ) ||
       providerType == QLatin1String( "xyz" ) )
  {
    return LoadingPriority::High;
  }
  
  // Memory layers get low priority
  if ( providerType == QLatin1String( "memory" ) )
  {
    return LoadingPriority::Background;
  }
  
  // Check if layer is currently visible (simplified check)
  const QDomElement layerTreeElement = element.firstChildElement( QStringLiteral( "layer-tree-layer" ) );
  if ( !layerTreeElement.isNull() )
  {
    const bool visible = layerTreeElement.attribute( QStringLiteral( "checked" ) ) == QLatin1String( "Qt::Checked" );
    if ( visible )
    {
      return LoadingPriority::High;
    }
  }
  
  return LoadingPriority::Normal;
}

bool QgsParallelLayerLoader::canLoadLayer( const LayerLoadingTask &task ) const
{
  // Check if all dependencies are satisfied
  for ( const QString &depId : task.dependencies )
  {
    if ( !mLoadedLayerIds.contains( depId ) && !mFailedLayerIds.contains( depId ) )
    {
      return false; // Dependency not yet processed
    }
    
    if ( mFailedLayerIds.contains( depId ) )
    {
      // Dependency failed - this layer might fail too, but we can try
      QgsDebugMsgLevel( QStringLiteral( "Layer %1 has failed dependency %2" ).arg( task.layerId, depId ), 3 );
    }
  }
  
  return true;
}

QList<QgsParallelLayerLoader::LayerLoadingTask*> QgsParallelLayerLoader::getNextLoadingBatch()
{
  QList<LayerLoadingTask*> batch;
  
  // Sort tasks by priority
  QList<LayerLoadingTask*> availableTasks;
  for ( LayerLoadingTask &task : mLoadingTasks )
  {
    if ( !task.isLoaded && !task.hasError && canLoadLayer( task ) )
    {
      availableTasks.append( &task );
    }
  }
  
  // Sort by priority (lower number = higher priority)
  std::sort( availableTasks.begin(), availableTasks.end(),
             []( const LayerLoadingTask *a, const LayerLoadingTask *b )
             {
               return static_cast<int>( a->priority ) < static_cast<int>( b->priority );
             } );
  
  // Select tasks for the batch
  const int maxBatchSize = mThreadPool->maxThreadCount() - mActiveWatchers.size();
  const int batchSize = qMin( maxBatchSize, availableTasks.size() );
  
  for ( int i = 0; i < batchSize; ++i )
  {
    batch.append( availableTasks[i] );
  }
  
  return batch;
}

void QgsParallelLayerLoader::loadLayerAsync( LayerLoadingTask *task, QgsProject *project, const QgsReadWriteContext &context )
{
  if ( !task || mCancelled )
  {
    return;
  }

  mCurrentLayerName = task->layerName;
  
  // Create a future for the layer loading
  QFuture<void> future = QtConcurrent::run( [this, task, project, context]()
  {
    QElapsedTimer timer;
    timer.start();
    
    QString errorMessage;
    QgsMapLayer *layer = createLayerFromElement( task->layerElement, context, &errorMessage );
    
    const qint64 loadTime = timer.elapsed();
    
    // Process layer style with caching if layer was created successfully
    if ( layer && errorMessage.isEmpty() )
    {
      // Look for style element in the layer XML
      const QDomElement styleElement = task->layerElement.firstChildElement( QStringLiteral( "renderer-v2" ) );
      if ( !styleElement.isNull() || !task->layerElement.firstChildElement( QStringLiteral( "pipe" ) ).isNull() )
      {
        // Use the whole layer element as style context for comprehensive caching
        processLayerStyleWithCache( layer, task->layerElement );
      }
    }
    
    // Update task with results (thread-safe)
    QMutexLocker locker( &mTaskMutex );
    
    if ( layer && errorMessage.isEmpty() )
    {
      task->layer = layer;
      task->isLoaded = true;
      mLoadedLayerIds.insert( task->layerId );
      mStatistics.layerLoadTimes[task->layerId] = loadTime;
      
      // Add layer to project (must be done in main thread)
      QMetaObject::invokeMethod( project, [project, layer]()
      {
        project->addMapLayer( layer );
      }, Qt::QueuedConnection );
      
      emit layerLoaded( task->layerId, layer );
    }
    else
    {
      task->hasError = true;
      task->errorMessage = errorMessage;
      mFailedLayerIds.insert( task->layerId );
      
      if ( layer )
      {
        // Clean up failed layer
        delete layer;
      }
      
      emit layerLoadFailed( task->layerId, errorMessage );
    }
  } );
  
  // Create a watcher for the future
  QFutureWatcher<void> *watcher = new QFutureWatcher<void>( this );
  connect( watcher, &QFutureWatcher<void>::finished, this, &QgsParallelLayerLoader::onLayerLoadingFinished );
  
  watcher->setFuture( future );
  mActiveWatchers.append( watcher );
}

QgsMapLayer *QgsParallelLayerLoader::createLayerFromElement( const QDomElement &element, 
                                                             const QgsReadWriteContext &context,
                                                             QString *errorMessage ) const
{
  try
  {
    // Use the existing QGIS layer factory to create the layer
    QgsMapLayer *layer = QgsMapLayerFactory::createLayer( element, context );
    
    if ( !layer )
    {
      if ( errorMessage )
      {
        *errorMessage = tr( "Failed to create layer from element" );
      }
      return nullptr;
    }
    
    // Verify the layer is valid
    if ( !layer->isValid() )
    {
      if ( errorMessage )
      {
        *errorMessage = tr( "Layer is not valid: %1" ).arg( layer->dataProvider() ? layer->dataProvider()->error().message() : tr( "Unknown error" ) );
      }
      delete layer;
      return nullptr;
    }
    
    return layer;
  }
  catch ( const std::exception &e )
  {
    if ( errorMessage )
    {
      *errorMessage = tr( "Exception during layer creation: %1" ).arg( e.what() );
    }
    return nullptr;
  }
}

void QgsParallelLayerLoader::processLayerStyleWithCache( QgsMapLayer *layer, const QDomElement &styleElement ) const
{
  if ( !layer || !mStyleCache || styleElement.isNull() )
  {
    return;
  }

  // Try to find similar styles first
  const QStringList similarStyles = mStyleCache->findSimilarStyles( styleElement );
  if ( !similarStyles.isEmpty() )
  {
    // Found similar style, check if we can reuse it
    QDomElement cachedStyleElement;
    if ( mStyleCache->getCachedLayerStyle( similarStyles.first(), cachedStyleElement ) )
    {
      // Use cached style as base and only apply differences
      QgsDebugMsgLevel( QStringLiteral( "Reusing similar cached style for layer %1" ).arg( layer->name() ), 3 );
      // Note: In a full implementation, we would apply cached style and only modify differences
    }
  }

  // Cache the current style for future reuse
  const QString fingerprint = mStyleCache->cacheLayerStyle( layer->id(), styleElement, layer );
  if ( !fingerprint.isEmpty() )
  {
    QgsDebugMsgLevel( QStringLiteral( "Cached style for layer %1 with fingerprint %2" )
                      .arg( layer->name() ).arg( fingerprint ), 3 );
    
    // Cache individual renderer and symbols for granular reuse
    const QDomElement rendererElement = styleElement.firstChildElement( QStringLiteral( "renderer-v2" ) );
    if ( !rendererElement.isNull() )
    {
      const QString layerType = layer->type() == QgsMapLayerType::VectorLayer ? QStringLiteral( "vector" ) :
                                layer->type() == QgsMapLayerType::RasterLayer ? QStringLiteral( "raster" ) :
                                QStringLiteral( "unknown" );
      mStyleCache->cacheRenderer( rendererElement, layerType );
      
      // Cache symbols within the renderer
      const QDomNodeList symbols = rendererElement.elementsByTagName( QStringLiteral( "symbol" ) );
      for ( int i = 0; i < symbols.count(); ++i )
      {
        const QDomElement symbolElement = symbols.at( i ).toElement();
        if ( !symbolElement.isNull() )
        {
          const QString symbolType = symbolElement.attribute( QStringLiteral( "type" ) );
          mStyleCache->cacheSymbol( symbolElement, symbolType );
        }
      }
    }
  }
}

qint64 QgsParallelLayerLoader::getCurrentMemoryUsageMB() const
{
#if defined(Q_OS_WIN)
  PROCESS_MEMORY_COUNTERS pmc;
  if ( GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
  {
    return pmc.WorkingSetSize / ( 1024 * 1024 );
  }
#elif defined(Q_OS_LINUX)
  std::ifstream file( "/proc/self/status" );
  std::string line;
  while ( std::getline( file, line ) )
  {
    if ( line.substr( 0, 6 ) == "VmRSS:" )
    {
      std::string memStr = line.substr( 7 );
      size_t pos = memStr.find( "kB" );
      if ( pos != std::string::npos )
      {
        memStr = memStr.substr( 0, pos );
        return std::stoll( memStr ) / 1024; // Convert KB to MB
      }
    }
  }
#elif defined(Q_OS_MACOS)
  task_basic_info info;
  mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
  if ( task_info( mach_task_self(), TASK_BASIC_INFO, ( task_info_t )&info, &count ) == KERN_SUCCESS )
  {
    return info.resident_size / ( 1024 * 1024 );
  }
#endif
  
  return 0; // Fallback if memory detection fails
}

bool QgsParallelLayerLoader::shouldThrottleLoading() const
{
  return mCurrentMemoryUsage > mConfig.maxMemoryUsageMB;
}

void QgsParallelLayerLoader::initializeStatistics()
{
  mStatistics = LoadingStatistics();
  mStatistics.totalLayers = mLoadingTasks.size();
  mStatistics.peakMemoryUsageMB = mInitialMemoryUsage;
}

void QgsParallelLayerLoader::updateStatistics()
{
  // Count parallel vs sequential loading
  for ( const LayerLoadingTask &task : mLoadingTasks )
  {
    if ( task.isLoaded )
    {
      // This is a simplified metric - in practice you'd track which layers
      // were actually loaded in parallel vs sequentially
      if ( mActiveWatchers.size() > 1 )
      {
        mStatistics.layersLoadedInParallel++;
      }
      else
      {
        mStatistics.layersLoadedSequentially++;
      }
    }
    else if ( task.hasError )
    {
      mStatistics.layersWithErrors++;
    }
  }
}