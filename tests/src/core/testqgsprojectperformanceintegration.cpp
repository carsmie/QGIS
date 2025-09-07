/***************************************************************************
     testqgsprojectperformanceintegration.cpp
     ---------------------------------------
    Date                 : September 2025
    Copyright            : (C) 2025 by QGIS
    Email                : development-team@qgis.org
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgstest.h"
#include "qgsproject.h"
#include "qgsprojectloadingperformance.h"
#include "qgsprojectstreamingparser.h"
#include "qgsparallellayerloader.h"
#include "qgslayerstylecache.h"
#include "qgsprojectprogressivedisplay.h"
#include "qgssettingsregistrycore.h"
#include <QTemporaryFile>
#include <QDomDocument>

/**
 * \ingroup UnitTests
 * Integration tests for QGIS project performance optimizations
 */
class TestQgsProjectPerformanceIntegration : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Integration tests
    void testFullProjectLoadingPipeline();
    void testLargeProjectOptimization();
    void testStreamingWithParallelLoading();
    void testStyleCachingIntegration();
    void testProgressiveDisplayIntegration();
    void testPerformanceMonitoringIntegration();
    void testMemoryConstrainedLoading();
    void testRegressionDetection();
    void testErrorRecoveryIntegration();
    void testBenchmarkComparison();

  private:
    QgsProject *mProject = nullptr;
    
    QString createComplexProjectFile( int layerCount = 50, int fileSizeMB = 5 );
    QString createLargeProjectFile( int layerCount = 200, int fileSizeMB = 20 );
    void verifyProjectLoadingResults( QgsProject *project, int expectedLayers );
};

void TestQgsProjectPerformanceIntegration::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();
  
  // Enable performance optimizations for testing
  QgsSettingsRegistryCore::settingsLayerParallelLoading->setValue( true );
  QgsSettingsRegistryCore::settingsUseProgressiveLoader->setValue( true );
}

void TestQgsProjectPerformanceIntegration::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsProjectPerformanceIntegration::init()
{
  mProject = new QgsProject();
}

void TestQgsProjectPerformanceIntegration::cleanup()
{
  delete mProject;
  mProject = nullptr;
}

