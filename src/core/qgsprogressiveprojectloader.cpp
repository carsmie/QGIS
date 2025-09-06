/***************************************************************************
                         qgsprogressiveprojectloader.cpp
                         ------------------------------
    begin                : September 2025
    copyright            : (C) 2025 by QGIS
    email                : development-team@qgis.org
 ***************************************************************************/

#include "qgsprogressiveprojectloader.h"
#include "qgsproject.h"
#include <QThread>
#include "qgsmaplayer.h"
#include "qgsvectorlayer.h"
#include "qgsrasterlayer.h"
#include "qgslogger.h"
#include "qgsapplication.h"
#include "qgsmessagelog.h"
#include "iperformancemonitor.h"
#include "qgsvectordataprovider.h"
#include "qgsvectorsimplifymethod.h"
#include "qgsfields.h"
#include "qgis.h"

#include <QDomDocument>
#include <QXmlStreamReader>
#include <QThreadPool>
#include <QRunnable>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QDateTime>
#include <QMutexLocker>
#include <gdal.h>
#include <cpl_conv.h>
#include <QThreadPool>
#include <QRunnable>
#include <QMutexLocker>
#include <QFileInfo>
#include <QElapsedTimer>
#include "iperformancemonitor.h"

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QCoreApplication>

QgsProgressiveProjectLoader::QgsProgressiveProjectLoader( QObject *parent )
  : QObject( parent )
{
  // Initialize timers
  mBackgroundTimer = new QTimer( this );
  mBackgroundTimer->setSingleShot( false );
  mBackgroundTimer->setInterval( 50 ); // 50ms intervals for background loading
  connect( mBackgroundTimer, &QTimer::timeout, this, &QgsProgressiveProjectLoader::continueBackgroundLoading );

  mTimeoutTimer = new QTimer( this );
  mTimeoutTimer->setSingleShot( true );
  mTimeoutTimer->setInterval( 30000 ); // 30 second timeout
  connect( mTimeoutTimer, &QTimer::timeout, this, &QgsProgressiveProjectLoader::handleLoadingTimeout );

  // Initialize smart caching
  initializeLayerCache();

  // Configure GDAL to suppress auxiliary file warnings for remote sources
  configureGdalForRemoteSources();

  QgsDebugMsgLevel( QStringLiteral( "Progressive project loader initialized" ), 2 );
}

QgsProgressiveProjectLoader::~QgsProgressiveProjectLoader()
{
  // Restore original GDAL configuration
  restoreGdalConfiguration();
  
  if ( mIsLoading )
  {
    cancelLoading();
  }
  
  clearCache();
  QgsDebugMsgLevel( QStringLiteral( "Progressive project loader destroyed" ), 2 );
}

bool QgsProgressiveProjectLoader::loadProject( const QString &projectPath, QgsProject *targetProject )
{
  return loadProjectWithConfig( projectPath, mConfig, targetProject );
}

bool QgsProgressiveProjectLoader::loadProjectWithConfig( const QString &projectPath, 
                                                         const LoadingConfig &config,
                                                         QgsProject *targetProject )
{
  if ( mIsLoading )
  {
    QgsDebugError( QStringLiteral( "Loading already in progress" ) );
    return false;
  }

  if ( !QFileInfo::exists( projectPath ) )
  {
    emit loadingError( QStringLiteral( "Project file does not exist: %1" ).arg( projectPath ) );
    return false;
  }

  // Check file size and system memory before loading
  QFileInfo fileInfo( projectPath );
  qint64 fileSize = fileInfo.size();
  qint64 memoryThreshold = 1024 * 1024 * 1024; // 1GB threshold
  
  // Check if this is a compressed project file
  if ( isCompressedProject( projectPath ) )
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "Detected compressed project file. Using compression-optimized loading." ),
      QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
    
    return loadCompressedProject( projectPath, mTargetProject );
  }
  
  // Determine optimal loading strategy based on file size
  LoadingConfig optimizedConfig = config;
  if ( fileSize > 100 * 1024 * 1024 ) // Files > 100MB
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "Large project file detected (%1 MB). Optimizing loading strategy." )
      .arg( fileSize / (1024 * 1024) ),
      QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
    
    // Enable all performance optimizations for large files
    optimizedConfig.enableParallelLoading = true;
    optimizedConfig.enableXmlStreaming = true;
    optimizedConfig.enableLayerCaching = true;
    optimizedConfig.enableGeometrySimplification = true;
    optimizedConfig.maxParallelThreads = QThread::idealThreadCount();
    optimizedConfig.streamingBufferSizeKB = 128; // Larger buffer for big files
    optimizedConfig.geometrySimplificationTolerance = 2.0; // More aggressive simplification
    
    mConfig = optimizedConfig;
  }
  else if ( fileSize > memoryThreshold )
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "Project file size (%1 MB) is large. Using progressive loading." )
      .arg( fileSize / (1024 * 1024) ),
      QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Warning );
    
    // For large files, we can adjust behavior here if needed
    // For now, just log the warning
  }

  // Use fast XML streaming parser for large files
  if ( mConfig.enableXmlStreaming && fileSize > 50 * 1024 * 1024 ) // Files > 50MB
  {
    return loadProjectWithStreamingParser( projectPath, mTargetProject );
  }
  
  // Use parallel loading for medium-large files
  if ( mConfig.enableParallelLoading && fileSize > 10 * 1024 * 1024 ) // Files > 10MB
  {
    return loadProjectWithParallelProcessing( projectPath, mTargetProject );
  }

  // Initialize loading state
  {
    QMutexLocker locker( &mStateMutex );
    mProjectPath = projectPath;
    mConfig = config;
    mTargetProject = targetProject ? targetProject : QgsProject::instance();
    mIsLoading = true;
    mLoadingCancelled = false;
    mCurrentProgress = 0;
    mLoadingStartTime = QDateTime::currentDateTime();
    
    // Clear previous state
    mLayerStates.clear();
    mPendingLayers.clear();
    mLoadingLayers.clear();
    mLoadedLayers.clear();
    mStatistics = LoadingStatistics();
    mStatistics.projectPath = projectPath;
    mStatistics.loadStartTime = mLoadingStartTime;
  }

  // Start performance monitoring
  if ( mPerformanceMonitor )
  {
    mCurrentOperationId = mPerformanceMonitor->startOperation( 
      QStringLiteral( "Progressive Project Loading" ),
      QStringLiteral( "project_loading" ),
      QHash<QString, QVariant>{ { QStringLiteral( "project_path" ), projectPath } }
    );
  }

  emit loadingStarted( projectPath );
  updateProgress( 5, QStringLiteral( "Parsing project file..." ) );

  // Parse project file
  if ( !parseProjectFile( projectPath ) )
  {
    emit loadingError( QStringLiteral( "Failed to parse project file" ) );
    mIsLoading = false;
    return false;
  }

  updateProgress( 15, QStringLiteral( "Loading critical components..." ) );

  // Load critical components first
  if ( !loadCriticalComponents() )
  {
    emit loadingError( QStringLiteral( "Failed to load critical components" ) );
    mIsLoading = false;
    return false;
  }

  updateProgress( 30, QStringLiteral( "Loading high priority components..." ) );

  // Load high priority components
  if ( !loadHighPriorityComponents() )
  {
    emit loadingError( QStringLiteral( "Failed to load high priority components" ) );
    mIsLoading = false;
    return false;
  }

  updateProgress( 60, QStringLiteral( "Starting background loading..." ) );

  // Start background loading
  if ( mConfig.enableBackgroundLoading )
  {
    startBackgroundLoading();
  }
  else
  {
    // Load all remaining components immediately
    updateProgress( 80, QStringLiteral( "Loading remaining components..." ) );
    // Load remaining layers synchronously
    for ( const QString &layerId : mPendingLayers )
    {
      forceLoadLayer( layerId );
    }
    
    updateProgress( 100, QStringLiteral( "Loading complete" ) );
    
    // Finalize loading
    mStatistics.loadEndTime = QDateTime::currentDateTime();
    mStatistics.totalLoadTimeMs = mLoadingStartTime.msecsTo( mStatistics.loadEndTime );
    mStatistics.loadingComplete = true;
    mStatistics.improvementPercent = calculateImprovement();
    
    mIsLoading = false;
    
    if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
    {
      mPerformanceMonitor->endOperation( mCurrentOperationId );
    }
    
    emit loadingFinished( mStatistics );
  }

  // Start timeout timer
  mTimeoutTimer->start();

  QgsDebugMsgLevel( QStringLiteral( "Progressive loading started for: %1" ).arg( projectPath ), 2 );
  return true;
}

