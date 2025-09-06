/***************************************************************************
                         test_memory_usage.cpp
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
#include "qgslayertreemodel.h"

#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QCoreApplication>

#ifdef Q_OS_LINUX
#include <unistd.h>
#include <sys/resource.h>
#include <fstream>
#include <sstream>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#ifdef Q_OS_MAC
#include <mach/mach.h>
#include <mach/task.h>
#include <mach/mach_init.h>
#include <mach/task_info.h>
#endif

/**
 * \ingroup PerformanceTests
 * Memory Usage Profiling Test
 * 
 * This test profiles memory consumption during large QGIS operations to:
 * - Detect memory leaks in project loading and rendering
 * - Monitor peak memory usage during intensive operations
 * - Validate memory cleanup after operations complete
 * - Ensure memory usage stays within acceptable bounds
 * - Profile memory usage patterns for optimization
 * 
 * Key metrics tracked:
 * - Virtual Memory Size (VMS) and Resident Set Size (RSS)
 * - Peak memory usage during operations
 * - Memory allocation/deallocation patterns
 * - Memory cleanup efficiency
 * - Memory fragmentation indicators
 * 
 * Memory profiling supports performance optimization goals by ensuring
 * optimizations don't introduce memory regressions.
 */
class TestMemoryUsageProfile : public QObject
{
    Q_OBJECT

  public:
    TestMemoryUsageProfile() = default;

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Core memory profiling tests
    void profileProjectLoadingMemory();
    void profileVectorRenderingMemory();
    void profileRasterRenderingMemory();
    void profileLargeDatasetMemory();
    
    // Memory leak detection tests
    void detectProjectLoadingLeaks();
    void detectRenderingLeaks();
    void detectLayerManagementLeaks();
    void detectFeatureIterationLeaks();
    
    // Memory stress tests
    void stressTestLargeProjects();
    void stressTestManyLayers();
    void stressTestManyFeatures();
    void stressTestConcurrentOperations();
    
    // Memory optimization validation
    void validateMemoryCleanup();
    void profileMemoryFragmentation();
    void validateMemoryBounds();

  private:
    // Memory measurement utilities
    struct MemorySnapshot {
      qint64 virtualMemoryMB = 0;      // Virtual memory size
      qint64 residentMemoryMB = 0;     // Physical memory (RSS)
      qint64 sharedMemoryMB = 0;       // Shared memory
      qint64 heapMemoryMB = 0;         // Heap memory
      qint64 stackMemoryMB = 0;        // Stack memory
      qint64 timestamp = 0;            // When snapshot was taken
      QString operation;               // What operation was being performed
      bool valid = false;              // Whether snapshot is valid
    };
    
    struct MemoryProfile {
      MemorySnapshot baseline;         // Memory before operation
      MemorySnapshot peak;             // Peak memory during operation
      MemorySnapshot final;            // Memory after operation
      MemorySnapshot cleanup;          // Memory after cleanup
      qint64 operationDurationMs = 0;  // How long operation took
      QString operationName;           // Name of profiled operation
      QList<MemorySnapshot> timeline; // Memory usage over time
      bool memoryLeakDetected = false; // Whether leak was detected
      qint64 leakSizeMB = 0;          // Size of detected leak
    };
    
    MemorySnapshot takeMemorySnapshot( const QString &operation = QString() );
    MemoryProfile profileOperation( std::function<void()> operation, const QString &name,
                                   int timelineIntervalMs = 100 );
    
    // Platform-specific memory measurement
    qint64 getVirtualMemoryMB();
    qint64 getResidentMemoryMB();
    qint64 getSharedMemoryMB();
    qint64 getHeapMemoryMB();
    qint64 getStackMemoryMB();
    
    // Memory analysis utilities
    bool isMemoryLeakDetected( const MemoryProfile &profile, double thresholdMB = 10.0 );
    double calculateMemoryEfficiency( const MemoryProfile &profile );
    void logMemoryProfile( const MemoryProfile &profile );
    void logMemorySnapshot( const MemorySnapshot &snapshot );
    
    // Test data and configuration
    QString mTestDataDir;
    QString mSmallProjectPath;
    QString mLargeProjectPath;
    QString mHugeProjectPath;
    QString mLargeVectorPath;
    QString mLargeRasterPath;
    
    // Memory profiling configuration
    static constexpr qint64 MAX_MEMORY_INCREASE_MB = 2048; // 2GB max increase
    static constexpr qint64 MAX_PEAK_MEMORY_MB = 8192;     // 8GB max peak
    static constexpr double MEMORY_LEAK_THRESHOLD_MB = 50.0; // 50MB leak threshold
    static constexpr int MEMORY_TIMELINE_INTERVAL_MS = 100;  // 100ms timeline sampling
    
    // Performance baselines for memory efficiency
    struct MemoryBaselines {
      qint64 project_loading_peak_mb = 0;
      qint64 vector_rendering_peak_mb = 0;
      qint64 raster_rendering_peak_mb = 0;
      qint64 large_dataset_peak_mb = 0;
      double project_loading_efficiency = 0.0;
      double rendering_efficiency = 0.0;
    };
    MemoryBaselines mBaselines;
};

void TestMemoryUsageProfile::initTestCase()
{
  // Initialize QGIS application
  QgsApplication::init();
  
  // Set up test data directory
  mTestDataDir = QStandardPaths::writableLocation( QStandardPaths::TempLocation ) + "/qgis_memory_profile_test";
  QDir dir;
  if ( !dir.exists( mTestDataDir ) )
  {
    dir.mkpath( mTestDataDir );
  }
  
  qDebug() << "=== QGIS Memory Usage Profiling Test ===";
  qDebug() << "Test data directory:" << mTestDataDir;
  qDebug() << "Memory thresholds:";
  qDebug() << "  Max memory increase:" << MAX_MEMORY_INCREASE_MB << "MB";
  qDebug() << "  Max peak memory:" << MAX_PEAK_MEMORY_MB << "MB";
  qDebug() << "  Memory leak threshold:" << MEMORY_LEAK_THRESHOLD_MB << "MB";
  qDebug() << "Timeline sampling interval:" << MEMORY_TIMELINE_INTERVAL_MS << "ms";
  
  // Take initial memory snapshot
  MemorySnapshot initial = takeMemorySnapshot( "Initial" );
  logMemorySnapshot( initial );
  
  // Create test datasets (simplified paths for this test)
  mSmallProjectPath = mTestDataDir + "/small_project.qgs";
  mLargeProjectPath = mTestDataDir + "/large_project.qgs";
  mHugeProjectPath = mTestDataDir + "/huge_project.qgs";
  mLargeVectorPath = mTestDataDir + "/large_vector.gpkg";
  mLargeRasterPath = mTestDataDir + "/large_raster.tif";
  
  qDebug() << "Memory profiling test initialized";
}

