/***************************************************************************
                         qgsprojectloadingperformance.cpp
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

#include "qgsprojectloadingperformance.h"
#include "moc_qgsprojectloadingperformance.cpp"

#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

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

QgsProjectLoadingPerformance::QgsProjectLoadingPerformance( QObject *parent )
  : QObject( parent )
{
}

void QgsProjectLoadingPerformance::startMonitoring( const QString &projectPath, const QString &expectedStrategy )
{
  if ( mIsMonitoring )
  {
    qWarning() << "Performance monitoring already active";
    return;
  }

  mIsMonitoring = true;
  mOverallStartTime = QDateTime::currentDateTime();
  
  // Initialize metrics
  mMetrics = LoadingMetrics();
  mMetrics.projectPath = projectPath;
  mMetrics.startTime = mOverallStartTime;
  mMetrics.loadingStrategy = expectedStrategy;
  
  // Get file size
  QFileInfo fileInfo( projectPath );
  if ( fileInfo.exists() )
  {
    mMetrics.fileSizeBytes = fileInfo.size();
  }
  
  // Record initial memory usage
  mMetrics.initialMemoryUsageMB = getCurrentMemoryUsageMB();
  
  emit monitoringStarted( projectPath );
}

void QgsProjectLoadingPerformance::stopMonitoring()
{
  if ( !mIsMonitoring )
  {
    return;
  }

  mIsMonitoring = false;
  mMetrics.endTime = QDateTime::currentDateTime();
  mMetrics.totalLoadTimeMs = mOverallStartTime.msecsTo( mMetrics.endTime );
  
  // Record final memory usage
  mMetrics.finalMemoryUsageMB = getCurrentMemoryUsageMB();
  
  // Calculate improvement if baseline is available
  if ( mMetrics.baselineLoadTimeMs > 0 )
  {
    calculateImprovement();
  }
  
  // Check for performance regression
  if ( isPerformanceRegression() )
  {
    double regressionPercent = ( ( double ) mMetrics.totalLoadTimeMs / mMetrics.baselineLoadTimeMs - 1.0 ) * 100.0;
    emit performanceRegression( regressionPercent );
  }
  else if ( mMetrics.improvementPercent > 5.0 )
  {
    emit performanceImprovement( mMetrics.improvementPercent );
  }
  
  emit monitoringCompleted( mMetrics );
}

void QgsProjectLoadingPerformance::startComponent( const QString &component )
{
  if ( mIsMonitoring )
  {
    mComponentStartTimes[component] = QDateTime::currentDateTime();
  }
}

void QgsProjectLoadingPerformance::endComponent( const QString &component )
{
  if ( !mIsMonitoring || !mComponentStartTimes.contains( component ) )
  {
    return;
  }

  const QDateTime endTime = QDateTime::currentDateTime();
  const qint64 elapsedMs = mComponentStartTimes[component].msecsTo( endTime );
  
  // Map component names to metrics fields
  if ( component == QLatin1String( "xml_parsing" ) )
  {
    mMetrics.xmlParsingTimeMs = elapsedMs;
  }
  else if ( component == QLatin1String( "layer_loading" ) )
  {
    mMetrics.layerLoadingTimeMs = elapsedMs;
  }
  else if ( component == QLatin1String( "layout_loading" ) )
  {
    mMetrics.layoutLoadingTimeMs = elapsedMs;
  }
  else if ( component == QLatin1String( "style_loading" ) )
  {
    mMetrics.styleLoadingTimeMs = elapsedMs;
  }
  else if ( component == QLatin1String( "dependency_resolution" ) )
  {
    mMetrics.dependencyResolutionTimeMs = elapsedMs;
  }
  
  mComponentStartTimes.remove( component );
}

void QgsProjectLoadingPerformance::recordLayerLoadTime( const QString &layerId, qint64 loadTimeMs )
{
  if ( mIsMonitoring )
  {
    mMetrics.layerLoadTimes[layerId] = loadTimeMs;
    mMetrics.totalLayers++;
  }
}

void QgsProjectLoadingPerformance::updateMemoryUsage()
{
  if ( mIsMonitoring )
  {
    const qint64 currentMemory = getCurrentMemoryUsageMB();
    if ( currentMemory > mMetrics.peakMemoryUsageMB )
    {
      mMetrics.peakMemoryUsageMB = currentMemory;
    }
  }
}

void QgsProjectLoadingPerformance::setBaselineTime( qint64 baselineTimeMs )
{
  mMetrics.baselineLoadTimeMs = baselineTimeMs;
}

void QgsProjectLoadingPerformance::addError( const QString &error )
{
  if ( mIsMonitoring )
  {
    mMetrics.errors.append( error );
  }
}

void QgsProjectLoadingPerformance::addWarning( const QString &warning )
{
  if ( mIsMonitoring )
  {
    mMetrics.warnings.append( warning );
  }
}

void QgsProjectLoadingPerformance::setProgressiveLoaderUsed( bool used )
{
  mMetrics.progressiveLoaderUsed = used;
}

void QgsProjectLoadingPerformance::setParallelThreadsUsed( int threads )
{
  mMetrics.parallelThreadsUsed = threads;
}

QJsonObject QgsProjectLoadingPerformance::exportToJson() const
{
  QJsonObject json;
  
  json[QStringLiteral( "project_path" )] = mMetrics.projectPath;
  json[QStringLiteral( "file_size_bytes" )] = mMetrics.fileSizeBytes;
  json[QStringLiteral( "file_size_mb" )] = mMetrics.fileSizeBytes / ( 1024.0 * 1024.0 );
  json[QStringLiteral( "start_time" )] = mMetrics.startTime.toString( Qt::ISODate );
  json[QStringLiteral( "end_time" )] = mMetrics.endTime.toString( Qt::ISODate );
  json[QStringLiteral( "total_load_time_ms" )] = mMetrics.totalLoadTimeMs;
  json[QStringLiteral( "total_load_time_seconds" )] = mMetrics.totalLoadTimeMs / 1000.0;
  
  // Component timings
  QJsonObject componentTimes;
  componentTimes[QStringLiteral( "xml_parsing_ms" )] = mMetrics.xmlParsingTimeMs;
  componentTimes[QStringLiteral( "layer_loading_ms" )] = mMetrics.layerLoadingTimeMs;
  componentTimes[QStringLiteral( "layout_loading_ms" )] = mMetrics.layoutLoadingTimeMs;
  componentTimes[QStringLiteral( "style_loading_ms" )] = mMetrics.styleLoadingTimeMs;
  componentTimes[QStringLiteral( "dependency_resolution_ms" )] = mMetrics.dependencyResolutionTimeMs;
  json[QStringLiteral( "component_times" )] = componentTimes;
  
  // Layer metrics
  QJsonObject layerMetrics;
  layerMetrics[QStringLiteral( "total_layers" )] = mMetrics.totalLayers;
  layerMetrics[QStringLiteral( "layers_loaded_in_parallel" )] = mMetrics.layersLoadedInParallel;
  layerMetrics[QStringLiteral( "layers_loaded_sequentially" )] = mMetrics.layersLoadedSequentially;
  
  QJsonObject layerTimes;
  for ( auto it = mMetrics.layerLoadTimes.begin(); it != mMetrics.layerLoadTimes.end(); ++it )
  {
    layerTimes[it.key()] = it.value();
  }
  layerMetrics[QStringLiteral( "layer_load_times" )] = layerTimes;
  json[QStringLiteral( "layer_metrics" )] = layerMetrics;
  
  // Memory usage
  QJsonObject memoryMetrics;
  memoryMetrics[QStringLiteral( "peak_memory_mb" )] = mMetrics.peakMemoryUsageMB;
  memoryMetrics[QStringLiteral( "initial_memory_mb" )] = mMetrics.initialMemoryUsageMB;
  memoryMetrics[QStringLiteral( "final_memory_mb" )] = mMetrics.finalMemoryUsageMB;
  memoryMetrics[QStringLiteral( "memory_increase_mb" )] = mMetrics.finalMemoryUsageMB - mMetrics.initialMemoryUsageMB;
  json[QStringLiteral( "memory_metrics" )] = memoryMetrics;
  
  // Performance comparison
  if ( mMetrics.baselineLoadTimeMs > 0 )
  {
    QJsonObject performanceComparison;
    performanceComparison[QStringLiteral( "baseline_load_time_ms" )] = mMetrics.baselineLoadTimeMs;
    performanceComparison[QStringLiteral( "improvement_percent" )] = mMetrics.improvementPercent;
    performanceComparison[QStringLiteral( "is_regression" )] = isPerformanceRegression();
    json[QStringLiteral( "performance_comparison" )] = performanceComparison;
  }
  
  // Loading strategy
  QJsonObject strategyInfo;
  strategyInfo[QStringLiteral( "loading_strategy" )] = mMetrics.loadingStrategy;
  strategyInfo[QStringLiteral( "progressive_loader_used" )] = mMetrics.progressiveLoaderUsed;
  strategyInfo[QStringLiteral( "parallel_threads_used" )] = mMetrics.parallelThreadsUsed;
  json[QStringLiteral( "strategy_info" )] = strategyInfo;
  
  // Errors and warnings
  if ( !mMetrics.errors.isEmpty() )
  {
    QJsonArray errorArray;
    for ( const QString &error : mMetrics.errors )
    {
      errorArray.append( error );
    }
    json[QStringLiteral( "errors" )] = errorArray;
  }
  
  if ( !mMetrics.warnings.isEmpty() )
  {
    QJsonArray warningArray;
    for ( const QString &warning : mMetrics.warnings )
    {
      warningArray.append( warning );
    }
    json[QStringLiteral( "warnings" )] = warningArray;
  }
  
  return json;
}

bool QgsProjectLoadingPerformance::saveToFile( const QString &filePath ) const
{
  const QJsonObject json = exportToJson();
  const QJsonDocument doc( json );
  
  QFile file( filePath );
  if ( !file.open( QIODevice::WriteOnly ) )
  {
    return false;
  }
  
  file.write( doc.toJson() );
  return true;
}

bool QgsProjectLoadingPerformance::loadBaseline( const QString &filePath )
{
  QFile file( filePath );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    return false;
  }
  
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &error );
  if ( error.error != QJsonParseError::NoError )
  {
    return false;
  }
  
  const QJsonObject json = doc.object();
  mMetrics.baselineLoadTimeMs = json[QStringLiteral( "total_load_time_ms" )].toInt();
  
  return true;
}

bool QgsProjectLoadingPerformance::isPerformanceRegression() const
{
  if ( mMetrics.baselineLoadTimeMs <= 0 || mMetrics.totalLoadTimeMs <= 0 )
  {
    return false;
  }
  
  // Consider it a regression if performance decreased by more than 10%
  const double regressionThreshold = 1.10;
  return ( ( double ) mMetrics.totalLoadTimeMs / mMetrics.baselineLoadTimeMs ) > regressionThreshold;
}

QString QgsProjectLoadingPerformance::getPerformanceSummary() const
{
  QString summary;
  summary += QStringLiteral( "Project Loading Performance Summary\n" );
  summary += QStringLiteral( "==================================\n\n" );
  
  summary += QStringLiteral( "Project: %1\n" ).arg( QFileInfo( mMetrics.projectPath ).baseName() );
  summary += QStringLiteral( "File Size: %1 MB\n" ).arg( mMetrics.fileSizeBytes / ( 1024.0 * 1024.0 ), 0, 'f', 2 );
  summary += QStringLiteral( "Total Loading Time: %1 seconds\n" ).arg( mMetrics.totalLoadTimeMs / 1000.0, 0, 'f', 2 );
  summary += QStringLiteral( "Loading Strategy: %1\n" ).arg( mMetrics.loadingStrategy );
  
  if ( mMetrics.progressiveLoaderUsed )
  {
    summary += QStringLiteral( "Progressive Loader: Used with %1 threads\n" ).arg( mMetrics.parallelThreadsUsed );
  }
  
  summary += QStringLiteral( "\nComponent Breakdown:\n" );
  summary += QStringLiteral( "- XML Parsing: %1 ms\n" ).arg( mMetrics.xmlParsingTimeMs );
  summary += QStringLiteral( "- Layer Loading: %1 ms (%2 layers)\n" ).arg( mMetrics.layerLoadingTimeMs ).arg( mMetrics.totalLayers );
  summary += QStringLiteral( "- Layout Loading: %1 ms\n" ).arg( mMetrics.layoutLoadingTimeMs );
  summary += QStringLiteral( "- Style Loading: %1 ms\n" ).arg( mMetrics.styleLoadingTimeMs );
  summary += QStringLiteral( "- Dependency Resolution: %1 ms\n" ).arg( mMetrics.dependencyResolutionTimeMs );
  
  summary += QStringLiteral( "\nMemory Usage:\n" );
  summary += QStringLiteral( "- Peak: %1 MB\n" ).arg( mMetrics.peakMemoryUsageMB );
  summary += QStringLiteral( "- Increase: %1 MB\n" ).arg( mMetrics.finalMemoryUsageMB - mMetrics.initialMemoryUsageMB );
  
  if ( mMetrics.baselineLoadTimeMs > 0 )
  {
    summary += QStringLiteral( "\nPerformance Comparison:\n" );
    summary += QStringLiteral( "- Baseline Time: %1 seconds\n" ).arg( mMetrics.baselineLoadTimeMs / 1000.0, 0, 'f', 2 );
    summary += QStringLiteral( "- Improvement: %1%\n" ).arg( mMetrics.improvementPercent, 0, 'f', 1 );
    
    if ( isPerformanceRegression() )
    {
      summary += QStringLiteral( "- ⚠️  PERFORMANCE REGRESSION DETECTED\n" );
    }
    else if ( mMetrics.improvementPercent > 5.0 )
    {
      summary += QStringLiteral( "- ✅ PERFORMANCE IMPROVEMENT ACHIEVED\n" );
    }
  }
  
  if ( !mMetrics.errors.isEmpty() )
  {
    summary += QStringLiteral( "\nErrors: %1\n" ).arg( mMetrics.errors.size() );
  }
  
  if ( !mMetrics.warnings.isEmpty() )
  {
    summary += QStringLiteral( "Warnings: %1\n" ).arg( mMetrics.warnings.size() );
  }
  
  return summary;
}

qint64 QgsProjectLoadingPerformance::getCurrentMemoryUsageMB() const
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

void QgsProjectLoadingPerformance::calculateImprovement()
{
  if ( mMetrics.baselineLoadTimeMs > 0 && mMetrics.totalLoadTimeMs > 0 )
  {
    const double improvementRatio = 1.0 - ( ( double ) mMetrics.totalLoadTimeMs / mMetrics.baselineLoadTimeMs );
    mMetrics.improvementPercent = improvementRatio * 100.0;
  }
}