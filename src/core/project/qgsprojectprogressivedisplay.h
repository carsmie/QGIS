/***************************************************************************
                         qgsprojectprogressivedisplay.h
                         ------------------------------
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

#ifndef QGSPROJECTPROGRESSIVEDISPLAY_H
#define QGSPROJECTPROGRESSIVEDISPLAY_H

#include "qgis_core.h"
#include <QObject>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <QElapsedTimer>

class QgsProject;
class QgsMapLayer;
class QgsMapCanvas;
class QgsLayerTree;
class QgsLayerTreeLayer;
class QgsRenderContext;

/**
 * \ingroup core
 * \brief Progressive display system for QGIS project loading
 *
 * The QgsProjectProgressiveDisplay provides immediate visual feedback during
 * project loading by progressively rendering layers as they become available.
 * This improves user experience for large projects by:
 *
 * - Displaying basic layer structure and extent immediately
 * - Rendering layers incrementally as they load
 * - Prioritizing visible and high-priority layers
 * - Providing smooth progress indication
 * - Maintaining responsive UI during loading
 * - Supporting cancellation and quality adjustment
 *
 * The system works by intercepting layer loading events and triggering
 * progressive rendering updates. It uses adaptive quality settings to
 * balance responsiveness with visual quality.
 *
 * \since QGIS 3.40
 */
class CORE_EXPORT QgsProjectProgressiveDisplay : public QObject
{
    Q_OBJECT

  public:

    /**
     * Progressive rendering strategies
     */
    enum class RenderingStrategy
    {
      Immediate,    //!< Render each layer immediately when loaded
      Batched,      //!< Render layers in small batches for efficiency
      Adaptive,     //!< Adaptively choose strategy based on layer complexity
      OnDemand      //!< Only render when explicitly requested
    };

    /**
     * Quality levels for progressive rendering
     */
    enum class QualityLevel
    {
      Draft,        //!< Fast draft quality for immediate feedback
      Preview,      //!< Medium quality for intermediate display
      Final         //!< Full quality for final rendering
    };

    /**
     * Layer rendering priority
     */
    enum class RenderingPriority
    {
      Background,   //!< Background/base layers (lowest priority)
      Normal,       //!< Normal priority layers
      Foreground,   //!< Foreground/overlay layers
      Critical      //!< Critical layers that must be shown first
    };

    /**
     * Progressive display configuration
     */
    struct DisplayConfig
    {
      RenderingStrategy strategy = RenderingStrategy::Adaptive;  //!< Rendering strategy to use
      QualityLevel initialQuality = QualityLevel::Draft;        //!< Initial quality level
      QualityLevel finalQuality = QualityLevel::Final;          //!< Final quality level
      int batchSize = 3;                                         //!< Number of layers per batch
      int refreshIntervalMs = 100;                               //!< Minimum time between refreshes
      int qualityUpgradeDelayMs = 1000;                          //!< Delay before upgrading quality
      bool enableLayerExtents = true;                            //!< Show layer extents initially
      bool enableProgressIndicator = true;                       //!< Show progress overlay
      bool enableCancellation = true;                            //!< Allow user cancellation
      double simplificationFactor = 0.5;                        //!< Simplification for draft rendering
      int maxRenderTimeMs = 200;                                 //!< Maximum time per render cycle
    };

    /**
     * Layer rendering information
     */
    struct LayerRenderInfo
    {
      QString layerId;                                           //!< Layer ID
      QString layerName;                                         //!< Layer display name
      RenderingPriority priority = RenderingPriority::Normal;   //!< Rendering priority
      QualityLevel currentQuality = QualityLevel::Draft;        //!< Current quality level
      bool isLoaded = false;                                     //!< Whether layer is fully loaded
      bool isVisible = true;                                     //!< Whether layer should be visible
      bool isRendered = false;                                   //!< Whether layer has been rendered
      QDateTime loadTime;                                        //!< When layer was loaded
      QDateTime lastRenderTime;                                  //!< Last render time
      qint64 renderDurationMs = 0;                               //!< Last render duration
      int renderAttempts = 0;                                    //!< Number of render attempts
    };

