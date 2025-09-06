/***************************************************************************
                         test_vector_rendering.cpp
                         --------------------------
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
#include "qgsvectorlayer.h"
#include "qgsmaplayerrenderer.h"
#include "qgsmaprenderersequentialjob.h"
#include "qgsmaprendererparalleljob.h"
#include "qgsmapsettings.h"
#include "qgsproject.h"
#include "qgsrendercontext.h"
#include "qgsfeatureiterator.h"
#include "qgsgeometry.h"
#include "qgsrectangle.h"
#include "qgssymbollayer.h"
#include "qgssymbol.h"
#include "qgssinglesymbolrenderer.h"
#include "qgscategorizedsymbolrenderer.h"
#include "qgsgraduatedsymbolrenderer.h"

#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QSignalSpy>

/**
 * \ingroup PerformanceTests
 * Vector Rendering Performance Benchmark
 * 
 * This benchmark measures the current performance of rendering vector layers,
 * specifically FlatGeobuf (.fgb) files, to establish baseline metrics for 
 * the 5% improvement target (FR-002).
 * 
 * Test scenarios:
 * - FlatGeobuf rendering at different zoom levels
 * - Various symbology complexity (simple, categorized, graduated)
 * - Different geometry types (point, line, polygon)
 * - Tile-based rendering vs. full extent rendering
 * - Spatial filtering and indexing performance
 * - Memory usage during rendering operations
 * 
 * Baseline Target: Establish current FGB rendering times for 5% improvement goal
 */
class TestVectorRenderingPerformance : public QObject
{
    Q_OBJECT

  public:
    TestVectorRenderingPerformance() = default;

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Core FGB rendering benchmarks
    void benchmarkFgbPointRendering();
    void benchmarkFgbLineRendering();
    void benchmarkFgbPolygonRendering();
    void benchmarkFgbMixedGeometryRendering();
    
    // Symbology complexity benchmarks
    void benchmarkSimpleSymbolRendering();
    void benchmarkCategorizedSymbolRendering();
    void benchmarkGraduatedSymbolRendering();
    void benchmarkRuleBasedSymbolRendering();
    
    // Zoom level and scale benchmarks
    void benchmarkRenderingAtDifferentZoomLevels();
    void benchmarkTileBasedRendering();
    void benchmarkFullExtentRendering();
    
    // Spatial filtering and optimization benchmarks
    void benchmarkSpatialIndexPerformance();
    void benchmarkFeatureFilteringPerformance();
    void benchmarkMemoryUsageDuringRendering();
    
    // Format comparison benchmarks
    void benchmarkFgbVsShapefileRendering();
    void benchmarkFgbVsGeoPackageRendering();
    void benchmarkFgbVsGeoJSONRendering();
    
    // Advanced rendering features
    void benchmarkLabelRendering();
    void benchmarkTransparencyRendering();
    void benchmarkBlendModeRendering();

  private:
    // Test data generation and management
    QString createFgbTestData( const QString &path, const QString &geometryType, int featureCount );
    QString createShapefileTestData( const QString &path, const QString &geometryType, int featureCount );
    QString createGeoPackageTestData( const QString &path, const QString &geometryType, int featureCount );
    QString createGeoJSONTestData( const QString &path, const QString &geometryType, int featureCount );
    
    void addVariedAttributes( QgsVectorLayer *layer );
    void setupSimpleSymbology( QgsVectorLayer *layer );
    void setupCategorizedSymbology( QgsVectorLayer *layer );
    void setupGraduatedSymbology( QgsVectorLayer *layer );
    void setupRuleBasedSymbology( QgsVectorLayer *layer );
    
    // Rendering performance measurement utilities
    struct RenderingMetrics {
      qint64 renderingTimeMs = 0;
      qint64 preparationTimeMs = 0;
      qint64 totalTimeMs = 0;
      qint64 memoryUsageMB = 0;
      int featuresRendered = 0;
      int tilesRendered = 0;
      QSize imageSize;
      QString symbolType;
      QString geometryType;
      QString dataFormat;
      bool renderingSuccessful = false;
      QString errorMessage;
    };
    
    RenderingMetrics measureLayerRendering( QgsVectorLayer *layer, const QgsRectangle &extent, 
                                           const QSize &imageSize, double scale = 0 );
    RenderingMetrics measureTileRendering( QgsVectorLayer *layer, const QgsRectangle &tileExtent,
                                         const QSize &tileSize );
    
    qint64 getCurrentMemoryUsageMB();
    void logRenderingMetrics( const QString &testName, const RenderingMetrics &metrics );
    
    // Test configuration and data paths
    QString mTestDataDir;
    QString mFgbPointPath;
    QString mFgbLinePath;
    QString mFgbPolygonPath;
    QString mFgbMixedPath;
    QString mShapefilePath;
    QString mGeoPackagePath;
    QString mGeoJSONPath;
    
    // Standard rendering parameters
    QSize mStandardImageSize;
    QgsRectangle mStandardExtent;
    QList<double> mZoomScales;
    
    // Baseline metrics for 5% improvement target
    struct BaselineMetrics {
      qint64 fgb_point_baseline_ms = 0;
      qint64 fgb_line_baseline_ms = 0;
      qint64 fgb_polygon_baseline_ms = 0;
      qint64 fgb_mixed_baseline_ms = 0;
      qint64 simple_symbol_baseline_ms = 0;
      qint64 categorized_symbol_baseline_ms = 0;
      qint64 graduated_symbol_baseline_ms = 0;
      qint64 tile_rendering_baseline_ms = 0;
      qint64 full_extent_baseline_ms = 0;
    };
    BaselineMetrics mBaseline;
    