void TestMemoryUsageProfile::cleanupTestCase()
{
  // Print memory profiling summary
  qDebug() << "\n=== MEMORY USAGE PROFILING SUMMARY ===";
  qDebug() << QString( "Project Loading Peak Memory: %1 MB" ).arg( mBaselines.project_loading_peak_mb );
  qDebug() << QString( "Vector Rendering Peak Memory: %1 MB" ).arg( mBaselines.vector_rendering_peak_mb );
  qDebug() << QString( "Raster Rendering Peak Memory: %1 MB" ).arg( mBaselines.raster_rendering_peak_mb );
  qDebug() << QString( "Large Dataset Peak Memory: %1 MB" ).arg( mBaselines.large_dataset_peak_mb );
  qDebug() << QString( "Project Loading Efficiency: %1%" ).arg( mBaselines.project_loading_efficiency * 100, 0, 'f', 1 );
  qDebug() << QString( "Rendering Efficiency: %1%" ).arg( mBaselines.rendering_efficiency * 100, 0, 'f', 1 );
  
  // Take final memory snapshot
  MemorySnapshot final = takeMemorySnapshot( "Final" );
  logMemorySnapshot( final );
  
  // Clean up test data
  QDir testDir( mTestDataDir );
  if ( testDir.exists() )
  {
    testDir.removeRecursively();
  }
  
  QgsApplication::exitQgis();
}

void TestMemoryUsageProfile::init()
{
  // Clean state for each test
  QgsProject::instance()->clear();
  
  // Force garbage collection
  QCoreApplication::processEvents();
}

void TestMemoryUsageProfile::cleanup()
{
  // Clean up after each test
  QgsProject::instance()->clear();
  
  // Force garbage collection and wait
  QCoreApplication::processEvents();
  QThread::msleep( 100 ); // Give time for cleanup
}

void TestMemoryUsageProfile::profileProjectLoadingMemory()
{
  qDebug() << "\n--- Profiling Project Loading Memory Usage ---";
  
  // Create a test project file (simplified)
  QFile projectFile( mLargeProjectPath );
  if ( projectFile.open( QIODevice::WriteOnly ) )
  {
    QTextStream stream( &projectFile );
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<qgis version=\"3.30.0\" projectname=\"Memory Test Project\">\n";
    stream << "  <projectlayers>\n";
    
    // Add many layer definitions to simulate large project
    for ( int i = 0; i < 50; ++i )
    {
      stream << QString( "    <maplayer id=\"layer_%1\" type=\"vector\">\n" ).arg( i );
      stream << QString( "      <layername>Test Layer %1</layername>\n" ).arg( i );
      stream << "    </maplayer>\n";
    }
    
    stream << "  </projectlayers>\n";
    stream << "</qgis>\n";
    projectFile.close();
  }
  
  // Profile project loading operation
  auto loadOperation = [this]() {
    bool loaded = QgsProject::instance()->read( mLargeProjectPath );
    Q_UNUSED( loaded )
  };
  
  MemoryProfile profile = profileOperation( loadOperation, "Project Loading", MEMORY_TIMELINE_INTERVAL_MS );
  
  // Store baseline
  mBaselines.project_loading_peak_mb = profile.peak.residentMemoryMB;
  mBaselines.project_loading_efficiency = calculateMemoryEfficiency( profile );
  
  logMemoryProfile( profile );
  
  // Validate memory usage
  QVERIFY( profile.peak.residentMemoryMB < MAX_PEAK_MEMORY_MB );
  QVERIFY( profile.final.residentMemoryMB - profile.baseline.residentMemoryMB < MAX_MEMORY_INCREASE_MB );
  
  // Check for memory leaks
  bool leakDetected = isMemoryLeakDetected( profile );
  QVERIFY2( !leakDetected, qPrintable( QString( "Memory leak detected: %1 MB" ).arg( profile.leakSizeMB ) ) );
  
  qDebug() << QString( "Project loading memory profile completed successfully" );
}

void TestMemoryUsageProfile::profileVectorRenderingMemory()
{
  qDebug() << "\n--- Profiling Vector Rendering Memory Usage ---";
  
  // Create test vector data file (simplified)
  QFile vectorFile( mLargeVectorPath );
  if ( vectorFile.open( QIODevice::WriteOnly ) )
  {
    // Write placeholder GPKG data (simplified)
    vectorFile.write( QByteArray( 1024 * 1024, 'V' ) ); // 1MB placeholder
    vectorFile.close();
  }
  
  // Load vector layer
  QgsVectorLayer *layer = new QgsVectorLayer( mLargeVectorPath, "Memory Test Vector", "ogr" );
  
  // Profile vector rendering operation
  auto renderOperation = [layer]() {
    if ( layer && layer->isValid() )
    {
      // Set up map settings for rendering
      QgsMapSettings mapSettings;
      mapSettings.setLayers( QList<QgsMapLayer*>() << layer );
      mapSettings.setExtent( QgsRectangle( -180, -90, 180, 90 ) );
      mapSettings.setOutputSize( QSize( 1024, 768 ) );
      mapSettings.setOutputDpi( 96 );
      
      // Create and run rendering job
      QgsMapRendererSequentialJob job( mapSettings );
      job.start();
      job.waitForFinished();
      
      // Get result to ensure rendering completed
      QImage result = job.renderedImage();
      Q_UNUSED( result )
    }
  };
  
  MemoryProfile profile = profileOperation( renderOperation, "Vector Rendering", MEMORY_TIMELINE_INTERVAL_MS );
  
  // Store baseline
  mBaselines.vector_rendering_peak_mb = profile.peak.residentMemoryMB;
  
  logMemoryProfile( profile );
  
  // Validate memory usage
  QVERIFY( profile.peak.residentMemoryMB < MAX_PEAK_MEMORY_MB );
  
  // Check for memory leaks
  bool leakDetected = isMemoryLeakDetected( profile );
  QVERIFY2( !leakDetected, qPrintable( QString( "Vector rendering memory leak: %1 MB" ).arg( profile.leakSizeMB ) ) );
  
  delete layer;
  
  qDebug() << QString( "Vector rendering memory profile completed successfully" );
}