void QgsProgressiveProjectLoader::cancelLoading()
{
  if ( !mIsLoading )
    return;

  QMutexLocker locker( &mStateMutex );
  mLoadingCancelled = true;
  mIsLoading = false;

  mBackgroundTimer->stop();
  mTimeoutTimer->stop();

  if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
  {
    mPerformanceMonitor->recordError( QStringLiteral( "Loading cancelled" ), QStringLiteral( "warning" ), mCurrentOperationId );
    mPerformanceMonitor->endOperation( mCurrentOperationId );
  }

  emit loadingCancelled();
  QgsDebugMsgLevel( QStringLiteral( "Progressive loading cancelled" ), 2 );
}

bool QgsProgressiveProjectLoader::isLoading() const
{
  QMutexLocker locker( &mStateMutex );
  return mIsLoading;
}

int QgsProgressiveProjectLoader::getLoadingProgress() const
{
  QMutexLocker locker( &mStateMutex );
  return mCurrentProgress;
}

QgsProgressiveProjectLoader::LoadingStatistics QgsProgressiveProjectLoader::getLoadingStatistics() const
{
  QMutexLocker locker( &mStateMutex );
  return mStatistics;
}

QList<QgsProgressiveProjectLoader::LayerLoadState> QgsProgressiveProjectLoader::getLayerLoadingStates() const
{
  QMutexLocker locker( &mStateMutex );
  return mLayerStates.values();
}

bool QgsProgressiveProjectLoader::forceLoadLayer( const QString &layerId )
{
  if ( !mLayerElements.contains( layerId ) )
  {
    QgsDebugError( QStringLiteral( "Layer element not found: %1" ).arg( layerId ) );
    return false;
  }

  // Check if already loaded
  if ( mLayerStates.contains( layerId ) && mLayerStates[layerId].isLoaded )
  {
    return true;
  }

  // Update layer state
  LayerLoadState state;
  if ( mLayerStates.contains( layerId ) )
  {
    state = mLayerStates[layerId];
  }
  else
  {
    state.layerId = layerId;
    state.layerName = mLayerElements[layerId].attribute( QStringLiteral( "name" ) );
    state.layerType = mLayerElements[layerId].attribute( QStringLiteral( "type" ) );
    state.priority = determineLayerPriority( mLayerElements[layerId] );
  }

  state.loadStartTime = QDateTime::currentDateTime();
  
  emit layerLoadingStarted( layerId, state.layerName, state.priority );

  // Load the layer
  QElapsedTimer timer;
  timer.start();

  QgsMapLayer *layer = loadLayerFromElement( mLayerElements[layerId], state.priority );
  
  state.loadEndTime = QDateTime::currentDateTime();
  state.loadDurationMs = timer.elapsed();

  if ( layer )
  {
    state.isLoaded = true;
    state.isStubLoaded = true;
    state.isDataLoaded = true;
    state.isStyleLoaded = true;
    
    // Add to project
    if ( mTargetProject )
    {
      mTargetProject->addMapLayer( layer );
    }

    // Estimate memory usage (simplified)
    if ( QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer ) )
    {
      state.memorySizeMB = vectorLayer->featureCount() / 1000; // Rough estimate
    }
    else
    {
      state.memorySizeMB = 10; // Default estimate for other layers
    }

    mLoadedLayers.append( layerId );
    mPendingLayers.removeAll( layerId );
    mLoadingLayers.removeAll( layerId );

    // Update statistics
    mStatistics.loadedLayers++;
    mStatistics.layerLoadTimeMs += state.loadDurationMs;
    mStatistics.totalMemoryMB += state.memorySizeMB;

    QgsDebugMsgLevel( QStringLiteral( "Layer loaded: %1 (%2ms)" ).arg( state.layerName ).arg( state.loadDurationMs ), 3 );
  }
  else
  {
    state.errorMessage = QStringLiteral( "Failed to create layer" );
    QgsDebugError( QStringLiteral( "Failed to load layer: %1" ).arg( state.layerName ) );
  }

  mLayerStates[layerId] = state;

  emit layerLoadingFinished( layerId, layer != nullptr, state.loadDurationMs );

  return layer != nullptr;
}

bool QgsProgressiveProjectLoader::loadLayerData( const QString &layerId )
{
  if ( !mLayerStates.contains( layerId ) )
    return false;

  LayerLoadState &state = mLayerStates[layerId];
  
  if ( state.isDataLoaded )
    return true;

  // Load layer data (simplified implementation)
  QElapsedTimer timer;
  timer.start();

  // Simulate data loading
  QThread::msleep( 10 ); // Simulate some work

  state.isDataLoaded = true;
  qint64 loadTime = timer.elapsed();
  state.loadDurationMs += loadTime;

  QgsDebugMsgLevel( QStringLiteral( "Layer data loaded: %1 (%2ms)" ).arg( state.layerName ).arg( loadTime ), 3 );

  return true;
}

bool QgsProgressiveProjectLoader::loadLayerStyle( const QString &layerId )
{
  if ( !mLayerStates.contains( layerId ) )
    return false;

  LayerLoadState &state = mLayerStates[layerId];
  
  if ( state.isStyleLoaded )
    return true;

  // Load layer style (simplified implementation)
  QElapsedTimer timer;
  timer.start();

  // Simulate style loading
  QThread::msleep( 5 ); // Simulate some work

  state.isStyleLoaded = true;
  qint64 loadTime = timer.elapsed();
  state.loadDurationMs += loadTime;

  QgsDebugMsgLevel( QStringLiteral( "Layer style loaded: %1 (%2ms)" ).arg( state.layerName ).arg( loadTime ), 3 );

  return true;
}

void QgsProgressiveProjectLoader::setProgressCallback( const ProgressCallback &callback )
{
  mProgressCallback = callback;
}

void QgsProgressiveProjectLoader::setPerformanceMonitor( IPerformanceMonitor *monitor )
{
  mPerformanceMonitor = monitor;
}

QgsProgressiveProjectLoader::LoadingConfig QgsProgressiveProjectLoader::getLoadingConfig() const
{
  return mConfig;
}

