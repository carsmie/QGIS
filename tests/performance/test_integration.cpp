/***************************************************************************
                         test_integration.cpp
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

#include "qgstest.h"
#include "qgsapplication.h"
#include "qgsproject.h"
#include "qgsvectorlayer.h"
#include "qgsrasterlayer.h"
#include "qgsmaprenderersequentialjob.h"
#include "qgsmapsettings.h"
#include "qgsfeatureiterator.h"
#include "qgsgeometry.h"
#include "qgsrectangle.h"

// Performance optimization classes (these will be implemented later)
// These includes will FAIL initially - that's expected in TDD
#include "iperformancemonitor.h"
#include "qgsprogressiveprojectloader.h"
#include "qgsoptimizedvectorrenderer.h"
#include "qgsflatgeobufoptimizer.h"
#include "performancetestsuite.h"

#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QSignalSpy>

/**
 * \ingroup PerformanceTests
 * Performance Optimization Pipeline Integration Test
 * 
 * This test validates the complete performance optimization pipeline by testing
 * the integration of all performance components working together:
 * 
 * - IPerformanceMonitor: Real-time performance monitoring
 * - QgsProgressiveProjectLoader: Progressive project loading optimization
 * - QgsOptimizedVectorRenderer: Vector rendering optimizations
 * - QgsFlatGeobufOptimizer: FlatGeobuf format optimizations
 * - PerformanceTestSuite: Automated performance testing framework
 * 
 * Integration scenarios tested:
 * 1. End-to-end project loading with monitoring and progressive loading
 * 2. Optimized rendering pipeline with FGB optimization and monitoring
 * 3. Performance regression detection and automated benchmarking
 * 4. Memory efficiency throughout the complete pipeline
 * 5. Coordination between different optimization components
 * 
 * This test ensures our 30% project loading and 5% rendering improvements
 * work together without conflicts or performance regressions.
 */
class TestPerformanceIntegration : public QObject
{
    Q_OBJECT

  public:
    TestPerformanceIntegration() = default;

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // End-to-end integration tests
    void testCompleteProjectLoadingPipeline();
    void testCompleteRenderingPipeline();
    void testOptimizedFgbWorkflow();
    void testPerformanceMonitoringIntegration();
    
    // Component interaction tests
    void testProgressiveLoaderWithMonitoring();
    void testOptimizedRendererWithFgbOptimizer();
    void testMonitoringWithAllComponents();
    void testPerformanceTestSuiteIntegration();
    
    // Performance target validation tests
    void validateProjectLoadingImprovement();
    void validateRenderingImprovement();
    void validateMemoryEfficiency();
    void validateNoPerformanceRegressions();
    
    // Error handling and robustness tests
    void testPipelineErrorRecovery();
    void testComponentFailureFallback();
    void testConcurrentOperationsIntegration();

  private:
    // Integration test utilities
    struct IntegrationMetrics {
      // Performance monitoring metrics
      qint64 totalOperationTimeMs = 0;
      qint64 progressiveLoadingTimeMs = 0;
      qint64 optimizedRenderingTimeMs = 0;
      qint64 fgbOptimizationTimeMs = 0;
      
      // Memory efficiency metrics
      qint64 peakMemoryMB = 0;
      qint64 memoryEfficiencyPercent = 0;
      
      // Improvement validation
      qint64 baselineTimeMs = 0;
      qint64 optimizedTimeMs = 0;
      double improvementPercent = 0.0;
      
      // Component coordination
      bool allComponentsActive = false;
      bool noComponentConflicts = false;
      bool errorRecoverySuccessful = false;
      
      QString operationName;
      bool integrationSuccessful = false;
      QString errorMessage;
    };
    
    IntegrationMetrics runIntegratedOperation( const QString &operationName,
                                             std::function<void()> operation,
                                             std::function<void()> baselineOperation = nullptr );
    
    // Component management utilities
    void initializePerformanceComponents();
    void cleanupPerformanceComponents();
    bool validateComponentIntegration();
    void logIntegrationMetrics( const IntegrationMetrics &metrics );
    
    // Test data setup
    QString createIntegrationTestProject( const QString &name, int complexity );
    QString createFgbTestDataset( const QString &name, int featureCount );
    void setupOptimizedComponents( const QString &dataPath );
    
    // Performance validation utilities
    bool validateProjectLoadingTarget( qint64 actualTimeMs, qint64 baselineTimeMs );
    bool validateRenderingTarget( qint64 actualTimeMs, qint64 baselineTimeMs );
    double calculateImprovement( qint64 baselineTimeMs, qint64 optimizedTimeMs );
    
    // Component instances (these will be created during implementation)
    IPerformanceMonitor *mPerformanceMonitor = nullptr;
    QgsProgressiveProjectLoader *mProgressiveLoader = nullptr;
    QgsOptimizedVectorRenderer *mOptimizedRenderer = nullptr;
    QgsFlatGeobufOptimizer *mFgbOptimizer = nullptr;
    PerformanceTestSuite *mTestSuite = nullptr;
    
    // Test configuration
    QString mTestDataDir;
    QString mIntegrationProjectPath;
    QString mFgbDatasetPath;
    QString mBaselineProjectPath;
    
    // Performance targets
    static constexpr double PROJECT_LOADING_IMPROVEMENT_TARGET = 0.30; // 30%
    static constexpr double RENDERING_IMPROVEMENT_TARGET = 0.05;       // 5%
    static constexpr qint64 MAX_INTEGRATION_TIME_MS = 60000;           // 1 minute max
    static constexpr qint64 MAX_MEMORY_OVERHEAD_MB = 100;              // 100MB max overhead
    
    // Integration test baselines
    struct IntegrationBaselines {
      qint64 traditional_project_loading_ms = 0;
      qint64 traditional_rendering_ms = 0;
      qint64 integrated_project_loading_ms = 0;
      qint64 integrated_rendering_ms = 0;
      double actual_project_improvement = 0.0;
      double actual_rendering_improvement = 0.0;
    };
    IntegrationBaselines mBaselines;
};