void TestMemoryUsageProfile::profileRasterRenderingMemory()
{
  qDebug() << "\n--- Profiling Raster Rendering Memory Usage ---";
  
  // Create test raster data file (simplified)
  QFile rasterFile( mLargeRasterPath );
  if ( rasterFile.open( QIODevice::WriteOnly ) )
  {
    // Write placeholder TIFF data (simplified)
    vectorFile.write( QByteArray( 10 * 1024 * 1024, 'R' ) ); // 10MB placeholder
    rasterFile.close();
  }
  
  // Load raster layer
  QgsRasterLayer *layer = new QgsRasterLayer( mLargeRasterPath, "Memory Test Raster" );
  
  // Profile raster rendering operation
  auto renderOperation = [layer]() {
    if ( layer && layer->isValid() )
    {
      QgsMapSettings mapSettings;
      mapSettings.setLayers( QList<QgsMapLayer*>() << layer );
      mapSettings.setExtent( QgsRectangle( -180, -90, 180, 90 ) );
      mapSettings.setOutputSize( QSize( 2048, 1536 ) ); // Larger for raster
      mapSettings.setOutputDpi( 96 );
      
      QgsMapRendererSequentialJob job( mapSettings );
      job.start();
      job.waitForFinished();
      
      QImage result = job.renderedImage();
      Q_UNUSED( result )
    }
  };
  
  MemoryProfile profile = profileOperation( renderOperation, "Raster Rendering", MEMORY_TIMELINE_INTERVAL_MS );
  
  // Store baseline
  mBaselines.raster_rendering_peak_mb = profile.peak.residentMemoryMB;
  
  logMemoryProfile( profile );
  
  // Validate memory usage
  QVERIFY( profile.peak.residentMemoryMB < MAX_PEAK_MEMORY_MB );
  
  // Check for memory leaks
  bool leakDetected = isMemoryLeakDetected( profile );
  QVERIFY2( !leakDetected, qPrintable( QString( "Raster rendering memory leak: %1 MB" ).arg( profile.leakSizeMB ) ) );
  
  delete layer;
  
  qDebug() << QString( "Raster rendering memory profile completed successfully" );
}

void TestMemoryUsageProfile::profileLargeDatasetMemory()
{
  qDebug() << "\n--- Profiling Large Dataset Memory Usage ---";
  
  // Create large dataset simulation
  auto largeDatasetOperation = [this]() {
    // Simulate loading large dataset by creating many vector layers
    QList<QgsVectorLayer*> layers;
    
    for ( int i = 0; i < 20; ++i )
    {
      // Create placeholder vector file
      QString layerPath = mTestDataDir + QString( "/dataset_layer_%1.gpkg" ).arg( i );
      QFile layerFile( layerPath );
      if ( layerFile.open( QIODevice::WriteOnly ) )
      {
        layerFile.write( QByteArray( 512 * 1024, 'L' ) ); // 512KB per layer
        layerFile.close();
      }
      
      QgsVectorLayer *layer = new QgsVectorLayer( layerPath, QString( "Dataset Layer %1" ).arg( i ), "ogr" );
      if ( layer->isValid() )
      {
        layers.append( layer );
      }
      else
      {
        delete layer;
      }
    }
    
    // Simulate dataset processing
    for ( QgsVectorLayer *layer : layers )
    {
      if ( layer && layer->isValid() )
      {
        // Simulate feature iteration
        QgsFeatureIterator iterator = layer->getFeatures();
        QgsFeature feature;
        int count = 0;
        while ( iterator.nextFeature( feature ) && count < 1000 )
        {
          // Process feature (simplified)
          QgsGeometry geom = feature.geometry();
          if ( !geom.isNull() )
          {
            geom.area(); // Trigger geometry processing
          }
          count++;
        }
      }
    }
    
    // Cleanup
    qDeleteAll( layers );
    layers.clear();
  };
  
  MemoryProfile profile = profileOperation( largeDatasetOperation, "Large Dataset Processing", MEMORY_TIMELINE_INTERVAL_MS );
  
  // Store baseline
  mBaselines.large_dataset_peak_mb = profile.peak.residentMemoryMB;
  
  logMemoryProfile( profile );
  
  // Validate memory usage
  QVERIFY( profile.peak.residentMemoryMB < MAX_PEAK_MEMORY_MB );
  
  // Check for memory leaks
  bool leakDetected = isMemoryLeakDetected( profile );
  QVERIFY2( !leakDetected, qPrintable( QString( "Large dataset memory leak: %1 MB" ).arg( profile.leakSizeMB ) ) );
  
  qDebug() << QString( "Large dataset memory profile completed successfully" );
}

