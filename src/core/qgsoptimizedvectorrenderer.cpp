/***************************************************************************
                         qgsoptimizedvectorrenderer_simple.cpp
                         -----------------------------
    begin                : September 2025
    copyright            : (C) 2025 by QGIS
    email                : development-team@qgis.org
 ***************************************************************************/

#include "qgsoptimizedvectorrenderer.h"
#include "qgsfeature.h"
#include "qgsvectorlayer.h"
#include "qgsmessagelog.h"

#include <QFile>
#include <QTextStream>
#include "qgsgeometry.h"
#include "qgslogger.h"
#include "symbology/qgsrenderer.h"
#include "qgsrendercontext.h"
#include "iperformancemonitor.h"

#include <QElapsedTimer>

QgsOptimizedVectorRenderer::QgsOptimizedVectorRenderer( QgsFeatureRenderer *baseRenderer, QObject *parent )
  : QObject( parent )
  , mBaseRenderer( baseRenderer )
{
  // Initialize cache maintenance timer
  mCacheMaintenanceTimer = new QTimer( this );
  mCacheMaintenanceTimer->setSingleShot( false );
  mCacheMaintenanceTimer->setInterval( 5000 ); // 5 seconds
  connect( mCacheMaintenanceTimer, &QTimer::timeout, this, &QgsOptimizedVectorRenderer::performCacheMaintenance );
  
  mCacheMaintenanceTimer->start();

  QgsDebugMsgLevel( QStringLiteral( "Optimized vector renderer initialized" ), 2 );
}

QgsOptimizedVectorRenderer::~QgsOptimizedVectorRenderer()
{
  if ( mCacheMaintenanceTimer )
    mCacheMaintenanceTimer->stop();

  QgsDebugMsgLevel( QStringLiteral( "Optimized vector renderer destroyed" ), 2 );
}

void QgsOptimizedVectorRenderer::setBaseRenderer( QgsFeatureRenderer *renderer )
{
  mBaseRenderer = renderer;
}

bool QgsOptimizedVectorRenderer::renderLayer( QgsVectorLayer *layer, QgsRenderContext &context )
{
  if ( !layer || !mBaseRenderer )
    return false;

  // Check memory before starting intensive rendering operations
  QFile statusFile( QStringLiteral( "/proc/self/status" ) );
  qint64 currentMemoryKB = 0;
  if ( statusFile.open( QIODevice::ReadOnly ) )
  {
    QTextStream stream( &statusFile );
    while ( !stream.atEnd() )
    {
      QString line = stream.readLine();
      if ( line.startsWith( QLatin1String( "VmRSS:" ) ) )
      {
        QStringList parts = line.split( QLatin1Char( ' ' ), Qt::SkipEmptyParts );
        if ( parts.size() >= 2 )
        {
          currentMemoryKB = parts[1].toLongLong();
          break;
        }
      }
    }
  }
  
  // Prevent rendering if memory usage is too high (> 6GB)
  if ( currentMemoryKB > 6 * 1024 * 1024 )
  {
    QgsMessageLog::logMessage(
      QStringLiteral( "Memory usage too high (%1 MB). Skipping optimized rendering." )
      .arg( currentMemoryKB / 1024 ),
      QStringLiteral( "Optimized Renderer" ), Qgis::MessageLevel::Warning );
    return false;
  }

  QElapsedTimer timer;
  timer.start();

  if ( mPerformanceMonitor )
  {
    mCurrentOperationId = mPerformanceMonitor->startOperation(
      QStringLiteral( "Optimized Layer Rendering" ),
      QStringLiteral( "optimized_render" ),
      QHash<QString, QVariant>{
        { QStringLiteral( "layer_id" ), layer->id() },
        { QStringLiteral( "layer_name" ), layer->name() },
        { QStringLiteral( "feature_count" ), layer->featureCount() }
      }
    );
  }

  // Start the base renderer
  mBaseRenderer->startRender( context, layer->fields() );

  // Basic feature iteration and rendering
  QgsFeatureIterator it = layer->getFeatures();
  QgsFeature feature;
  int featuresRendered = 0;

  while ( it.nextFeature( feature ) )
  {
    if ( mBaseRenderer->willRenderFeature( feature, context ) )
    {
      mBaseRenderer->renderFeature( feature, context );
      featuresRendered++;
    }
  }

  // Stop the base renderer
  mBaseRenderer->stopRender( context );

  qint64 renderTime = timer.elapsed();

  // Update statistics
  mStatistics.featuresRendered += featuresRendered;
  mStatistics.totalRenderTimeMs += renderTime;
  if ( mStatistics.featuresRendered > 0 )
  {
    mStatistics.averageFeatureTimeMs = double( mStatistics.totalRenderTimeMs ) / mStatistics.featuresRendered;
  }

  emit renderingStatisticsUpdated( mStatistics );

  if ( mPerformanceMonitor && !mCurrentOperationId.isEmpty() )
  {
    mPerformanceMonitor->recordMetric( QStringLiteral( "render_time" ), renderTime,
                                      QStringLiteral( "ms" ), QStringLiteral( "optimized_render" ), mCurrentOperationId );
    mPerformanceMonitor->recordMetric( QStringLiteral( "features_rendered" ), featuresRendered,
                                      QStringLiteral( "count" ), QStringLiteral( "optimized_render" ), mCurrentOperationId );
    mPerformanceMonitor->endOperation( mCurrentOperationId );
  }

  QgsDebugMsgLevel( QStringLiteral( "Rendered %1 features in %2ms" ).arg( featuresRendered ).arg( renderTime ), 2 );

  return true;
}

void QgsOptimizedVectorRenderer::setOptimizationConfig( const OptimizationConfig &config )
{
  mConfig = config;
  QgsDebugMsgLevel( QStringLiteral( "Optimization configuration updated" ), 3 );
}

QgsOptimizedVectorRenderer::RenderingStatistics QgsOptimizedVectorRenderer::getRenderingStatistics() const
{
  return mStatistics;
}

void QgsOptimizedVectorRenderer::setPerformanceMonitor( IPerformanceMonitor *monitor )
{
  mPerformanceMonitor = monitor;
}

void QgsOptimizedVectorRenderer::performCacheMaintenance()
{
  // Basic maintenance - could be expanded for actual cache operations
  QgsDebugMsgLevel( QStringLiteral( "Performing cache maintenance" ), 4 );
}