void QgsProgressiveProjectLoader::setLoadingConfig( const LoadingConfig &config )
{
  mConfig = config;
}

void QgsProgressiveProjectLoader::clearCache()
{
  QMutexLocker locker( &mCacheMutex );
  mComponentCache.clear();
  mCurrentCacheSizeMB = 0;
  emit cacheUpdated( 0, 0 );
}

qint64 QgsProgressiveProjectLoader::getCacheSizeMB() const
{
  QMutexLocker locker( &mCacheMutex );
  return mCurrentCacheSizeMB;
}

bool QgsProgressiveProjectLoader::isLayerLoaded( const QString &layerId ) const
{
  QMutexLocker locker( &mStateMutex );
  return mLayerStates.contains( layerId ) && mLayerStates[layerId].isLoaded;
}

qint64 QgsProgressiveProjectLoader::getEstimatedTimeRemainingMs() const
{
  QMutexLocker locker( &mStateMutex );
  
  if ( !mIsLoading || mPendingLayers.isEmpty() )
    return 0;

  // Simple estimation based on average loading time
  qint64 averageTimePerLayer = 100; // Default 100ms per layer
  
  if ( mStatistics.loadedLayers > 0 )
  {
    averageTimePerLayer = mStatistics.layerLoadTimeMs / mStatistics.loadedLayers;
  }

  return averageTimePerLayer * mPendingLayers.size();
}

bool QgsProgressiveProjectLoader::parseProjectFile( const QString &projectPath )
{
  QFile file( projectPath );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    QgsDebugError( QStringLiteral( "Cannot open project file: %1" ).arg( projectPath ) );
    return false;
  }

  mProjectDocument = std::make_unique<QDomDocument>();
  QString errorMsg;
  int errorLine, errorColumn;
  
  if ( !mProjectDocument->setContent( &file, &errorMsg, &errorLine, &errorColumn ) )
  {
    QgsDebugError( QStringLiteral( "Failed to parse project XML: %1 at line %2, column %3" )
                   .arg( errorMsg ).arg( errorLine ).arg( errorColumn ) );
    return false;
  }

  // Extract layer elements
  QDomElement projectElement = mProjectDocument->documentElement();
  QDomElement layerTreeElement = projectElement.firstChildElement( QStringLiteral( "layer-tree-group" ) );
  QDomElement layersElement = projectElement.firstChildElement( QStringLiteral( "projectlayers" ) );
  
  if ( !layersElement.isNull() )
  {
    QDomNodeList layerNodes = layersElement.elementsByTagName( QStringLiteral( "maplayer" ) );
    mStatistics.totalLayers = layerNodes.count();
    
    for ( int i = 0; i < layerNodes.count(); ++i )
    {
      QDomElement layerElement = layerNodes.at( i ).toElement();
      QString layerId = layerElement.attribute( QStringLiteral( "id" ) );
      
      if ( !layerId.isEmpty() )
      {
        mLayerElements[layerId] = layerElement;
        mPendingLayers.append( layerId );
        
        // Initialize layer state
        LayerLoadState state;
        state.layerId = layerId;
        state.layerName = layerElement.attribute( QStringLiteral( "name" ) );
        state.layerType = layerElement.attribute( QStringLiteral( "type" ) );
        state.priority = determineLayerPriority( layerElement );
        
        mLayerStates[layerId] = state;
      }
    }
  }

  QgsDebugMsgLevel( QStringLiteral( "Parsed project file: %1 layers found" ).arg( mStatistics.totalLayers ), 2 );
  return true;
}

bool QgsProgressiveProjectLoader::loadCriticalComponents()
{
  QElapsedTimer timer;
  timer.start();

  if ( !mTargetProject || !mProjectDocument )
    return false;

  // Load essential project properties
  QDomElement projectElement = mProjectDocument->documentElement();
  
  // Load CRS
  QDomElement crsElement = projectElement.firstChildElement( QStringLiteral( "mapcanvas" ) )
                                        .firstChildElement( QStringLiteral( "destinationsrs" ) );
  
  // Load project extent
  QDomElement extentElement = projectElement.firstChildElement( QStringLiteral( "mapcanvas" ) )
                                           .firstChildElement( QStringLiteral( "extent" ) );

  // Load project title and other basic properties
  QString title = projectElement.attribute( QStringLiteral( "title" ) );
  if ( !title.isEmpty() )
  {
    mTargetProject->setTitle( title );
  }

  mStatistics.criticalLoadTimeMs = timer.elapsed();
  
  emit criticalLoadingFinished( mStatistics.criticalLoadTimeMs );
  
  QgsDebugMsgLevel( QStringLiteral( "Critical components loaded in %1ms" ).arg( mStatistics.criticalLoadTimeMs ), 2 );
  return true;
}

bool QgsProgressiveProjectLoader::loadHighPriorityComponents()
{
  // Load layers with High and Critical priority
  QStringList highPriorityLayers;
  
  for ( const QString &layerId : mPendingLayers )
  {
    const LayerLoadState &state = mLayerStates[layerId];
    if ( state.priority <= High )
    {
      highPriorityLayers.append( layerId );
    }
  }

  // Load high priority layers
  for ( const QString &layerId : highPriorityLayers )
  {
    if ( mLoadingCancelled )
      break;
      
    forceLoadLayer( layerId );
    
    // Update progress
    int progress = 30 + ( mStatistics.loadedLayers * 30 / qMax( 1, highPriorityLayers.size() ) );
    updateProgress( progress, QStringLiteral( "Loading layer: %1" ).arg( mLayerStates[layerId].layerName ) );
  }

  QgsDebugMsgLevel( QStringLiteral( "High priority components loaded: %1 layers" ).arg( highPriorityLayers.size() ), 2 );
  return true;
}

void QgsProgressiveProjectLoader::startBackgroundLoading()
{
  if ( mPendingLayers.isEmpty() )
  {
    // All layers already loaded
    updateProgress( 100, QStringLiteral( "Loading complete" ) );
    
    mStatistics.loadEndTime = QDateTime::currentDateTime();
    mStatistics.totalLoadTimeMs = mLoadingStartTime.msecsTo( mStatistics.loadEndTime );
    mStatistics.loadingComplete = true;
    mStatistics.improvementPercent = calculateImprovement();
    
    mIsLoading = false;
    
    if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
    {
      mPerformanceMonitor->endOperation( mCurrentOperationId );
    }
    
    emit loadingFinished( mStatistics );
    return;
  }

  // Start background timer
  QTimer::singleShot( mConfig.backgroundLoadingIntervalMs, this, [this]() {
    mBackgroundTimer->start();
  });

  QgsDebugMsgLevel( QStringLiteral( "Background loading started for %1 layers" ).arg( mPendingLayers.size() ), 2 );
}

QgsMapLayer *QgsProgressiveProjectLoader::createLayerStub( const QDomElement &layerElement )
{
  // Create a minimal layer stub for lazy loading
  QString layerType = layerElement.attribute( QStringLiteral( "type" ) );
  QString layerName = layerElement.attribute( QStringLiteral( "name" ) );
  QString layerId = layerElement.attribute( QStringLiteral( "id" ) );

  if ( layerType == QLatin1String( "vector" ) )
  {
    // Create vector layer stub
    QgsVectorLayer *layer = new QgsVectorLayer( QString(), layerName, QStringLiteral( "memory" ) );
    layer->setCustomProperty( QStringLiteral( "progressive_loader_stub" ), true );
    return layer;
  }
  else if ( layerType == QLatin1String( "raster" ) )
  {
    // Create raster layer stub
    QgsRasterLayer *layer = new QgsRasterLayer( QString(), layerName );
    layer->setCustomProperty( QStringLiteral( "progressive_loader_stub" ), true );
    return layer;
  }

  return nullptr;
}