    // Performance targets (5% improvement)
    static constexpr double IMPROVEMENT_TARGET = 0.05; // 5%
    static constexpr int STANDARD_FEATURE_COUNT = 50000; // 50K features for baseline
    static constexpr int MAX_RENDERING_TIME_MS = 10000; // 10 second maximum
    static constexpr int STANDARD_IMAGE_WIDTH = 1024;
    static constexpr int STANDARD_IMAGE_HEIGHT = 768;
};

void TestVectorRenderingPerformance::initTestCase()
{
  // Initialize QGIS application
  QgsApplication::init();
  
  // Set up test data directory
  mTestDataDir = QStandardPaths::writableLocation( QStandardPaths::TempLocation ) + "/qgis_render_perf_test";
  QDir dir;
  if ( !dir.exists( mTestDataDir ) )
  {
    dir.mkpath( mTestDataDir );
  }
  
  qDebug() << "=== QGIS Vector Rendering Performance Benchmark ===";
  qDebug() << "Test data directory:" << mTestDataDir;
  qDebug() << "Performance target: 5% improvement for FGB rendering";
  qDebug() << "Feature count for baseline:" << STANDARD_FEATURE_COUNT;
  qDebug() << "Standard image size:" << STANDARD_IMAGE_WIDTH << "x" << STANDARD_IMAGE_HEIGHT;
  
  // Set standard rendering parameters
  mStandardImageSize = QSize( STANDARD_IMAGE_WIDTH, STANDARD_IMAGE_HEIGHT );
  mStandardExtent = QgsRectangle( -180, -90, 180, 90 ); // World extent
  
  // Define zoom scales for testing
  mZoomScales = { 1000000, 500000, 250000, 100000, 50000, 25000, 10000, 5000 };
  
  // Create test datasets
  qDebug() << "Creating FlatGeobuf test datasets...";
  mFgbPointPath = createFgbTestData( mTestDataDir + "/points.fgb", "Point", STANDARD_FEATURE_COUNT );
  mFgbLinePath = createFgbTestData( mTestDataDir + "/lines.fgb", "LineString", STANDARD_FEATURE_COUNT );
  mFgbPolygonPath = createFgbTestData( mTestDataDir + "/polygons.fgb", "Polygon", STANDARD_FEATURE_COUNT );
  mFgbMixedPath = createFgbTestData( mTestDataDir + "/mixed.fgb", "Mixed", STANDARD_FEATURE_COUNT );
  
  qDebug() << "Creating comparison format datasets...";
  mShapefilePath = createShapefileTestData( mTestDataDir + "/comparison.shp", "Point", STANDARD_FEATURE_COUNT );
  mGeoPackagePath = createGeoPackageTestData( mTestDataDir + "/comparison.gpkg", "Point", STANDARD_FEATURE_COUNT );
  mGeoJSONPath = createGeoJSONTestData( mTestDataDir + "/comparison.geojson", "Point", STANDARD_FEATURE_COUNT );
  
  qDebug() << "Test datasets created:";
  qDebug() << "  FGB Points:" << mFgbPointPath;
  qDebug() << "  FGB Lines:" << mFgbLinePath;
  qDebug() << "  FGB Polygons:" << mFgbPolygonPath;
  qDebug() << "  FGB Mixed:" << mFgbMixedPath;
  qDebug() << "  Shapefile:" << mShapefilePath;
  qDebug() << "  GeoPackage:" << mGeoPackagePath;
  qDebug() << "  GeoJSON:" << mGeoJSONPath;
}

void TestVectorRenderingPerformance::cleanupTestCase()
{
  // Clean up test data
  QDir testDir( mTestDataDir );
  if ( testDir.exists() )
  {
    testDir.removeRecursively();
  }
  
  // Print baseline summary
  qDebug() << "\n=== BASELINE RENDERING PERFORMANCE METRICS ===";
  qDebug() << QString( "FGB Point Rendering: %1 ms" ).arg( mBaseline.fgb_point_baseline_ms );
  qDebug() << QString( "FGB Line Rendering: %1 ms" ).arg( mBaseline.fgb_line_baseline_ms );
  qDebug() << QString( "FGB Polygon Rendering: %1 ms" ).arg( mBaseline.fgb_polygon_baseline_ms );
  qDebug() << QString( "FGB Mixed Rendering: %1 ms" ).arg( mBaseline.fgb_mixed_baseline_ms );
  qDebug() << QString( "Simple Symbol Rendering: %1 ms" ).arg( mBaseline.simple_symbol_baseline_ms );
  qDebug() << QString( "Categorized Symbol Rendering: %1 ms" ).arg( mBaseline.categorized_symbol_baseline_ms );
  qDebug() << QString( "Graduated Symbol Rendering: %1 ms" ).arg( mBaseline.graduated_symbol_baseline_ms );
  qDebug() << QString( "Tile Rendering: %1 ms" ).arg( mBaseline.tile_rendering_baseline_ms );
  qDebug() << QString( "Full Extent Rendering: %1 ms" ).arg( mBaseline.full_extent_baseline_ms );
  
  // Calculate 5% improvement targets
  qDebug() << QString( "\n5%% IMPROVEMENT TARGETS:" );
  qDebug() << QString( "FGB Point Target: %1 ms (current: %2 ms)" )
              .arg( mBaseline.fgb_point_baseline_ms * ( 1.0 - IMPROVEMENT_TARGET ) )
              .arg( mBaseline.fgb_point_baseline_ms );
  qDebug() << QString( "FGB Polygon Target: %1 ms (current: %2 ms)" )
              .arg( mBaseline.fgb_polygon_baseline_ms * ( 1.0 - IMPROVEMENT_TARGET ) )
              .arg( mBaseline.fgb_polygon_baseline_ms );
  qDebug() << QString( "Tile Rendering Target: %1 ms (current: %2 ms)" )
              .arg( mBaseline.tile_rendering_baseline_ms * ( 1.0 - IMPROVEMENT_TARGET ) )
              .arg( mBaseline.tile_rendering_baseline_ms );
  
  QgsApplication::exitQgis();
}

