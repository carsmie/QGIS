/***************************************************************************
                         qgsprojectstreamingparser.cpp
                         -----------------------------
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

#include "qgsprojectstreamingparser.h"
#include "qgsproject.h"
#include "qgslayertree.h"
#include "qgsmaplayerstore.h"
#include "qgsvectorlayer.h"
#include "qgslogger.h"
#include "qgsapplication.h"

#include <QTimer>
#include <QFileInfo>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QElapsedTimer>

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

QgsProjectStreamingParser::QgsProjectStreamingParser( QObject *parent )
  : QObject( parent )
{
  // Initialize progress timer
  mProgressTimer = new QTimer( this );
  connect( mProgressTimer, &QTimer::timeout, this, &QgsProjectStreamingParser::updateProgress );
  
  // Initialize default processors
  initializeDefaultProcessors();
}

QgsProjectStreamingParser::~QgsProjectStreamingParser()
{
  cleanupMemory();
}

void QgsProjectStreamingParser::setParsingConfig( const ParsingConfig &config )
{
  mConfig = config;
  
  // Update progress timer interval
  if ( mProgressTimer )
  {
    mProgressTimer->setInterval( config.progressReportIntervalMs );
  }
}

void QgsProjectStreamingParser::registerElementProcessor( const QString &elementName, ElementProcessorCallback callback )
{
  mElementProcessors[elementName] = callback;
}

bool QgsProjectStreamingParser::parseProjectFile( const QString &filename, QgsProject *project )
{
  QFile file( filename );
  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
  {
    emit errorOccurred( tr( "Unable to open project file: %1" ).arg( filename ) );
    return false;
  }

  mTotalBytes = file.size();
  return parseFromDevice( &file, project );
}

bool QgsProjectStreamingParser::parseFromDevice( QIODevice *device, QgsProject *project )
{
  if ( !device || !device->isOpen() || !project )
  {
    emit errorOccurred( tr( "Invalid device or project" ) );
    return false;
  }

  // Initialize parsing state
  mCancelled = false;
  mProgress = 0;
  mParsedBytes = 0;
  mElementsProcessed = 0;
  mElementsDeferred = 0;
  mParsingStartTime = QDateTime::currentDateTime();
  mInitialMemoryUsage = getCurrentMemoryUsageMB();
  mPeakMemoryUsage = mInitialMemoryUsage;
  
  // Clear previous parsing results
  mParsedElements.clear();
  mDeferredElements.clear();
  mStatistics.clear();
  
  // Start progress reporting
  if ( mConfig.progressReportIntervalMs > 0 )
  {
    mProgressTimer->start();
  }
  
  QXmlStreamReader reader( device );
  bool success = true;
  
  QgsDebugMsgLevel( QStringLiteral( "Starting streaming parse with strategy: %1" )
                    .arg( static_cast<int>( mConfig.strategy ) ), 2 );
  
  try
  {
    while ( !reader.atEnd() && !mCancelled )
    {
      QXmlStreamReader::TokenType token = reader.readNext();
      
      if ( token == QXmlStreamReader::StartElement )
      {
        if ( !parseElement( reader, project ) )
        {
          success = false;
          break;
        }
      }
      
      // Update parsed bytes estimate
      mParsedBytes = device->pos();
      
      // Check memory usage and cleanup if necessary
      const qint64 currentMemory = getCurrentMemoryUsageMB();
      if ( currentMemory > mPeakMemoryUsage )
      {
        mPeakMemoryUsage = currentMemory;
      }
      
      if ( currentMemory > mConfig.maxMemoryUsageMB )
      {
        QgsDebugMsgLevel( QStringLiteral( "Memory usage exceeded threshold, cleaning up" ), 3 );
        cleanupMemory();
      }
    }
    
    if ( reader.hasError() && !mCancelled )
    {
      emit errorOccurred( tr( "XML parsing error: %1" ).arg( reader.errorString() ) );
      success = false;
    }
  }
  catch ( const std::exception &e )
  {
    emit errorOccurred( tr( "Exception during parsing: %1" ).arg( e.what() ) );
    success = false;
  }
  
  // Stop progress reporting
  mProgressTimer->stop();
  
  if ( success && !mCancelled )
  {
    // Process elements by priority if using progressive strategy
    if ( mConfig.strategy == ParsingStrategy::Progressive || mConfig.strategy == ParsingStrategy::Essential )
    {
      success = processElementsByPriority( project );
    }
  }
  
  // Update final statistics
  const QDateTime endTime = QDateTime::currentDateTime();
  mStatistics[QStringLiteral( "parsing_time_ms" )] = mParsingStartTime.msecsTo( endTime );
  mStatistics[QStringLiteral( "elements_processed" )] = mElementsProcessed;
  mStatistics[QStringLiteral( "elements_deferred" )] = mElementsDeferred;
  mStatistics[QStringLiteral( "peak_memory_mb" )] = mPeakMemoryUsage;
  mStatistics[QStringLiteral( "memory_increase_mb" )] = mPeakMemoryUsage - mInitialMemoryUsage;
  mStatistics[QStringLiteral( "bytes_processed" )] = mParsedBytes;
  mStatistics[QStringLiteral( "cancelled" )] = mCancelled;
  
  mProgress = mCancelled ? mProgress : 100;
  emit progressChanged( mProgress, QString() );
  emit parsingCompleted( success && !mCancelled, mStatistics );
  
  return success && !mCancelled;
}

void QgsProjectStreamingParser::cancelParsing()
{
  mCancelled = true;
  QgsDebugMsgLevel( QStringLiteral( "Streaming parser cancelled" ), 2 );
}

QVariantMap QgsProjectStreamingParser::getParsingStatistics() const
{
  return mStatistics;
}

bool QgsProjectStreamingParser::processDeferredElements( const QStringList &elementNames )
{
  if ( mDeferredElements.isEmpty() )
  {
    return true;
  }

  QgsDebugMsgLevel( QStringLiteral( "Processing %1 deferred elements" ).arg( mDeferredElements.size() ), 2 );
  
  for ( const ElementInfo &elementInfo : std::as_const( mDeferredElements ) )
  {
    if ( mCancelled )
    {
      return false;
    }
    
    // Skip if specific element names were requested and this isn't one of them
    if ( !elementNames.isEmpty() && !elementNames.contains( elementInfo.name ) )
    {
      continue;
    }
    
    // Process the deferred element
    if ( mElementProcessors.contains( elementInfo.name ) )
    {
      const ElementProcessorCallback &processor = mElementProcessors[elementInfo.name];
      if ( processor )
      {
        // Note: This would need the original project context, which should be passed in
        // For now, we'll emit the elementParsed signal to let the caller handle it
        emit elementParsed( elementInfo );
        mElementsProcessed++;
      }
    }
  }
  
  return true;
}

QgsProjectStreamingParser::ElementPriority QgsProjectStreamingParser::getElementPriority( const QString &elementName ) const
{
  // Critical elements - needed for basic project functionality
  if ( elementName == QLatin1String( "qgis" ) ||
       elementName == QLatin1String( "projectCrs" ) ||
       elementName == QLatin1String( "properties" ) ||
       elementName == QLatin1String( "title" ) ||
       elementName == QLatin1String( "projectname" ) )
  {
    return ElementPriority::Critical;
  }
  
  // High priority - important for project setup
  if ( elementName == QLatin1String( "Variables" ) ||
       elementName == QLatin1String( "layer-tree-group" ) ||
       elementName == QLatin1String( "SpatialRefSys" ) ||
       elementName == QLatin1String( "homePath" ) ||
       elementName == QLatin1String( "ProjectTimeSettings" ) )
  {
    return ElementPriority::High;
  }
  
  // Medium priority - essential layers and core functionality
  if ( elementName == QLatin1String( "maplayer" ) ||
       elementName == QLatin1String( "mapcanvas" ) ||
       elementName == QLatin1String( "legend" ) ||
       elementName == QLatin1String( "projectlayers" ) )
  {
    // Check if this is a priority layer
    // Note: This would require looking at the layer ID, which needs more context
    return ElementPriority::Medium;
  }
  
  // Low priority - styling and non-essential layers
  if ( elementName == QLatin1String( "renderer-v2" ) ||
       elementName == QLatin1String( "labeling" ) ||
       elementName == QLatin1String( "symbol" ) ||
       elementName == QLatin1String( "layer-style-override" ) )
  {
    return ElementPriority::Low;
  }
  
  // Background priority - layouts, annotations, etc.
  if ( elementName == QLatin1String( "Layouts" ) ||
       elementName == QLatin1String( "Annotations" ) ||
       elementName == QLatin1String( "auxiliaryLayer" ) ||
       elementName == QLatin1String( "layout" ) )
  {
    return ElementPriority::Background;
  }
  
  // Default to medium priority for unknown elements
  return ElementPriority::Medium;
}

qint64 QgsProjectStreamingParser::estimateElementSize( const QXmlStreamReader &reader ) const
{
  Q_UNUSED( reader )
  
  // This is a rough estimation - in a real implementation, you might
  // look at the element name, attributes, and potentially read ahead
  // to get a better size estimate
  
  const QString elementName = reader.name().toString();
  
  if ( elementName == QLatin1String( "maplayer" ) )
  {
    return 1024 * 50; // Estimate 50KB per layer
  }
  else if ( elementName == QLatin1String( "layout" ) )
  {
    return 1024 * 20; // Estimate 20KB per layout
  }
  else if ( elementName == QLatin1String( "symbol" ) )
  {
    return 1024 * 5; // Estimate 5KB per symbol
  }
  else if ( elementName == QLatin1String( "renderer-v2" ) )
  {
    return 1024 * 10; // Estimate 10KB per renderer
  }
  
  return 1024; // Default 1KB estimate
}

bool QgsProjectStreamingParser::shouldDeferElement( const QString &elementName, qint64 estimatedSize ) const
{
  // Check if element is explicitly marked for deferral
  if ( mConfig.deferredElements.contains( elementName ) )
  {
    return true;
  }
  
  // For Essential strategy, defer everything below High priority
  if ( mConfig.strategy == ParsingStrategy::Essential )
  {
    const ElementPriority priority = getElementPriority( elementName );
    return priority > ElementPriority::High;
  }
  
  // For LazyLoad strategy, defer large elements
  if ( mConfig.strategy == ParsingStrategy::LazyLoad )
  {
    return estimatedSize > 1024 * 10; // Defer elements larger than 10KB
  }
  
  // Check memory constraints
  const qint64 currentMemory = getCurrentMemoryUsageMB();
  if ( currentMemory + ( estimatedSize / ( 1024 * 1024 ) ) > mConfig.maxMemoryUsageMB )
  {
    return true;
  }
  
  return false;
}

bool QgsProjectStreamingParser::parseElement( QXmlStreamReader &reader, QgsProject *project )
{
  const QString elementName = reader.name().toString();
  const ElementPriority priority = getElementPriority( elementName );
  const qint64 estimatedSize = estimateElementSize( reader );
  
  mCurrentElement = elementName;
  
  // Check if this element should be deferred
  if ( shouldDeferElement( elementName, estimatedSize ) )
  {
    // Store element info for later processing
    ElementInfo elementInfo;
    elementInfo.name = elementName;
    elementInfo.priority = priority;
    elementInfo.sizeBytesEstimate = estimatedSize;
    elementInfo.isDeferred = true;
    
    // For deferred elements, we might want to save the raw XML for later processing
    // This is a simplified approach - in practice, you might want to save just
    // the element position or create a minimal DOM representation
    elementInfo.domElement = streamToDomElement( reader );
    
    mDeferredElements.append( elementInfo );
    mElementsDeferred++;
    
    QgsDebugMsgLevel( QStringLiteral( "Deferred element: %1 (estimated size: %2 bytes)" )
                      .arg( elementName ).arg( estimatedSize ), 3 );
    
    return true;
  }
  
  // Process the element immediately
  ElementInfo elementInfo;
  elementInfo.name = elementName;
  elementInfo.priority = priority;
  elementInfo.sizeBytesEstimate = estimatedSize;
  elementInfo.isDeferred = false;
  elementInfo.domElement = streamToDomElement( reader );
  
  // Try to process with registered processor
  if ( mElementProcessors.contains( elementName ) )
  {
    const ElementProcessorCallback &processor = mElementProcessors[elementName];
    if ( processor )
    {
      if ( !processor( elementInfo, project ) )
      {
        emit errorOccurred( tr( "Failed to process element: %1" ).arg( elementName ) );
        return false;
      }
    }
  }
  
  mParsedElements.append( elementInfo );
  mElementsProcessed++;
  
  emit elementParsed( elementInfo );
  
  return true;
}

QDomElement QgsProjectStreamingParser::streamToDomElement( QXmlStreamReader &reader )
{
  // This is a simplified conversion - in practice, you might want to
  // be more efficient about this, especially for large elements
  
  const QString elementName = reader.name().toString();
  
  // Create a temporary document to hold the element
  QDomDocument tempDoc;
  QDomElement element = tempDoc.createElement( elementName );
  
  // Copy attributes
  const QXmlStreamAttributes attributes = reader.attributes();
  for ( const QXmlStreamAttribute &attr : attributes )
  {
    element.setAttribute( attr.name().toString(), attr.value().toString() );
  }
  
  // For now, we'll skip the content and child elements to keep it simple
  // In a full implementation, you'd recursively parse child elements
  
  return element;
}

bool QgsProjectStreamingParser::processElementsByPriority( QgsProject *project )
{
  Q_UNUSED( project )
  
  // Sort parsed elements by priority
  std::sort( mParsedElements.begin(), mParsedElements.end(),
             []( const ElementInfo & a, const ElementInfo & b )
             {
               return static_cast<int>( a.priority ) < static_cast<int>( b.priority );
             } );
  
  QgsDebugMsgLevel( QStringLiteral( "Processing %1 elements by priority" ).arg( mParsedElements.size() ), 2 );
  
  // Process elements in priority order
  for ( const ElementInfo &elementInfo : std::as_const( mParsedElements ) )
  {
    if ( mCancelled )
    {
      return false;
    }
    
    // For Essential strategy, stop after High priority elements
    if ( mConfig.strategy == ParsingStrategy::Essential &&
         elementInfo.priority > ElementPriority::High )
    {
      break;
    }
    
    // Emit signal to indicate processing
    emit elementParsed( elementInfo );
  }
  
  return true;
}

qint64 QgsProjectStreamingParser::getCurrentMemoryUsageMB() const
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

void QgsProjectStreamingParser::cleanupMemory()
{
  // Remove processed elements with low priority to free memory
  auto it = mParsedElements.begin();
  while ( it != mParsedElements.end() )
  {
    if ( it->priority >= ElementPriority::Low && !it->domElement.isNull() )
    {
      it->domElement = QDomElement(); // Clear the DOM element to free memory
      QgsDebugMsgLevel( QStringLiteral( "Cleaned up element: %1" ).arg( it->name ), 4 );
    }
    ++it;
  }
  
  QgsDebugMsgLevel( QStringLiteral( "Memory cleanup completed" ), 3 );
}

void QgsProjectStreamingParser::initializeDefaultProcessors()
{
  // Register default processors for common elements
  
  // Project root element processor
  registerElementProcessor( QStringLiteral( "qgis" ), []( const ElementInfo & elementInfo, QgsProject * project ) -> bool
  {
    Q_UNUSED( elementInfo )
    Q_UNUSED( project )
    QgsDebugMsgLevel( QStringLiteral( "Processing project root element" ), 3 );
    return true;
  } );
  
  // Layer processor
  registerElementProcessor( QStringLiteral( "maplayer" ), []( const ElementInfo & elementInfo, QgsProject * project ) -> bool
  {
    Q_UNUSED( elementInfo )
    Q_UNUSED( project )
    QgsDebugMsgLevel( QStringLiteral( "Processing map layer element" ), 3 );
    // In a real implementation, this would create the actual layer
    return true;
  } );
  
  // Properties processor
  registerElementProcessor( QStringLiteral( "properties" ), []( const ElementInfo & elementInfo, QgsProject * project ) -> bool
  {
    Q_UNUSED( elementInfo )
    Q_UNUSED( project )
    QgsDebugMsgLevel( QStringLiteral( "Processing project properties" ), 3 );
    return true;
  } );
}

void QgsProjectStreamingParser::updateProgress()
{
  if ( mTotalBytes > 0 )
  {
    const int newProgress = static_cast<int>( ( mParsedBytes * 100 ) / mTotalBytes );
    if ( newProgress != mProgress )
    {
      mProgress = newProgress;
      emit progressChanged( mProgress, mCurrentElement );
    }
  }
}