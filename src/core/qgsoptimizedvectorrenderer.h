/***************************************************************************
                         qgsoptimizedvectorrenderer_simple.h
                         ---------------------------
    begin                : September 2025
    copyright            : (C) 2025 by QGIS
    email                : development-team@qgis.org
 ***************************************************************************/

#ifndef QGSOPTIMIZEDVECTORRENDERER_H
#define QGSOPTIMIZEDVECTORRENDERER_H

#include "qgis_core.h"
#include "qgsgeometry.h"
#include "qgsrendercontext.h"
#include "qgsfields.h"
#include "qgswkbtypes.h"

#include <QObject>
#include <QTimer>

class QgsVectorLayer;
class QgsFeatureRenderer;
class IPerformanceMonitor;

/**
 * \ingroup core
 * \class QgsOptimizedVectorRenderer
 * \brief Simplified optimized vector renderer for improved performance in QGIS vector rendering operations.
 */
class CORE_EXPORT QgsOptimizedVectorRenderer : public QObject
{
    Q_OBJECT

  public:

    /**
     * Basic optimization configuration
     */
    struct OptimizationConfig
    {
      //! Enable geometry caching
      bool enableGeometryCache = true;
      
      //! Geometry cache size in MB
      int geometryCacheSizeMB = 256;
      
      //! Enable background processing
      bool enableBackgroundProcessing = true;
    };

    /**
     * Basic rendering statistics
     */
    struct RenderingStatistics
    {
      //! Total features rendered
      qint64 featuresRendered = 0;
      
      //! Total rendering time in milliseconds
      qint64 totalRenderTimeMs = 0;
      
      //! Average rendering time per feature
      double averageFeatureTimeMs = 0.0;
      
      //! Cache hit rate
      double cacheHitRate = 0.0;
    };

    /**
     * Constructor
     */
    explicit QgsOptimizedVectorRenderer( QgsFeatureRenderer *baseRenderer = nullptr, QObject *parent = nullptr );

    /**
     * Destructor
     */
    ~QgsOptimizedVectorRenderer() override;

    /**
     * Set the base renderer to optimize
     */
    void setBaseRenderer( QgsFeatureRenderer *renderer );

    /**
     * Get the base renderer
     */
    QgsFeatureRenderer *baseRenderer() const { return mBaseRenderer; }

    /**
     * Render layer using optimized rendering pipeline
     */
    bool renderLayer( QgsVectorLayer *layer, QgsRenderContext &context );

    /**
     * Configure optimization settings
     */
    void setOptimizationConfig( const OptimizationConfig &config );

    /**
     * Get optimization config
     */
    OptimizationConfig getOptimizationConfig() const { return mConfig; }

    /**
     * Get rendering statistics
     */
    RenderingStatistics getRenderingStatistics() const;

    /**
     * Set performance monitor
     */
    void setPerformanceMonitor( IPerformanceMonitor *monitor );

  signals:
    
    /**
     * Emitted when rendering statistics are updated
     */
    void renderingStatisticsUpdated( const RenderingStatistics &statistics );

    /**
     * Emitted when background processing starts
     */
    void backgroundProcessingStarted( const QString &taskId );

    /**
     * Emitted when background processing finishes
     */
    void backgroundProcessingFinished( const QString &taskId, bool success );

  private slots:
    
    void performCacheMaintenance();

  private:

    // Base renderer to optimize
    QgsFeatureRenderer *mBaseRenderer = nullptr;

    // Configuration
    OptimizationConfig mConfig;

    // Statistics
    RenderingStatistics mStatistics;

    // Performance monitor
    IPerformanceMonitor *mPerformanceMonitor = nullptr;

    // Timers
    QTimer *mCacheMaintenanceTimer = nullptr;

    // Current operation ID for performance monitoring
    QString mCurrentOperationId;
};

#endif // QGSOPTIMIZEDVECTORRENDERER_H