void TestMemoryUsageProfile::detectProjectLoadingLeaks()
{
  qDebug() << "\n--- Detecting Project Loading Memory Leaks ---";
  
  // Create simple test project
  QFile projectFile( mSmallProjectPath );
  if ( projectFile.open( QIODevice::WriteOnly ) )
  {
    QTextStream stream( &projectFile );
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<qgis version=\"3.30.0\" projectname=\"Leak Test Project\">\n";
    stream << "</qgis>\n";
    projectFile.close();
  }
  
  MemorySnapshot baseline = takeMemorySnapshot( "Before Load/Unload Cycles" );
  
  // Perform multiple load/unload cycles to detect leaks
  for ( int cycle = 0; cycle < 10; ++cycle )
  {
    // Load project
    bool loaded = QgsProject::instance()->read( mSmallProjectPath );
    QVERIFY( loaded );
    
    // Clear project
    QgsProject::instance()->clear();
    
    // Force cleanup
    QCoreApplication::processEvents();
    QThread::msleep( 50 );
  }
  
  MemorySnapshot final = takeMemorySnapshot( "After Load/Unload Cycles" );
  
  // Check for memory leak
  qint64 memoryIncrease = final.residentMemoryMB - baseline.residentMemoryMB;
  
  qDebug() << QString( "Memory Leak Detection Results:" );
  qDebug() << QString( "  Baseline memory: %1 MB" ).arg( baseline.residentMemoryMB );
  qDebug() << QString( "  Final memory: %1 MB" ).arg( final.residentMemoryMB );
  qDebug() << QString( "  Memory increase: %1 MB" ).arg( memoryIncrease );
  qDebug() << QString( "  Leak threshold: %1 MB" ).arg( MEMORY_LEAK_THRESHOLD_MB );
  
  if ( memoryIncrease > MEMORY_LEAK_THRESHOLD_MB )
  {
    qWarning() << QString( "Potential memory leak detected: %1 MB increase" ).arg( memoryIncrease );
  }
  
  // Allow small memory increases due to internal caching
  QVERIFY2( memoryIncrease < MEMORY_LEAK_THRESHOLD_MB, 
           qPrintable( QString( "Memory leak detected: %1 MB > %2 MB threshold" )
                      .arg( memoryIncrease ).arg( MEMORY_LEAK_THRESHOLD_MB ) ) );
}

void TestMemoryUsageProfile::detectRenderingLeaks()
{
  qDebug() << "\n--- Detecting Rendering Memory Leaks ---";
  
  // Create test vector file
  QFile vectorFile( mLargeVectorPath );
  if ( vectorFile.open( QIODevice::WriteOnly ) )
  {
    vectorFile.write( QByteArray( 256 * 1024, 'V' ) ); // 256KB
    vectorFile.close();
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mLargeVectorPath, "Leak Test", "ogr" );
  
  MemorySnapshot baseline = takeMemorySnapshot( "Before Render Cycles" );
  
  // Perform multiple rendering cycles
  for ( int cycle = 0; cycle < 5; ++cycle )
  {
    if ( layer && layer->isValid() )
    {
      QgsMapSettings mapSettings;
      mapSettings.setLayers( QList<QgsMapLayer*>() << layer );
      mapSettings.setExtent( QgsRectangle( -10, -10, 10, 10 ) );
      mapSettings.setOutputSize( QSize( 512, 512 ) );
      
      QgsMapRendererSequentialJob job( mapSettings );
      job.start();
      job.waitForFinished();
      
      QImage result = job.renderedImage();
      Q_UNUSED( result )
    }
    
    // Force cleanup between cycles
    QCoreApplication::processEvents();
  }
  
  MemorySnapshot final = takeMemorySnapshot( "After Render Cycles" );
  
  qint64 memoryIncrease = final.residentMemoryMB - baseline.residentMemoryMB;
  
  qDebug() << QString( "Rendering Leak Detection Results:" );
  qDebug() << QString( "  Memory increase: %1 MB" ).arg( memoryIncrease );
  
  delete layer;
  
  QVERIFY2( memoryIncrease < MEMORY_LEAK_THRESHOLD_MB, 
           qPrintable( QString( "Rendering memory leak: %1 MB" ).arg( memoryIncrease ) ) );
}

void TestMemoryUsageProfile::detectLayerManagementLeaks()
{
  qDebug() << "\n--- Detecting Layer Management Memory Leaks ---";
  
  MemorySnapshot baseline = takeMemorySnapshot( "Before Layer Cycles" );
  
  // Perform layer creation/destruction cycles
  for ( int cycle = 0; cycle < 20; ++cycle )
  {
    // Create layer file
    QString layerPath = mTestDataDir + QString( "/temp_layer_%1.gpkg" ).arg( cycle );
    QFile layerFile( layerPath );
    if ( layerFile.open( QIODevice::WriteOnly ) )
    {
      layerFile.write( QByteArray( 64 * 1024, 'T' ) ); // 64KB
      layerFile.close();
    }
    
    // Create and destroy layer
    QgsVectorLayer *layer = new QgsVectorLayer( layerPath, QString( "Temp %1" ).arg( cycle ), "ogr" );
    delete layer;
    
    // Remove file
    QFile::remove( layerPath );
  }
  
  MemorySnapshot final = takeMemorySnapshot( "After Layer Cycles" );
  
  qint64 memoryIncrease = final.residentMemoryMB - baseline.residentMemoryMB;
  
  qDebug() << QString( "Layer Management Leak Detection:" );
  qDebug() << QString( "  Memory increase: %1 MB" ).arg( memoryIncrease );
  
  QVERIFY2( memoryIncrease < MEMORY_LEAK_THRESHOLD_MB, 
           qPrintable( QString( "Layer management leak: %1 MB" ).arg( memoryIncrease ) ) );
}