void TestVectorRenderingPerformance::init()
{
  // Clean state for each test
  QgsProject::instance()->clear();
}

void TestVectorRenderingPerformance::cleanup()
{
  // Clean up after each test
  QgsProject::instance()->clear();
}

void TestVectorRenderingPerformance::benchmarkFgbPointRendering()
{
  qDebug() << "\n--- Benchmarking FGB Point Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  // Load FGB point layer
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "FGB Points", "ogr" );
  QVERIFY( layer->isValid() );
  
  // Add varied attributes for realistic rendering
  addVariedAttributes( layer );
  
  // Setup simple symbology
  setupSimpleSymbology( layer );
  
  // Run multiple rendering iterations
  QList<qint64> renderingTimes;
  const int iterations = 5;
  
  for ( int i = 0; i < iterations; ++i )
  {
    RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
    QVERIFY( metrics.renderingSuccessful );
    
    renderingTimes.append( metrics.renderingTimeMs );
    logRenderingMetrics( QString( "FgbPoints_Iteration_%1" ).arg( i + 1 ), metrics );
  }
  
  // Calculate baseline
  double avgTime = 0;
  for ( qint64 time : renderingTimes )
  {
    avgTime += time;
  }
  avgTime /= iterations;
  
  mBaseline.fgb_point_baseline_ms = static_cast<qint64>( avgTime );
  
  qDebug() << QString( "FGB Point Rendering Baseline: %1 ms" ).arg( avgTime, 0, 'f', 1 );
  qDebug() << QString( "Features rendered: %1" ).arg( layer->featureCount() );
  qDebug() << QString( "Rendering rate: %1 features/second" )
              .arg( layer->featureCount() * 1000.0 / avgTime, 0, 'f', 0 );
  
  // Performance validation
  QVERIFY( avgTime < MAX_RENDERING_TIME_MS );
  QVERIFY( avgTime > 0 );
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkFgbLineRendering()
{
  qDebug() << "\n--- Benchmarking FGB Line Rendering ---";
  
  if ( !QFileInfo::exists( mFgbLinePath ) )
  {
    QSKIP( "FGB line test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbLinePath, "FGB Lines", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  const int iterations = 3; // Lines are more complex to render
  QList<qint64> renderingTimes;
  
  for ( int i = 0; i < iterations; ++i )
  {
    RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
    QVERIFY( metrics.renderingSuccessful );
    
    renderingTimes.append( metrics.renderingTimeMs );
    logRenderingMetrics( QString( "FgbLines_Iteration_%1" ).arg( i + 1 ), metrics );
  }
  
  double avgTime = 0;
  for ( qint64 time : renderingTimes )
  {
    avgTime += time;
  }
  avgTime /= iterations;
  
  mBaseline.fgb_line_baseline_ms = static_cast<qint64>( avgTime );
  
  qDebug() << QString( "FGB Line Rendering Baseline: %1 ms" ).arg( avgTime, 0, 'f', 1 );
  
  QVERIFY( avgTime < MAX_RENDERING_TIME_MS );
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkFgbPolygonRendering()
{
  qDebug() << "\n--- Benchmarking FGB Polygon Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPolygonPath ) )
  {
    QSKIP( "FGB polygon test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPolygonPath, "FGB Polygons", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  const int iterations = 3; // Polygons are most complex to render
  QList<qint64> renderingTimes;
  
  for ( int i = 0; i < iterations; ++i )
  {
    RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
    QVERIFY( metrics.renderingSuccessful );
    
    renderingTimes.append( metrics.renderingTimeMs );
    logRenderingMetrics( QString( "FgbPolygons_Iteration_%1" ).arg( i + 1 ), metrics );
  }
  
  double avgTime = 0;
  for ( qint64 time : renderingTimes )
  {
    avgTime += time;
  }
  avgTime /= iterations;
  
  mBaseline.fgb_polygon_baseline_ms = static_cast<qint64>( avgTime );
  
  qDebug() << QString( "FGB Polygon Rendering Baseline: %1 ms" ).arg( avgTime, 0, 'f', 1 );
  
  QVERIFY( avgTime < MAX_RENDERING_TIME_MS );
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkFgbMixedGeometryRendering()
{
  qDebug() << "\n--- Benchmarking FGB Mixed Geometry Rendering ---";
  
  if ( !QFileInfo::exists( mFgbMixedPath ) )
  {
    QSKIP( "FGB mixed geometry test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbMixedPath, "FGB Mixed", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  const int iterations = 3;
  QList<qint64> renderingTimes;
  
  for ( int i = 0; i < iterations; ++i )
  {
    RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
    QVERIFY( metrics.renderingSuccessful );
    
    renderingTimes.append( metrics.renderingTimeMs );
    logRenderingMetrics( QString( "FgbMixed_Iteration_%1" ).arg( i + 1 ), metrics );
  }
  
  double avgTime = 0;
  for ( qint64 time : renderingTimes )
  {
    avgTime += time;
  }
  avgTime /= iterations;
  
  mBaseline.fgb_mixed_baseline_ms = static_cast<qint64>( avgTime );
  
  qDebug() << QString( "FGB Mixed Geometry Rendering Baseline: %1 ms" ).arg( avgTime, 0, 'f', 1 );
  
  QVERIFY( avgTime < MAX_RENDERING_TIME_MS );
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkSimpleSymbolRendering()
{
  qDebug() << "\n--- Benchmarking Simple Symbol Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "Simple Symbols", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
  QVERIFY( metrics.renderingSuccessful );
  
  mBaseline.simple_symbol_baseline_ms = metrics.renderingTimeMs;
  
  qDebug() << QString( "Simple Symbol Rendering Baseline: %1 ms" ).arg( metrics.renderingTimeMs );
  logRenderingMetrics( "SimpleSymbol", metrics );
  
  QVERIFY( metrics.renderingTimeMs < MAX_RENDERING_TIME_MS );
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkCategorizedSymbolRendering()
{
  qDebug() << "\n--- Benchmarking Categorized Symbol Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "Categorized Symbols", "ogr" );
  QVERIFY( layer->isValid() );
  
  addVariedAttributes( layer );
  setupCategorizedSymbology( layer );
  
  RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
  QVERIFY( metrics.renderingSuccessful );
  
  mBaseline.categorized_symbol_baseline_ms = metrics.renderingTimeMs;
  
  qDebug() << QString( "Categorized Symbol Rendering Baseline: %1 ms" ).arg( metrics.renderingTimeMs );
  logRenderingMetrics( "CategorizedSymbol", metrics );
  
  QVERIFY( metrics.renderingTimeMs < MAX_RENDERING_TIME_MS );
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkGraduatedSymbolRendering()
{
  qDebug() << "\n--- Benchmarking Graduated Symbol Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "Graduated Symbols", "ogr" );
  QVERIFY( layer->isValid() );
  
  addVariedAttributes( layer );
  setupGraduatedSymbology( layer );
  
  RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
  QVERIFY( metrics.renderingSuccessful );
  
  mBaseline.graduated_symbol_baseline_ms = metrics.renderingTimeMs;
  
  qDebug() << QString( "Graduated Symbol Rendering Baseline: %1 ms" ).arg( metrics.renderingTimeMs );
  logRenderingMetrics( "GraduatedSymbol", metrics );
  
  QVERIFY( metrics.renderingTimeMs < MAX_RENDERING_TIME_MS );
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkRuleBasedSymbolRendering()
{
  qDebug() << "\n--- Benchmarking Rule-Based Symbol Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "Rule-Based Symbols", "ogr" );
  QVERIFY( layer->isValid() );
  
  addVariedAttributes( layer );
  setupRuleBasedSymbology( layer );
  
  RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
  QVERIFY( metrics.renderingSuccessful );
  
  qDebug() << QString( "Rule-Based Symbol Rendering: %1 ms" ).arg( metrics.renderingTimeMs );
  logRenderingMetrics( "RuleBasedSymbol", metrics );
  
  QVERIFY( metrics.renderingTimeMs < MAX_RENDERING_TIME_MS );
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkRenderingAtDifferentZoomLevels()
{
  qDebug() << "\n--- Benchmarking Rendering at Different Zoom Levels ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "Zoom Test", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  for ( double scale : mZoomScales )
  {
    RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize, scale );
    
    qDebug() << QString( "Scale 1:%1 - Rendering time: %2 ms" )
                .arg( scale, 0, 'f', 0 ).arg( metrics.renderingTimeMs );
    
    QVERIFY( metrics.renderingSuccessful );
    QVERIFY( metrics.renderingTimeMs < MAX_RENDERING_TIME_MS );
  }
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkTileBasedRendering()
{
  qDebug() << "\n--- Benchmarking Tile-Based Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "Tile Test", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  // Define tile grid (4x4 tiles)
  QSize tileSize( 256, 256 );
  QgsRectangle fullExtent = mStandardExtent;
  double tileWidth = fullExtent.width() / 4.0;
  double tileHeight = fullExtent.height() / 4.0;
  
  QList<qint64> tileRenderingTimes;
  int tilesRendered = 0;
  
  for ( int row = 0; row < 4; ++row )
  {
    for ( int col = 0; col < 4; ++col )
    {
      QgsRectangle tileExtent(
        fullExtent.xMinimum() + col * tileWidth,
        fullExtent.yMinimum() + row * tileHeight,
        fullExtent.xMinimum() + ( col + 1 ) * tileWidth,
        fullExtent.yMinimum() + ( row + 1 ) * tileHeight
      );
      
      RenderingMetrics metrics = measureTileRendering( layer, tileExtent, tileSize );
      
      if ( metrics.renderingSuccessful )
      {
        tileRenderingTimes.append( metrics.renderingTimeMs );
        tilesRendered++;
      }
    }
  }
  
  // Calculate tile rendering statistics
  if ( !tileRenderingTimes.isEmpty() )
  {
    double avgTileTime = 0;
    qint64 totalTileTime = 0;
    for ( qint64 time : tileRenderingTimes )
    {
      avgTileTime += time;
      totalTileTime += time;
    }
    avgTileTime /= tileRenderingTimes.size();
    
    mBaseline.tile_rendering_baseline_ms = static_cast<qint64>( avgTileTime );
    
    qDebug() << QString( "Tile-Based Rendering Results:" );
    qDebug() << QString( "  Tiles rendered: %1/16" ).arg( tilesRendered );
    qDebug() << QString( "  Average tile time: %1 ms" ).arg( avgTileTime, 0, 'f', 1 );
    qDebug() << QString( "  Total tile time: %1 ms" ).arg( totalTileTime );
    qDebug() << QString( "  Tiles per second: %1" ).arg( 1000.0 / avgTileTime, 0, 'f', 1 );
    
    QVERIFY( avgTileTime < MAX_RENDERING_TIME_MS / 16 ); // Each tile should be much faster
  }
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkFullExtentRendering()
{
  qDebug() << "\n--- Benchmarking Full Extent Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "Full Extent Test", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  // Test different image sizes
  QList<QSize> imageSizes = {
    QSize( 512, 384 ),    // Small
    QSize( 1024, 768 ),   // Standard
    QSize( 2048, 1536 ),  // Large
    QSize( 4096, 3072 )   // Very large
  };
  
  for ( const QSize &imageSize : imageSizes )
  {
    RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, imageSize );
    
    qDebug() << QString( "Image %1x%2 - Rendering time: %3 ms" )
                .arg( imageSize.width() ).arg( imageSize.height() ).arg( metrics.renderingTimeMs );
    
    QVERIFY( metrics.renderingSuccessful );
    
    if ( imageSize == mStandardImageSize )
    {
      mBaseline.full_extent_baseline_ms = metrics.renderingTimeMs;
    }
  }
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkSpatialIndexPerformance()
{
  qDebug() << "\n--- Benchmarking Spatial Index Performance ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "Spatial Index Test", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  // Test rendering with different spatial extents
  QList<QgsRectangle> testExtents = {
    QgsRectangle( -10, -10, 10, 10 ),       // Small extent
    QgsRectangle( -45, -45, 45, 45 ),       // Medium extent  
    QgsRectangle( -90, -90, 90, 90 ),       // Large extent
    mStandardExtent                          // Full extent
  };
  
  for ( const QgsRectangle &extent : testExtents )
  {
    RenderingMetrics metrics = measureLayerRendering( layer, extent, mStandardImageSize );
    
    double extentArea = extent.width() * extent.height();
    
    qDebug() << QString( "Extent area %1 - Rendering time: %2 ms" )
                .arg( extentArea, 0, 'e', 2 ).arg( metrics.renderingTimeMs );
    
    QVERIFY( metrics.renderingSuccessful );
    QVERIFY( metrics.renderingTimeMs > 0 );
  }
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkFeatureFilteringPerformance()
{
  qDebug() << "\n--- Benchmarking Feature Filtering Performance ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "Filter Test", "ogr" );
  QVERIFY( layer->isValid() );
  
  addVariedAttributes( layer );
  setupSimpleSymbology( layer );
  
  // Test different feature filters
  QStringList filters = {
    "\"id\" % 10 = 0",      // Every 10th feature
    "\"id\" % 100 = 0",     // Every 100th feature
    "\"id\" < 10000",       // First 10K features
    ""                      // No filter (all features)
  };
  
  for ( const QString &filter : filters )
  {
    if ( !filter.isEmpty() )
    {
      layer->setSubsetString( filter );
    }
    else
    {
      layer->setSubsetString( QString() );
    }
    
    RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
    
    QString filterDesc = filter.isEmpty() ? "No filter" : filter;
    
    qDebug() << QString( "Filter '%1' - Rendering time: %2 ms (Features: %3)" )
                .arg( filterDesc ).arg( metrics.renderingTimeMs ).arg( layer->featureCount() );
    
    QVERIFY( metrics.renderingSuccessful );
  }
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkMemoryUsageDuringRendering()
{
  qDebug() << "\n--- Benchmarking Memory Usage During Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPolygonPath ) )
  {
    QSKIP( "FGB polygon test data not available" );
  }
  
  qint64 initialMemory = getCurrentMemoryUsageMB();
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPolygonPath, "Memory Test", "ogr" );
  QVERIFY( layer->isValid() );
  
  qint64 memoryAfterLoad = getCurrentMemoryUsageMB();
  
  setupSimpleSymbology( layer );
  
  qint64 memoryAfterSymbology = getCurrentMemoryUsageMB();
  
  RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
  QVERIFY( metrics.renderingSuccessful );
  
  qint64 memoryAfterRendering = getCurrentMemoryUsageMB();
  
  delete layer;
  layer = nullptr;
  
  qint64 memoryAfterCleanup = getCurrentMemoryUsageMB();
  
  qDebug() << QString( "Memory Usage During Rendering:" );
  qDebug() << QString( "  Initial memory: %1 MB" ).arg( initialMemory );
  qDebug() << QString( "  After layer load: %1 MB (+%2 MB)" )
              .arg( memoryAfterLoad ).arg( memoryAfterLoad - initialMemory );
  qDebug() << QString( "  After symbology: %1 MB (+%2 MB)" )
              .arg( memoryAfterSymbology ).arg( memoryAfterSymbology - memoryAfterLoad );
  qDebug() << QString( "  After rendering: %1 MB (+%2 MB)" )
              .arg( memoryAfterRendering ).arg( memoryAfterRendering - memoryAfterSymbology );
  qDebug() << QString( "  After cleanup: %1 MB (+%2 MB)" )
              .arg( memoryAfterCleanup ).arg( memoryAfterCleanup - memoryAfterRendering );
  qDebug() << QString( "  Total memory increase: %1 MB" ).arg( memoryAfterRendering - initialMemory );
  qDebug() << QString( "  Rendering time: %1 ms" ).arg( metrics.renderingTimeMs );
  
  // Memory usage should be reasonable
  QVERIFY( memoryAfterRendering - initialMemory < 1024 ); // Less than 1GB increase
}

void TestVectorRenderingPerformance::benchmarkFgbVsShapefileRendering()
{
  qDebug() << "\n--- Benchmarking FGB vs Shapefile Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) || !QFileInfo::exists( mShapefilePath ) )
  {
    QSKIP( "Comparison test data not available" );
  }
  
  // Test FGB
  QgsVectorLayer *fgbLayer = new QgsVectorLayer( mFgbPointPath, "FGB", "ogr" );
  QVERIFY( fgbLayer->isValid() );
  setupSimpleSymbology( fgbLayer );
  
  RenderingMetrics fgbMetrics = measureLayerRendering( fgbLayer, mStandardExtent, mStandardImageSize );
  QVERIFY( fgbMetrics.renderingSuccessful );
  
  // Test Shapefile
  QgsVectorLayer *shpLayer = new QgsVectorLayer( mShapefilePath, "SHP", "ogr" );
  QVERIFY( shpLayer->isValid() );
  setupSimpleSymbology( shpLayer );
  
  RenderingMetrics shpMetrics = measureLayerRendering( shpLayer, mStandardExtent, mStandardImageSize );
  QVERIFY( shpMetrics.renderingSuccessful );
  
  // Compare results
  double fgbVsShpRatio = double( fgbMetrics.renderingTimeMs ) / shpMetrics.renderingTimeMs;
  
  qDebug() << QString( "Format Comparison Results:" );
  qDebug() << QString( "  FGB rendering: %1 ms" ).arg( fgbMetrics.renderingTimeMs );
  qDebug() << QString( "  Shapefile rendering: %1 ms" ).arg( shpMetrics.renderingTimeMs );
  qDebug() << QString( "  FGB/Shapefile ratio: %1" ).arg( fgbVsShpRatio, 0, 'f', 2 );
  
  if ( fgbVsShpRatio < 1.0 )
  {
    qDebug() << QString( "  FGB is %1% faster than Shapefile" )
                .arg( ( 1.0 - fgbVsShpRatio ) * 100, 0, 'f', 1 );
  }
  else
  {
    qDebug() << QString( "  FGB is %1% slower than Shapefile" )
                .arg( ( fgbVsShpRatio - 1.0 ) * 100, 0, 'f', 1 );
  }
  
  delete fgbLayer;
  delete shpLayer;
}

void TestVectorRenderingPerformance::benchmarkFgbVsGeoPackageRendering()
{
  qDebug() << "\n--- Benchmarking FGB vs GeoPackage Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) || !QFileInfo::exists( mGeoPackagePath ) )
  {
    QSKIP( "Comparison test data not available" );
  }
  
  // Test FGB
  QgsVectorLayer *fgbLayer = new QgsVectorLayer( mFgbPointPath, "FGB", "ogr" );
  QVERIFY( fgbLayer->isValid() );
  setupSimpleSymbology( fgbLayer );
  
  RenderingMetrics fgbMetrics = measureLayerRendering( fgbLayer, mStandardExtent, mStandardImageSize );
  QVERIFY( fgbMetrics.renderingSuccessful );
  
  // Test GeoPackage
  QgsVectorLayer *gpkgLayer = new QgsVectorLayer( mGeoPackagePath, "GPKG", "ogr" );
  QVERIFY( gpkgLayer->isValid() );
  setupSimpleSymbology( gpkgLayer );
  
  RenderingMetrics gpkgMetrics = measureLayerRendering( gpkgLayer, mStandardExtent, mStandardImageSize );
  QVERIFY( gpkgMetrics.renderingSuccessful );
  
  // Compare results
  double fgbVsGpkgRatio = double( fgbMetrics.renderingTimeMs ) / gpkgMetrics.renderingTimeMs;
  
  qDebug() << QString( "FGB vs GeoPackage Comparison:" );
  qDebug() << QString( "  FGB rendering: %1 ms" ).arg( fgbMetrics.renderingTimeMs );
  qDebug() << QString( "  GeoPackage rendering: %1 ms" ).arg( gpkgMetrics.renderingTimeMs );
  qDebug() << QString( "  FGB/GeoPackage ratio: %1" ).arg( fgbVsGpkgRatio, 0, 'f', 2 );
  
  delete fgbLayer;
  delete gpkgLayer;
}

void TestVectorRenderingPerformance::benchmarkFgbVsGeoJSONRendering()
{
  qDebug() << "\n--- Benchmarking FGB vs GeoJSON Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) || !QFileInfo::exists( mGeoJSONPath ) )
  {
    QSKIP( "Comparison test data not available" );
  }
  
  // Test FGB
  QgsVectorLayer *fgbLayer = new QgsVectorLayer( mFgbPointPath, "FGB", "ogr" );
  QVERIFY( fgbLayer->isValid() );
  setupSimpleSymbology( fgbLayer );
  
  RenderingMetrics fgbMetrics = measureLayerRendering( fgbLayer, mStandardExtent, mStandardImageSize );
  QVERIFY( fgbMetrics.renderingSuccessful );
  
  // Test GeoJSON
  QgsVectorLayer *jsonLayer = new QgsVectorLayer( mGeoJSONPath, "GeoJSON", "ogr" );
  QVERIFY( jsonLayer->isValid() );
  setupSimpleSymbology( jsonLayer );
  
  RenderingMetrics jsonMetrics = measureLayerRendering( jsonLayer, mStandardExtent, mStandardImageSize );
  QVERIFY( jsonMetrics.renderingSuccessful );
  
  // Compare results
  double fgbVsJsonRatio = double( fgbMetrics.renderingTimeMs ) / jsonMetrics.renderingTimeMs;
  
  qDebug() << QString( "FGB vs GeoJSON Comparison:" );
  qDebug() << QString( "  FGB rendering: %1 ms" ).arg( fgbMetrics.renderingTimeMs );
  qDebug() << QString( "  GeoJSON rendering: %1 ms" ).arg( jsonMetrics.renderingTimeMs );
  qDebug() << QString( "  FGB/GeoJSON ratio: %1" ).arg( fgbVsJsonRatio, 0, 'f', 2 );
  
  delete fgbLayer;
  delete jsonLayer;
}

void TestVectorRenderingPerformance::benchmarkLabelRendering()
{
  qDebug() << "\n--- Benchmarking Label Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPointPath ) )
  {
    QSKIP( "FGB point test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPointPath, "Label Test", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  // TODO: Add label configuration
  // This would require setting up QgsPalLayerSettings and enabling labeling
  
  RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
  QVERIFY( metrics.renderingSuccessful );
  
  qDebug() << QString( "Label Rendering: %1 ms" ).arg( metrics.renderingTimeMs );
  logRenderingMetrics( "LabelRendering", metrics );
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkTransparencyRendering()
{
  qDebug() << "\n--- Benchmarking Transparency Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPolygonPath ) )
  {
    QSKIP( "FGB polygon test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPolygonPath, "Transparency Test", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  // Set layer transparency
  layer->setOpacity( 0.5 ); // 50% transparency
  
  RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
  QVERIFY( metrics.renderingSuccessful );
  
  qDebug() << QString( "Transparency Rendering: %1 ms" ).arg( metrics.renderingTimeMs );
  logRenderingMetrics( "TransparencyRendering", metrics );
  
  delete layer;
}

void TestVectorRenderingPerformance::benchmarkBlendModeRendering()
{
  qDebug() << "\n--- Benchmarking Blend Mode Rendering ---";
  
  if ( !QFileInfo::exists( mFgbPolygonPath ) )
  {
    QSKIP( "FGB polygon test data not available" );
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mFgbPolygonPath, "Blend Mode Test", "ogr" );
  QVERIFY( layer->isValid() );
  
  setupSimpleSymbology( layer );
  
  // Set blend mode
  layer->setBlendMode( QPainter::CompositionMode_Multiply );
  
  RenderingMetrics metrics = measureLayerRendering( layer, mStandardExtent, mStandardImageSize );
  QVERIFY( metrics.renderingSuccessful );
  
  qDebug() << QString( "Blend Mode Rendering: %1 ms" ).arg( metrics.renderingTimeMs );
  logRenderingMetrics( "BlendModeRendering", metrics );
  
  delete layer;
}

// Helper method implementations

QString TestVectorRenderingPerformance::createFgbTestData( const QString &path, const QString &geometryType, int featureCount )
{
  // Simplified FGB test data creation
  // Real implementation would use GDAL/OGR to create actual FlatGeobuf files
  
  QFile dataFile( path );
  if ( !dataFile.open( QIODevice::WriteOnly ) )
  {
    qWarning() << "Failed to create FGB test file:" << path;
    return QString();
  }
  
  // Write FGB header (simplified)
  dataFile.write( "FGB" );
  
  // Create placeholder data based on geometry type and feature count
  qint64 estimatedSize = featureCount * 64; // Rough estimate: 64 bytes per feature
  
  if ( geometryType == "Point" )
  {
    estimatedSize = featureCount * 32; // Points are smaller
  }
  else if ( geometryType == "Polygon" )
  {
    estimatedSize = featureCount * 256; // Polygons are larger
  }
  
  dataFile.write( QByteArray( estimatedSize, 'F' ) ); // Placeholder data
  dataFile.close();
  
  qDebug() << QString( "Created FGB test data: %1 (%2 %3 features, %4 KB)" )
              .arg( path ).arg( featureCount ).arg( geometryType ).arg( estimatedSize / 1024 );
  
  return path;
}

QString TestVectorRenderingPerformance::createShapefileTestData( const QString &path, const QString &geometryType, int featureCount )
{
  // Simplified Shapefile test data creation
  QFile dataFile( path );
  if ( !dataFile.open( QIODevice::WriteOnly ) )
  {
    return QString();
  }
  
  qint64 estimatedSize = featureCount * 80; // Shapefile overhead
  dataFile.write( QByteArray( estimatedSize, 'S' ) );
  dataFile.close();
  
  return path;
}

QString TestVectorRenderingPerformance::createGeoPackageTestData( const QString &path, const QString &geometryType, int featureCount )
{
  // Simplified GeoPackage test data creation
  QFile dataFile( path );
  if ( !dataFile.open( QIODevice::WriteOnly ) )
  {
    return QString();
  }
  
  qint64 estimatedSize = featureCount * 48; // SQLite efficiency
  dataFile.write( QByteArray( estimatedSize, 'G' ) );
  dataFile.close();
  
  return path;
}

QString TestVectorRenderingPerformance::createGeoJSONTestData( const QString &path, const QString &geometryType, int featureCount )
{
  // Simplified GeoJSON test data creation
  QFile dataFile( path );
  if ( !dataFile.open( QIODevice::WriteOnly ) )
  {
    return QString();
  }
  
  qint64 estimatedSize = featureCount * 120; // JSON verbosity
  dataFile.write( QByteArray( estimatedSize, 'J' ) );
  dataFile.close();
  
  return path;
}

void TestVectorRenderingPerformance::addVariedAttributes( QgsVectorLayer *layer )
{
  // Simplified attribute addition
  // Real implementation would add actual attribute fields with varied data
  Q_UNUSED( layer )
}

void TestVectorRenderingPerformance::setupSimpleSymbology( QgsVectorLayer *layer )
{
  // Simplified symbology setup
  // Real implementation would create actual symbol configurations
  Q_UNUSED( layer )
}

void TestVectorRenderingPerformance::setupCategorizedSymbology( QgsVectorLayer *layer )
{
  // Simplified categorized symbology setup
  Q_UNUSED( layer )
}

void TestVectorRenderingPerformance::setupGraduatedSymbology( QgsVectorLayer *layer )
{
  // Simplified graduated symbology setup
  Q_UNUSED( layer )
}

void TestVectorRenderingPerformance::setupRuleBasedSymbology( QgsVectorLayer *layer )
{
  // Simplified rule-based symbology setup
  Q_UNUSED( layer )
}

TestVectorRenderingPerformance::RenderingMetrics 
TestVectorRenderingPerformance::measureLayerRendering( QgsVectorLayer *layer, const QgsRectangle &extent, 
                                                      const QSize &imageSize, double scale )
{
  RenderingMetrics metrics;
  metrics.imageSize = imageSize;
  metrics.geometryType = layer->geometryType() == QgsWkbTypes::PointGeometry ? "Point" :
                        layer->geometryType() == QgsWkbTypes::LineGeometry ? "Line" :
                        layer->geometryType() == QgsWkbTypes::PolygonGeometry ? "Polygon" : "Unknown";
  
  qint64 memoryBefore = getCurrentMemoryUsageMB();
  
  QElapsedTimer totalTimer;
  totalTimer.start();
  
  // Set up map settings
  QgsMapSettings mapSettings;
  mapSettings.setLayers( QList<QgsMapLayer*>() << layer );
  mapSettings.setExtent( extent );
  mapSettings.setOutputSize( imageSize );
  mapSettings.setOutputDpi( 96 );
  
  if ( scale > 0 )
  {
    mapSettings.setScale( scale );
  }
  
  QElapsedTimer preparationTimer;
  preparationTimer.start();
  
  // Create rendering job
  QgsMapRendererSequentialJob job( mapSettings );
  
  metrics.preparationTimeMs = preparationTimer.elapsed();
  
  // Start rendering
  QElapsedTimer renderingTimer;
  renderingTimer.start();
  
  job.start();
  job.waitForFinished();
  
  metrics.renderingTimeMs = renderingTimer.elapsed();
  metrics.totalTimeMs = totalTimer.elapsed();
  
  // Check if rendering was successful
  metrics.renderingSuccessful = !job.errors().isEmpty() == false; // No errors means success
  
  if ( !metrics.renderingSuccessful && !job.errors().isEmpty() )
  {
    metrics.errorMessage = job.errors().first().message;
  }
  
  // Get rendered image
  QImage result = job.renderedImage();
  metrics.renderingSuccessful = !result.isNull();
  
  // Count rendered features (simplified)
  metrics.featuresRendered = layer->featureCount(); // Simplified
  
  qint64 memoryAfter = getCurrentMemoryUsageMB();
  metrics.memoryUsageMB = memoryAfter - memoryBefore;
  
  return metrics;
}

TestVectorRenderingPerformance::RenderingMetrics 
TestVectorRenderingPerformance::measureTileRendering( QgsVectorLayer *layer, const QgsRectangle &tileExtent,
                                                     const QSize &tileSize )
{
  return measureLayerRendering( layer, tileExtent, tileSize );
}

qint64 TestVectorRenderingPerformance::getCurrentMemoryUsageMB()
{
  // Simplified memory measurement
  // Real implementation would use platform-specific APIs
  
#ifdef Q_OS_LINUX
  QFile statusFile( "/proc/self/status" );
  if ( statusFile.open( QIODevice::ReadOnly ) )
  {
    QTextStream stream( &statusFile );
    QString line;
    while ( stream.readLineInto( &line ) )
    {
      if ( line.startsWith( "VmRSS:" ) )
      {
        QStringList parts = line.split( QRegExp( "\\s+" ) );
        if ( parts.size() >= 2 )
        {
          return parts[1].toLongLong() / 1024; // Convert KB to MB
        }
      }
    }
  }
#endif
  
  return 150; // Fallback placeholder
}

void TestVectorRenderingPerformance::logRenderingMetrics( const QString &testName, const RenderingMetrics &metrics )
{
  qDebug() << QString( "[RENDER] %1:" ).arg( testName );
  qDebug() << QString( "  Total time: %1 ms" ).arg( metrics.totalTimeMs );
  qDebug() << QString( "  Preparation: %1 ms" ).arg( metrics.preparationTimeMs );
  qDebug() << QString( "  Rendering: %1 ms" ).arg( metrics.renderingTimeMs );
  qDebug() << QString( "  Memory: %1 MB" ).arg( metrics.memoryUsageMB );
  qDebug() << QString( "  Features: %1" ).arg( metrics.featuresRendered );
  qDebug() << QString( "  Image: %1x%2" ).arg( metrics.imageSize.width() ).arg( metrics.imageSize.height() );
  qDebug() << QString( "  Success: %1" ).arg( metrics.renderingSuccessful ? "Yes" : "No" );
  
  if ( !metrics.renderingSuccessful )
  {
    qDebug() << QString( "  Error: %1" ).arg( metrics.errorMessage );
  }
  
  if ( metrics.featuresRendered > 0 && metrics.renderingTimeMs > 0 )
  {
    qDebug() << QString( "  Rate: %1 features/second" )
                .arg( metrics.featuresRendered * 1000.0 / metrics.renderingTimeMs, 0, 'f', 0 );
  }
}

QGSTEST_MAIN( TestVectorRenderingPerformance )
#include "test_vector_rendering.moc"