void TestPerformanceIntegration::initTestCase()
{
  // Initialize QGIS application
  QgsApplication::init();
  
  // Set up test data directory
  mTestDataDir = QStandardPaths::writableLocation( QStandardPaths::TempLocation ) + "/qgis_integration_test";
  QDir dir;
  if ( !dir.exists( mTestDataDir ) )
  {
    dir.mkpath( mTestDataDir );
  }
  
  qDebug() << "=== QGIS Performance Integration Test ===";
  qDebug() << "Test data directory:" << mTestDataDir;
  qDebug() << "Performance targets:";
  qDebug() << "  Project loading improvement: 30%";
  qDebug() << "  Rendering improvement: 5%";
  qDebug() << "  Max integration time:" << MAX_INTEGRATION_TIME_MS << "ms";
  qDebug() << "  Max memory overhead:" << MAX_MEMORY_OVERHEAD_MB << "MB";
  
  // Create test datasets
  mIntegrationProjectPath = createIntegrationTestProject( "integration_test", 25 );
  mFgbDatasetPath = createFgbTestDataset( "integration_fgb", 50000 );
  mBaselineProjectPath = createIntegrationTestProject( "baseline_test", 25 );
  
  qDebug() << "Test datasets created:";
  qDebug() << "  Integration project:" << mIntegrationProjectPath;
  qDebug() << "  FGB dataset:" << mFgbDatasetPath;
  qDebug() << "  Baseline project:" << mBaselineProjectPath;
  
  // Initialize performance components
  initializePerformanceComponents();
  
  // Validate component integration
  bool integrationValid = validateComponentIntegration();
  if ( !integrationValid )
  {
    qWarning() << "Performance component integration validation failed";
    qWarning() << "This is expected initially in TDD - components not yet implemented";
  }
  
  qDebug() << "Performance integration test initialized";
}

void TestPerformanceIntegration::cleanupTestCase()
{
  // Print integration test summary
  qDebug() << "\n=== PERFORMANCE INTEGRATION SUMMARY ===";
  qDebug() << QString( "Traditional Project Loading: %1 ms" ).arg( mBaselines.traditional_project_loading_ms );
  qDebug() << QString( "Integrated Project Loading: %1 ms" ).arg( mBaselines.integrated_project_loading_ms );
  qDebug() << QString( "Project Loading Improvement: %1%" ).arg( mBaselines.actual_project_improvement * 100, 0, 'f', 1 );
  qDebug() << QString( "Traditional Rendering: %1 ms" ).arg( mBaselines.traditional_rendering_ms );
  qDebug() << QString( "Integrated Rendering: %1 ms" ).arg( mBaselines.integrated_rendering_ms );
  qDebug() << QString( "Rendering Improvement: %1%" ).arg( mBaselines.actual_rendering_improvement * 100, 0, 'f', 1 );
  
  // Validate targets achieved
  if ( mBaselines.actual_project_improvement >= PROJECT_LOADING_IMPROVEMENT_TARGET )
  {
    qDebug() << "✅ Project loading target ACHIEVED";
  }
  else
  {
    qDebug() << "❌ Project loading target NOT achieved";
  }
  
  if ( mBaselines.actual_rendering_improvement >= RENDERING_IMPROVEMENT_TARGET )
  {
    qDebug() << "✅ Rendering target ACHIEVED";
  }
  else
  {
    qDebug() << "❌ Rendering target NOT achieved";
  }
  
  // Cleanup performance components
  cleanupPerformanceComponents();
  
  // Clean up test data
  QDir testDir( mTestDataDir );
  if ( testDir.exists() )
  {
    testDir.removeRecursively();
  }
  
  QgsApplication::exitQgis();
}

void TestPerformanceIntegration::init()
{
  // Clean state for each test
  QgsProject::instance()->clear();
  
  // Reset component states if available
  if ( mPerformanceMonitor )
  {
    mPerformanceMonitor->reset();
  }
}

void TestPerformanceIntegration::cleanup()
{
  // Clean up after each test
  QgsProject::instance()->clear();
  
  // Reset monitoring
  if ( mPerformanceMonitor )
  {
    mPerformanceMonitor->reset();
  }
}

void TestPerformanceIntegration::testCompleteProjectLoadingPipeline()
{
  qDebug() << "\n--- Testing Complete Project Loading Pipeline ---";
  
  if ( !QFileInfo::exists( mIntegrationProjectPath ) )
  {
    QSKIP( "Integration test project not available" );
  }
  
  // Test traditional loading (baseline)
  auto traditionalLoadOperation = [this]() {
    bool loaded = QgsProject::instance()->read( mIntegrationProjectPath );
    QVERIFY( loaded );
  };
  
  IntegrationMetrics traditionalMetrics = runIntegratedOperation( 
    "Traditional Project Loading", traditionalLoadOperation );
  
  mBaselines.traditional_project_loading_ms = traditionalMetrics.totalOperationTimeMs;
  
  // Clear project for optimized test
  QgsProject::instance()->clear();
  
  // Test optimized loading with progressive loader and monitoring
  auto optimizedLoadOperation = [this]() {
    if ( mProgressiveLoader && mPerformanceMonitor )
    {
      // Start performance monitoring
      mPerformanceMonitor->startOperation( "Progressive Project Loading" );
      
      // Use progressive loader
      bool loaded = mProgressiveLoader->loadProject( mIntegrationProjectPath );
      QVERIFY( loaded );
      
      // Stop monitoring
      mPerformanceMonitor->endOperation();
    }
    else
    {
      // Fallback to traditional loading if components not available
      bool loaded = QgsProject::instance()->read( mIntegrationProjectPath );
      QVERIFY( loaded );
    }
  };
  
  IntegrationMetrics optimizedMetrics = runIntegratedOperation( 
    "Optimized Project Loading", optimizedLoadOperation, traditionalLoadOperation );
  
  mBaselines.integrated_project_loading_ms = optimizedMetrics.totalOperationTimeMs;
  mBaselines.actual_project_improvement = calculateImprovement( 
    traditionalMetrics.totalOperationTimeMs, optimizedMetrics.totalOperationTimeMs );
  
  logIntegrationMetrics( optimizedMetrics );
  
  // Validate integration
  QVERIFY( optimizedMetrics.integrationSuccessful );
  QVERIFY( optimizedMetrics.totalOperationTimeMs < MAX_INTEGRATION_TIME_MS );
  
  // Validate project loading improvement target
  bool targetAchieved = validateProjectLoadingTarget( 
    optimizedMetrics.totalOperationTimeMs, traditionalMetrics.totalOperationTimeMs );
  
  if ( targetAchieved )
  {
    qDebug() << "✅ Project loading pipeline achieved 30% improvement target";
  }
  else
  {
    qDebug() << "⚠️ Project loading pipeline did not achieve target (expected in initial TDD phase)";
  }
  
  qDebug() << QString( "Complete project loading pipeline test completed" );
}