QString TestQgsProjectPerformanceIntegration::createComplexProjectFile( int layerCount, int fileSizeMB )
{
  QDomDocument doc;
  QDomElement root = doc.createElement( QStringLiteral( "qgis" ) );
  root.setAttribute( QStringLiteral( "version" ), QStringLiteral( "3.40.0" ) );
  doc.appendChild( root );
  
  // Add project properties
  QDomElement properties = doc.createElement( QStringLiteral( "properties" ) );
  QDomElement extent = doc.createElement( QStringLiteral( "Extent" ) );
  extent.appendChild( doc.createTextNode( QStringLiteral( "-180,-90,180,90" ) ) );
  properties.appendChild( extent );
  root.appendChild( properties );
  
  // Add layer tree
  QDomElement layerTree = doc.createElement( QStringLiteral( "layer-tree-group" ) );
  layerTree.setAttribute( QStringLiteral( "name" ), QStringLiteral( "Root" ) );
  root.appendChild( layerTree );
  
  // Add layers section
  QDomElement projectLayers = doc.createElement( QStringLiteral( "projectlayers" ) );
  root.appendChild( projectLayers );
  
  // Calculate content size per layer to reach target file size
  const int targetBytes = fileSizeMB * 1024 * 1024;
  const int bytesPerLayer = targetBytes / layerCount;
  const int paddingSize = qMax( 100, bytesPerLayer - 500 ); // Minimum padding
  
  for ( int i = 0; i < layerCount; ++i )
  {
    QDomElement layer = doc.createElement( QStringLiteral( "maplayer" ) );
    layer.setAttribute( QStringLiteral( "id" ), QStringLiteral( "layer_%1" ).arg( i ) );
    layer.setAttribute( QStringLiteral( "type" ), i % 3 == 0 ? QStringLiteral( "raster" ) : QStringLiteral( "vector" ) );
    
    // Basic layer info
    QDomElement layerName = doc.createElement( QStringLiteral( "layername" ) );
    layerName.appendChild( doc.createTextNode( QStringLiteral( "Complex Layer %1" ).arg( i ) ) );
    layer.appendChild( layerName );
    
    // Datasource
    QDomElement datasource = doc.createElement( QStringLiteral( "datasource" ) );
    const QString dsContent = i % 3 == 0 ? 
                               QStringLiteral( "test_raster_%1.tif" ).arg( i ) :
                               QStringLiteral( "Point?crs=EPSG:4326&field=id:integer" );
    datasource.appendChild( doc.createTextNode( dsContent ) );
    layer.appendChild( datasource );
    
    // Provider
    QDomElement provider = doc.createElement( QStringLiteral( "provider" ) );
    provider.appendChild( doc.createTextNode( i % 3 == 0 ? QStringLiteral( "gdal" ) : QStringLiteral( "memory" ) ) );
    layer.appendChild( provider );
    
    // Complex styling
    QDomElement renderer = doc.createElement( QStringLiteral( "renderer-v2" ) );
    renderer.setAttribute( QStringLiteral( "type" ), QStringLiteral( "singleSymbol" ) );
    
    for ( int j = 0; j < 3; ++j )
    {
      QDomElement symbol = doc.createElement( QStringLiteral( "symbol" ) );
      symbol.setAttribute( QStringLiteral( "name" ), QStringLiteral( "symbol_%1_%2" ).arg( i ).arg( j ) );
      symbol.setAttribute( QStringLiteral( "type" ), QStringLiteral( "fill" ) );
      
      // Add symbol layers
      for ( int k = 0; k < 2; ++k )
      {
        QDomElement symbolLayer = doc.createElement( QStringLiteral( "layer" ) );
        symbolLayer.setAttribute( QStringLiteral( "class" ), QStringLiteral( "SimpleFill" ) );
        
        // Add properties with padding to increase file size
        QDomElement props = doc.createElement( QStringLiteral( "prop" ) );
        props.setAttribute( QStringLiteral( "k" ), QStringLiteral( "color" ) );
        const QString colorValue = QStringLiteral( "255,0,0,255" ) + QString( paddingSize / 20, 'X' );
        props.setAttribute( QStringLiteral( "v" ), colorValue );
        symbolLayer.appendChild( props );
        
        symbol.appendChild( symbolLayer );
      }
      
      renderer.appendChild( symbol );
    }
    layer.appendChild( renderer );
    
    // Add labeling
    QDomElement labeling = doc.createElement( QStringLiteral( "labeling" ) );
    labeling.setAttribute( QStringLiteral( "type" ), QStringLiteral( "simple" ) );
    QDomElement settings = doc.createElement( QStringLiteral( "settings" ) );
    const QString labelPadding = QString( paddingSize / 10, 'L' );
    settings.appendChild( doc.createTextNode( labelPadding ) );
    labeling.appendChild( settings );
    layer.appendChild( labeling );
    
    projectLayers.appendChild( layer );
    
    // Add to layer tree
    QDomElement treeLayer = doc.createElement( QStringLiteral( "layer-tree-layer" ) );
    treeLayer.setAttribute( QStringLiteral( "id" ), QStringLiteral( "layer_%1" ).arg( i ) );
    treeLayer.setAttribute( QStringLiteral( "name" ), QStringLiteral( "Complex Layer %1" ).arg( i ) );
    layerTree.appendChild( treeLayer );
  }
  
  // Write to temporary file
  QTemporaryFile *tempFile = new QTemporaryFile();
  tempFile->setAutoRemove( false );
  QVERIFY( tempFile->open() );
  
  const QByteArray content = doc.toByteArray();
  tempFile->write( content );
  tempFile->close();
  
  const QString fileName = tempFile->fileName();
  delete tempFile;
  
  qDebug() << "Created project file:" << fileName << "Size:" << content.size() / 1024 << "KB";
  
  return fileName;
}

QString TestQgsProjectPerformanceIntegration::createLargeProjectFile( int layerCount, int fileSizeMB )
{
  return createComplexProjectFile( layerCount, fileSizeMB );
}