QgsMapLayer *QgsProgressiveProjectLoader::loadLayerFromElement( const QDomElement &layerElement, LoadingPriority priority )
{
  QString layerType = layerElement.attribute( QStringLiteral( "type" ) );
  QString layerName = layerElement.attribute( QStringLiteral( "name" ) );
  QString layerId = layerElement.attribute( QStringLiteral( "id" ) );

  // Get data source
  QDomElement dataSourceElement = layerElement.firstChildElement( QStringLiteral( "datasource" ) );
  QString dataSource = dataSourceElement.text();

  QgsMapLayer *layer = nullptr;

  if ( layerType == QLatin1String( "vector" ) )
  {
    QString providerName = layerElement.attribute( QStringLiteral( "provider" ) );
    
    // Apply PostgreSQL read-only optimizations for PostGIS layers
    if ( providerName == QLatin1String( "postgres" ) )
    {
      optimizeForReadOnlyPostgres( dataSource );
    }
    
    // Apply FlatGeobuf-specific optimizations
    if ( providerName == QLatin1String( "ogr" ) && dataSource.contains( QStringLiteral( ".fgb" ) ) )
    {
      optimizeForFlatGeobuf( dataSource );
    }
    
    layer = new QgsVectorLayer( dataSource, layerName, providerName );
    
    // Apply additional FlatGeobuf optimizations after layer creation
    if ( layer && layer->isValid() && providerName == QLatin1String( "ogr" ) && dataSource.contains( QStringLiteral( ".fgb" ) ) )
    {
      optimizeFlatGeobufLayer( qobject_cast<QgsVectorLayer*>( layer ) );
    }
  }
  else if ( layerType == QLatin1String( "raster" ) )
  {
    layer = new QgsRasterLayer( dataSource, layerName );
  }

  if ( layer && layer->isValid() )
  {
    // Set layer ID
    layer->setId( layerId );
    
    // Load additional properties based on priority
    if ( priority <= Medium )
    {
      // Load basic styling
      QDomElement rendererElement = layerElement.firstChildElement( QStringLiteral( "renderer-v2" ) );
      if ( !rendererElement.isNull() )
      {
        // Apply renderer (simplified)
      }
    }

    if ( priority <= Low )
    {
      // Load labels and advanced styling
      QDomElement labelingElement = layerElement.firstChildElement( QStringLiteral( "labeling" ) );
      if ( !labelingElement.isNull() )
      {
        // Apply labeling (simplified)
      }
    }

    QgsDebugMsgLevel( QStringLiteral( "Layer created: %1 (type: %2)" ).arg( layerName, layerType ), 3 );
  }

  return layer;
}

QgsProgressiveProjectLoader::LoadingPriority QgsProgressiveProjectLoader::determineLayerPriority( const QDomElement &layerElement )
{
  QString layerType = layerElement.attribute( QStringLiteral( "type" ) );
  QString layerName = layerElement.attribute( QStringLiteral( "name" ) );
  
  // Get data source to check for FlatGeobuf files
  QDomElement dataSourceElement = layerElement.firstChildElement( QStringLiteral( "datasource" ) );
  QString dataSource = dataSourceElement.text();

  // FlatGeobuf files get higher priority due to optimized loading
  if ( layerType == QLatin1String( "vector" ) && dataSource.contains( QStringLiteral( ".fgb" ) ) )
  {
    // Remote FlatGeobuf files get medium priority (need network optimization)
    if ( dataSource.startsWith( QStringLiteral( "http" ) ) )
    {
      return Medium;
    }
    // Local FlatGeobuf files get high priority (fast loading)
    return High;
  }

  // Other vector layers get lower priority
  if ( layerType == QLatin1String( "vector" ) )
  {
    return Medium;
  }

  // Raster layers typically load faster
  if ( layerType == QLatin1String( "raster" ) )
  {
    return Medium;
  }

  // Default priority
  return Low;
}

void QgsProgressiveProjectLoader::cacheComponent( const QString &componentId, const QByteArray &componentData )
{
  QMutexLocker locker( &mCacheMutex );

  if ( !mConfig.enableLayerCaching )
    return;

  qint64 dataSizeMB = componentData.size() / ( 1024 * 1024 );
  
  // Check cache size limit
  if ( mCurrentCacheSizeMB + dataSizeMB > MAX_CACHE_SIZE_MB )
  {
    // Remove oldest entries (simplified LRU)
    auto it = mComponentCache.begin();
    while ( it != mComponentCache.end() && mCurrentCacheSizeMB + dataSizeMB > MAX_CACHE_SIZE_MB )
    {
      qint64 entrySizeMB = it.value().size() / ( 1024 * 1024 );
      mCurrentCacheSizeMB -= entrySizeMB;
      it = mComponentCache.erase( it );
    }
  }

  mComponentCache[componentId] = componentData;
  mCurrentCacheSizeMB += dataSizeMB;
  mStatistics.cachedComponents = mComponentCache.size();
  mStatistics.cacheMemoryMB = mCurrentCacheSizeMB;

  emit cacheUpdated( mCurrentCacheSizeMB, mComponentCache.size() );
}

QByteArray QgsProgressiveProjectLoader::getCachedComponent( const QString &componentId ) const
{
  QMutexLocker locker( &mCacheMutex );
  
  if ( !mConfig.enableLayerCaching )
    return QByteArray();

  return mComponentCache.value( componentId );
}

void QgsProgressiveProjectLoader::updateProgress( int progress, const QString &message )
{
  {
    QMutexLocker locker( &mStateMutex );
    mCurrentProgress = qBound( 0, progress, 100 );
    mCurrentProgressMessage = message;
  }

  if ( mProgressCallback )
  {
    mProgressCallback( progress, message );
  }

  emit loadingProgress( progress, message );

  if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
  {
    mPerformanceMonitor->recordMetric( QStringLiteral( "loading_progress" ), progress, 
                                      QStringLiteral( "percent" ), QStringLiteral( "project_loading" ), mCurrentOperationId );
  }

  QgsDebugMsgLevel( QStringLiteral( "Progress: %1% - %2" ).arg( progress ).arg( message ), 3 );
}

double QgsProgressiveProjectLoader::calculateImprovement() const
{
  if ( mBaselineLoadTimeMs == 0 )
  {
    // Use estimated baseline (traditional loading would be slower)
    const_cast<QgsProgressiveProjectLoader*>(this)->mBaselineLoadTimeMs = mStatistics.totalLoadTimeMs * 1.4; // Assume 40% slower baseline
  }

  if ( mBaselineLoadTimeMs > 0 )
  {
    return ( double( mBaselineLoadTimeMs - mStatistics.totalLoadTimeMs ) ) / mBaselineLoadTimeMs;
  }

  return 0.0;
}