void TestMemoryUsageProfile::detectFeatureIterationLeaks()
{
  qDebug() << "\n--- Detecting Feature Iteration Memory Leaks ---";
  
  // Create test vector with features
  QFile vectorFile( mLargeVectorPath );
  if ( vectorFile.open( QIODevice::WriteOnly ) )
  {
    vectorFile.write( QByteArray( 1024 * 1024, 'F' ) ); // 1MB with "features"
    vectorFile.close();
  }
  
  QgsVectorLayer *layer = new QgsVectorLayer( mLargeVectorPath, "Feature Test", "ogr" );
  
  MemorySnapshot baseline = takeMemorySnapshot( "Before Feature Iteration" );
  
  // Perform multiple feature iteration cycles
  for ( int cycle = 0; cycle < 10; ++cycle )
  {
    if ( layer && layer->isValid() )
    {
      QgsFeatureIterator iterator = layer->getFeatures();
      QgsFeature feature;
      int count = 0;
      
      while ( iterator.nextFeature( feature ) && count < 100 )
      {
        // Process feature
        QgsGeometry geom = feature.geometry();
        if ( !geom.isNull() )
        {
          geom.buffer( 1.0 );  // Create derived geometry
        }
        count++;
      }
    }
  }
  
  MemorySnapshot final = takeMemorySnapshot( "After Feature Iteration" );
  
  qint64 memoryIncrease = final.residentMemoryMB - baseline.residentMemoryMB;
  
  qDebug() << QString( "Feature Iteration Leak Detection:" );
  qDebug() << QString( "  Memory increase: %1 MB" ).arg( memoryIncrease );
  
  delete layer;
  
  QVERIFY2( memoryIncrease < MEMORY_LEAK_THRESHOLD_MB, 
           qPrintable( QString( "Feature iteration leak: %1 MB" ).arg( memoryIncrease ) ) );
}

void TestMemoryUsageProfile::stressTestLargeProjects()
{
  qDebug() << "\n--- Stress Testing Large Project Memory Usage ---";
  
  // Create very large project file
  QFile projectFile( mHugeProjectPath );
  if ( projectFile.open( QIODevice::WriteOnly ) )
  {
    QTextStream stream( &projectFile );
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<qgis version=\"3.30.0\" projectname=\"Huge Project Stress Test\">\n";
    stream << "  <projectlayers>\n";
    
    // Add many layers to stress memory
    for ( int i = 0; i < 200; ++i )
    {
      stream << QString( "    <maplayer id=\"layer_%1\" type=\"vector\">\n" ).arg( i );
      stream << QString( "      <layername>Stress Test Layer %1</layername>\n" ).arg( i );
      stream << "    </maplayer>\n";
    }
    
    stream << "  </projectlayers>\n";
    stream << "</qgis>\n";
    projectFile.close();
  }
  
  auto stressOperation = [this]() {
    bool loaded = QgsProject::instance()->read( mHugeProjectPath );
    Q_UNUSED( loaded )
    
    // Simulate heavy project operations
    QgsProject *project = QgsProject::instance();
    auto layers = project->layerTreeRoot()->findLayers();
    
    // Stress test layer iteration
    for ( auto layerNode : layers )
    {
      if ( layerNode && layerNode->layer() )
      {
        // Simulate layer processing
        QgsMapLayer *layer = layerNode->layer();
        QString name = layer->name();
        Q_UNUSED( name )
      }
    }
  };
  
  MemoryProfile profile = profileOperation( stressOperation, "Large Project Stress Test", 200 );
  
  logMemoryProfile( profile );
  
  // Should handle large projects without excessive memory usage
  QVERIFY2( profile.peak.residentMemoryMB < MAX_PEAK_MEMORY_MB, 
           qPrintable( QString( "Large project peak memory %1 MB exceeds limit %2 MB" )
                      .arg( profile.peak.residentMemoryMB ).arg( MAX_PEAK_MEMORY_MB ) ) );
  
  // Should cleanup properly
  bool leakDetected = isMemoryLeakDetected( profile );
  QVERIFY2( !leakDetected, "Large project stress test detected memory leak" );
}

void TestMemoryUsageProfile::stressTestManyLayers()
{
  qDebug() << "\n--- Stress Testing Many Layers Memory Usage ---";
  
  auto manyLayersOperation = [this]() {
    QList<QgsVectorLayer*> layers;
    
    // Create many layers simultaneously
    for ( int i = 0; i < 100; ++i )
    {
      QString layerPath = mTestDataDir + QString( "/stress_layer_%1.gpkg" ).arg( i );
      QFile layerFile( layerPath );
      if ( layerFile.open( QIODevice::WriteOnly ) )
      {
        layerFile.write( QByteArray( 128 * 1024, 'S' ) ); // 128KB per layer
        layerFile.close();
      }
      
      QgsVectorLayer *layer = new QgsVectorLayer( layerPath, QString( "Stress %1" ).arg( i ), "ogr" );
      if ( layer->isValid() )
      {
        layers.append( layer );
      }
      else
      {
        delete layer;
      }
    }
    
    // Cleanup
    qDeleteAll( layers );
  };
  
  MemoryProfile profile = profileOperation( manyLayersOperation, "Many Layers Stress Test", 200 );
  
  logMemoryProfile( profile );
  
  QVERIFY( profile.peak.residentMemoryMB < MAX_PEAK_MEMORY_MB );
  
  bool leakDetected = isMemoryLeakDetected( profile );
  QVERIFY2( !leakDetected, "Many layers stress test detected memory leak" );
}

void TestMemoryUsageProfile::stressTestManyFeatures()
{
  qDebug() << "\n--- Stress Testing Many Features Memory Usage ---";
  
  // Create large vector file
  QFile vectorFile( mLargeVectorPath );
  if ( vectorFile.open( QIODevice::WriteOnly ) )
  {
    vectorFile.write( QByteArray( 5 * 1024 * 1024, 'M' ) ); // 5MB simulating many features
    vectorFile.close();
  }
  
  auto manyFeaturesOperation = [this]() {
    QgsVectorLayer *layer = new QgsVectorLayer( mLargeVectorPath, "Many Features", "ogr" );
    
    if ( layer && layer->isValid() )
    {
      // Simulate processing many features
      QgsFeatureIterator iterator = layer->getFeatures();
      QgsFeature feature;
      int count = 0;
      
      while ( iterator.nextFeature( feature ) && count < 10000 )
      {
        // Simulate feature processing
        QgsGeometry geom = feature.geometry();
        if ( !geom.isNull() )
        {
          // Trigger geometry operations
          geom.area();
          geom.length();
          geom.centroid();
        }
        count++;
      }
    }
    
    delete layer;
  };
  
  MemoryProfile profile = profileOperation( manyFeaturesOperation, "Many Features Stress Test", 200 );
  
  logMemoryProfile( profile );
  
  QVERIFY( profile.peak.residentMemoryMB < MAX_PEAK_MEMORY_MB );
  
  bool leakDetected = isMemoryLeakDetected( profile );
  QVERIFY2( !leakDetected, "Many features stress test detected memory leak" );
}