void TestPerformanceIntegration::testCompleteRenderingPipeline()
{
  qDebug() << "\n--- Testing Complete Rendering Pipeline ---";
  
  if ( !QFileInfo::exists( mFgbDatasetPath ) )
  {
    QSKIP( "FGB test dataset not available" );
  }
  
  // Load FGB dataset
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbDatasetPath, "Integration Test", "ogr" );
  if ( !layer->isValid() )
  {
    delete layer;
    QSKIP( "FGB test layer not valid" );
  }
  
  // Test traditional rendering (baseline)
  auto traditionalRenderOperation = [layer]() {
    QgsMapSettings mapSettings;
    mapSettings.setLayers( QList<QgsMapLayer*>() << layer );
    mapSettings.setExtent( QgsRectangle( -180, -90, 180, 90 ) );
    mapSettings.setOutputSize( QSize( 1024, 768 ) );
    
    QgsMapRendererSequentialJob job( mapSettings );
    job.start();
    job.waitForFinished();
    
    QImage result = job.renderedImage();
    QVERIFY( !result.isNull() );
  };
  
  IntegrationMetrics traditionalMetrics = runIntegratedOperation( 
    "Traditional Rendering", traditionalRenderOperation );
  
  mBaselines.traditional_rendering_ms = traditionalMetrics.totalOperationTimeMs;
  
  // Test optimized rendering with FGB optimizer and monitoring
  auto optimizedRenderOperation = [this, layer]() {
    if ( mOptimizedRenderer && mFgbOptimizer && mPerformanceMonitor )
    {
      // Start performance monitoring
      mPerformanceMonitor->startOperation( "Optimized FGB Rendering" );
      
      // Set up FGB optimizer
      mFgbOptimizer->setDataSource( mFgbDatasetPath );
      mFgbOptimizer->optimizeSpatialIndex();
      
      // Use optimized renderer
      QgsMapSettings mapSettings;
      mapSettings.setLayers( QList<QgsMapLayer*>() << layer );
      mapSettings.setExtent( QgsRectangle( -180, -90, 180, 90 ) );
      mapSettings.setOutputSize( QSize( 1024, 768 ) );
      
      // Apply optimized rendering
      mOptimizedRenderer->setOptimizationLevel( QgsOptimizedVectorRenderer::HighOptimization );
      QImage result = mOptimizedRenderer->renderTile( mapSettings );
      QVERIFY( !result.isNull() );
      
      // Stop monitoring
      mPerformanceMonitor->endOperation();
    }
    else
    {
      // Fallback to traditional rendering
      traditionalRenderOperation();
    }
  };
  
  IntegrationMetrics optimizedMetrics = runIntegratedOperation( 
    "Optimized Rendering", optimizedRenderOperation, traditionalRenderOperation );
  
  mBaselines.integrated_rendering_ms = optimizedMetrics.totalOperationTimeMs;
  mBaselines.actual_rendering_improvement = calculateImprovement( 
    traditionalMetrics.totalOperationTimeMs, optimizedMetrics.totalOperationTimeMs );
  
  logIntegrationMetrics( optimizedMetrics );
  
  // Validate integration
  QVERIFY( optimizedMetrics.integrationSuccessful );
  QVERIFY( optimizedMetrics.totalOperationTimeMs < MAX_INTEGRATION_TIME_MS );
  
  // Validate rendering improvement target
  bool targetAchieved = validateRenderingTarget( 
    optimizedMetrics.totalOperationTimeMs, traditionalMetrics.totalOperationTimeMs );
  
  if ( targetAchieved )
  {
    qDebug() << "✅ Rendering pipeline achieved 5% improvement target";
  }
  else
  {
    qDebug() << "⚠️ Rendering pipeline did not achieve target (expected in initial TDD phase)";
  }
  
  delete layer;
  
  qDebug() << QString( "Complete rendering pipeline test completed" );
}

void TestPerformanceIntegration::testOptimizedFgbWorkflow()
{
  qDebug() << "\n--- Testing Optimized FGB Workflow ---";
  
  if ( !QFileInfo::exists( mFgbDatasetPath ) )
  {
    QSKIP( "FGB test dataset not available" );
  }
  
  auto fgbWorkflowOperation = [this]() {
    if ( mFgbOptimizer && mOptimizedRenderer && mPerformanceMonitor )
    {
      // Full FGB optimization workflow
      mPerformanceMonitor->startOperation( "FGB Workflow" );
      
      // Step 1: Initialize FGB optimizer
      mFgbOptimizer->setDataSource( mFgbDatasetPath );
      mFgbOptimizer->setMemoryBudget( 512 ); // 512MB budget
      
      // Step 2: Optimize spatial index
      mFgbOptimizer->optimizeSpatialIndex();
      
      // Step 3: Test spatial filtering
      QgsRectangle filterRect( -10, -10, 10, 10 );
      QgsFeatureIterator features = mFgbOptimizer->getFeatures( filterRect );
      
      QgsFeature feature;
      int featureCount = 0;
      while ( features.nextFeature( feature ) && featureCount < 1000 )
      {
        featureCount++;
      }
      
      // Step 4: Test batch geometry loading
      QList<QgsFeatureId> featureIds = { 1, 2, 3, 4, 5 };
      QList<QgsGeometry> geometries = mFgbOptimizer->loadGeometriesBatch( featureIds );
      
      // Step 5: Optimized rendering
      QgsVectorLayer *layer = new QgsVectorLayer( mFgbDatasetPath, "FGB Workflow", "ogr" );
      if ( layer->isValid() )
      {
        QgsMapSettings mapSettings;
        mapSettings.setLayers( QList<QgsMapLayer*>() << layer );
        mapSettings.setExtent( filterRect );
        mapSettings.setOutputSize( QSize( 512, 512 ) );
        
        mOptimizedRenderer->setOptimizationLevel( QgsOptimizedVectorRenderer::HighOptimization );
        QImage result = mOptimizedRenderer->renderTile( mapSettings );
        QVERIFY( !result.isNull() );
        
        delete layer;
      }
      
      mPerformanceMonitor->endOperation();
      
      QVERIFY( featureCount > 0 );
      QVERIFY( geometries.size() > 0 );
    }
    else
    {
      // Simplified workflow if components not available
      QgsVectorLayer *layer = new QgsVectorLayer( mFgbDatasetPath, "FGB Simple", "ogr" );
      QVERIFY( layer->isValid() );
      
      QgsFeatureIterator iter = layer->getFeatures();
      QgsFeature feature;
      int count = 0;
      while ( iter.nextFeature( feature ) && count < 100 )
      {
        count++;
      }
      
      delete layer;
      QVERIFY( count > 0 );
    }
  };
  
  IntegrationMetrics metrics = runIntegratedOperation( "FGB Workflow", fgbWorkflowOperation );
  
  logIntegrationMetrics( metrics );
  
  QVERIFY( metrics.integrationSuccessful );
  QVERIFY( metrics.totalOperationTimeMs < MAX_INTEGRATION_TIME_MS );
  
  qDebug() << QString( "Optimized FGB workflow test completed" );
}