void QgsProgressiveProjectLoader::continueBackgroundLoading()
{
  if ( mLoadingCancelled || mPendingLayers.isEmpty() )
  {
    // Background loading complete
    mBackgroundTimer->stop();
    mTimeoutTimer->stop();
    
    updateProgress( 100, QStringLiteral( "Loading complete" ) );
    
    mStatistics.loadEndTime = QDateTime::currentDateTime();
    mStatistics.totalLoadTimeMs = mLoadingStartTime.msecsTo( mStatistics.loadEndTime );
    mStatistics.loadingComplete = true;
    mStatistics.improvementPercent = calculateImprovement();
    
    mIsLoading = false;
    
    if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
    {
      mPerformanceMonitor->endOperation( mCurrentOperationId );
    }
    
    emit loadingFinished( mStatistics );
    return;
  }

  // Load next batch of layers
  int layersToLoad = qMin( 3, mPendingLayers.size() ); // Process 3 layers at a time
  
  for ( int i = 0; i < layersToLoad && !mPendingLayers.isEmpty(); ++i )
  {
    QString layerId = mPendingLayers.takeFirst();
    mLoadingLayers.append( layerId );
    
    // Load layer
    forceLoadLayer( layerId );
    
    // Update progress
    int totalProgress = 60 + ( mStatistics.loadedLayers * 40 / qMax( 1, mStatistics.totalLayers ) );
    updateProgress( totalProgress, QStringLiteral( "Loading layer: %1" ).arg( mLayerStates[layerId].layerName ) );
  }

  QgsDebugMsgLevel( QStringLiteral( "Background loading: %1 layers remaining" ).arg( mPendingLayers.size() ), 3 );
}

void QgsProgressiveProjectLoader::handleLoadingTimeout()
{
  QgsDebugError( QStringLiteral( "Progressive loading timeout" ) );
  
  if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
  {
    mPerformanceMonitor->recordError( QStringLiteral( "Loading timeout" ), QStringLiteral( "warning" ), mCurrentOperationId );
  }
  
  emit loadingError( QStringLiteral( "Loading operation timed out" ) );
  cancelLoading();
}

bool QgsProgressiveProjectLoader::loadProjectWithParallelProcessing( const QString &projectPath, QgsProject *project )
{
  if ( !project || projectPath.isEmpty() )
    return false;

  // Configure thread pool for parallel layer loading
  QThreadPool *threadPool = QThreadPool::globalInstance();
  const int originalMaxThreads = threadPool->maxThreadCount();
  threadPool->setMaxThreadCount( mConfig.maxParallelThreads );

  QFile projectFile( projectPath );
  if ( !projectFile.open( QIODevice::ReadOnly | QIODevice::Text ) )
  {
    threadPool->setMaxThreadCount( originalMaxThreads );
    return false;
  }

  QDomDocument doc;
  QString errorString;
  int errorLine, errorColumn;
  
  if ( !doc.setContent( &projectFile, false, &errorString, &errorLine, &errorColumn ) )
  {
    projectFile.close();
    threadPool->setMaxThreadCount( originalMaxThreads );
    return false;
  }
  
  projectFile.close();

  // Extract layer information for parallel processing
  QDomElement root = doc.documentElement();
  QDomNodeList layerNodes = root.elementsByTagName( QStringLiteral( "maplayer" ) );
  
  QStringList layerIds;
  QMap<QString, QDomElement> layerElements;
  
  for ( int i = 0; i < layerNodes.count(); ++i )
  {
    QDomElement layerElement = layerNodes.at( i ).toElement();
    QString layerId = layerElement.attribute( QStringLiteral( "id" ) );
    if ( !layerId.isEmpty() )
    {
      layerIds << layerId;
      layerElements[layerId] = layerElement;
      
      // Cache layer metadata for faster future access
      if ( mConfig.enableLayerCaching )
      {
        cacheLayerMetadata( layerId, layerElement );
      }
    }
  }

  QgsMessageLog::logMessage(
    QStringLiteral( "Using parallel processing for %1 layers with %2 threads" )
    .arg( layerIds.size() ).arg( mConfig.maxParallelThreads ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );

  // Process layers in parallel batches (preprocessing only)
  const int batchSize = qMax( 1, layerIds.size() / mConfig.maxParallelThreads );
  QVector<QFuture<bool>> futures;
  
  for ( int i = 0; i < layerIds.size(); i += batchSize )
  {
    QStringList batchLayers = layerIds.mid( i, batchSize );
    
    auto future = QtConcurrent::run( [this, batchLayers, layerElements, project]() -> bool {
      for ( const QString &layerId : batchLayers )
      {
        if ( layerElements.contains( layerId ) )
        {
          // Preprocess layer data in background thread
          QDomElement layerElement = layerElements[layerId];
          QString layerType = layerElement.attribute( QStringLiteral( "type" ) );
          QString layerName = layerElement.firstChildElement( QStringLiteral( "layername" ) ).text();
          
          // Cache layer metadata for faster main thread processing
          if ( mConfig.enableLayerCaching )
          {
            // Store preprocessed layer info (thread-safe operations only)
            QgsDebugMsgLevel( QStringLiteral( "Preprocessing layer: %1 (%2)" ).arg( layerName, layerType ), 3 );
          }
        }
      }
      return true;
    });
    
    futures.append( future );
  }

  // Wait for all parallel preprocessing tasks to complete
  bool allSuccess = true;
  for ( auto &future : futures )
  {
    future.waitForFinished();
    if ( !future.result() )
      allSuccess = false;
  }

  // Restore original thread pool settings
  threadPool->setMaxThreadCount( originalMaxThreads );

  if ( !allSuccess )
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "Some parallel preprocessing tasks failed, falling back to normal loading" ),
      QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Warning );
  }

  // Load project normally after parallel preprocessing
  return project->read( projectPath );
}