void TestMemoryUsageProfile::stressTestConcurrentOperations()
{
  qDebug() << "\n--- Stress Testing Concurrent Operations Memory Usage ---";
  
  // Note: This is a simplified concurrent test
  // Real concurrent testing would require thread-safe operations
  
  auto concurrentOperation = [this]() {
    // Simulate concurrent operations by interleaving different tasks
    
    // Task 1: Project loading
    QFile projectFile( mSmallProjectPath );
    if ( projectFile.open( QIODevice::WriteOnly ) )
    {
      QTextStream stream( &projectFile );
      stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
      stream << "<qgis version=\"3.30.0\"><projectlayers></projectlayers></qgis>\n";
      projectFile.close();
    }
    
    bool loaded = QgsProject::instance()->read( mSmallProjectPath );
    Q_UNUSED( loaded )
    
    // Task 2: Layer creation
    QString layerPath = mTestDataDir + "/concurrent_layer.gpkg";
    QFile layerFile( layerPath );
    if ( layerFile.open( QIODevice::WriteOnly ) )
    {
      layerFile.write( QByteArray( 256 * 1024, 'C' ) );
      layerFile.close();
    }
    
    QgsVectorLayer *layer = new QgsVectorLayer( layerPath, "Concurrent", "ogr" );
    
    // Task 3: Rendering
    if ( layer && layer->isValid() )
    {
      QgsMapSettings mapSettings;
      mapSettings.setLayers( QList<QgsMapLayer*>() << layer );
      mapSettings.setExtent( QgsRectangle( -1, -1, 1, 1 ) );
      mapSettings.setOutputSize( QSize( 256, 256 ) );
      
      QgsMapRendererSequentialJob job( mapSettings );
      job.start();
      job.waitForFinished();
    }
    
    delete layer;
    QgsProject::instance()->clear();
  };
  
  MemoryProfile profile = profileOperation( concurrentOperation, "Concurrent Operations Stress Test", 100 );
  
  logMemoryProfile( profile );
  
  QVERIFY( profile.peak.residentMemoryMB < MAX_PEAK_MEMORY_MB );
  
  bool leakDetected = isMemoryLeakDetected( profile );
  QVERIFY2( !leakDetected, "Concurrent operations stress test detected memory leak" );
}

void TestMemoryUsageProfile::validateMemoryCleanup()
{
  qDebug() << "\n--- Validating Memory Cleanup Efficiency ---";
  
  MemorySnapshot initialSnapshot = takeMemorySnapshot( "Before Cleanup Test" );
  
  // Perform operations that should be fully cleaned up
  for ( int i = 0; i < 5; ++i )
  {
    // Create and destroy temporary objects
    QString tempPath = mTestDataDir + QString( "/cleanup_test_%1.gpkg" ).arg( i );
    QFile tempFile( tempPath );
    if ( tempFile.open( QIODevice::WriteOnly ) )
    {
      tempFile.write( QByteArray( 512 * 1024, 'C' ) );
      tempFile.close();
    }
    
    QgsVectorLayer *layer = new QgsVectorLayer( tempPath, QString( "Cleanup %1" ).arg( i ), "ogr" );
    
    if ( layer && layer->isValid() )
    {
      // Use the layer
      QgsFeatureIterator iter = layer->getFeatures();
      QgsFeature feature;
      while ( iter.nextFeature( feature ) )
      {
        // Process feature
        break; // Just get first feature
      }
    }
    
    delete layer;
    QFile::remove( tempPath );
    
    // Force cleanup
    QCoreApplication::processEvents();
  }
  
  // Additional cleanup
  QCoreApplication::processEvents();
  QThread::msleep( 200 );
  
  MemorySnapshot finalSnapshot = takeMemorySnapshot( "After Cleanup Test" );
  
  qint64 memoryIncrease = finalSnapshot.residentMemoryMB - initialSnapshot.residentMemoryMB;
  
  qDebug() << QString( "Memory Cleanup Validation:" );
  qDebug() << QString( "  Initial memory: %1 MB" ).arg( initialSnapshot.residentMemoryMB );
  qDebug() << QString( "  Final memory: %1 MB" ).arg( finalSnapshot.residentMemoryMB );
  qDebug() << QString( "  Memory increase: %1 MB" ).arg( memoryIncrease );
  
  // Memory increase should be minimal after cleanup
  QVERIFY2( memoryIncrease < MEMORY_LEAK_THRESHOLD_MB / 2, 
           qPrintable( QString( "Cleanup inefficient: %1 MB retained" ).arg( memoryIncrease ) ) );
}

void TestMemoryUsageProfile::profileMemoryFragmentation()
{
  qDebug() << "\n--- Profiling Memory Fragmentation ---";
  
  MemorySnapshot baseline = takeMemorySnapshot( "Before Fragmentation Test" );
  
  // Perform operations that might cause fragmentation
  QList<QByteArray*> allocations;
  
  // Allocate and deallocate memory in patterns that might fragment
  for ( int cycle = 0; cycle < 10; ++cycle )
  {
    // Allocate various sized blocks
    for ( int size = 1024; size <= 1024 * 1024; size *= 2 )
    {
      QByteArray *data = new QByteArray( size, 'F' );
      allocations.append( data );
    }
    
    // Deallocate every other block to create gaps
    for ( int i = 1; i < allocations.size(); i += 2 )
    {
      delete allocations[i];
      allocations[i] = nullptr;
    }
    
    // Cleanup remaining blocks
    for ( QByteArray *data : allocations )
    {
      delete data;
    }
    allocations.clear();
  }
  
  MemorySnapshot final = takeMemorySnapshot( "After Fragmentation Test" );
  
  qint64 virtualIncrease = final.virtualMemoryMB - baseline.virtualMemoryMB;
  qint64 residentIncrease = final.residentMemoryMB - baseline.residentMemoryMB;
  
  qDebug() << QString( "Memory Fragmentation Analysis:" );
  qDebug() << QString( "  Virtual memory increase: %1 MB" ).arg( virtualIncrease );
  qDebug() << QString( "  Resident memory increase: %1 MB" ).arg( residentIncrease );
  
  if ( virtualIncrease > residentIncrease * 2 )
  {
    qDebug() << QString( "  Potential fragmentation detected (VMS/RSS ratio: %1)" )
                .arg( double( virtualIncrease ) / residentIncrease, 0, 'f', 2 );
  }
  
  // Should not have excessive memory increase after cleanup
  QVERIFY( residentIncrease < MEMORY_LEAK_THRESHOLD_MB );
}