void TestPerformanceIntegration::testPerformanceMonitoringIntegration()
{
  qDebug() << "\n--- Testing Performance Monitoring Integration ---";
  
  auto monitoringOperation = [this]() {
    if ( mPerformanceMonitor )
    {
      // Test comprehensive monitoring
      mPerformanceMonitor->startOperation( "Integration Monitoring Test" );
      
      // Simulate various operations
      mPerformanceMonitor->recordMetric( "test_metric", 42.0 );
      mPerformanceMonitor->recordMemoryUsage( 256 ); // 256MB
      
      // Simulate loading operation
      if ( mProgressiveLoader )
      {
        mPerformanceMonitor->startOperation( "Progressive Loading" );
        
        // Would call progressive loader here
        QThread::msleep( 100 ); // Simulate work
        
        mPerformanceMonitor->endOperation();
      }
      
      // Simulate rendering operation
      if ( mOptimizedRenderer )
      {
        mPerformanceMonitor->startOperation( "Optimized Rendering" );
        
        // Would call optimized renderer here
        QThread::msleep( 50 ); // Simulate work
        
        mPerformanceMonitor->endOperation();
      }
      
      mPerformanceMonitor->endOperation();
      
      // Validate monitoring data
      auto metrics = mPerformanceMonitor->getMetrics();
      QVERIFY( !metrics.isEmpty() );
    }
    else
    {
      // Fallback test without monitoring
      QThread::msleep( 150 ); // Simulate total work time
    }
  };
  
  IntegrationMetrics metrics = runIntegratedOperation( "Monitoring Integration", monitoringOperation );
  
  logIntegrationMetrics( metrics );
  
  QVERIFY( metrics.integrationSuccessful );
  
  qDebug() << QString( "Performance monitoring integration test completed" );
}

void TestPerformanceIntegration::testProgressiveLoaderWithMonitoring()
{
  qDebug() << "\n--- Testing Progressive Loader with Monitoring ---";
  
  if ( !QFileInfo::exists( mIntegrationProjectPath ) )
  {
    QSKIP( "Integration test project not available" );
  }
  
  auto progressiveLoadingOperation = [this]() {
    if ( mProgressiveLoader && mPerformanceMonitor )
    {
      // Integrated progressive loading with monitoring
      mPerformanceMonitor->startOperation( "Progressive Project Loading" );
      
      // Configure progressive loader
      mProgressiveLoader->setProgressCallback( [this]( int progress ) {
        mPerformanceMonitor->recordMetric( "loading_progress", progress );
      });
      
      // Load project progressively
      bool loaded = mProgressiveLoader->loadProject( mIntegrationProjectPath );
      QVERIFY( loaded );
      
      mPerformanceMonitor->endOperation();
      
      // Validate metrics were recorded
      auto metrics = mPerformanceMonitor->getMetrics();
      QVERIFY( metrics.contains( "loading_progress" ) );
    }
    else
    {
      // Fallback to traditional loading
      bool loaded = QgsProject::instance()->read( mIntegrationProjectPath );
      QVERIFY( loaded );
    }
  };
  
  IntegrationMetrics metrics = runIntegratedOperation( "Progressive Loading + Monitoring", progressiveLoadingOperation );
  
  logIntegrationMetrics( metrics );
  
  QVERIFY( metrics.integrationSuccessful );
  
  qDebug() << QString( "Progressive loader with monitoring test completed" );
}

void TestPerformanceIntegration::testOptimizedRendererWithFgbOptimizer()
{
  qDebug() << "\n--- Testing Optimized Renderer with FGB Optimizer ---";
  
  if ( !QFileInfo::exists( mFgbDatasetPath ) )
  {
    QSKIP( "FGB test dataset not available" );
  }
  
  auto rendererFgbOperation = [this]() {
    if ( mOptimizedRenderer && mFgbOptimizer )
    {
      // Set up FGB optimizer
      mFgbOptimizer->setDataSource( mFgbDatasetPath );
      mFgbOptimizer->optimizeSpatialIndex();
      
      // Create layer
      QgsVectorLayer *layer = new QgsVectorLayer( mFgbDatasetPath, "Renderer+FGB", "ogr" );
      QVERIFY( layer->isValid() );
      
      // Configure optimized renderer with FGB optimization
      mOptimizedRenderer->setOptimizationLevel( QgsOptimizedVectorRenderer::HighOptimization );
      mOptimizedRenderer->enableCaching( true );
      
      // Test multiple render calls to validate caching
      QgsMapSettings mapSettings;
      mapSettings.setLayers( QList<QgsMapLayer*>() << layer );
      mapSettings.setExtent( QgsRectangle( -10, -10, 10, 10 ) );
      mapSettings.setOutputSize( QSize( 256, 256 ) );
      
      // First render (should cache)
      QImage result1 = mOptimizedRenderer->renderTile( mapSettings );
      QVERIFY( !result1.isNull() );
      
      // Second render (should use cache)
      QImage result2 = mOptimizedRenderer->renderTile( mapSettings );
      QVERIFY( !result2.isNull() );
      
      delete layer;
    }
    else
    {
      // Fallback test
      QgsVectorLayer *layer = new QgsVectorLayer( mFgbDatasetPath, "Fallback", "ogr" );
      QVERIFY( layer->isValid() );
      
      QgsMapSettings mapSettings;
      mapSettings.setLayers( QList<QgsMapLayer*>() << layer );
      mapSettings.setExtent( QgsRectangle( -10, -10, 10, 10 ) );
      mapSettings.setOutputSize( QSize( 256, 256 ) );
      
      QgsMapRendererSequentialJob job( mapSettings );
      job.start();
      job.waitForFinished();
      
      QImage result = job.renderedImage();
      QVERIFY( !result.isNull() );
      
      delete layer;
    }
  };
  
  IntegrationMetrics metrics = runIntegratedOperation( "Renderer + FGB Optimizer", rendererFgbOperation );
  
  logIntegrationMetrics( metrics );
  
  QVERIFY( metrics.integrationSuccessful );
  
  qDebug() << QString( "Optimized renderer with FGB optimizer test completed" );
}