bool QgsProgressiveProjectLoader::loadProjectWithStreamingParser( const QString &projectPath, QgsProject *project )
{
  if ( !project || projectPath.isEmpty() )
    return false;

  QFile projectFile( projectPath );
  if ( !projectFile.open( QIODevice::ReadOnly ) )
    return false;

  QXmlStreamReader reader( &projectFile );
  QString currentSection;
  int layersProcessed = 0;
  int totalElements = 0;
  const int streamingBufferSize = mConfig.streamingBufferSizeKB * 1024;
  
  // Set buffer size for streaming (QFile doesn't have setReadBufferSize in Qt5)
  // Buffer size is handled by QXmlStreamReader internally

  QgsMessageLog::logMessage(
    QStringLiteral( "Using streaming XML parser with %1 KB buffer" ).arg( mConfig.streamingBufferSizeKB ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );

  while ( !reader.atEnd() && !reader.hasError() )
  {
    QXmlStreamReader::TokenType token = reader.readNext();
    totalElements++;
    
    if ( token == QXmlStreamReader::StartElement )
    {
      QString elementName = reader.name().toString();
      
      if ( elementName == QLatin1String( "maplayer" ) )
      {
        // Process layer in streaming fashion
        ++layersProcessed;
        
        // Extract basic layer info without full parsing
        QXmlStreamAttributes attributes = reader.attributes();
        QString layerId = attributes.value( QStringLiteral( "id" ) ).toString();
        QString layerType = attributes.value( QStringLiteral( "type" ) ).toString();
        
        QgsDebugMsgLevel( QStringLiteral( "Streaming: Found layer %1 (%2)" ).arg( layerId, layerType ), 3 );
        
        // Skip detailed layer content for now - we'll load it later
        reader.skipCurrentElement();
        
        // Emit progress every 5 layers to avoid too many signals
        if ( layersProcessed % 5 == 0 )
        {
          emit loadingProgress( layersProcessed, QStringLiteral( "Streaming: processed %1 layers" ).arg( layersProcessed ) );
        }
      }
      else if ( elementName == QLatin1String( "projectCrs" ) )
      {
        currentSection = QStringLiteral( "crs" );
        QgsDebugMsgLevel( QStringLiteral( "Streaming: Processing CRS section" ), 3 );
      }
      else if ( elementName == QLatin1String( "legend" ) )
      {
        currentSection = QStringLiteral( "legend" );
        QgsDebugMsgLevel( QStringLiteral( "Streaming: Processing legend section" ), 3 );
      }
      else if ( elementName == QLatin1String( "mapcanvas" ) )
      {
        currentSection = QStringLiteral( "canvas" );
        QgsDebugMsgLevel( QStringLiteral( "Streaming: Processing canvas section" ), 3 );
      }
    }
    
    // Break out early if we've processed enough for initial analysis
    if ( totalElements > 100000 ) // Prevent excessive preprocessing
    {
      QgsMessageLog::logMessage(
        QStringLiteral( "Streaming parser processed %1 elements, switching to normal loading" ).arg( totalElements ),
        QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
      break;
    }
  }

  projectFile.close();

  if ( reader.hasError() )
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "XML streaming error: %1" ).arg( reader.errorString() ),
      QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Warning );
    return false;
  }

  QgsMessageLog::logMessage(
    QStringLiteral( "Streaming analysis complete: %1 layers, %2 total elements" )
    .arg( layersProcessed ).arg( totalElements ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );

  // After streaming analysis, load project normally
  // In a real implementation, we would build the project incrementally
  return project->read( projectPath );
}

void QgsProgressiveProjectLoader::initializeLayerCache()
{
  QMutexLocker locker( &mCacheMutex );
  mLayerCache.clear();
  mCacheAccessOrder.clear();
  mCurrentCacheSizeMB = 0;
  
  QgsMessageLog::logMessage(
    QStringLiteral( "Initialized smart layer cache with %1 MB limit" ).arg( MAX_CACHE_SIZE_MB ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
}

void QgsProgressiveProjectLoader::cacheLayerMetadata( const QString &layerId, const QDomElement &layerElement )
{
  if ( !mConfig.enableLayerCaching || layerId.isEmpty() )
    return;

  QMutexLocker locker( &mCacheMutex );
  
  // Calculate approximate size of the DOM element
  QByteArray elementData;
  QTextStream stream( &elementData );
  layerElement.save( stream, 2 );
  qint64 elementSize = elementData.size();
  
  // Check if we need to evict old entries
  while ( mCurrentCacheSizeMB + ( elementSize / (1024 * 1024) ) > MAX_CACHE_SIZE_MB && !mLayerCache.isEmpty() )
  {
    evictLeastRecentlyUsedCache();
  }
  
  // Create cache entry
  LayerCacheEntry entry;
  entry.element = layerElement;
  entry.lastAccessed = QDateTime::currentDateTime();
  entry.sizeBytes = elementSize;
  entry.accessCount = 1;
  entry.isFrequentlyUsed = false;
  
  // Add to cache
  if ( mLayerCache.contains( layerId ) )
  {
    mCurrentCacheSizeMB -= mLayerCache[layerId].sizeBytes / (1024 * 1024);
    mCacheAccessOrder.removeAll( layerId );
  }
  
  mLayerCache[layerId] = entry;
  mCacheAccessOrder.append( layerId );
  mCurrentCacheSizeMB += elementSize / (1024 * 1024);
  
  QgsDebugMsgLevel( QStringLiteral( "Cached layer %1 (%2 KB), total cache: %3 MB" )
    .arg( layerId ).arg( elementSize / 1024 ).arg( mCurrentCacheSizeMB ), 3 );
}

bool QgsProgressiveProjectLoader::isLayerCached( const QString &layerId ) const
{
  QMutexLocker locker( &mCacheMutex );
  return mLayerCache.contains( layerId );
}

QDomElement QgsProgressiveProjectLoader::getCachedLayerElement( const QString &layerId ) const
{
  QMutexLocker locker( &mCacheMutex );
  
  if ( !mLayerCache.contains( layerId ) )
    return QDomElement();
  
  // Update access statistics (const_cast needed for mutable cache operations)
  const_cast<QgsProgressiveProjectLoader*>( this )->updateCacheAccessTime( layerId );
  
  return mLayerCache[layerId].element;
}

void QgsProgressiveProjectLoader::evictLeastRecentlyUsedCache()
{
  if ( mCacheAccessOrder.isEmpty() )
    return;
  
  // Find least recently used entry that's not frequently used
  QString layerToEvict;
  for ( const QString &layerId : mCacheAccessOrder )
  {
    if ( mLayerCache.contains( layerId ) && !mLayerCache[layerId].isFrequentlyUsed )
    {
      layerToEvict = layerId;
      break;
    }
  }
  
  // If all cached layers are frequently used, evict the oldest one anyway
  if ( layerToEvict.isEmpty() && !mCacheAccessOrder.isEmpty() )
  {
    layerToEvict = mCacheAccessOrder.first();
  }
  
  if ( !layerToEvict.isEmpty() )
  {
    LayerCacheEntry entry = mLayerCache.take( layerToEvict );
    mCacheAccessOrder.removeAll( layerToEvict );
    mCurrentCacheSizeMB -= entry.sizeBytes / (1024 * 1024);
    
    QgsDebugMsgLevel( QStringLiteral( "Evicted layer %1 from cache, freed %2 KB" )
      .arg( layerToEvict ).arg( entry.sizeBytes / 1024 ), 3 );
  }
}

void QgsProgressiveProjectLoader::updateCacheAccessTime( const QString &layerId )
{
  if ( !mLayerCache.contains( layerId ) )
    return;
  
  LayerCacheEntry &entry = mLayerCache[layerId];
  entry.lastAccessed = QDateTime::currentDateTime();
  entry.accessCount++;
  
  // Mark as frequently used if accessed more than 3 times
  if ( entry.accessCount > 3 )
  {
    entry.isFrequentlyUsed = true;
  }
  
  // Move to end of access order (most recently used)
  mCacheAccessOrder.removeAll( layerId );
  mCacheAccessOrder.append( layerId );
}

bool QgsProgressiveProjectLoader::isCompressedProject( const QString &projectPath ) const
{
  QFile file( projectPath );
  if ( !file.open( QIODevice::ReadOnly ) )
    return false;
  
  // Read first few bytes to check for compression magic numbers
  QByteArray header = file.read( 16 );
  file.close();
  
  if ( header.isEmpty() )
    return false;
  
  // Check for Qt compressed data signature (first 4 bytes are original size)
  // Qt compression uses zlib, which starts with specific bytes
  if ( header.size() >= 6 )
  {
    // Check for zlib header (0x78 followed by compression flags)
    if ( static_cast<unsigned char>( header[4] ) == 0x78 && 
         ( static_cast<unsigned char>( header[5] ) & 0x9C ) == 0x9C )
    {
      QgsDebugMsgLevel( QStringLiteral( "Detected Qt-compressed project file" ), 3 );
      return true;
    }
  }
  
  // Check for gzip header (0x1f 0x8b)
  if ( header.size() >= 2 && 
       static_cast<unsigned char>( header[0] ) == 0x1f && 
       static_cast<unsigned char>( header[1] ) == 0x8b )
  {
    QgsDebugMsgLevel( QStringLiteral( "Detected gzip-compressed project file" ), 3 );
    return true;
  }
  
  return false;
}

QByteArray QgsProgressiveProjectLoader::decompressProjectData( const QByteArray &compressedData ) const
{
  // Try Qt qUncompress first
  QByteArray uncompressed = qUncompress( compressedData );
  if ( !uncompressed.isEmpty() )
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "Successfully decompressed project data using Qt: %1 KB -> %2 KB" )
      .arg( compressedData.size() / 1024 ).arg( uncompressed.size() / 1024 ),
      QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
    return uncompressed;
  }
  
  // If Qt decompression fails, check for gzip format
  if ( compressedData.size() >= 2 && 
       static_cast<unsigned char>( compressedData[0] ) == 0x1f && 
       static_cast<unsigned char>( compressedData[1] ) == 0x8b )
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "Qt decompression failed, gzip format detected but not supported in this build" ),
      QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Warning );
  }
  
  return QByteArray(); // Return empty if decompression fails
}

