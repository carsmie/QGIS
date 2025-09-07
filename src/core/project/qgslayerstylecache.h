/***************************************************************************
                         qgslayerstylecache.h
                         --------------------
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

#ifndef QGSLAYERSTYLECACHE_H
#define QGSLAYERSTYLECACHE_H

#include "qgis_core.h"
#include <QObject>
#include <QHash>
#include <QMutex>
#include <QDateTime>
#include <QDomElement>
#include <QMutexLocker>

class QgsMapLayer;
class QgsFeatureRenderer;
class QgsSymbol;
class QgsLabelingEngineSettings;
class QgsAbstractVectorLayerLabeling;

/**
 * \ingroup core
 * \brief Intelligent caching system for layer styles and rendering components
 *
 * The QgsLayerStyleCache provides high-performance caching and reuse of layer styles,
 * renderers, symbols, and labeling configurations. This significantly reduces the time
 * required to load projects with many layers that share similar styling.
 *
 * Key features:
 * - Style fingerprinting for efficient cache key generation
 * - Hierarchical caching (symbols, renderers, full styles)
 * - Memory-aware cache management with automatic cleanup
 * - Style similarity detection and partial reuse
 * - Thread-safe operations for parallel loading
 * - Statistics and performance monitoring
 *
 * \since QGIS 3.34
 */
class CORE_EXPORT QgsLayerStyleCache : public QObject
{
    Q_OBJECT

  public:

    /**
     * Cache entry types for different style components
     */
    enum class CacheEntryType
    {
      Symbol,           //!< Individual symbol cache entry
      Renderer,         //!< Feature renderer cache entry
      Labeling,         //!< Labeling configuration cache entry
      FullStyle,        //!< Complete layer style cache entry
      StyleSheet        //!< Style sheet/template cache entry
    };

    /**
     * Cache configuration options
     */
    struct CacheConfig
    {
      qint64 maxMemoryUsageMB = 128;        //!< Maximum memory usage for cache
      int maxEntries = 1000;                //!< Maximum number of cache entries
      int maxSymbolEntries = 5000;          //!< Maximum number of symbol entries
      bool enableSimilarityDetection = true; //!< Enable style similarity detection
      bool enableStatistics = true;         //!< Enable cache statistics collection
      qint64 entryExpirationMs = 300000;    //!< Entry expiration time (5 minutes)
      double similarityThreshold = 0.85;    //!< Similarity threshold for style reuse
    };

    /**
     * Information about a cached style entry
     */
    struct CacheEntry
    {
      QString fingerprint;              //!< Style fingerprint (cache key)
      CacheEntryType type;              //!< Type of cached entry
      QDateTime creationTime;           //!< When entry was created
      QDateTime lastAccessTime;         //!< When entry was last accessed
      qint64 memorySizeBytes = 0;       //!< Estimated memory usage
      int accessCount = 0;              //!< Number of times accessed
      QDomElement styleElement;         //!< Original style XML element
      QVariant cachedObject;            //!< The actual cached object
      QStringList tags;                 //!< Tags for categorization
    };

    /**
     * Cache statistics
     */
    struct CacheStatistics
    {
      int totalEntries = 0;             //!< Total number of cache entries
      int hitCount = 0;                 //!< Number of cache hits
      int missCount = 0;                //!< Number of cache misses
      qint64 totalMemoryUsage = 0;      //!< Total memory usage in bytes
      qint64 timeSavedMs = 0;           //!< Estimated time saved by caching
      double hitRatio = 0.0;            //!< Cache hit ratio (0-1)
      QHash<CacheEntryType, int> entriesByType; //!< Entries count by type
    };

    /**
     * Constructor
     * \param parent Parent object
     */
    explicit QgsLayerStyleCache( QObject *parent = nullptr );

    /**
     * Destructor
     */
    ~QgsLayerStyleCache() override;

    /**
     * Set cache configuration
     * \param config The configuration to use
     */
    void setCacheConfig( const CacheConfig &config );

    /**
     * Get current cache configuration
     * \returns Current configuration
     */
    CacheConfig cacheConfig() const { return mConfig; }

    /**
     * Cache a complete layer style
     * \param layerId Layer ID for the style
     * \param styleElement XML element containing the style
     * \param layer The layer object (optional, for additional context)
     * \returns Fingerprint of the cached style
     */
    QString cacheLayerStyle( const QString &layerId, 
                             const QDomElement &styleElement,
                             QgsMapLayer *layer = nullptr );

    /**
     * Retrieve a cached layer style
     * \param fingerprint Style fingerprint to look up
     * \param styleElement Output parameter for the style element
     * \returns true if style was found in cache
     */
    bool getCachedLayerStyle( const QString &fingerprint, QDomElement &styleElement );

    /**
     * Cache a feature renderer
     * \param rendererElement XML element containing the renderer
     * \param layerType Type of layer this renderer is for
     * \returns Fingerprint of the cached renderer
     */
    QString cacheRenderer( const QDomElement &rendererElement, const QString &layerType );