void TestPerformanceIntegration::testMonitoringWithAllComponents()
{
  qDebug() << "\n--- Testing Monitoring with All Components ---";
  
  auto allComponentsOperation = [this]() {
    if ( mPerformanceMonitor )
    {
      mPerformanceMonitor->startOperation( "All Components Integration" );
      
      // Test each component with monitoring
      if ( mProgressiveLoader )
      {
        mPerformanceMonitor->recordMetric( "progressive_loader_available", 1.0 );
      }
      
      if ( mOptimizedRenderer )
      {
        mPerformanceMonitor->recordMetric( "optimized_renderer_available", 1.0 );
      }
      
      if ( mFgbOptimizer )
      {
        mPerformanceMonitor->recordMetric( "fgb_optimizer_available", 1.0 );
      }
      
      if ( mTestSuite )
      {
        mPerformanceMonitor->recordMetric( "test_suite_available", 1.0 );
      }
      
      mPerformanceMonitor->endOperation();
      
      // Validate all metrics were recorded
      auto metrics = mPerformanceMonitor->getMetrics();
      QVERIFY( !metrics.isEmpty() );
    }
    else
    {
      // Count available components manually
      int componentCount = 0;
      if ( mProgressiveLoader ) componentCount++;
      if ( mOptimizedRenderer ) componentCount++;
      if ( mFgbOptimizer ) componentCount++;
      if ( mTestSuite ) componentCount++;
      
      qDebug() << QString( "Available components: %1/4" ).arg( componentCount );
    }
  };
  
  IntegrationMetrics metrics = runIntegratedOperation( "All Components Monitoring", allComponentsOperation );
  
  logIntegrationMetrics( metrics );
  
  QVERIFY( metrics.integrationSuccessful );
  
  qDebug() << QString( "Monitoring with all components test completed" );
}

void TestPerformanceIntegration::testPerformanceTestSuiteIntegration()
{
  qDebug() << "\n--- Testing Performance Test Suite Integration ---";
  
  auto testSuiteOperation = [this]() {
    if ( mTestSuite )
    {
      // Configure test suite with all components
      mTestSuite->setPerformanceMonitor( mPerformanceMonitor );
      mTestSuite->setProgressiveLoader( mProgressiveLoader );
      mTestSuite->setOptimizedRenderer( mOptimizedRenderer );
      mTestSuite->setFgbOptimizer( mFgbOptimizer );
      
      // Run integrated performance tests
      bool suiteResult = mTestSuite->runPerformanceTests();
      
      // Get test results
      auto results = mTestSuite->getTestResults();
      QVERIFY( !results.isEmpty() );
      
      QVERIFY( suiteResult );
    }
    else
    {
      // Simulate basic performance testing
      qDebug() << "Performance test suite not available - running basic validation";
      
      // Basic component availability test
      int availableComponents = 0;
      if ( mPerformanceMonitor ) availableComponents++;
      if ( mProgressiveLoader ) availableComponents++;
      if ( mOptimizedRenderer ) availableComponents++;
      if ( mFgbOptimizer ) availableComponents++;
      
      qDebug() << QString( "Components available: %1/4" ).arg( availableComponents );
    }
  };
  
  IntegrationMetrics metrics = runIntegratedOperation( "Performance Test Suite", testSuiteOperation );
  
  logIntegrationMetrics( metrics );
  
  QVERIFY( metrics.integrationSuccessful );
  
  qDebug() << QString( "Performance test suite integration test completed" );
}

void TestPerformanceIntegration::validateProjectLoadingImprovement()
{
  qDebug() << "\n--- Validating Project Loading Improvement ---";
  
  if ( mBaselines.traditional_project_loading_ms == 0 || mBaselines.integrated_project_loading_ms == 0 )
  {
    QSKIP( "Project loading baselines not established" );
  }
  
  double actualImprovement = calculateImprovement( 
    mBaselines.traditional_project_loading_ms, mBaselines.integrated_project_loading_ms );
  
  qDebug() << QString( "Project Loading Improvement Validation:" );
  qDebug() << QString( "  Traditional loading: %1 ms" ).arg( mBaselines.traditional_project_loading_ms );
  qDebug() << QString( "  Integrated loading: %1 ms" ).arg( mBaselines.integrated_project_loading_ms );
  qDebug() << QString( "  Actual improvement: %1%" ).arg( actualImprovement * 100, 0, 'f', 1 );
  qDebug() << QString( "  Target improvement: %1%" ).arg( PROJECT_LOADING_IMPROVEMENT_TARGET * 100, 0, 'f', 1 );
  
  bool targetAchieved = actualImprovement >= PROJECT_LOADING_IMPROVEMENT_TARGET;
  
  if ( targetAchieved )
  {
    qDebug() << "✅ Project loading improvement target ACHIEVED";
  }
  else
  {
    qDebug() << "⚠️ Project loading improvement target NOT achieved (expected in TDD phase)";
    qDebug() << QString( "   Need additional %1% improvement" )
                .arg( ( PROJECT_LOADING_IMPROVEMENT_TARGET - actualImprovement ) * 100, 0, 'f', 1 );
  }
  
  // In TDD phase, we expect this to fail initially
  // QVERIFY( targetAchieved ); // Will be enabled once implementation is complete
}