    /**
     * Display statistics
     */
    struct DisplayStatistics
    {
      int totalLayers = 0;                                       //!< Total number of layers
      int loadedLayers = 0;                                      //!< Number of loaded layers
      int renderedLayers = 0;                                    //!< Number of rendered layers
      qint64 totalLoadTime = 0;                                  //!< Total loading time (ms)
      qint64 totalRenderTime = 0;                                //!< Total rendering time (ms)
      double averageRenderTime = 0;                              //!< Average render time per layer
      int refreshCount = 0;                                      //!< Number of display refreshes
      QDateTime displayStartTime;                                //!< When display started
      QDateTime lastUpdateTime;                                  //!< Last update time
      QualityLevel currentGlobalQuality = QualityLevel::Draft;  //!< Current global quality
    };

    /**
     * Constructor
     * \param parent Parent object
     */
    explicit QgsProjectProgressiveDisplay( QObject *parent = nullptr );

    /**
     * Destructor
     */
    ~QgsProjectProgressiveDisplay() override;

    /**
     * Set the display configuration
     * \param config Configuration to use
     */
    void setDisplayConfig( const DisplayConfig &config );

    /**
     * Get the current display configuration
     * \returns Current configuration
     */
    DisplayConfig displayConfig() const { return mConfig; }

    /**
     * Set the target canvas for progressive display
     * \param canvas Canvas to render to
     */
    void setTargetCanvas( QgsMapCanvas *canvas );

    /**
     * Get the target canvas
     * \returns Current canvas or nullptr if not set
     */
    QgsMapCanvas *targetCanvas() const { return mCanvas; }

    /**
     * Start progressive display for a project
     * \param project Project to display
     * \returns true if display was started successfully
     */
    bool startDisplay( QgsProject *project );

    /**
     * Stop progressive display
     */
    void stopDisplay();

    /**
     * Check if progressive display is active
     * \returns true if display is running
     */
    bool isDisplayActive() const { return mDisplayActive; }

    /**
     * Add a layer to the progressive display queue
     * \param layer Layer to add
     * \param priority Rendering priority
     */
    void addLayer( QgsMapLayer *layer, RenderingPriority priority = RenderingPriority::Normal );

    /**
     * Remove a layer from progressive display
     * \param layerId Layer ID to remove
     */
    void removeLayer( const QString &layerId );

    /**
     * Update layer rendering priority
     * \param layerId Layer ID
     * \param priority New priority
     */
    void updateLayerPriority( const QString &layerId, RenderingPriority priority );

    /**
     * Set layer visibility
     * \param layerId Layer ID
     * \param visible Whether layer should be visible
     */
    void setLayerVisibility( const QString &layerId, bool visible );

    /**
     * Force refresh of the display
     */
    void refreshDisplay();

    /**
     * Upgrade display quality
     * \param targetQuality Target quality level
     */
    void upgradeQuality( QualityLevel targetQuality );

    /**
     * Get current display statistics
     * \returns Display statistics
     */
    DisplayStatistics getStatistics() const;

    /**
     * Export statistics as variant map
     * \returns Statistics as QVariantMap
     */
    QVariantMap exportStatistics() const;

    /**
     * Get information about a specific layer
     * \param layerId Layer ID
     * \returns Layer render info or empty struct if not found
     */
    LayerRenderInfo getLayerInfo( const QString &layerId ) const;

    /**
     * Get all layer information
     * \returns List of all layer render info
     */
    QList<LayerRenderInfo> getAllLayerInfo() const;

  signals:

    /**
     * Emitted when display starts
     * \param totalLayers Total number of layers to display
     */
    void displayStarted( int totalLayers );

    /**
     * Emitted when display progress changes
     * \param loadedLayers Number of loaded layers
     * \param renderedLayers Number of rendered layers
     * \param totalLayers Total number of layers
     */
    void displayProgressChanged( int loadedLayers, int renderedLayers, int totalLayers );