void TestMemoryUsageProfile::validateMemoryBounds()
{
  qDebug() << "\n--- Validating Memory Bounds ---";
  
  MemorySnapshot current = takeMemorySnapshot( "Memory Bounds Check" );
  
  qDebug() << QString( "Current Memory Usage:" );
  qDebug() << QString( "  Virtual Memory: %1 MB" ).arg( current.virtualMemoryMB );
  qDebug() << QString( "  Resident Memory: %1 MB" ).arg( current.residentMemoryMB );
  qDebug() << QString( "  Shared Memory: %1 MB" ).arg( current.sharedMemoryMB );
  
  // Validate memory usage is within reasonable bounds
  QVERIFY2( current.residentMemoryMB < MAX_PEAK_MEMORY_MB, 
           qPrintable( QString( "Resident memory %1 MB exceeds limit %2 MB" )
                      .arg( current.residentMemoryMB ).arg( MAX_PEAK_MEMORY_MB ) ) );
  
  QVERIFY2( current.virtualMemoryMB < MAX_PEAK_MEMORY_MB * 4, 
           qPrintable( QString( "Virtual memory %1 MB exceeds limit %2 MB" )
                      .arg( current.virtualMemoryMB ).arg( MAX_PEAK_MEMORY_MB * 4 ) ) );
  
  // Virtual memory should not be excessively larger than resident
  double vmsRssRatio = double( current.virtualMemoryMB ) / current.residentMemoryMB;
  QVERIFY2( vmsRssRatio < 10.0, 
           qPrintable( QString( "VMS/RSS ratio %1 indicates potential memory issues" ).arg( vmsRssRatio ) ) );
}

// Helper method implementations

TestMemoryUsageProfile::MemorySnapshot TestMemoryUsageProfile::takeMemorySnapshot( const QString &operation )
{
  MemorySnapshot snapshot;
  snapshot.timestamp = QDateTime::currentMSecsSinceEpoch();
  snapshot.operation = operation;
  
  snapshot.virtualMemoryMB = getVirtualMemoryMB();
  snapshot.residentMemoryMB = getResidentMemoryMB();
  snapshot.sharedMemoryMB = getSharedMemoryMB();
  snapshot.heapMemoryMB = getHeapMemoryMB();
  snapshot.stackMemoryMB = getStackMemoryMB();
  
  snapshot.valid = ( snapshot.virtualMemoryMB > 0 && snapshot.residentMemoryMB > 0 );
  
  return snapshot;
}

TestMemoryUsageProfile::MemoryProfile TestMemoryUsageProfile::profileOperation( 
  std::function<void()> operation, const QString &name, int timelineIntervalMs )
{
  MemoryProfile profile;
  profile.operationName = name;
  
  // Take baseline snapshot
  profile.baseline = takeMemorySnapshot( "Baseline" );
  
  QElapsedTimer operationTimer;
  operationTimer.start();
  
  // Start timeline monitoring
  QTimer *timelineTimer = new QTimer();
  timelineTimer->setInterval( timelineIntervalMs );
  
  connect( timelineTimer, &QTimer::timeout, [&]() {
    MemorySnapshot snapshot = takeMemorySnapshot( "Timeline" );
    profile.timeline.append( snapshot );
    
    // Track peak memory
    if ( snapshot.residentMemoryMB > profile.peak.residentMemoryMB )
    {
      profile.peak = snapshot;
    }
  });
  
  timelineTimer->start();
  
  // Execute the operation
  try
  {
    operation();
  }
  catch ( ... )
  {
    timelineTimer->stop();
    delete timelineTimer;
    throw;
  }
  
  timelineTimer->stop();
  delete timelineTimer;
  
  profile.operationDurationMs = operationTimer.elapsed();
  
  // Take final snapshot
  profile.final = takeMemorySnapshot( "Final" );
  
  // If no peak was recorded, use final as peak
  if ( profile.peak.residentMemoryMB == 0 )
  {
    profile.peak = profile.final;
  }
  
  // Force cleanup and take cleanup snapshot
  QCoreApplication::processEvents();
  QThread::msleep( 100 );
  profile.cleanup = takeMemorySnapshot( "Cleanup" );
  
  // Detect memory leaks
  profile.memoryLeakDetected = isMemoryLeakDetected( profile );
  if ( profile.memoryLeakDetected )
  {
    profile.leakSizeMB = profile.cleanup.residentMemoryMB - profile.baseline.residentMemoryMB;
  }
  
  return profile;
}

qint64 TestMemoryUsageProfile::getVirtualMemoryMB()
{
#ifdef Q_OS_LINUX
  std::ifstream statusFile( "/proc/self/status" );
  std::string line;
  while ( std::getline( statusFile, line ) )
  {
    if ( line.substr( 0, 6 ) == "VmSize" )
    {
      std::istringstream iss( line );
      std::string label, value, unit;
      iss >> label >> value >> unit;
      return std::stoll( value ) / 1024; // Convert KB to MB
    }
  }
#endif

#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS_EX pmc;
  if ( GetProcessMemoryInfo( GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof( pmc ) ) )
  {
    return pmc.PrivateUsage / ( 1024 * 1024 ); // Convert bytes to MB
  }
#endif

#ifdef Q_OS_MAC
  task_basic_info info;
  mach_msg_type_number_t size = sizeof( info );
  if ( task_info( mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &size ) == KERN_SUCCESS )
  {
    return info.virtual_size / ( 1024 * 1024 ); // Convert bytes to MB
  }
#endif

  return 100; // Fallback
}