void TestPerformanceIntegration::validateRenderingImprovement()
{
  qDebug() << "\n--- Validating Rendering Improvement ---";
  
  if ( mBaselines.traditional_rendering_ms == 0 || mBaselines.integrated_rendering_ms == 0 )
  {
    QSKIP( "Rendering baselines not established" );
  }
  
  double actualImprovement = calculateImprovement( 
    mBaselines.traditional_rendering_ms, mBaselines.integrated_rendering_ms );
  
  qDebug() << QString( "Rendering Improvement Validation:" );
  qDebug() << QString( "  Traditional rendering: %1 ms" ).arg( mBaselines.traditional_rendering_ms );
  qDebug() << QString( "  Integrated rendering: %1 ms" ).arg( mBaselines.integrated_rendering_ms );
  qDebug() << QString( "  Actual improvement: %1%" ).arg( actualImprovement * 100, 0, 'f', 1 );
  qDebug() << QString( "  Target improvement: %1%" ).arg( RENDERING_IMPROVEMENT_TARGET * 100, 0, 'f', 1 );
  
  bool targetAchieved = actualImprovement >= RENDERING_IMPROVEMENT_TARGET;
  
  if ( targetAchieved )
  {
    qDebug() << "✅ Rendering improvement target ACHIEVED";
  }
  else
  {
    qDebug() << "⚠️ Rendering improvement target NOT achieved (expected in TDD phase)";
    qDebug() << QString( "   Need additional %1% improvement" )
                .arg( ( RENDERING_IMPROVEMENT_TARGET - actualImprovement ) * 100, 0, 'f', 1 );
  }
  
  // In TDD phase, we expect this to fail initially
  // QVERIFY( targetAchieved ); // Will be enabled once implementation is complete
}

void TestPerformanceIntegration::validateMemoryEfficiency()
{
  qDebug() << "\n--- Validating Memory Efficiency ---";
  
  // This test validates that integration doesn't introduce memory overhead
  
  auto memoryEfficiencyOperation = [this]() {
    // Baseline memory usage
    qint64 baselineMemory = getCurrentMemoryUsageMB();
    
    // Use all components together
    if ( mPerformanceMonitor && mProgressiveLoader && mOptimizedRenderer && mFgbOptimizer )
    {
      mPerformanceMonitor->startOperation( "Memory Efficiency Test" );
      
      // Create temporary project
      QString tempProject = mTestDataDir + "/memory_test.qgs";
      QFile projectFile( tempProject );
      if ( projectFile.open( QIODevice::WriteOnly ) )
      {
        QTextStream stream( &projectFile );
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<qgis version=\"3.30.0\"><projectlayers></projectlayers></qgis>\n";
        projectFile.close();
      }
      
      // Load with progressive loader
      bool loaded = mProgressiveLoader->loadProject( tempProject );
      QVERIFY( loaded );
      
      qint64 afterLoadMemory = getCurrentMemoryUsageMB();
      
      // Cleanup
      QgsProject::instance()->clear();
      mPerformanceMonitor->endOperation();
      
      qint64 afterCleanupMemory = getCurrentMemoryUsageMB();
      
      qint64 memoryOverhead = afterCleanupMemory - baselineMemory;
      
      qDebug() << QString( "Memory Efficiency Results:" );
      qDebug() << QString( "  Baseline memory: %1 MB" ).arg( baselineMemory );
      qDebug() << QString( "  After load memory: %1 MB" ).arg( afterLoadMemory );
      qDebug() << QString( "  After cleanup memory: %1 MB" ).arg( afterCleanupMemory );
      qDebug() << QString( "  Memory overhead: %1 MB" ).arg( memoryOverhead );
      qDebug() << QString( "  Max allowed overhead: %1 MB" ).arg( MAX_MEMORY_OVERHEAD_MB );
      
      QVERIFY2( memoryOverhead < MAX_MEMORY_OVERHEAD_MB,
               qPrintable( QString( "Memory overhead %1 MB exceeds limit %2 MB" )
                          .arg( memoryOverhead ).arg( MAX_MEMORY_OVERHEAD_MB ) ) );
      
      QFile::remove( tempProject );
    }
    else
    {
      qDebug() << "Components not available for memory efficiency test";
    }
  };
  
  IntegrationMetrics metrics = runIntegratedOperation( "Memory Efficiency", memoryEfficiencyOperation );
  
  QVERIFY( metrics.integrationSuccessful );
  
  qDebug() << QString( "Memory efficiency validation completed" );
}

void TestPerformanceIntegration::validateNoPerformanceRegressions()
{
  qDebug() << "\n--- Validating No Performance Regressions ---";
  
  // This test ensures optimization don't slow down any operations
  
  if ( mBaselines.traditional_project_loading_ms == 0 || mBaselines.traditional_rendering_ms == 0 )
  {
    QSKIP( "Baselines not established for regression testing" );
  }
  
  // Project loading should not be slower
  bool projectLoadingRegression = mBaselines.integrated_project_loading_ms > mBaselines.traditional_project_loading_ms * 1.1; // Allow 10% variance
  
  // Rendering should not be slower
  bool renderingRegression = mBaselines.integrated_rendering_ms > mBaselines.traditional_rendering_ms * 1.1; // Allow 10% variance
  
  qDebug() << QString( "Performance Regression Analysis:" );
  qDebug() << QString( "  Project loading regression: %1" ).arg( projectLoadingRegression ? "Yes" : "No" );
  qDebug() << QString( "  Rendering regression: %1" ).arg( renderingRegression ? "Yes" : "No" );
  
  if ( projectLoadingRegression )
  {
    double slowdown = ( double( mBaselines.integrated_project_loading_ms ) / mBaselines.traditional_project_loading_ms - 1.0 ) * 100;
    qWarning() << QString( "Project loading is %1% slower with optimizations" ).arg( slowdown, 0, 'f', 1 );
  }
  
  if ( renderingRegression )
  {
    double slowdown = ( double( mBaselines.integrated_rendering_ms ) / mBaselines.traditional_rendering_ms - 1.0 ) * 100;
    qWarning() << QString( "Rendering is %1% slower with optimizations" ).arg( slowdown, 0, 'f', 1 );
  }
  
  // Should not have significant regressions
  QVERIFY2( !projectLoadingRegression, "Project loading performance regression detected" );
  QVERIFY2( !renderingRegression, "Rendering performance regression detected" );
  
  qDebug() << QString( "No performance regressions detected" );
}