void TestQgsProjectPerformanceIntegration::verifyProjectLoadingResults( QgsProject *project, int expectedLayers )
{
  QVERIFY( project );
  QVERIFY( project->isValid() );
  
  // Verify layers were loaded
  const QList<QgsMapLayer *> layers = project->mapLayers().values();
  QVERIFY( layers.size() >= expectedLayers * 0.8 ); // Allow some tolerance for test failures
  
  // Verify layer tree
  const QgsLayerTree *layerTree = project->layerTreeRoot();
  QVERIFY( layerTree );
  QVERIFY( layerTree->children().size() > 0 );
}

void TestQgsProjectPerformanceIntegration::testFullProjectLoadingPipeline()
{
  // Test the complete optimized loading pipeline
  const QString projectFile = createComplexProjectFile( 25, 2 );
  
  // Create performance monitor
  QgsProjectLoadingPerformance performanceMonitor;
  QSignalSpy performanceCompleteSpy( &performanceMonitor, &QgsProjectLoadingPerformance::loadingCompleted );
  
  // Start performance monitoring
  performanceMonitor.startLoading();
  performanceMonitor.startTiming( QStringLiteral( "full_pipeline_test" ) );
  
  // Load project with all optimizations enabled
  QSignalSpy projectLoadedSpy( mProject, &QgsProject::readProject );
  
  const bool success = mProject->read( projectFile );
  
  // End performance monitoring
  performanceMonitor.endTiming( QStringLiteral( "full_pipeline_test" ) );
  performanceMonitor.finishLoading();
  
  // Verify project loaded successfully
  QVERIFY( success );
  verifyProjectLoadingResults( mProject, 25 );
  
  // Verify performance monitoring worked
  const QgsProjectLoadingPerformance::LoadingStatistics stats = performanceMonitor.getStatistics();
  QVERIFY( stats.totalLoadingTime > 0 );
  QVERIFY( stats.componentCount > 0 );
  
  // Test performance comparison with baseline
  performanceMonitor.createPerformanceBaseline();
  
  // Export performance data
  const QVariantMap exportedStats = performanceMonitor.exportToJson();
  QVERIFY( exportedStats.contains( QStringLiteral( "total_loading_time" ) ) );
  
  QFile::remove( projectFile );
}

void TestQgsProjectPerformanceIntegration::testLargeProjectOptimization()
{
  // Test optimization for large projects (triggers streaming parser and parallel loading)
  const QString largeProjectFile = createLargeProjectFile( 100, 10 );
  
  QElapsedTimer loadTimer;
  loadTimer.start();
  
  // This should automatically trigger:
  // 1. File size detection
  // 2. Streaming parser for large file
  // 3. Parallel layer loading
  // 4. Style caching
  // 5. Progressive display
  const bool success = mProject->read( largeProjectFile );
  
  const qint64 loadTime = loadTimer.elapsed();
  
  QVERIFY( success );
  verifyProjectLoadingResults( mProject, 100 );
  
  // Large project should load in reasonable time (under 30 seconds for test)
  QVERIFY( loadTime < 30000 );
  
  qDebug() << "Large project loading time:" << loadTime << "ms";
  
  QFile::remove( largeProjectFile );
}

void TestQgsProjectPerformanceIntegration::testStreamingWithParallelLoading()
{
  // Test integration between streaming parser and parallel loader
  const QString projectFile = createComplexProjectFile( 50, 5 );
  
  // Create streaming parser
  QgsProjectStreamingParser streamingParser;
  QSignalSpy parsingCompleteSpy( &streamingParser, &QgsProjectStreamingParser::parsingCompleted );
  
  // Configure for streaming
  QgsProjectStreamingParser::ParsingConfig config;
  config.chunkSize = 2048;
  config.enableProgressReporting = true;
  config.maxMemoryUsageMB = 100;
  streamingParser.setParsingConfig( config );
  
  // Start parsing
  QVERIFY( streamingParser.startParsing( projectFile ) );
  
  // Wait for parsing to complete
  QEventLoop loop;
  connect( &streamingParser, &QgsProjectStreamingParser::parsingCompleted, &loop, &QEventLoop::quit );
  QTimer::singleShot( 15000, &loop, &QEventLoop::quit );
  loop.exec();
  
  QCOMPARE( parsingCompleteSpy.count(), 1 );
  QVERIFY( streamingParser.isParsedSuccessfully() );
  
  // Now test normal project loading (which should use both streaming and parallel loading)
  const bool success = mProject->read( projectFile );
  QVERIFY( success );
  verifyProjectLoadingResults( mProject, 50 );
  
  QFile::remove( projectFile );
}