    /**
     * Emitted when a layer is rendered
     * \param layerId Layer ID
     * \param quality Quality level used
     * \param renderTime Render duration in milliseconds
     */
    void layerRendered( const QString &layerId, QgsProjectProgressiveDisplay::QualityLevel quality, qint64 renderTime );

    /**
     * Emitted when display quality is upgraded
     * \param oldQuality Previous quality level
     * \param newQuality New quality level
     */
    void qualityUpgraded( QgsProjectProgressiveDisplay::QualityLevel oldQuality, QgsProjectProgressiveDisplay::QualityLevel newQuality );

    /**
     * Emitted when display is completed
     * \param success Whether display completed successfully
     * \param statistics Final display statistics
     */
    void displayCompleted( bool success, const QgsProjectProgressiveDisplay::DisplayStatistics &statistics );

    /**
     * Emitted when display is cancelled
     */
    void displayCancelled();

    /**
     * Emitted when an error occurs during display
     * \param errorMessage Error description
     */
    void displayError( const QString &errorMessage );

  public slots:

    /**
     * Cancel the current progressive display
     */
    void cancelDisplay();

    /**
     * Slot called when a layer finishes loading
     * \param layerId Layer ID
     */
    void onLayerLoaded( const QString &layerId );

    /**
     * Slot called when a layer fails to load
     * \param layerId Layer ID
     * \param errorMessage Error description
     */
    void onLayerLoadFailed( const QString &layerId, const QString &errorMessage );

  private slots:

    /**
     * Process the next batch of layers for rendering
     */
    void processRenderQueue();

    /**
     * Upgrade rendering quality if conditions are met
     */
    void attemptQualityUpgrade();

    /**
     * Update display statistics
     */
    void updateStatistics();

  private:

    //! Initialize progressive display system
    void initializeDisplay();

    //! Setup layer tree for progressive rendering
    void setupLayerTree( QgsProject *project );

    //! Determine rendering priority for a layer
    RenderingPriority calculateLayerPriority( QgsMapLayer *layer ) const;

    //! Get the next batch of layers to render
    QList<QString> getNextRenderBatch();

    //! Render a specific layer with given quality
    bool renderLayer( const QString &layerId, QualityLevel quality );

    //! Render multiple layers as a batch
    void renderLayerBatch( const QStringList &layerIds, QualityLevel quality );

    //! Check if layer should be rendered based on current strategy
    bool shouldRenderLayer( const LayerRenderInfo &info ) const;

    //! Check if quality upgrade is needed
    bool shouldUpgradeQuality() const;

    //! Create render context for progressive rendering
    QgsRenderContext createRenderContext( QualityLevel quality ) const;

    //! Apply quality settings to render context
    void applyQualitySettings( QgsRenderContext &context, QualityLevel quality ) const;

    //! Update layer tree visibility
    void updateLayerTreeVisibility();

    //! Cleanup display resources
    void cleanupDisplay();

    DisplayConfig mConfig;
    QgsProject *mProject = nullptr;
    QgsMapCanvas *mCanvas = nullptr;
    QgsLayerTree *mLayerTree = nullptr;
    
    // Display state
    bool mDisplayActive = false;
    bool mCancelled = false;
    QElapsedTimer mDisplayTimer;
    QDateTime mDisplayStartTime;
    QDateTime mLastRefreshTime;
    
    // Layer management
    QMap<QString, LayerRenderInfo> mLayerInfo;
    QQueue<QString> mRenderQueue;
    QStringList mPriorityQueue;
    QMutex mQueueMutex;
    
    // Timers
    QTimer *mRenderTimer = nullptr;
    QTimer *mQualityUpgradeTimer = nullptr;
    QTimer *mStatisticsTimer = nullptr;
    
    // Statistics
    DisplayStatistics mStatistics;
    QElapsedTimer mRenderTimer_internal;
    
    // Quality management
    QualityLevel mCurrentGlobalQuality = QualityLevel::Draft;
    bool mQualityUpgradeScheduled = false;
};

#endif // QGSPROJECTPROGRESSIVEDISPLAY_H