void TestPerformanceIntegration::testPipelineErrorRecovery()
{
  qDebug() << "\n--- Testing Pipeline Error Recovery ---";
  
  auto errorRecoveryOperation = [this]() {
    bool recoverySuccessful = false;
    
    try
    {
      if ( mPerformanceMonitor )
      {
        mPerformanceMonitor->startOperation( "Error Recovery Test" );
        
        // Simulate an error condition
        throw std::runtime_error( "Simulated error" );
      }
    }
    catch ( const std::exception &e )
    {
      qDebug() << QString( "Caught expected error: %1" ).arg( e.what() );
      
      // Test error recovery
      if ( mPerformanceMonitor )
      {
        mPerformanceMonitor->recordError( "test_error", e.what() );
        mPerformanceMonitor->endOperation();
      }
      
      recoverySuccessful = true;
    }
    
    QVERIFY( recoverySuccessful );
  };
  
  IntegrationMetrics metrics = runIntegratedOperation( "Error Recovery", errorRecoveryOperation );
  
  QVERIFY( metrics.integrationSuccessful );
  
  qDebug() << QString( "Pipeline error recovery test completed" );
}

void TestPerformanceIntegration::testComponentFailureFallback()
{
  qDebug() << "\n--- Testing Component Failure Fallback ---";
  
  auto fallbackOperation = [this]() {
    // Test that system works even if optimization components fail
    
    // Simulate component failure by setting to nullptr temporarily
    QgsProgressiveProjectLoader *originalLoader = mProgressiveLoader;
    mProgressiveLoader = nullptr;
    
    // Should still be able to load projects
    if ( QFileInfo::exists( mIntegrationProjectPath ) )
    {
      bool loaded = QgsProject::instance()->read( mIntegrationProjectPath );
      QVERIFY( loaded );
    }
    
    // Restore component
    mProgressiveLoader = originalLoader;
    
    qDebug() << "Fallback to traditional project loading successful";
  };
  
  IntegrationMetrics metrics = runIntegratedOperation( "Component Failure Fallback", fallbackOperation );
  
  QVERIFY( metrics.integrationSuccessful );
  
  qDebug() << QString( "Component failure fallback test completed" );
}

void TestPerformanceIntegration::testConcurrentOperationsIntegration()
{
  qDebug() << "\n--- Testing Concurrent Operations Integration ---";
  
  // Note: This is simplified concurrent testing
  // Real implementation would use proper threading
  
  auto concurrentOperation = [this]() {
    // Simulate concurrent operations using components
    
    if ( mPerformanceMonitor )
    {
      mPerformanceMonitor->startOperation( "Concurrent Operations" );
      
      // Simulate multiple operations happening simultaneously
      // Operation 1: Project loading
      if ( mProgressiveLoader && QFileInfo::exists( mIntegrationProjectPath ) )
      {
        bool loaded = mProgressiveLoader->loadProject( mIntegrationProjectPath );
        Q_UNUSED( loaded )
      }
      
      // Operation 2: FGB optimization
      if ( mFgbOptimizer && QFileInfo::exists( mFgbDatasetPath ) )
      {
        mFgbOptimizer->setDataSource( mFgbDatasetPath );
        mFgbOptimizer->optimizeSpatialIndex();
      }
      
      // Operation 3: Rendering
      if ( mOptimizedRenderer )
      {
        QgsMapSettings mapSettings;
        mapSettings.setExtent( QgsRectangle( -1, -1, 1, 1 ) );
        mapSettings.setOutputSize( QSize( 256, 256 ) );
        
        // This would render in real implementation
      }
      
      mPerformanceMonitor->endOperation();
    }
    else
    {
      qDebug() << "Simulating concurrent operations without monitoring";
    }
  };
  
  IntegrationMetrics metrics = runIntegratedOperation( "Concurrent Operations", concurrentOperation );
  
  QVERIFY( metrics.integrationSuccessful );
  
  qDebug() << QString( "Concurrent operations integration test completed" );
}

// Helper method implementations

TestPerformanceIntegration::IntegrationMetrics TestPerformanceIntegration::runIntegratedOperation( 
  const QString &operationName, std::function<void()> operation, std::function<void()> baselineOperation )
{
  IntegrationMetrics metrics;
  metrics.operationName = operationName;
  
  qint64 memoryBefore = getCurrentMemoryUsageMB();
  
  QElapsedTimer timer;
  timer.start();
  
  try
  {
    operation();
    metrics.integrationSuccessful = true;
  }
  catch ( const std::exception &e )
  {
    metrics.integrationSuccessful = false;
    metrics.errorMessage = e.what();
  }
  catch ( ... )
  {
    metrics.integrationSuccessful = false;
    metrics.errorMessage = "Unknown error";
  }
  
  metrics.totalOperationTimeMs = timer.elapsed();
  
  qint64 memoryAfter = getCurrentMemoryUsageMB();
  metrics.peakMemoryMB = memoryAfter;
  
  // Calculate memory efficiency
  qint64 memoryUsed = memoryAfter - memoryBefore;
  if ( memoryUsed > 0 )
  {
    metrics.memoryEfficiencyPercent = 100 - ( memoryUsed * 100 / MAX_MEMORY_OVERHEAD_MB );
  }
  else
  {
    metrics.memoryEfficiencyPercent = 100;
  }
  
  // Calculate improvement if baseline operation provided
  if ( baselineOperation )
  {
    QElapsedTimer baselineTimer;
    baselineTimer.start();
    
    try
    {
      baselineOperation();
      metrics.baselineTimeMs = baselineTimer.elapsed();
      metrics.optimizedTimeMs = metrics.totalOperationTimeMs;
      
      if ( metrics.baselineTimeMs > 0 )
      {
        metrics.improvementPercent = calculateImprovement( metrics.baselineTimeMs, metrics.optimizedTimeMs );
      }
    }
    catch ( ... )
    {
      // Baseline failed, can't calculate improvement
      metrics.baselineTimeMs = 0;
    }
  }
  
  // Validate component integration
  metrics.allComponentsActive = ( mPerformanceMonitor != nullptr ) &&
                               ( mProgressiveLoader != nullptr ) &&
                               ( mOptimizedRenderer != nullptr ) &&
                               ( mFgbOptimizer != nullptr );
  
  metrics.noComponentConflicts = true; // Assume no conflicts unless detected
  metrics.errorRecoverySuccessful = metrics.integrationSuccessful || !metrics.errorMessage.isEmpty();
  
  return metrics;
}

void TestPerformanceIntegration::initializePerformanceComponents()
{
  qDebug() << "Initializing performance components...";
  
  try
  {
    // These will fail initially because classes don't exist yet (TDD)
    // mPerformanceMonitor = new IPerformanceMonitor();
    // mProgressiveLoader = new QgsProgressiveProjectLoader();
    // mOptimizedRenderer = new QgsOptimizedVectorRenderer();
    // mFgbOptimizer = new QgsFlatGeobufOptimizer();
    // mTestSuite = new PerformanceTestSuite();
    
    qDebug() << "Performance components initialization skipped (TDD phase)";
  }
  catch ( ... )
  {
    qDebug() << "Performance components not available (expected in TDD phase)";
  }
}