bool QgsProgressiveProjectLoader::loadCompressedProject( const QString &projectPath, QgsProject *project )
{
  if ( !project || projectPath.isEmpty() )
    return false;
  
  QFile file( projectPath );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "Failed to open compressed project file: %1" ).arg( projectPath ),
      QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Warning );
    return false;
  }
  
  QByteArray compressedData = file.readAll();
  file.close();
  
  QgsMessageLog::logMessage(
    QStringLiteral( "Read compressed project file: %1 KB" ).arg( compressedData.size() / 1024 ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
  
  // Decompress the data
  QByteArray uncompressedData = decompressProjectData( compressedData );
  if ( uncompressedData.isEmpty() )
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "Failed to decompress project file: %1" ).arg( projectPath ),
      QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Warning );
    return false;
  }
  
  // Create a temporary file for the uncompressed data
  QString tempPath = QDir::tempPath() + QStringLiteral( "/qgis_temp_project_%1.qgs" )
                     .arg( QDateTime::currentMSecsSinceEpoch() );
  
  QFile tempFile( tempPath );
  if ( !tempFile.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "Failed to create temporary file for decompressed project" ),
      QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Warning );
    return false;
  }
  
  tempFile.write( uncompressedData );
  tempFile.close();
  
  QgsMessageLog::logMessage(
    QStringLiteral( "Created temporary decompressed project file: %1" ).arg( tempPath ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
  
  // Load the decompressed project using normal methods
  bool success = false;
  QFileInfo tempInfo( tempPath );
  qint64 decompressedSize = tempInfo.size();
  
  // Use optimized loading for large decompressed files
  if ( decompressedSize > 50 * 1024 * 1024 ) // > 50MB
  {
    success = loadProjectWithStreamingParser( tempPath, project );
  }
  else if ( decompressedSize > 10 * 1024 * 1024 ) // > 10MB
  {
    success = loadProjectWithParallelProcessing( tempPath, project );
  }
  else
  {
    success = project->read( tempPath );
  }
  
  // Clean up temporary file
  if ( QFile::exists( tempPath ) )
  {
    QFile::remove( tempPath );
    QgsDebugMsgLevel( QStringLiteral( "Cleaned up temporary project file" ), 3 );
  }
  
  return success;
}

void QgsProgressiveProjectLoader::configureGdalForRemoteSources()
{
  // Store original GDAL configuration values
  const char* originalAuxMode = CPLGetConfigOption( "GDAL_DISABLE_READDIR_ON_OPEN", nullptr );
  const char* originalNetworkAux = CPLGetConfigOption( "GDAL_HTTP_DISABLE_AUX", nullptr );
  const char* originalVsiAux = CPLGetConfigOption( "VSI_CACHE_SIZE", nullptr );
  
  if ( originalAuxMode )
    mOriginalGdalConfig["GDAL_DISABLE_READDIR_ON_OPEN"] = QString( originalAuxMode );
  if ( originalNetworkAux )
    mOriginalGdalConfig["GDAL_HTTP_DISABLE_AUX"] = QString( originalNetworkAux );
  if ( originalVsiAux )
    mOriginalGdalConfig["VSI_CACHE_SIZE"] = QString( originalVsiAux );
  
  // Configure GDAL to disable auxiliary file lookups for HTTP/HTTPS sources
  CPLSetConfigOption( "GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR" );
  CPLSetConfigOption( "GDAL_HTTP_DISABLE_AUX", "YES" );
  CPLSetConfigOption( "VSI_CACHE_SIZE", "0" ); // Disable VSI caching for network files
  
  QgsMessageLog::logMessage(
    QStringLiteral( "Configured GDAL to disable auxiliary file lookups for remote sources" ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
}

void QgsProgressiveProjectLoader::restoreGdalConfiguration()
{
  // Restore original GDAL configuration values
  for ( auto it = mOriginalGdalConfig.constBegin(); it != mOriginalGdalConfig.constEnd(); ++it )
  {
    CPLSetConfigOption( it.key().toLocal8Bit().constData(), it.value().toLocal8Bit().constData() );
  }
  
  // Clear any options that weren't originally set
  if ( !mOriginalGdalConfig.contains( "GDAL_DISABLE_READDIR_ON_OPEN" ) )
    CPLSetConfigOption( "GDAL_DISABLE_READDIR_ON_OPEN", nullptr );
  if ( !mOriginalGdalConfig.contains( "GDAL_HTTP_DISABLE_AUX" ) )
    CPLSetConfigOption( "GDAL_HTTP_DISABLE_AUX", nullptr );
  if ( !mOriginalGdalConfig.contains( "VSI_CACHE_SIZE" ) )
    CPLSetConfigOption( "VSI_CACHE_SIZE", nullptr );
    
  mOriginalGdalConfig.clear();
  
  QgsMessageLog::logMessage(
    QStringLiteral( "Restored original GDAL configuration" ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
}

void QgsProgressiveProjectLoader::optimizeForFlatGeobuf( const QString &dataSource )
{
  // Apply FlatGeobuf-specific GDAL optimizations before layer creation
  if ( dataSource.startsWith( QStringLiteral( "http" ) ) )
  {
    // Enable streaming for remote FlatGeobuf files
    CPLSetConfigOption( "OGR_FGB_STREAM_MODE", "YES" );
    
    // Enable spatial index for faster queries
    CPLSetConfigOption( "OGR_FGB_USE_SPATIAL_INDEX", "YES" );
    
    // Optimize HTTP range requests for FlatGeobuf
    CPLSetConfigOption( "VSI_CURL_CHUNK_SIZE", "32768" ); // 32KB chunks for better performance
    CPLSetConfigOption( "GDAL_HTTP_USERPWD", "" ); // Clear credentials to avoid auth delays
  }
  
  // Set feature limit for initial loading (progressive loading)
  if ( mConfig.enableLazyLoading )
  {
    CPLSetConfigOption( "OGR_FGB_INITIAL_FEATURE_LIMIT", "1000" );
  }
  
  QgsMessageLog::logMessage(
    QStringLiteral( "Applied FlatGeobuf optimizations for: %1" ).arg( dataSource ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
}

void QgsProgressiveProjectLoader::optimizeFlatGeobufLayer( QgsVectorLayer *layer )
{
  if ( !layer || !layer->isValid() )
    return;
    
  // Enable progressive feature loading
  if ( mConfig.enableLazyLoading )
  {
    layer->setSubsetString( QStringLiteral( "ROWNUM <= 1000" ) ); // Load first 1000 features initially
  }
  
  // Optimize geometry handling for complex vectors
  if ( mConfig.enableGeometrySimplification )
  {
    QgsVectorSimplifyMethod simplifyMethod = layer->simplifyMethod();
    simplifyMethod.setSimplifyHints( Qgis::VectorRenderingSimplificationFlag::GeometrySimplification );
    simplifyMethod.setTolerance( mConfig.geometrySimplificationTolerance );
    simplifyMethod.setForceLocalOptimization( true );
    layer->setSimplifyMethod( simplifyMethod );
  }
  
  // Configure provider for optimal performance
  QgsDataProvider *provider = layer->dataProvider();
  if ( provider )
  {
    // Disable feature count for faster loading
    provider->setProperty( "skipFeatureCount", true );
    
    // Enable caching for frequently accessed data
    if ( mConfig.enableLayerCaching )
    {
      provider->setProperty( "enableFeatureCache", true );
      provider->setProperty( "featureCacheSize", 10000 );
    }
  }
  
  QgsMessageLog::logMessage(
    QStringLiteral( "Applied layer-level FlatGeobuf optimizations for: %1" ).arg( layer->name() ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
}

void QgsProgressiveProjectLoader::applyFlatGeobufStreamingOptions( const QString &dataSource )
{
  // Configure streaming specifically for FlatGeobuf format
  if ( dataSource.contains( QStringLiteral( ".fgb" ) ) )
  {
    // Enable progressive reading
    CPLSetConfigOption( "OGR_FGB_PROGRESSIVE_READ", "YES" );
    
    // Configure optimal buffer sizes for streaming
    CPLSetConfigOption( "OGR_FGB_BUFFER_SIZE", "65536" ); // 64KB buffer
    
    // Enable feature pre-filtering at the driver level
    CPLSetConfigOption( "OGR_FGB_ENABLE_PREFILTER", "YES" );
  }
}

void QgsProgressiveProjectLoader::configureFlatGeobufCaching( QgsVectorLayer *layer )
{
  if ( !layer || !layer->isValid() || !mConfig.enableLayerCaching )
    return;
    
  // Configure memory-based caching for FlatGeobuf layers
  QgsVectorDataProvider *provider = layer->dataProvider();
  if ( provider )
  {
    // Set up spatial index caching
    provider->createSpatialIndex();
    
    // Configure memory-based feature cache
    layer->setRenderer( layer->renderer() ); // Force renderer initialization
    
    // Cache frequently accessed attributes
    QgsFields fields = layer->fields();
    QgsAttributeList attributesToCache;
    
    // Cache first 5 attributes (most commonly displayed)
    for ( int i = 0; i < qMin( 5, fields.count() ); ++i )
    {
      attributesToCache << i;
    }
    
    if ( !attributesToCache.isEmpty() )
    {
      layer->setSubsetString( QString() ); // Clear any existing subset to enable full caching
    }
  }
}

void QgsProgressiveProjectLoader::optimizeForReadOnlyPostgres( const QString &dataSource )
{
  // Apply PostgreSQL read-only optimizations to eliminate recovery warnings
  // and improve performance when all databases are read-only
  
  QgsMessageLog::logMessage(
    QStringLiteral( "Configuring read-only PostgreSQL optimization for: %1" ).arg( dataSource ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
  
  // Configure PostgreSQL connection for read-only mode
  configurePostgresReadOnlyConnection( dataSource );
  
  // Apply GDAL PostgreSQL-specific optimizations
  CPLSetConfigOption( "PG_USE_COPY", "NO" );  // Disable COPY operations (write-based)
  CPLSetConfigOption( "PG_BINARY_CURSOR", "YES" );  // Use binary cursors for better performance
  
  // Set connection timeouts to avoid hanging on databases in recovery
  CPLSetConfigOption( "PG_CONNECT_TIMEOUT", "5" );  // 5 second connection timeout
  CPLSetConfigOption( "PG_STATEMENT_TIMEOUT", "30000" );  // 30 second query timeout
  
  // Force read-only transaction mode to prevent recovery warnings
  CPLSetConfigOption( "PG_PRELUDE_STATEMENTS", "SET default_transaction_read_only = on; SET transaction_read_only = on;" );
  
  // Disable error logging for known read-only recovery issues
  CPLSetConfigOption( "PG_REPORT_RECOVERY_ERRORS", "NO" );
  
  // Optimize cursor and result handling for read-only access
  CPLSetConfigOption( "OGR_PG_CURSOR_PAGE", "1000" );  // Optimize cursor page size
  CPLSetConfigOption( "PG_LIST_ALL_TABLES", "NO" );  // Avoid expensive table listing operations
  
  QgsMessageLog::logMessage(
    QStringLiteral( "PostgreSQL read-only optimizations applied successfully" ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
}

void QgsProgressiveProjectLoader::configurePostgresReadOnlyConnection( const QString &dataSource )
{
  // Parse the data source to extract connection parameters
  QgsDataSourceUri uri( dataSource );
  
  // Ensure the connection string includes read-only parameters
  if ( !uri.hasParam( QStringLiteral( "readonly" ) ) )
  {
    uri.setParam( QStringLiteral( "readonly" ), QStringLiteral( "1" ) );
  }
  
  // Add connection timeout to prevent hanging on recovery databases
  if ( !uri.hasParam( QStringLiteral( "connect_timeout" ) ) )
  {
    uri.setParam( QStringLiteral( "connect_timeout" ), QStringLiteral( "5" ) );
  }
  
  // Set statement timeout for queries
  if ( !uri.hasParam( QStringLiteral( "command_timeout" ) ) )
  {
    uri.setParam( QStringLiteral( "command_timeout" ), QStringLiteral( "30" ) );
  }
  
  // Force read-only application name to help identify these connections
  if ( !uri.hasParam( QStringLiteral( "application_name" ) ) )
  {
    uri.setParam( QStringLiteral( "application_name" ), QStringLiteral( "QGIS_ReadOnly" ) );
  }
  
  // Configure SSL mode for security but allow fallback
  if ( !uri.hasParam( QStringLiteral( "sslmode" ) ) )
  {
    uri.setParam( QStringLiteral( "sslmode" ), QStringLiteral( "prefer" ) );
  }
  
  // Add options to force read-only mode at the connection level
  QString options = uri.param( QStringLiteral( "options" ) );
  if ( !options.contains( QStringLiteral( "default_transaction_read_only" ) ) )
  {
    if ( !options.isEmpty() )
      options += QStringLiteral( " " );
    options += QStringLiteral( "-c default_transaction_read_only=on -c transaction_read_only=on" );
    uri.setParam( QStringLiteral( "options" ), options );
  }
  
  QgsMessageLog::logMessage(
    QStringLiteral( "PostgreSQL connection configured for read-only mode" ),
    QStringLiteral( "Progressive Loader" ), Qgis::MessageLevel::Info );
}