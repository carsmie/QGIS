/***************************************************************************
                         qgslayerstylecache.cpp
                         ----------------------
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

#include "qgslayerstylecache.h"
#include "moc_qgslayerstylecache.cpp"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QMutexLocker>
#include <QDomDocument>

QgsLayerStyleCache::QgsLayerStyleCache( QObject *parent )
  : QObject( parent )
  , mLastCleanup( QDateTime::currentDateTime() )
{
  // Initialize cleanup timer
  mCleanupTimer = new QTimer( this );
  connect( mCleanupTimer, &QTimer::timeout, this, &QgsLayerStyleCache::performPeriodicCleanup );
  mCleanupTimer->setInterval( 60000 ); // Clean up every minute
  mCleanupTimer->start();
  
  // Initialize statistics
  updateStatistics();
}

QgsLayerStyleCache::~QgsLayerStyleCache()
{
  clearCache();
}

void QgsLayerStyleCache::setCacheConfig( const CacheConfig &config )
{
  QMutexLocker locker( &mCacheMutex );
  mConfig = config;
  
  // Update cleanup timer interval based on expiration time
  if ( mCleanupTimer )
  {
    const int cleanupInterval = qMax( 30000, static_cast<int>( config.entryExpirationMs / 10 ) );
    mCleanupTimer->setInterval( cleanupInterval );
  }
  
  // Compact cache if new limits are exceeded
  if ( shouldCompactCache() )
  {
    compactCache();
  }
}

QString QgsLayerStyleCache::cacheLayerStyle( const QString &layerId, 
                                             const QDomElement &styleElement,
                                             QgsMapLayer *layer )
{
  Q_UNUSED( layer ) // Reserved for future use
  
  if ( styleElement.isNull() )
  {
    return QString();
  }

  QMutexLocker locker( &mCacheMutex );
  
  const QString fingerprint = generateStyleFingerprint( styleElement, true );
  
  // Check if already cached
  if ( mCacheEntries.contains( fingerprint ) )
  {
    CacheEntry &entry = mCacheEntries[fingerprint];
    updateAccessStatistics( entry );
    mCacheHits++;
    return fingerprint;
  }
  
  // Create new cache entry
  CacheEntry entry;
  entry.fingerprint = fingerprint;
  entry.type = CacheEntryType::FullStyle;
  entry.creationTime = QDateTime::currentDateTime();
  entry.lastAccessTime = entry.creationTime;
  entry.styleElement = styleElement;
  entry.memorySizeBytes = calculateElementMemoryUsage( styleElement );
  entry.accessCount = 1;
  
  // Add layer categorization tags
  entry.tags.append( QStringLiteral( "layer:%1" ).arg( layerId ) );
  const QString layerType = styleElement.parentNode().toElement().attribute( QStringLiteral( "type" ) );
  if ( !layerType.isEmpty() )
  {
    entry.tags.append( QStringLiteral( "type:%1" ).arg( layerType ) );
  }
  
  // Check memory limits before adding
  const qint64 currentMemory = getCurrentMemoryUsage();
  const qint64 maxMemoryBytes = mConfig.maxMemoryUsageMB * 1024 * 1024;
  
  if ( currentMemory + entry.memorySizeBytes > maxMemoryBytes )
  {
    // Need to free up space
    const qint64 targetMemory = maxMemoryBytes * 0.8; // Target 80% of max
    compactCache( targetMemory / ( 1024 * 1024 ) );
  }
  
  // Add to cache
  mCacheEntries[fingerprint] = entry;
  
  // Update layer to fingerprint mapping
  if ( !layerId.isEmpty() )
  {
    mLayerToFingerprints[layerId].append( fingerprint );
  }
  
  mCacheMisses++;
  updateStatistics();
  
  return fingerprint;
}

bool QgsLayerStyleCache::getCachedLayerStyle( const QString &fingerprint, QDomElement &styleElement )
{
  QMutexLocker locker( &mCacheMutex );
  
  if ( !mCacheEntries.contains( fingerprint ) )
  {
    mCacheMisses++;
    return false;
  }
  
  CacheEntry &entry = mCacheEntries[fingerprint];
  
  // Check if entry has expired
  if ( isEntryExpired( entry ) )
  {
    mCacheEntries.remove( fingerprint );
    mCacheMisses++;
    return false;
  }
  
  updateAccessStatistics( entry );
  styleElement = entry.styleElement;
  mCacheHits++;
  
  return true;
}

QString QgsLayerStyleCache::cacheRenderer( const QDomElement &rendererElement, const QString &layerType )
{
  if ( rendererElement.isNull() )
  {
    return QString();
  }

  QMutexLocker locker( &mCacheMutex );
  
  const QString fingerprint = generateStyleFingerprint( rendererElement, true );
  
  // Check if already cached
  if ( mCacheEntries.contains( fingerprint ) )
  {
    CacheEntry &entry = mCacheEntries[fingerprint];
    updateAccessStatistics( entry );
    mCacheHits++;
    return fingerprint;
  }
  
  // Create new cache entry
  CacheEntry entry;
  entry.fingerprint = fingerprint;
  entry.type = CacheEntryType::Renderer;
  entry.creationTime = QDateTime::currentDateTime();
  entry.lastAccessTime = entry.creationTime;
  entry.styleElement = rendererElement;
  entry.memorySizeBytes = calculateElementMemoryUsage( rendererElement );
  entry.accessCount = 1;
  entry.tags.append( QStringLiteral( "renderer" ) );
  entry.tags.append( QStringLiteral( "type:%1" ).arg( layerType ) );
  
  mCacheEntries[fingerprint] = entry;
  mCacheMisses++;
  updateStatistics();
  
  return fingerprint;
}

bool QgsLayerStyleCache::getCachedRenderer( const QString &fingerprint, QDomElement &rendererElement )
{
  QMutexLocker locker( &mCacheMutex );
  
  if ( !mCacheEntries.contains( fingerprint ) )
  {
    mCacheMisses++;
    return false;
  }
  
  CacheEntry &entry = mCacheEntries[fingerprint];
  
  if ( isEntryExpired( entry ) )
  {
    mCacheEntries.remove( fingerprint );
    mCacheMisses++;
    return false;
  }
  
  updateAccessStatistics( entry );
  rendererElement = entry.styleElement;
  mCacheHits++;
  
  return true;
}

QString QgsLayerStyleCache::cacheSymbol( const QDomElement &symbolElement, const QString &symbolType )
{
  if ( symbolElement.isNull() )
  {
    return QString();
  }

  QMutexLocker locker( &mCacheMutex );
  
  const QString fingerprint = generateStyleFingerprint( symbolElement, false ); // Less detailed for symbols
  
  // Check if already cached
  if ( mCacheEntries.contains( fingerprint ) )
  {
    CacheEntry &entry = mCacheEntries[fingerprint];
    updateAccessStatistics( entry );
    mCacheHits++;
    return fingerprint;
  }
  
  // Create new cache entry
  CacheEntry entry;
  entry.fingerprint = fingerprint;
  entry.type = CacheEntryType::Symbol;
  entry.creationTime = QDateTime::currentDateTime();
  entry.lastAccessTime = entry.creationTime;
  entry.styleElement = symbolElement;
  entry.memorySizeBytes = calculateElementMemoryUsage( symbolElement );
  entry.accessCount = 1;
  entry.tags.append( QStringLiteral( "symbol" ) );
  entry.tags.append( QStringLiteral( "symbol_type:%1" ).arg( symbolType ) );
  
  mCacheEntries[fingerprint] = entry;
  mCacheMisses++;
  updateStatistics();
  
  return fingerprint;
}

bool QgsLayerStyleCache::getCachedSymbol( const QString &fingerprint, QDomElement &symbolElement )
{
  QMutexLocker locker( &mCacheMutex );
  
  if ( !mCacheEntries.contains( fingerprint ) )
  {
    mCacheMisses++;
    return false;
  }
  
  CacheEntry &entry = mCacheEntries[fingerprint];
  
  if ( isEntryExpired( entry ) )
  {
    mCacheEntries.remove( fingerprint );
    mCacheMisses++;
    return false;
  }
  
  updateAccessStatistics( entry );
  symbolElement = entry.styleElement;
  mCacheHits++;
  
  return true;
}

QStringList QgsLayerStyleCache::findSimilarStyles( const QDomElement &styleElement, double threshold )
{
  if ( threshold < 0 )
  {
    threshold = mConfig.similarityThreshold;
  }

  QMutexLocker locker( &mCacheMutex );
  
  if ( !mConfig.enableSimilarityDetection )
  {
    return QStringList();
  }
  
  QStringList similarFingerprints;
  
  // Extract properties for comparison
  const QVariantMap targetProperties = extractStyleProperties( styleElement );
  
  // Compare with all cached entries
  for ( auto it = mCacheEntries.begin(); it != mCacheEntries.end(); ++it )
  {
    const CacheEntry &entry = it.value();
    
    // Only compare similar types
    if ( entry.type != CacheEntryType::FullStyle && entry.type != CacheEntryType::Renderer )
    {
      continue;
    }
    
    const QVariantMap entryProperties = extractStyleProperties( entry.styleElement );
    const double similarity = comparePropertyMaps( targetProperties, entryProperties );
    
    if ( similarity >= threshold )
    {
      similarFingerprints.append( entry.fingerprint );
    }
  }
  
  return similarFingerprints;
}

QString QgsLayerStyleCache::generateStyleFingerprint( const QDomElement &element, bool includeDetails ) const
{
  if ( element.isNull() )
  {
    return QString();
  }

  QCryptographicHash hash( QCryptographicHash::Sha256 );
  
  // Add element name and basic attributes
  hash.addData( element.tagName().toUtf8() );
  
  // Add key attributes for fingerprinting
  const QStringList importantAttributes = {
    QStringLiteral( "type" ),
    QStringLiteral( "symbolType" ),
    QStringLiteral( "alpha" ),
    QStringLiteral( "clip_to_extent" ),
    QStringLiteral( "enabled" )
  };
  
  for ( const QString &attr : importantAttributes )
  {
    if ( element.hasAttribute( attr ) )
    {
      hash.addData( attr.toUtf8() );
      hash.addData( element.attribute( attr ).toUtf8() );
    }
  }
  
  if ( includeDetails )
  {
    // Add text content for detailed fingerprinting
    const QString textContent = element.text().trimmed();
    if ( !textContent.isEmpty() )
    {
      hash.addData( textContent.toUtf8() );
    }
    
    // Add child element information
    const QDomNodeList children = element.childNodes();
    for ( int i = 0; i < children.count(); ++i )
    {
      const QDomElement child = children.at( i ).toElement();
      if ( !child.isNull() )
      {
        hash.addData( child.tagName().toUtf8() );
        
        // Add some key child attributes
        if ( child.hasAttribute( QStringLiteral( "type" ) ) )
        {
          hash.addData( child.attribute( QStringLiteral( "type" ) ).toUtf8() );
        }
      }
    }
  }
  
  return QString::fromLatin1( hash.result().toHex() );
}

double QgsLayerStyleCache::calculateStyleSimilarity( const QDomElement &element1, const QDomElement &element2 ) const
{
  if ( element1.isNull() || element2.isNull() )
  {
    return 0.0;
  }

  const QVariantMap props1 = extractStyleProperties( element1 );
  const QVariantMap props2 = extractStyleProperties( element2 );
  
  return comparePropertyMaps( props1, props2 );
}

void QgsLayerStyleCache::clearCache()
{
  QMutexLocker locker( &mCacheMutex );
  
  mCacheEntries.clear();
  mLayerToFingerprints.clear();
  
  // Reset statistics
  mCacheHits = 0;
  mCacheMisses = 0;
  mTotalSaveTime = 0;
  
  updateStatistics();
}

void QgsLayerStyleCache::cleanupExpiredEntries()
{
  QMutexLocker locker( &mCacheMutex );
  
  QStringList expiredKeys;
  
  for ( auto it = mCacheEntries.begin(); it != mCacheEntries.end(); ++it )
  {
    if ( isEntryExpired( it.value() ) )
    {
      expiredKeys.append( it.key() );
    }
  }
  
  for ( const QString &key : expiredKeys )
  {
    mCacheEntries.remove( key );
  }
  
  // Clean up layer mappings
  for ( auto it = mLayerToFingerprints.begin(); it != mLayerToFingerprints.end(); )
  {
    QStringList &fingerprints = it.value();
    fingerprints.removeIf( [this]( const QString & fp ) { return !mCacheEntries.contains( fp ); } );
    
    if ( fingerprints.isEmpty() )
    {
      it = mLayerToFingerprints.erase( it );
    }
    else
    {
      ++it;
    }
  }
  
  if ( !expiredKeys.isEmpty() )
  {
    updateStatistics();
  }
}

void QgsLayerStyleCache::removeCachedLayer( const QString &layerId )
{
  QMutexLocker locker( &mCacheMutex );
  
  if ( !mLayerToFingerprints.contains( layerId ) )
  {
    return;
  }
  
  const QStringList fingerprints = mLayerToFingerprints.take( layerId );
  
  for ( const QString &fingerprint : fingerprints )
  {
    mCacheEntries.remove( fingerprint );
  }
  
  updateStatistics();
}

QgsLayerStyleCache::CacheStatistics QgsLayerStyleCache::getStatistics() const
{
  QMutexLocker locker( &mCacheMutex );
  return mStatistics;
}

QVariantMap QgsLayerStyleCache::exportStatistics() const
{
  QMutexLocker locker( &mCacheMutex );
  
  QVariantMap stats;
  stats[QStringLiteral( "total_entries" )] = mStatistics.totalEntries;
  stats[QStringLiteral( "hit_count" )] = mStatistics.hitCount;
  stats[QStringLiteral( "miss_count" )] = mStatistics.missCount;
  stats[QStringLiteral( "hit_ratio" )] = mStatistics.hitRatio;
  stats[QStringLiteral( "total_memory_mb" )] = mStatistics.totalMemoryUsage / ( 1024.0 * 1024.0 );
  stats[QStringLiteral( "time_saved_seconds" )] = mStatistics.timeSavedMs / 1000.0;
  
  QVariantMap typeBreakdown;
  for ( auto it = mStatistics.entriesByType.begin(); it != mStatistics.entriesByType.end(); ++it )
  {
    QString typeName;
    switch ( it.key() )
    {
      case CacheEntryType::Symbol:
        typeName = QStringLiteral( "symbols" );
        break;
      case CacheEntryType::Renderer:
        typeName = QStringLiteral( "renderers" );
        break;
      case CacheEntryType::Labeling:
        typeName = QStringLiteral( "labeling" );
        break;
      case CacheEntryType::FullStyle:
        typeName = QStringLiteral( "full_styles" );
        break;
      case CacheEntryType::StyleSheet:
        typeName = QStringLiteral( "stylesheets" );
        break;
    }
    typeBreakdown[typeName] = it.value();
  }
  stats[QStringLiteral( "entries_by_type" )] = typeBreakdown;
  
  return stats;
}

void QgsLayerStyleCache::preloadStyles( const QList<QDomElement> &styleElements, const QStringList &layerIds )
{
  for ( int i = 0; i < styleElements.size(); ++i )
  {
    const QDomElement &element = styleElements[i];
    const QString layerId = ( i < layerIds.size() ) ? layerIds[i] : QString();
    
    // Cache the full style
    cacheLayerStyle( layerId, element );
    
    // Cache individual renderers if present
    const QDomElement rendererElement = element.firstChildElement( QStringLiteral( "renderer-v2" ) );
    if ( !rendererElement.isNull() )
    {
      const QString layerType = element.parentNode().toElement().attribute( QStringLiteral( "type" ) );
      cacheRenderer( rendererElement, layerType );
      
      // Cache symbols within the renderer
      const QDomNodeList symbols = rendererElement.elementsByTagName( QStringLiteral( "symbol" ) );
      for ( int j = 0; j < symbols.count(); ++j )
      {
        const QDomElement symbolElement = symbols.at( j ).toElement();
        if ( !symbolElement.isNull() )
        {
          const QString symbolType = symbolElement.attribute( QStringLiteral( "type" ) );
          cacheSymbol( symbolElement, symbolType );
        }
      }
    }
  }
}

bool QgsLayerStyleCache::shouldCompactCache() const
{
  QMutexLocker locker( &mCacheMutex );
  
  const qint64 currentMemory = getCurrentMemoryUsage();
  const qint64 maxMemoryBytes = mConfig.maxMemoryUsageMB * 1024 * 1024;
  
  return ( currentMemory > maxMemoryBytes ) || ( mCacheEntries.size() > mConfig.maxEntries );
}

void QgsLayerStyleCache::compactCache( qint64 targetMemoryMB )
{
  QMutexLocker locker( &mCacheMutex );
  
  const int initialEntries = mCacheEntries.size();
  const qint64 initialMemory = getCurrentMemoryUsage();
  
  qint64 targetMemoryBytes;
  if ( targetMemoryMB > 0 )
  {
    targetMemoryBytes = targetMemoryMB * 1024 * 1024;
  }
  else
  {
    targetMemoryBytes = mConfig.maxMemoryUsageMB * 1024 * 1024 * 0.7; // Target 70% of max
  }
  
  // Remove expired entries first
  cleanupExpiredEntries();
  
  // If still over limit, remove LRU entries
  qint64 currentMemory = getCurrentMemoryUsage();
  if ( currentMemory > targetMemoryBytes )
  {
    // Calculate how many entries to remove
    const double avgEntrySize = static_cast<double>( currentMemory ) / mCacheEntries.size();
    const int entriesToRemove = static_cast<int>( ( currentMemory - targetMemoryBytes ) / avgEntrySize ) + 1;
    
    removeLRUEntries( entriesToRemove );
  }
  
  const int finalEntries = mCacheEntries.size();
  const qint64 finalMemory = getCurrentMemoryUsage();
  const int entriesRemoved = initialEntries - finalEntries;
  const qint64 memoryFreed = ( initialMemory - finalMemory ) / ( 1024 * 1024 );
  
  updateStatistics();
  
  emit cacheCompacted( entriesRemoved, memoryFreed );
}

void QgsLayerStyleCache::performPeriodicCleanup()
{
  const QDateTime now = QDateTime::currentDateTime();
  
  // Only cleanup if enough time has passed
  if ( mLastCleanup.msecsTo( now ) > mConfig.entryExpirationMs / 4 )
  {
    cleanupExpiredEntries();
    mLastCleanup = now;
    
    // Check if compaction is needed
    if ( shouldCompactCache() )
    {
      compactCache();
    }
  }
}

qint64 QgsLayerStyleCache::calculateElementMemoryUsage( const QDomElement &element ) const
{
  if ( element.isNull() )
  {
    return 0;
  }

  // Rough estimation based on XML content
  const QString xmlString = element.text();
  qint64 baseSize = xmlString.length() * sizeof( QChar ); // Basic text content
  
  // Add overhead for attributes
  const QDomNamedNodeMap attributes = element.attributes();
  for ( int i = 0; i < attributes.count(); ++i )
  {
    const QDomNode attr = attributes.item( i );
    baseSize += attr.nodeName().length() * sizeof( QChar );
    baseSize += attr.nodeValue().length() * sizeof( QChar );
  }
  
  // Add overhead for child elements (recursive)
  const QDomNodeList children = element.childNodes();
  for ( int i = 0; i < children.count(); ++i )
  {
    const QDomElement child = children.at( i ).toElement();
    if ( !child.isNull() )
    {
      baseSize += child.tagName().length() * sizeof( QChar );
      baseSize += 64; // Overhead for DOM structure
    }
  }
  
  // Add base overhead for the cache entry itself
  baseSize += 256; // Approximate overhead for CacheEntry structure
  
  return baseSize;
}

void QgsLayerStyleCache::updateAccessStatistics( CacheEntry &entry )
{
  entry.lastAccessTime = QDateTime::currentDateTime();
  entry.accessCount++;
}

bool QgsLayerStyleCache::isEntryExpired( const CacheEntry &entry ) const
{
  const QDateTime now = QDateTime::currentDateTime();
  return entry.creationTime.msecsTo( now ) > mConfig.entryExpirationMs;
}

QVariantMap QgsLayerStyleCache::extractStyleProperties( const QDomElement &element ) const
{
  QVariantMap properties;
  
  // Extract basic properties
  properties[QStringLiteral( "tag_name" )] = element.tagName();
  
  // Extract key attributes
  const QStringList keyAttributes = {
    QStringLiteral( "type" ), QStringLiteral( "symbolType" ),
    QStringLiteral( "alpha" ), QStringLiteral( "enabled" )
  };
  
  for ( const QString &attr : keyAttributes )
  {
    if ( element.hasAttribute( attr ) )
    {
      properties[attr] = element.attribute( attr );
    }
  }
  
  // Count child elements by type
  QHash<QString, int> childCounts;
  const QDomNodeList children = element.childNodes();
  for ( int i = 0; i < children.count(); ++i )
  {
    const QDomElement child = children.at( i ).toElement();
    if ( !child.isNull() )
    {
      childCounts[child.tagName()]++;
    }
  }
  
  // Convert child counts to properties
  for ( auto it = childCounts.begin(); it != childCounts.end(); ++it )
  {
    properties[QStringLiteral( "child_%1_count" ).arg( it.key() )] = it.value();
  }
  
  return properties;
}

double QgsLayerStyleCache::comparePropertyMaps( const QVariantMap &props1, const QVariantMap &props2 ) const
{
  if ( props1.isEmpty() && props2.isEmpty() )
  {
    return 1.0;
  }
  
  if ( props1.isEmpty() || props2.isEmpty() )
  {
    return 0.0;
  }
  
  // Get all unique keys
  QSet<QString> allKeys;
  allKeys.unite( QSet<QString>( props1.keyBegin(), props1.keyEnd() ) );
  allKeys.unite( QSet<QString>( props2.keyBegin(), props2.keyEnd() ) );
  
  int matches = 0;
  int total = allKeys.size();
  
  for ( const QString &key : allKeys )
  {
    const QVariant value1 = props1.value( key );
    const QVariant value2 = props2.value( key );
    
    if ( value1 == value2 )
    {
      matches++;
    }
    else if ( value1.canConvert<QString>() && value2.canConvert<QString>() )
    {
      // For string values, check for partial similarity
      const QString str1 = value1.toString();
      const QString str2 = value2.toString();
      
      if ( !str1.isEmpty() && !str2.isEmpty() )
      {
        // Simple similarity check - could be enhanced with more sophisticated algorithms
        const int commonLength = qMin( str1.length(), str2.length() );
        int commonChars = 0;
        for ( int i = 0; i < commonLength; ++i )
        {
          if ( str1[i] == str2[i] )
          {
            commonChars++;
          }
        }
        
        const double stringSimilarity = static_cast<double>( commonChars ) / qMax( str1.length(), str2.length() );
        if ( stringSimilarity > 0.7 ) // Partial match
        {
          matches++;
        }
      }
    }
  }
  
  return static_cast<double>( matches ) / total;
}

void QgsLayerStyleCache::removeLRUEntries( int targetCount )
{
  if ( targetCount <= 0 || mCacheEntries.size() <= targetCount )
  {
    return;
  }

  // Create a list of entries sorted by last access time (oldest first)
  QList<QPair<QDateTime, QString>> entries;
  for ( auto it = mCacheEntries.begin(); it != mCacheEntries.end(); ++it )
  {
    entries.append( qMakePair( it.value().lastAccessTime, it.key() ) );
  }
  
  std::sort( entries.begin(), entries.end() );
  
  // Remove the oldest entries
  const int toRemove = qMin( targetCount, entries.size() );
  for ( int i = 0; i < toRemove; ++i )
  {
    const QString fingerprint = entries[i].second;
    mCacheEntries.remove( fingerprint );
  }
  
  // Clean up layer mappings
  for ( auto it = mLayerToFingerprints.begin(); it != mLayerToFingerprints.end(); )
  {
    QStringList &fingerprints = it.value();
    fingerprints.removeIf( [this]( const QString & fp ) { return !mCacheEntries.contains( fp ); } );
    
    if ( fingerprints.isEmpty() )
    {
      it = mLayerToFingerprints.erase( it );
    }
    else
    {
      ++it;
    }
  }
}

qint64 QgsLayerStyleCache::getCurrentMemoryUsage() const
{
  qint64 totalMemory = 0;
  
  for ( const CacheEntry &entry : mCacheEntries )
  {
    totalMemory += entry.memorySizeBytes;
  }
  
  return totalMemory;
}

void QgsLayerStyleCache::updateStatistics()
{
  mStatistics.totalEntries = mCacheEntries.size();
  mStatistics.hitCount = mCacheHits;
  mStatistics.missCount = mCacheMisses;
  mStatistics.totalMemoryUsage = getCurrentMemoryUsage();
  
  const qint64 totalRequests = mCacheHits + mCacheMisses;
  mStatistics.hitRatio = totalRequests > 0 ? static_cast<double>( mCacheHits ) / totalRequests : 0.0;
  
  // Estimate time saved (very rough approximation)
  mStatistics.timeSavedMs = mCacheHits * 10; // Assume 10ms saved per cache hit
  
  // Count entries by type
  mStatistics.entriesByType.clear();
  for ( const CacheEntry &entry : mCacheEntries )
  {
    mStatistics.entriesByType[entry.type]++;
  }
  
  // Check memory threshold
  const qint64 maxMemoryBytes = mConfig.maxMemoryUsageMB * 1024 * 1024;
  if ( mStatistics.totalMemoryUsage > maxMemoryBytes )
  {
    emit memoryThresholdExceeded( mStatistics.totalMemoryUsage / ( 1024 * 1024 ), mConfig.maxMemoryUsageMB );
  }
  
  emit statisticsUpdated( mStatistics );
}