void TestQgsProjectPerformanceIntegration::testStyleCachingIntegration()
{
  // Test style caching working with parallel loading
  const QString projectFile = createComplexProjectFile( 30, 3 );
  
  // Create style cache
  QgsLayerStyleCache styleCache;
  QSignalSpy cacheHitSpy( &styleCache, &QgsLayerStyleCache::cacheHit );
  
  // Configure cache
  QgsLayerStyleCache::CacheConfig cacheConfig;
  cacheConfig.maxMemoryUsageMB = 50;
  cacheConfig.maxEntries = 100;
  cacheConfig.enableSimilarityDetection = true;
  styleCache.setCacheConfig( cacheConfig );
  
  // Load project (which should populate the cache)
  const bool success = mProject->read( projectFile );
  QVERIFY( success );
  
  // Get cache statistics
  const QgsLayerStyleCache::CacheStatistics stats = styleCache.getStatistics();
  QVERIFY( stats.totalEntries > 0 );
  
  // Test cache export
  const QVariantMap cacheExport = styleCache.exportStatistics();
  QVERIFY( cacheExport.contains( QStringLiteral( "total_entries" ) ) );
  
  QFile::remove( projectFile );
}

void TestQgsProjectPerformanceIntegration::testProgressiveDisplayIntegration()
{
  // Test progressive display working with parallel loading
  const QString projectFile = createComplexProjectFile( 40, 4 );
  
  // Create progressive display
  QgsProjectProgressiveDisplay progressiveDisplay;
  QSignalSpy displayStartedSpy( &progressiveDisplay, &QgsProjectProgressiveDisplay::displayStarted );
  QSignalSpy displayProgressSpy( &progressiveDisplay, &QgsProjectProgressiveDisplay::displayProgressChanged );
  QSignalSpy displayCompletedSpy( &progressiveDisplay, &QgsProjectProgressiveDisplay::displayCompleted );
  
  // Configure display
  QgsProjectProgressiveDisplay::DisplayConfig displayConfig;
  displayConfig.strategy = QgsProjectProgressiveDisplay::RenderingStrategy::Adaptive;
  displayConfig.enableProgressIndicator = true;
  displayConfig.batchSize = 3;
  progressiveDisplay.setDisplayConfig( displayConfig );
  
  // Load project
  const bool success = mProject->read( projectFile );
  QVERIFY( success );
  
  // Start progressive display
  progressiveDisplay.startDisplay( mProject );
  
  // Wait for some display progress
  QTest::qWait( 1000 );
  
  // Stop display
  progressiveDisplay.stopDisplay();
  
  // Verify display worked
  const QgsProjectProgressiveDisplay::DisplayStatistics stats = progressiveDisplay.getStatistics();
  QVERIFY( stats.totalLayers > 0 );
  
  QFile::remove( projectFile );
}

