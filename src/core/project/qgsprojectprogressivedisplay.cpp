/***************************************************************************
                         qgsprojectprogressivedisplay.cpp
                         --------------------------------
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

#include "qgsprojectprogressivedisplay.h"
#include "moc_qgsprojectprogressivedisplay.cpp"

#include "qgsproject.h"
#include "qgsmaplayerstore.h"
#include "qgsmapcanvas.h"
#include "qgslayertree.h"
#include "qgslayertreeutils.h"
#include "qgslayertreelayer.h"
#include "qgsrendercontext.h"
#include "qgsmaprendererparalleljob.h"
#include "qgsmapsettings.h"
#include "qgslogger.h"
#include "qgsmessagelog.h"
#include "qgsvectorlayer.h"
#include "qgsrasterlayer.h"

#include <QApplication>
#include <QThread>
#include <QMutexLocker>

QgsProjectProgressiveDisplay::QgsProjectProgressiveDisplay( QObject *parent )
  : QObject( parent )
{
  // Initialize timers
  mRenderTimer = new QTimer( this );
  mRenderTimer->setSingleShot( false );
  connect( mRenderTimer, &QTimer::timeout, this, &QgsProjectProgressiveDisplay::processRenderQueue );
  
  mQualityUpgradeTimer = new QTimer( this );
  mQualityUpgradeTimer->setSingleShot( true );
  connect( mQualityUpgradeTimer, &QTimer::timeout, this, &QgsProjectProgressiveDisplay::attemptQualityUpgrade );
  
  mStatisticsTimer = new QTimer( this );
  connect( mStatisticsTimer, &QTimer::timeout, this, &QgsProjectProgressiveDisplay::updateStatistics );
  mStatisticsTimer->start( 1000 ); // Update statistics every second
  
  // Initialize display state
  mDisplayStartTime = QDateTime::currentDateTime();
  mLastRefreshTime = mDisplayStartTime;
}

QgsProjectProgressiveDisplay::~QgsProjectProgressiveDisplay()
{
  stopDisplay();
}

void QgsProjectProgressiveDisplay::setDisplayConfig( const DisplayConfig &config )
{
  mConfig = config;
  
  // Update timer intervals based on configuration
  if ( mRenderTimer )
  {
    mRenderTimer->setInterval( mConfig.refreshIntervalMs );
  }
  
  if ( mQualityUpgradeTimer )
  {
    mQualityUpgradeTimer->setInterval( mConfig.qualityUpgradeDelayMs );
  }
  
  // Update current quality settings
  mCurrentGlobalQuality = mConfig.initialQuality;
}

void QgsProjectProgressiveDisplay::setTargetCanvas( QgsMapCanvas *canvas )
{
  mCanvas = canvas;
}

bool QgsProjectProgressiveDisplay::startDisplay( QgsProject *project )
{
  if ( !project || !mCanvas )
  {
    QgsDebugError( QStringLiteral( "Cannot start progressive display: missing project or canvas" ) );
    return false;
  }

  if ( mDisplayActive )
  {
    stopDisplay();
  }

  mProject = project;
  mDisplayActive = true;
  mCancelled = false;
  mDisplayStartTime = QDateTime::currentDateTime();
  mDisplayTimer.start();
  
  // Initialize display system
  initializeDisplay();
  
  // Setup layer tree for progressive rendering
  setupLayerTree( project );
  
  // Start the rendering process
  mRenderTimer->start( mConfig.refreshIntervalMs );
  
  // Schedule quality upgrade
  if ( mConfig.qualityUpgradeDelayMs > 0 )
  {
    mQualityUpgradeTimer->start( mConfig.qualityUpgradeDelayMs );
  }
  
  // Initialize statistics
  mStatistics.displayStartTime = mDisplayStartTime;
  mStatistics.totalLayers = mLayerInfo.size();
  mStatistics.currentGlobalQuality = mCurrentGlobalQuality;
  
  emit displayStarted( mStatistics.totalLayers );
  
  QgsDebugMsgLevel( QStringLiteral( "Progressive display started for %1 layers" )
                    .arg( mStatistics.totalLayers ), 2 );
  
  return true;
}

void QgsProjectProgressiveDisplay::stopDisplay()
{
  if ( !mDisplayActive )
  {
    return;
  }

  mDisplayActive = false;
  
  // Stop all timers
  if ( mRenderTimer )
  {
    mRenderTimer->stop();
  }
  if ( mQualityUpgradeTimer )
  {
    mQualityUpgradeTimer->stop();
  }
  
  // Clear queues
  {
    QMutexLocker locker( &mQueueMutex );
    mRenderQueue.clear();
    mPriorityQueue.clear();
  }
  
  // Cleanup resources
  cleanupDisplay();
  
  // Emit completion signal
  const bool success = !mCancelled && ( mStatistics.renderedLayers == mStatistics.totalLayers );
  emit displayCompleted( success, mStatistics );
  
  QgsDebugMsgLevel( QStringLiteral( "Progressive display stopped. Success: %1, Rendered: %2/%3" )
                    .arg( success )
                    .arg( mStatistics.renderedLayers )
                    .arg( mStatistics.totalLayers ), 2 );
}

void QgsProjectProgressiveDisplay::addLayer( QgsMapLayer *layer, RenderingPriority priority )
{
  if ( !layer )
  {
    return;
  }

  LayerRenderInfo info;
  info.layerId = layer->id();
  info.layerName = layer->name();
  info.priority = priority;
  info.isLoaded = layer->isValid();
  info.isVisible = true;
  info.currentQuality = mConfig.initialQuality;
  info.loadTime = QDateTime::currentDateTime();
  
  // Determine if layer should be rendered immediately
  const bool shouldRenderNow = ( mConfig.strategy == RenderingStrategy::Immediate ) ||
                                ( priority == RenderingPriority::Critical );
  
  {
    QMutexLocker locker( &mQueueMutex );
    mLayerInfo[layer->id()] = info;
    
    if ( shouldRenderNow && info.isLoaded )
    {
      mPriorityQueue.prepend( layer->id() );
    }
    else
    {
      mRenderQueue.enqueue( layer->id() );
    }
  }
  
  // Update statistics
  mStatistics.totalLayers = mLayerInfo.size();
  if ( info.isLoaded )
  {
    mStatistics.loadedLayers++;
  }
  
  QgsDebugMsgLevel( QStringLiteral( "Added layer to progressive display: %1 (priority: %2)" )
                    .arg( layer->name() ).arg( static_cast<int>( priority ) ), 3 );
}

void QgsProjectProgressiveDisplay::removeLayer( const QString &layerId )
{
  {
    QMutexLocker locker( &mQueueMutex );
    
    if ( mLayerInfo.contains( layerId ) )
    {
      mLayerInfo.remove( layerId );
      
      // Remove from queues
      QQueue<QString> newQueue;
      while ( !mRenderQueue.isEmpty() )
      {
        const QString id = mRenderQueue.dequeue();
        if ( id != layerId )
        {
          newQueue.enqueue( id );
        }
      }
      mRenderQueue = newQueue;
      
      mPriorityQueue.removeAll( layerId );
    }
  }
  
  // Update statistics
  mStatistics.totalLayers = mLayerInfo.size();
  updateStatistics();
}

void QgsProjectProgressiveDisplay::updateLayerPriority( const QString &layerId, RenderingPriority priority )
{
  QMutexLocker locker( &mQueueMutex );
  
  if ( mLayerInfo.contains( layerId ) )
  {
    mLayerInfo[layerId].priority = priority;
    
    // Move to priority queue if critical
    if ( priority == RenderingPriority::Critical )
    {
      // Remove from regular queue
      QQueue<QString> newQueue;
      while ( !mRenderQueue.isEmpty() )
      {
        const QString id = mRenderQueue.dequeue();
        if ( id != layerId )
        {
          newQueue.enqueue( id );
        }
      }
      mRenderQueue = newQueue;
      
      // Add to priority queue if not already there
      if ( !mPriorityQueue.contains( layerId ) )
      {
        mPriorityQueue.prepend( layerId );
      }
    }
  }
}

void QgsProjectProgressiveDisplay::setLayerVisibility( const QString &layerId, bool visible )
{
  QMutexLocker locker( &mQueueMutex );
  
  if ( mLayerInfo.contains( layerId ) )
  {
    mLayerInfo[layerId].isVisible = visible;
    
    // Update layer tree if available
    if ( mLayerTree )
    {
      QgsLayerTreeLayer *treeLayer = mLayerTree->findLayer( layerId );
      if ( treeLayer )
      {
        treeLayer->setItemVisibilityChecked( visible );
      }
    }
  }
}

void QgsProjectProgressiveDisplay::refreshDisplay()
{
  if ( !mDisplayActive || !mCanvas )
  {
    return;
  }

  // Force immediate processing of render queue
  processRenderQueue();
  
  // Refresh the canvas
  mCanvas->refresh();
  
  mLastRefreshTime = QDateTime::currentDateTime();
  mStatistics.refreshCount++;
}

void QgsProjectProgressiveDisplay::upgradeQuality( QualityLevel targetQuality )
{
  if ( targetQuality <= mCurrentGlobalQuality )
  {
    return;
  }

  const QualityLevel oldQuality = mCurrentGlobalQuality;
  mCurrentGlobalQuality = targetQuality;
  
  // Re-queue all layers for rendering with new quality
  {
    QMutexLocker locker( &mQueueMutex );
    
    for ( auto it = mLayerInfo.begin(); it != mLayerInfo.end(); ++it )
    {
      LayerRenderInfo &info = it.value();
      if ( info.currentQuality < targetQuality && info.isVisible && info.isLoaded )
      {
        info.currentQuality = targetQuality;
        if ( !mRenderQueue.contains( it.key() ) && !mPriorityQueue.contains( it.key() ) )
        {
          mRenderQueue.enqueue( it.key() );
        }
      }
    }
  }
  
  mStatistics.currentGlobalQuality = mCurrentGlobalQuality;
  emit qualityUpgraded( oldQuality, targetQuality );
  
  QgsDebugMsgLevel( QStringLiteral( "Upgraded rendering quality from %1 to %2" )
                    .arg( static_cast<int>( oldQuality ) )
                    .arg( static_cast<int>( targetQuality ) ), 2 );
}

QgsProjectProgressiveDisplay::DisplayStatistics QgsProjectProgressiveDisplay::getStatistics() const
{
  return mStatistics;
}

QVariantMap QgsProjectProgressiveDisplay::exportStatistics() const
{
  QVariantMap stats;
  
  stats[QStringLiteral( "total_layers" )] = mStatistics.totalLayers;
  stats[QStringLiteral( "loaded_layers" )] = mStatistics.loadedLayers;
  stats[QStringLiteral( "rendered_layers" )] = mStatistics.renderedLayers;
  stats[QStringLiteral( "total_load_time_ms" )] = mStatistics.totalLoadTime;
  stats[QStringLiteral( "total_render_time_ms" )] = mStatistics.totalRenderTime;
  stats[QStringLiteral( "average_render_time_ms" )] = mStatistics.averageRenderTime;
  stats[QStringLiteral( "refresh_count" )] = mStatistics.refreshCount;
  stats[QStringLiteral( "current_quality" )] = static_cast<int>( mStatistics.currentGlobalQuality );
  
  if ( mDisplayTimer.isValid() )
  {
    stats[QStringLiteral( "display_duration_ms" )] = mDisplayTimer.elapsed();
  }
  
  return stats;
}

QgsProjectProgressiveDisplay::LayerRenderInfo QgsProjectProgressiveDisplay::getLayerInfo( const QString &layerId ) const
{
  QMutexLocker locker( &mQueueMutex );
  return mLayerInfo.value( layerId, LayerRenderInfo() );
}

QList<QgsProjectProgressiveDisplay::LayerRenderInfo> QgsProjectProgressiveDisplay::getAllLayerInfo() const
{
  QMutexLocker locker( &mQueueMutex );
  return mLayerInfo.values();
}

void QgsProjectProgressiveDisplay::cancelDisplay()
{
  if ( !mDisplayActive )
  {
    return;
  }

  mCancelled = true;
  stopDisplay();
  emit displayCancelled();
  
  QgsDebugMsgLevel( QStringLiteral( "Progressive display cancelled by user" ), 2 );
}

void QgsProjectProgressiveDisplay::onLayerLoaded( const QString &layerId )
{
  QMutexLocker locker( &mQueueMutex );
  
  if ( mLayerInfo.contains( layerId ) )
  {
    LayerRenderInfo &info = mLayerInfo[layerId];
    info.isLoaded = true;
    info.loadTime = QDateTime::currentDateTime();
    
    // Add to render queue if not already there
    if ( !mRenderQueue.contains( layerId ) && !mPriorityQueue.contains( layerId ) )
    {
      if ( info.priority == RenderingPriority::Critical )
      {
        mPriorityQueue.prepend( layerId );
      }
      else
      {
        mRenderQueue.enqueue( layerId );
      }
    }
    
    mStatistics.loadedLayers++;
  }
}

void QgsProjectProgressiveDisplay::onLayerLoadFailed( const QString &layerId, const QString &errorMessage )
{
  Q_UNUSED( errorMessage )
  
  QMutexLocker locker( &mQueueMutex );
  
  if ( mLayerInfo.contains( layerId ) )
  {
    // Remove failed layer from rendering queues
    QQueue<QString> newQueue;
    while ( !mRenderQueue.isEmpty() )
    {
      const QString id = mRenderQueue.dequeue();
      if ( id != layerId )
      {
        newQueue.enqueue( id );
      }
    }
    mRenderQueue = newQueue;
    
    mPriorityQueue.removeAll( layerId );
    
    // Mark as failed but keep in layer info for statistics
    mLayerInfo[layerId].isLoaded = false;
  }
}

void QgsProjectProgressiveDisplay::processRenderQueue()
{
  if ( !mDisplayActive || mCancelled || !mCanvas )
  {
    return;
  }

  // Check if enough time has passed since last refresh
  const QDateTime now = QDateTime::currentDateTime();
  if ( mLastRefreshTime.msecsTo( now ) < mConfig.refreshIntervalMs )
  {
    return;
  }

  // Get next batch of layers to render
  const QStringList layersToRender = getNextRenderBatch();
  
  if ( layersToRender.isEmpty() )
  {
    // Check if we're done
    if ( mStatistics.renderedLayers >= mStatistics.totalLayers )
    {
      stopDisplay();
    }
    return;
  }

  // Render the batch
  renderLayerBatch( layersToRender, mCurrentGlobalQuality );
  
  // Update last refresh time
  mLastRefreshTime = now;
  
  // Emit progress update
  emit displayProgressChanged( mStatistics.loadedLayers, mStatistics.renderedLayers, mStatistics.totalLayers );
}

void QgsProjectProgressiveDisplay::attemptQualityUpgrade()
{
  if ( shouldUpgradeQuality() )
  {
    QualityLevel nextQuality = mCurrentGlobalQuality;
    
    // Determine next quality level
    switch ( mCurrentGlobalQuality )
    {
      case QualityLevel::Draft:
        nextQuality = QualityLevel::Preview;
        break;
      case QualityLevel::Preview:
        nextQuality = QualityLevel::Final;
        break;
      case QualityLevel::Final:
        return; // Already at highest quality
    }
    
    upgradeQuality( nextQuality );
    
    // Schedule next upgrade if not at final quality
    if ( nextQuality != mConfig.finalQuality )
    {
      mQualityUpgradeTimer->start( mConfig.qualityUpgradeDelayMs );
    }
  }
}

void QgsProjectProgressiveDisplay::updateStatistics()
{
  if ( !mDisplayActive )
  {
    return;
  }

  // Update timing statistics
  mStatistics.lastUpdateTime = QDateTime::currentDateTime();
  if ( mDisplayTimer.isValid() )
  {
    mStatistics.totalLoadTime = mDisplayTimer.elapsed();
  }
  
  // Calculate render time statistics
  qint64 totalRenderTime = 0;
  int renderedLayers = 0;
  
  for ( const LayerRenderInfo &info : mLayerInfo )
  {
    if ( info.isRendered )
    {
      renderedLayers++;
      totalRenderTime += info.renderDurationMs;
    }
  }
  
  mStatistics.renderedLayers = renderedLayers;
  mStatistics.totalRenderTime = totalRenderTime;
  mStatistics.averageRenderTime = renderedLayers > 0 ? static_cast<double>( totalRenderTime ) / renderedLayers : 0;
}

void QgsProjectProgressiveDisplay::initializeDisplay()
{
  mLayerInfo.clear();
  mRenderQueue.clear();
  mPriorityQueue.clear();
  
  // Reset statistics
  mStatistics = DisplayStatistics();
  mStatistics.displayStartTime = mDisplayStartTime;
  mStatistics.currentGlobalQuality = mCurrentGlobalQuality;
  
  // Initialize quality settings
  mCurrentGlobalQuality = mConfig.initialQuality;
  mQualityUpgradeScheduled = false;
}

void QgsProjectProgressiveDisplay::setupLayerTree( QgsProject *project )
{
  if ( !project )
  {
    return;
  }

  mLayerTree = project->layerTreeRoot();
  
  // Add all existing layers to display queue
  const QList<QgsMapLayer *> layers = project->mapLayers().values();
  for ( QgsMapLayer *layer : layers )
  {
    if ( layer && layer->isValid() )
    {
      const RenderingPriority priority = calculateLayerPriority( layer );
      addLayer( layer, priority );
    }
  }
  
  QgsDebugMsgLevel( QStringLiteral( "Setup layer tree with %1 layers" ).arg( layers.size() ), 3 );
}

QgsProjectProgressiveDisplay::RenderingPriority QgsProjectProgressiveDisplay::calculateLayerPriority( QgsMapLayer *layer ) const
{
  if ( !layer )
  {
    return RenderingPriority::Normal;
  }

  // Determine priority based on layer properties
  if ( layer->type() == QgsMapLayerType::RasterLayer )
  {
    // Base raster layers typically have lower priority
    return RenderingPriority::Background;
  }
  else if ( layer->type() == QgsMapLayerType::VectorLayer )
  {
    QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer );
    if ( vectorLayer )
    {
      // Point layers often contain important features
      if ( vectorLayer->geometryType() == QgsWkbTypes::PointGeometry )
      {
        return RenderingPriority::Foreground;
      }
      // Line and polygon layers are typically normal priority
      return RenderingPriority::Normal;
    }
  }
  
  // Default to normal priority
  return RenderingPriority::Normal;
}

QStringList QgsProjectProgressiveDisplay::getNextRenderBatch()
{
  QMutexLocker locker( &mQueueMutex );
  
  QStringList batch;
  
  // First, process priority queue
  while ( !mPriorityQueue.isEmpty() && batch.size() < mConfig.batchSize )
  {
    const QString layerId = mPriorityQueue.takeFirst();
    if ( mLayerInfo.contains( layerId ) && shouldRenderLayer( mLayerInfo[layerId] ) )
    {
      batch.append( layerId );
    }
  }
  
  // Fill remaining slots from regular queue
  while ( !mRenderQueue.isEmpty() && batch.size() < mConfig.batchSize )
  {
    const QString layerId = mRenderQueue.dequeue();
    if ( mLayerInfo.contains( layerId ) && shouldRenderLayer( mLayerInfo[layerId] ) )
    {
      batch.append( layerId );
    }
  }
  
  return batch;
}

bool QgsProjectProgressiveDisplay::renderLayer( const QString &layerId, QualityLevel quality )
{
  if ( !mCanvas || !mProject )
  {
    return false;
  }

  QMutexLocker locker( &mQueueMutex );
  
  if ( !mLayerInfo.contains( layerId ) )
  {
    return false;
  }
  
  LayerRenderInfo &info = mLayerInfo[layerId];
  
  // Get the actual layer
  QgsMapLayer *layer = mProject->mapLayer( layerId );
  if ( !layer || !layer->isValid() )
  {
    return false;
  }

  locker.unlock();
  
  // Start timing
  QElapsedTimer renderTimer;
  renderTimer.start();
  
  try
  {
    // Create render context with quality settings
    QgsRenderContext context = createRenderContext( quality );
    applyQualitySettings( context, quality );
    
    // Note: In a full implementation, we would perform actual layer rendering here
    // For now, we simulate the rendering process
    
    // Simulate render time based on layer type and quality
    int simulatedRenderTime = 50; // Base time
    if ( quality == QualityLevel::Final )
    {
      simulatedRenderTime *= 3;
    }
    else if ( quality == QualityLevel::Preview )
    {
      simulatedRenderTime *= 2;
    }
    
    // Add variation based on layer type
    if ( layer->type() == QgsMapLayerType::VectorLayer )
    {
      QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer );
      if ( vectorLayer )
      {
        const long featureCount = vectorLayer->featureCount();
        simulatedRenderTime += qMin( static_cast<int>( featureCount / 1000 ), 200 );
      }
    }
    
    // Simulate processing time
    QThread::msleep( qMin( simulatedRenderTime, mConfig.maxRenderTimeMs ) );
    
    const qint64 renderTime = renderTimer.elapsed();
    
    // Update layer info
    locker.relock();
    info.isRendered = true;
    info.currentQuality = quality;
    info.lastRenderTime = QDateTime::currentDateTime();
    info.renderDurationMs = renderTime;
    info.renderAttempts++;
    locker.unlock();
    
    emit layerRendered( layerId, quality, renderTime );
    
    QgsDebugMsgLevel( QStringLiteral( "Rendered layer %1 in %2ms (quality: %3)" )
                      .arg( layer->name() ).arg( renderTime ).arg( static_cast<int>( quality ) ), 3 );
    
    return true;
  }
  catch ( const std::exception &e )
  {
    const QString errorMsg = tr( "Error rendering layer %1: %2" ).arg( layer->name() ).arg( e.what() );
    QgsMessageLog::logMessage( errorMsg, QStringLiteral( "Progressive Display" ), Qgis::MessageLevel::Warning );
    emit displayError( errorMsg );
    return false;
  }
}

void QgsProjectProgressiveDisplay::renderLayerBatch( const QStringList &layerIds, QualityLevel quality )
{
  for ( const QString &layerId : layerIds )
  {
    if ( mCancelled )
    {
      break;
    }
    
    renderLayer( layerId, quality );
  }
  
  // Refresh canvas after batch
  if ( mCanvas && !mCancelled )
  {
    mCanvas->refresh();
  }
}

bool QgsProjectProgressiveDisplay::shouldRenderLayer( const LayerRenderInfo &info ) const
{
  // Don't render if already rendered at current or higher quality
  if ( info.isRendered && info.currentQuality >= mCurrentGlobalQuality )
  {
    return false;
  }
  
  // Don't render if not loaded or not visible
  if ( !info.isLoaded || !info.isVisible )
  {
    return false;
  }
  
  // Apply strategy-specific logic
  switch ( mConfig.strategy )
  {
    case RenderingStrategy::Immediate:
      return true;
      
    case RenderingStrategy::Batched:
      return true; // Batching is handled by batch size
      
    case RenderingStrategy::Adaptive:
      // Adaptive strategy considers render attempts and priority
      return info.renderAttempts < 3; // Limit retry attempts
      
    case RenderingStrategy::OnDemand:
      return info.priority == RenderingPriority::Critical;
  }
  
  return true;
}

bool QgsProjectProgressiveDisplay::shouldUpgradeQuality() const
{
  // Check if we have rendered enough layers to warrant quality upgrade
  const double renderRatio = mStatistics.totalLayers > 0 ? 
                             static_cast<double>( mStatistics.renderedLayers ) / mStatistics.totalLayers : 0;
  
  // Upgrade quality when at least 50% of layers are rendered
  return renderRatio >= 0.5 && mCurrentGlobalQuality < mConfig.finalQuality;
}

QgsRenderContext QgsProjectProgressiveDisplay::createRenderContext( QualityLevel quality ) const
{
  Q_UNUSED( quality )
  
  // Create a basic render context
  // In a full implementation, this would create a proper render context from the canvas
  QgsRenderContext context;
  
  // Set basic properties
  context.setScaleFactor( 1.0 );
  context.setRendererScale( 1000000 ); // Default scale
  
  return context;
}

void QgsProjectProgressiveDisplay::applyQualitySettings( QgsRenderContext &context, QualityLevel quality ) const
{
  switch ( quality )
  {
    case QualityLevel::Draft:
      // Fast, low-quality settings
      context.setRenderingStopped( false );
      // Additional draft settings would be applied here
      break;
      
    case QualityLevel::Preview:
      // Medium quality settings
      // Additional preview settings would be applied here
      break;
      
    case QualityLevel::Final:
      // High quality settings
      // Additional final quality settings would be applied here
      break;
  }
}

void QgsProjectProgressiveDisplay::updateLayerTreeVisibility()
{
  if ( !mLayerTree )
  {
    return;
  }

  // Update layer tree visibility based on current layer states
  for ( const auto &info : mLayerInfo )
  {
    QgsLayerTreeLayer *treeLayer = mLayerTree->findLayer( info.layerId );
    if ( treeLayer )
    {
      treeLayer->setItemVisibilityChecked( info.isVisible );
    }
  }
}

void QgsProjectProgressiveDisplay::cleanupDisplay()
{
  mProject = nullptr;
  mLayerTree = nullptr;
  mLayerInfo.clear();
  
  {
    QMutexLocker locker( &mQueueMutex );
    mRenderQueue.clear();
    mPriorityQueue.clear();
  }
}