    /**
     * Retrieve a cached renderer
     * \param fingerprint Renderer fingerprint to look up
     * \param rendererElement Output parameter for the renderer element
     * \returns true if renderer was found in cache
     */
    bool getCachedRenderer( const QString &fingerprint, QDomElement &rendererElement );

    /**
     * Cache a symbol
     * \param symbolElement XML element containing the symbol
     * \param symbolType Type of symbol (marker, line, fill)
     * \returns Fingerprint of the cached symbol
     */
    QString cacheSymbol( const QDomElement &symbolElement, const QString &symbolType );

    /**
     * Retrieve a cached symbol
     * \param fingerprint Symbol fingerprint to look up
     * \param symbolElement Output parameter for the symbol element
     * \returns true if symbol was found in cache
     */
    bool getCachedSymbol( const QString &fingerprint, QDomElement &symbolElement );

    /**
     * Find similar styles that can be reused
     * \param styleElement Style element to find similarities for
     * \param threshold Similarity threshold (0-1)
     * \returns List of fingerprints of similar styles
     */
    QStringList findSimilarStyles( const QDomElement &styleElement, double threshold = -1.0 );

    /**
     * Generate a fingerprint for a style element
     * \param element XML element to fingerprint
     * \param includeDetails Whether to include detailed properties in fingerprint
     * \returns Unique fingerprint string
     */
    QString generateStyleFingerprint( const QDomElement &element, bool includeDetails = true ) const;

    /**
     * Calculate similarity between two style elements
     * \param element1 First style element
     * \param element2 Second style element
     * \returns Similarity score (0-1, where 1 is identical)
     */
    double calculateStyleSimilarity( const QDomElement &element1, const QDomElement &element2 ) const;

    /**
     * Clear all cache entries
     */
    void clearCache();

    /**
     * Remove expired cache entries
     */
    void cleanupExpiredEntries();

    /**
     * Remove cache entries for a specific layer
     * \param layerId Layer ID to remove entries for
     */
    void removeCachedLayer( const QString &layerId );

    /**
     * Get current cache statistics
     * \returns Cache statistics
     */
    CacheStatistics getStatistics() const;

    /**
     * Export cache statistics to JSON
     * \returns JSON object with statistics
     */
    QVariantMap exportStatistics() const;

    /**
     * Preload styles from a list of style elements
     * \param styleElements List of style elements to preload
     * \param layerIds Corresponding layer IDs (optional)
     */
    void preloadStyles( const QList<QDomElement> &styleElements, 
                        const QStringList &layerIds = QStringList() );

    /**
     * Check if cache should be compacted (too much memory usage)
     * \returns true if compaction is recommended
     */
    bool shouldCompactCache() const;

    /**
     * Compact the cache by removing least recently used entries
     * \param targetMemoryMB Target memory usage after compaction
     */
    void compactCache( qint64 targetMemoryMB = -1 );

  signals:

    /**
     * Emitted when cache memory usage exceeds threshold
     * \param currentUsageMB Current memory usage in MB
     * \param maxUsageMB Maximum configured memory usage
     */
    void memoryThresholdExceeded( qint64 currentUsageMB, qint64 maxUsageMB );

    /**
     * Emitted when cache statistics are updated
     * \param statistics Current cache statistics
     */
    void statisticsUpdated( const QgsLayerStyleCache::CacheStatistics &statistics );

    /**
     * Emitted when cache is compacted
     * \param entriesRemoved Number of entries removed
     * \param memoryFreedMB Memory freed in MB
     */
    void cacheCompacted( int entriesRemoved, qint64 memoryFreedMB );

  private slots:

    /**
     * Periodic cleanup of expired entries
     */
    void performPeriodicCleanup();

  private:

    //! Calculate memory usage of a DOM element
    qint64 calculateElementMemoryUsage( const QDomElement &element ) const;

    //! Update access time and count for a cache entry
    void updateAccessStatistics( CacheEntry &entry );

    //! Check if an entry has expired
    bool isEntryExpired( const CacheEntry &entry ) const;

    //! Extract style properties for similarity comparison
    QVariantMap extractStyleProperties( const QDomElement &element ) const;

    //! Compare two property maps for similarity
    double comparePropertyMaps( const QVariantMap &props1, const QVariantMap &props2 ) const;

    //! Remove least recently used entries
    void removeLRUEntries( int targetCount );

    //! Get current memory usage of all cache entries
    qint64 getCurrentMemoryUsage() const;

    //! Update cache statistics
    void updateStatistics();

    CacheConfig mConfig;
    QHash<QString, CacheEntry> mCacheEntries;
    QHash<QString, QStringList> mLayerToFingerprints; // Map layer IDs to their style fingerprints
    
    // Thread safety
    mutable QMutex mCacheMutex;
    
    // Statistics
    CacheStatistics mStatistics;
    QDateTime mLastCleanup;
    QTimer *mCleanupTimer = nullptr;
    
    // Performance tracking
    qint64 mCacheHits = 0;
    qint64 mCacheMisses = 0;
    qint64 mTotalSaveTime = 0; // Time saved by cache hits
};

#endif // QGSLAYERSTYLECACHE_H