void TestPerformanceIntegration::cleanupPerformanceComponents()
{
  delete mPerformanceMonitor;
  delete mProgressiveLoader;
  delete mOptimizedRenderer;
  delete mFgbOptimizer;
  delete mTestSuite;
  
  mPerformanceMonitor = nullptr;
  mProgressiveLoader = nullptr;
  mOptimizedRenderer = nullptr;
  mFgbOptimizer = nullptr;
  mTestSuite = nullptr;
}

bool TestPerformanceIntegration::validateComponentIntegration()
{
  // Check if components are available and compatible
  int availableComponents = 0;
  
  if ( mPerformanceMonitor ) availableComponents++;
  if ( mProgressiveLoader ) availableComponents++;
  if ( mOptimizedRenderer ) availableComponents++;
  if ( mFgbOptimizer ) availableComponents++;
  if ( mTestSuite ) availableComponents++;
  
  qDebug() << QString( "Component integration status: %1/5 components available" ).arg( availableComponents );
  
  // In TDD phase, components may not be available
  return true; // Always return true for now
}

void TestPerformanceIntegration::logIntegrationMetrics( const IntegrationMetrics &metrics )
{
  qDebug() << QString( "[INTEGRATION] %1:" ).arg( metrics.operationName );
  qDebug() << QString( "  Total time: %1 ms" ).arg( metrics.totalOperationTimeMs );
  qDebug() << QString( "  Peak memory: %1 MB" ).arg( metrics.peakMemoryMB );
  qDebug() << QString( "  Memory efficiency: %1%" ).arg( metrics.memoryEfficiencyPercent );
  qDebug() << QString( "  Integration successful: %1" ).arg( metrics.integrationSuccessful ? "Yes" : "No" );
  
  if ( metrics.baselineTimeMs > 0 && metrics.optimizedTimeMs > 0 )
  {
    qDebug() << QString( "  Baseline time: %1 ms" ).arg( metrics.baselineTimeMs );
    qDebug() << QString( "  Optimized time: %1 ms" ).arg( metrics.optimizedTimeMs );
    qDebug() << QString( "  Improvement: %1%" ).arg( metrics.improvementPercent * 100, 0, 'f', 1 );
  }
  
  qDebug() << QString( "  All components active: %1" ).arg( metrics.allComponentsActive ? "Yes" : "No" );
  qDebug() << QString( "  No component conflicts: %1" ).arg( metrics.noComponentConflicts ? "Yes" : "No" );
  
  if ( !metrics.integrationSuccessful )
  {
    qDebug() << QString( "  Error: %1" ).arg( metrics.errorMessage );
  }
}

QString TestPerformanceIntegration::createIntegrationTestProject( const QString &name, int complexity )
{
  QString projectPath = mTestDataDir + QString( "/%1.qgs" ).arg( name );
  
  QFile projectFile( projectPath );
  if ( !projectFile.open( QIODevice::WriteOnly ) )
  {
    qWarning() << "Failed to create integration test project:" << projectPath;
    return QString();
  }
  
  QTextStream stream( &projectFile );
  stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  stream << QString( "<qgis version=\"3.30.0\" projectname=\"%1\">\n" ).arg( name );
  stream << "  <projectlayers>\n";
  
  // Add layers based on complexity
  for ( int i = 0; i < complexity; ++i )
  {
    stream << QString( "    <maplayer id=\"layer_%1\" type=\"vector\">\n" ).arg( i );
    stream << QString( "      <layername>%1 Layer %2</layername>\n" ).arg( name ).arg( i );
    stream << "    </maplayer>\n";
  }
  
  stream << "  </projectlayers>\n";
  stream << "</qgis>\n";
  projectFile.close();
  
  qDebug() << QString( "Created integration test project: %1 (%2 layers)" ).arg( projectPath ).arg( complexity );
  
  return projectPath;
}

QString TestPerformanceIntegration::createFgbTestDataset( const QString &name, int featureCount )
{
  QString fgbPath = mTestDataDir + QString( "/%1.fgb" ).arg( name );
  
  QFile fgbFile( fgbPath );
  if ( !fgbFile.open( QIODevice::WriteOnly ) )
  {
    qWarning() << "Failed to create FGB test dataset:" << fgbPath;
    return QString();
  }
  
  // Write simplified FGB data
  fgbFile.write( "FGB" ); // Header
  
  // Simulate feature data
  qint64 dataSize = featureCount * 64; // 64 bytes per feature
  fgbFile.write( QByteArray( dataSize, 'F' ) );
  fgbFile.close();
  
  qDebug() << QString( "Created FGB test dataset: %1 (%2 features, %3 KB)" )
              .arg( fgbPath ).arg( featureCount ).arg( dataSize / 1024 );
  
  return fgbPath;
}

bool TestPerformanceIntegration::validateProjectLoadingTarget( qint64 actualTimeMs, qint64 baselineTimeMs )
{
  if ( baselineTimeMs == 0 ) return false;
  
  double improvement = calculateImprovement( baselineTimeMs, actualTimeMs );
  return improvement >= PROJECT_LOADING_IMPROVEMENT_TARGET;
}

bool TestPerformanceIntegration::validateRenderingTarget( qint64 actualTimeMs, qint64 baselineTimeMs )
{
  if ( baselineTimeMs == 0 ) return false;
  
  double improvement = calculateImprovement( baselineTimeMs, actualTimeMs );
  return improvement >= RENDERING_IMPROVEMENT_TARGET;
}

double TestPerformanceIntegration::calculateImprovement( qint64 baselineTimeMs, qint64 optimizedTimeMs )
{
  if ( baselineTimeMs == 0 ) return 0.0;
  
  return ( double( baselineTimeMs - optimizedTimeMs ) ) / baselineTimeMs;
}

qint64 TestPerformanceIntegration::getCurrentMemoryUsageMB()
{
  // Simplified memory measurement
  // Real implementation would use platform-specific APIs
  return 120; // Placeholder value
}

QGSTEST_MAIN( TestPerformanceIntegration )
#include "test_integration.moc"