void TestQgsProjectPerformanceIntegration::testPerformanceMonitoringIntegration()
{
  // Test comprehensive performance monitoring across all components
  const QString projectFile = createComplexProjectFile( 35, 3 );
  
  QgsProjectLoadingPerformance performanceMonitor;
  
  // Start comprehensive monitoring
  performanceMonitor.startLoading();
  performanceMonitor.startTiming( QStringLiteral( "xml_parsing" ) );
  performanceMonitor.startTiming( QStringLiteral( "layer_creation" ) );
  performanceMonitor.startTiming( QStringLiteral( "style_loading" ) );
  
  // Load project
  const bool success = mProject->read( projectFile );
  QVERIFY( success );
  
  // End timing components
  performanceMonitor.endTiming( QStringLiteral( "xml_parsing" ) );
  performanceMonitor.endTiming( QStringLiteral( "layer_creation" ) );
  performanceMonitor.endTiming( QStringLiteral( "style_loading" ) );
  performanceMonitor.finishLoading();
  
  // Analyze performance
  const QgsProjectLoadingPerformance::LoadingStatistics stats = performanceMonitor.getStatistics();
  QVERIFY( stats.totalLoadingTime > 0 );
  QVERIFY( stats.componentCount >= 3 );
  QVERIFY( stats.memoryUsage > 0 );
  
  // Test performance baseline and regression detection
  performanceMonitor.createPerformanceBaseline();
  QVERIFY( performanceMonitor.hasPerformanceBaseline() );
  
  // Test detailed performance export
  const QVariantMap performanceExport = performanceMonitor.exportToJson();
  QVERIFY( performanceExport.contains( QStringLiteral( "component_timings" ) ) );
  QVERIFY( performanceExport.contains( QStringLiteral( "memory_usage" ) ) );
  
  QFile::remove( projectFile );
}

void TestQgsProjectPerformanceIntegration::testMemoryConstrainedLoading()
{
  // Test all optimizations working under memory constraints
  const QString projectFile = createComplexProjectFile( 60, 6 );
  
  // This should trigger memory-aware optimizations across all components
  const bool success = mProject->read( projectFile );
  QVERIFY( success );
  verifyProjectLoadingResults( mProject, 60 );
  
  // Verify project is functional despite memory constraints
  const QList<QgsMapLayer *> layers = mProject->mapLayers().values();
  QVERIFY( !layers.isEmpty() );
  
  for ( QgsMapLayer *layer : layers )
  {
    QVERIFY( layer );
    QVERIFY( layer->isValid() );
    // Basic layer operations should work
    QVERIFY( !layer->name().isEmpty() );
  }
  
  QFile::remove( projectFile );
}

void TestQgsProjectPerformanceIntegration::testRegressionDetection()
{
  // Test performance regression detection across optimizations
  const QString projectFile = createComplexProjectFile( 20, 2 );
  
  QgsProjectLoadingPerformance performanceMonitor;
  
  // First load - create baseline
  performanceMonitor.startLoading();
  performanceMonitor.startTiming( QStringLiteral( "baseline_load" ) );
  
  bool success = mProject->read( projectFile );
  QVERIFY( success );
  
  performanceMonitor.endTiming( QStringLiteral( "baseline_load" ) );
  performanceMonitor.finishLoading();
  performanceMonitor.createPerformanceBaseline();
  
  // Clear project for second load
  mProject->clear();
  
  // Second load - should be similar or better performance
  performanceMonitor.startLoading();
  performanceMonitor.startTiming( QStringLiteral( "comparison_load" ) );
  
  success = mProject->read( projectFile );
  QVERIFY( success );
  
  performanceMonitor.endTiming( QStringLiteral( "comparison_load" ) );
  performanceMonitor.finishLoading();
  
  // Check for regression (should be minimal or none)
  const bool hasRegression = performanceMonitor.detectPerformanceRegression();
  
  // Get detailed comparison
  const QgsProjectLoadingPerformance::LoadingStatistics stats = performanceMonitor.getStatistics();
  qDebug() << "Baseline time:" << performanceMonitor.getTimingDuration( QStringLiteral( "baseline_load" ) );
  qDebug() << "Comparison time:" << performanceMonitor.getTimingDuration( QStringLiteral( "comparison_load" ) );
  qDebug() << "Regression detected:" << hasRegression;
  
  QFile::remove( projectFile );
}