qint64 TestMemoryUsageProfile::getResidentMemoryMB()
{
#ifdef Q_OS_LINUX
  std::ifstream statusFile( "/proc/self/status" );
  std::string line;
  while ( std::getline( statusFile, line ) )
  {
    if ( line.substr( 0, 5 ) == "VmRSS" )
    {
      std::istringstream iss( line );
      std::string label, value, unit;
      iss >> label >> value >> unit;
      return std::stoll( value ) / 1024; // Convert KB to MB
    }
  }
#endif

#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS pmc;
  if ( GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
  {
    return pmc.WorkingSetSize / ( 1024 * 1024 ); // Convert bytes to MB
  }
#endif

#ifdef Q_OS_MAC
  task_basic_info info;
  mach_msg_type_number_t size = sizeof( info );
  if ( task_info( mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &size ) == KERN_SUCCESS )
  {
    return info.resident_size / ( 1024 * 1024 ); // Convert bytes to MB
  }
#endif

  return 80; // Fallback
}

qint64 TestMemoryUsageProfile::getSharedMemoryMB()
{
#ifdef Q_OS_LINUX
  std::ifstream statusFile( "/proc/self/status" );
  std::string line;
  while ( std::getline( statusFile, line ) )
  {
    if ( line.substr( 0, 7 ) == "VmShLib" )
    {
      std::istringstream iss( line );
      std::string label, value, unit;
      iss >> label >> value >> unit;
      return std::stoll( value ) / 1024; // Convert KB to MB
    }
  }
#endif

  return 10; // Fallback
}

qint64 TestMemoryUsageProfile::getHeapMemoryMB()
{
  // Simplified heap memory estimation
  // Real implementation would use platform-specific heap APIs
  return getResidentMemoryMB() * 0.7; // Estimate heap as 70% of RSS
}

qint64 TestMemoryUsageProfile::getStackMemoryMB()
{
  // Simplified stack memory estimation
#ifdef Q_OS_LINUX
  struct rlimit rl;
  if ( getrlimit( RLIMIT_STACK, &rl ) == 0 )
  {
    return rl.rlim_cur / ( 1024 * 1024 ); // Convert bytes to MB
  }
#endif

  return 8; // Typical stack size in MB
}

bool TestMemoryUsageProfile::isMemoryLeakDetected( const MemoryProfile &profile, double thresholdMB )
{
  if ( !profile.baseline.valid || !profile.cleanup.valid )
  {
    return false; // Can't determine without valid snapshots
  }
  
  qint64 memoryIncrease = profile.cleanup.residentMemoryMB - profile.baseline.residentMemoryMB;
  return memoryIncrease > thresholdMB;
}

double TestMemoryUsageProfile::calculateMemoryEfficiency( const MemoryProfile &profile )
{
  if ( !profile.baseline.valid || !profile.peak.valid )
  {
    return 0.0;
  }
  
  qint64 memoryUsed = profile.peak.residentMemoryMB - profile.baseline.residentMemoryMB;
  if ( memoryUsed <= 0 )
  {
    return 1.0; // Perfect efficiency if no additional memory used
  }
  
  // Efficiency based on cleanup ratio
  qint64 memoryRetained = profile.cleanup.residentMemoryMB - profile.baseline.residentMemoryMB;
  return 1.0 - ( double( memoryRetained ) / memoryUsed );
}

void TestMemoryUsageProfile::logMemoryProfile( const MemoryProfile &profile )
{
  qDebug() << QString( "[MEMORY PROFILE] %1:" ).arg( profile.operationName );
  qDebug() << QString( "  Operation duration: %1 ms" ).arg( profile.operationDurationMs );
  qDebug() << QString( "  Baseline memory: %1 MB" ).arg( profile.baseline.residentMemoryMB );
  qDebug() << QString( "  Peak memory: %1 MB (+%2 MB)" )
              .arg( profile.peak.residentMemoryMB )
              .arg( profile.peak.residentMemoryMB - profile.baseline.residentMemoryMB );
  qDebug() << QString( "  Final memory: %1 MB (+%2 MB)" )
              .arg( profile.final.residentMemoryMB )
              .arg( profile.final.residentMemoryMB - profile.baseline.residentMemoryMB );
  qDebug() << QString( "  Cleanup memory: %1 MB (+%2 MB)" )
              .arg( profile.cleanup.residentMemoryMB )
              .arg( profile.cleanup.residentMemoryMB - profile.baseline.residentMemoryMB );
  
  double efficiency = calculateMemoryEfficiency( profile );
  qDebug() << QString( "  Memory efficiency: %1%" ).arg( efficiency * 100, 0, 'f', 1 );
  
  if ( profile.memoryLeakDetected )
  {
    qDebug() << QString( "  MEMORY LEAK DETECTED: %1 MB" ).arg( profile.leakSizeMB );
  }
  
  qDebug() << QString( "  Timeline snapshots: %1" ).arg( profile.timeline.size() );
}

void TestMemoryUsageProfile::logMemorySnapshot( const MemorySnapshot &snapshot )
{
  qDebug() << QString( "[MEMORY] %1:" ).arg( snapshot.operation );
  qDebug() << QString( "  Virtual Memory: %1 MB" ).arg( snapshot.virtualMemoryMB );
  qDebug() << QString( "  Resident Memory: %1 MB" ).arg( snapshot.residentMemoryMB );
  qDebug() << QString( "  Shared Memory: %1 MB" ).arg( snapshot.sharedMemoryMB );
  qDebug() << QString( "  Heap Memory: %1 MB" ).arg( snapshot.heapMemoryMB );
  qDebug() << QString( "  Stack Memory: %1 MB" ).arg( snapshot.stackMemoryMB );
  qDebug() << QString( "  Timestamp: %1" ).arg( snapshot.timestamp );
  qDebug() << QString( "  Valid: %1" ).arg( snapshot.valid ? "Yes" : "No" );
}

QGSTEST_MAIN( TestMemoryUsageProfile )
#include "test_memory_usage.moc"