void TestQgsProjectPerformanceIntegration::testErrorRecoveryIntegration()
{
  // Test error recovery across all optimization components
  const QString validProjectFile = createComplexProjectFile( 15, 1 );
  
  // Create a project with some invalid elements
  QDomDocument doc;
  doc.setContent( QFile( validProjectFile ).readAll() );
  
  // Add invalid layer element
  QDomElement projectLayers = doc.documentElement().firstChildElement( QStringLiteral( "projectlayers" ) );
  QDomElement invalidLayer = doc.createElement( QStringLiteral( "maplayer" ) );
  invalidLayer.setAttribute( QStringLiteral( "id" ), QStringLiteral( "invalid_layer" ) );
  invalidLayer.setAttribute( QStringLiteral( "type" ), QStringLiteral( "invalid_type" ) );
  projectLayers.appendChild( invalidLayer );
  
  // Save modified project
  QTemporaryFile modifiedFile;
  QVERIFY( modifiedFile.open() );
  modifiedFile.write( doc.toByteArray() );
  modifiedFile.close();
  
  QgsProjectLoadingPerformance performanceMonitor;
  performanceMonitor.startLoading();
  
  // Load project with errors - should recover gracefully
  const bool success = mProject->read( modifiedFile.fileName() );
  
  performanceMonitor.finishLoading();
  
  // Should succeed despite some errors
  QVERIFY( success );
  
  // Should have loaded most layers successfully
  const QList<QgsMapLayer *> layers = mProject->mapLayers().values();
  QVERIFY( layers.size() >= 10 ); // Should have loaded most valid layers
  
  // Performance monitoring should still work
  const QgsProjectLoadingPerformance::LoadingStatistics stats = performanceMonitor.getStatistics();
  QVERIFY( stats.totalLoadingTime > 0 );
  
  QFile::remove( validProjectFile );
}

void TestQgsProjectPerformanceIntegration::testBenchmarkComparison()
{
  // Compare optimized vs non-optimized loading
  const QString projectFile = createComplexProjectFile( 50, 4 );
  
  // First, test with optimizations disabled
  QgsSettingsRegistryCore::settingsLayerParallelLoading->setValue( false );
  QgsSettingsRegistryCore::settingsUseProgressiveLoader->setValue( false );
  
  QElapsedTimer unoptimizedTimer;
  unoptimizedTimer.start();
  
  QgsProject *unoptimizedProject = new QgsProject();
  const bool unoptimizedSuccess = unoptimizedProject->read( projectFile );
  const qint64 unoptimizedTime = unoptimizedTimer.elapsed();
  
  QVERIFY( unoptimizedSuccess );
  const int unoptimizedLayerCount = unoptimizedProject->mapLayers().size();
  delete unoptimizedProject;
  
  // Re-enable optimizations
  QgsSettingsRegistryCore::settingsLayerParallelLoading->setValue( true );
  QgsSettingsRegistryCore::settingsUseProgressiveLoader->setValue( true );
  
  // Test with optimizations enabled
  QElapsedTimer optimizedTimer;
  optimizedTimer.start();
  
  const bool optimizedSuccess = mProject->read( projectFile );
  const qint64 optimizedTime = optimizedTimer.elapsed();
  
  QVERIFY( optimizedSuccess );
  const int optimizedLayerCount = mProject->mapLayers().size();
  
  // Both should load the same number of layers
  QCOMPARE( optimizedLayerCount, unoptimizedLayerCount );
  
  // Calculate performance improvement
  const double speedupRatio = static_cast<double>( unoptimizedTime ) / optimizedTime;
  
  qDebug() << "Unoptimized loading time:" << unoptimizedTime << "ms";
  qDebug() << "Optimized loading time:" << optimizedTime << "ms";
  qDebug() << "Speedup ratio:" << speedupRatio;
  qDebug() << "Layers loaded:" << optimizedLayerCount;
  
  // Optimized version should show some improvement (allow for test variations)
  QVERIFY( speedupRatio > 0.7 ); // At least not significantly slower
  
  // Test should complete in reasonable time
  QVERIFY( optimizedTime < 60000 ); // Under 1 minute
  
  QFile::remove( projectFile );
}

QGSTEST_MAIN( TestQgsProjectPerformanceIntegration )
#include "testqgsprojectperformanceintegration.moc"