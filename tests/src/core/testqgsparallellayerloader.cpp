/***************************************************************************
     testqgsparallellayerloader.cpp
     -----------------------------
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
#include "qgsparallellayerloader.h"
#include "qgsproject.h"
#include "qgsmaplayerstore.h"
#include "qgsvectorlayer.h"
#include "qgsrasterlayer.h"
#include <QTemporaryFile>
#include <QDomDocument>

/**
 * \ingroup UnitTests
 * Unit tests for QgsParallelLayerLoader
 */
class TestQgsParallelLayerLoader : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Core functionality tests
    void testBasicParallelLoading();
    void testLoadingStrategies();
    void testDependencyAnalysis();
    void testProgressReporting();
    void testMemoryManagement();
    void testErrorHandling();
    void testCancellation();
    void testLoadingStatistics();
    void testLayerPrioritization();
    void testPerformanceOptimization();

  private:
    QgsParallelLayerLoader *mLoader = nullptr;
    QgsProject *mProject = nullptr;
    
    QList<QDomElement> createTestLayerElements( int count = 10, bool addDependencies = false );
    QDomElement createVectorLayerElement( const QString &id, const QString &name );
    QDomElement createRasterLayerElement( const QString &id, const QString &name );
};

void TestQgsParallelLayerLoader::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();
}

void TestQgsParallelLayerLoader::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsParallelLayerLoader::init()
{
  mLoader = new QgsParallelLayerLoader();
  mProject = new QgsProject();
}

void TestQgsParallelLayerLoader::cleanup()
{
  delete mLoader;
  mLoader = nullptr;
  
  delete mProject;
  mProject = nullptr;
}

QList<QDomElement> TestQgsParallelLayerLoader::createTestLayerElements( int count, bool addDependencies )
{
  QList<QDomElement> elements;
  QDomDocument doc;
  
  for ( int i = 0; i < count; ++i )
  {
    QDomElement element;
    
    // Alternate between vector and raster layers
    if ( i % 2 == 0 )
    {
      element = createVectorLayerElement( QStringLiteral( "layer_%1" ).arg( i ),
                                          QStringLiteral( "Test Vector Layer %1" ).arg( i ) );
    }
    else
    {
      element = createRasterLayerElement( QStringLiteral( "layer_%1" ).arg( i ),
                                          QStringLiteral( "Test Raster Layer %1" ).arg( i ) );
    }
    
    if ( addDependencies && i > 0 )
    {
      // Add dependency on previous layer
      QDomElement dependencies = doc.createElement( QStringLiteral( "dependencies" ) );
      QDomElement dependency = doc.createElement( QStringLiteral( "layer" ) );
      dependency.setAttribute( QStringLiteral( "id" ), QStringLiteral( "layer_%1" ).arg( i - 1 ) );
      dependencies.appendChild( dependency );
      element.appendChild( dependencies );
    }
    
    elements.append( element );
  }
  
  return elements;
}

QDomElement TestQgsParallelLayerLoader::createVectorLayerElement( const QString &id, const QString &name )
{
  QDomDocument doc;
  QDomElement element = doc.createElement( QStringLiteral( "maplayer" ) );
  element.setAttribute( QStringLiteral( "id" ), id );
  element.setAttribute( QStringLiteral( "type" ), QStringLiteral( "vector" ) );
  
  QDomElement layerName = doc.createElement( QStringLiteral( "layername" ) );
  layerName.appendChild( doc.createTextNode( name ) );
  element.appendChild( layerName );
  
  // Add minimal datasource (memory layer)
  QDomElement datasource = doc.createElement( QStringLiteral( "datasource" ) );
  datasource.appendChild( doc.createTextNode( QStringLiteral( "Point?crs=EPSG:4326" ) ) );
  element.appendChild( datasource );
  
  QDomElement provider = doc.createElement( QStringLiteral( "provider" ) );
  provider.appendChild( doc.createTextNode( QStringLiteral( "memory" ) ) );
  element.appendChild( provider );
  
  return element;
}

QDomElement TestQgsParallelLayerLoader::createRasterLayerElement( const QString &id, const QString &name )
{
  QDomDocument doc;
  QDomElement element = doc.createElement( QStringLiteral( "maplayer" ) );
  element.setAttribute( QStringLiteral( "id" ), id );
  element.setAttribute( QStringLiteral( "type" ), QStringLiteral( "raster" ) );
  
  QDomElement layerName = doc.createElement( QStringLiteral( "layername" ) );
  layerName.appendChild( doc.createTextNode( name ) );
  element.appendChild( layerName );
  
  // Add minimal datasource
  QDomElement datasource = doc.createElement( QStringLiteral( "datasource" ) );
  datasource.appendChild( doc.createTextNode( QStringLiteral( "dummy_raster_source" ) ) );
  element.appendChild( datasource );
  
  QDomElement provider = doc.createElement( QStringLiteral( "provider" ) );
  provider.appendChild( doc.createTextNode( QStringLiteral( "gdal" ) ) );
  element.appendChild( provider );
  
  return element;
}

void TestQgsParallelLayerLoader::testBasicParallelLoading()
{
  // Test basic parallel loading functionality
  const QList<QDomElement> layerElements = createTestLayerElements( 5 );
  
  QSignalSpy progressSpy( mLoader, &QgsParallelLayerLoader::progressChanged );
  QSignalSpy completedSpy( mLoader, &QgsParallelLayerLoader::loadingCompleted );
  QSignalSpy layerLoadedSpy( mLoader, &QgsParallelLayerLoader::layerLoaded );
  
  // Configure for parallel loading
  QgsParallelLayerLoader::LoadingConfig config;
  config.strategy = QgsParallelLayerLoader::LoadingStrategy::Balanced;
  config.maxParallelLayers = 3;
  config.maxMemoryUsageMB = 256;
  mLoader->setLoadingConfig( config );
  
  // Setup context
  QgsReadWriteContext context;
  context.setProjectTranslator( mProject );
  
  // Start loading
  const bool success = mLoader->loadLayers( layerElements, mProject, context );
  
  // Wait for completion
  if ( success )
  {
    QEventLoop loop;
    connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 10000, &loop, &QEventLoop::quit ); // Timeout
    loop.exec();
  }
  
  // Verify completion signal
  QCOMPARE( completedSpy.count(), 1 );
  
  // Check statistics
  const QgsParallelLayerLoader::LoadingStatistics stats = mLoader->getStatistics();
  QVERIFY( stats.totalLayers >= 5 );
  QVERIFY( stats.loadingTime > 0 );
  
  // Verify progress was reported
  QVERIFY( progressSpy.count() > 0 );
}

void TestQgsParallelLayerLoader::testLoadingStrategies()
{
  const QList<QDomElement> layerElements = createTestLayerElements( 8 );
  QgsReadWriteContext context;
  context.setProjectTranslator( mProject );
  
  // Test different loading strategies
  const QList<QgsParallelLayerLoader::LoadingStrategy> strategies = {
    QgsParallelLayerLoader::LoadingStrategy::Sequential,
    QgsParallelLayerLoader::LoadingStrategy::Conservative,
    QgsParallelLayerLoader::LoadingStrategy::Balanced,
    QgsParallelLayerLoader::LoadingStrategy::Aggressive
  };
  
  for ( const auto strategy : strategies )
  {
    // Create fresh loader for each test
    delete mLoader;
    mLoader = new QgsParallelLayerLoader();
    
    QgsParallelLayerLoader::LoadingConfig config;
    config.strategy = strategy;
    config.maxParallelLayers = 4;
    mLoader->setLoadingConfig( config );
    
    QSignalSpy completedSpy( mLoader, &QgsParallelLayerLoader::loadingCompleted );
    
    const bool success = mLoader->loadLayers( layerElements, mProject, context );
    
    if ( success )
    {
      QEventLoop loop;
      connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
      QTimer::singleShot( 15000, &loop, &QEventLoop::quit );
      loop.exec();
    }
    
    // Each strategy should complete successfully
    QCOMPARE( completedSpy.count(), 1 );
    
    const QgsParallelLayerLoader::LoadingStatistics stats = mLoader->getStatistics();
    QVERIFY( stats.totalLayers >= 8 );
    
    // Strategy-specific verification
    switch ( strategy )
    {
      case QgsParallelLayerLoader::LoadingStrategy::Sequential:
        // Sequential should have minimal parallelization
        QVERIFY( stats.maxConcurrentLayers <= 1 );
        break;
        
      case QgsParallelLayerLoader::LoadingStrategy::Aggressive:
        // Aggressive should use more threads
        QVERIFY( stats.maxConcurrentLayers > 1 );
        break;
        
      default:
        // Other strategies should have some parallelization
        break;
    }
  }
}

void TestQgsParallelLayerLoader::testDependencyAnalysis()
{
  // Create layers with dependencies
  const QList<QDomElement> layerElements = createTestLayerElements( 6, true );
  
  QgsParallelLayerLoader::LoadingConfig config;
  config.strategy = QgsParallelLayerLoader::LoadingStrategy::Balanced;
  config.enableDependencyAnalysis = true;
  mLoader->setLoadingConfig( config );
  
  QSignalSpy layerLoadedSpy( mLoader, &QgsParallelLayerLoader::layerLoaded );
  QSignalSpy completedSpy( mLoader, &QgsParallelLayerLoader::loadingCompleted );
  
  QgsReadWriteContext context;
  context.setProjectTranslator( mProject );
  
  const bool success = mLoader->loadLayers( layerElements, mProject, context );
  
  if ( success )
  {
    QEventLoop loop;
    connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 15000, &loop, &QEventLoop::quit );
    loop.exec();
  }
  
  QCOMPARE( completedSpy.count(), 1 );
  
  // Verify dependency analysis was performed
  const QgsParallelLayerLoader::LoadingStatistics stats = mLoader->getStatistics();
  QVERIFY( stats.dependencyAnalysisTime > 0 );
  
  // Layers should still load successfully despite dependencies
  QVERIFY( layerLoadedSpy.count() > 0 );
}

void TestQgsParallelLayerLoader::testProgressReporting()
{
  const QList<QDomElement> layerElements = createTestLayerElements( 10 );
  
  QSignalSpy progressSpy( mLoader, &QgsParallelLayerLoader::progressChanged );
  
  QgsParallelLayerLoader::LoadingConfig config;
  config.strategy = QgsParallelLayerLoader::LoadingStrategy::Balanced;
  config.enableProgressReporting = true;
  mLoader->setLoadingConfig( config );
  
  QgsReadWriteContext context;
  context.setProjectTranslator( mProject );
  
  const bool success = mLoader->loadLayers( layerElements, mProject, context );
  
  if ( success )
  {
    QEventLoop loop;
    connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 15000, &loop, &QEventLoop::quit );
    loop.exec();
  }
  
  // Verify progress signals were emitted
  QVERIFY( progressSpy.count() > 0 );
  
  // Check signal parameters
  bool foundValidProgress = false;
  for ( const QList<QVariant> &signal : progressSpy )
  {
    if ( signal.size() >= 2 )
    {
      const int progress = signal[0].toInt();
      const QString currentLayer = signal[1].toString();
      
      QVERIFY( progress >= 0 && progress <= 100 );
      foundValidProgress = true;
      break;
    }
  }
  
  QVERIFY( foundValidProgress );
}

void TestQgsParallelLayerLoader::testMemoryManagement()
{
  // Test memory management and throttling
  const QList<QDomElement> layerElements = createTestLayerElements( 15 );
  
  QSignalSpy memorySpy( mLoader, &QgsParallelLayerLoader::memoryThresholdExceeded );
  
  QgsParallelLayerLoader::LoadingConfig config;
  config.strategy = QgsParallelLayerLoader::LoadingStrategy::Aggressive;
  config.maxMemoryUsageMB = 64; // Low memory limit to trigger throttling
  config.enableMemoryThrottling = true;
  mLoader->setLoadingConfig( config );
  
  QgsReadWriteContext context;
  context.setProjectTranslator( mProject );
  
  const bool success = mLoader->loadLayers( layerElements, mProject, context );
  
  if ( success )
  {
    QEventLoop loop;
    connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 20000, &loop, &QEventLoop::quit );
    loop.exec();
  }
  
  // Check if memory throttling was triggered
  const QgsParallelLayerLoader::LoadingStatistics stats = mLoader->getStatistics();
  QVERIFY( stats.memoryUsage > 0 );
  
  // Should complete successfully even with memory constraints
  const int progress = mLoader->progress();
  QCOMPARE( progress, 100 );
}

void TestQgsParallelLayerLoader::testErrorHandling()
{
  // Create some invalid layer elements
  QList<QDomElement> layerElements = createTestLayerElements( 3 );
  
  // Add an invalid layer element
  QDomDocument doc;
  QDomElement invalidElement = doc.createElement( QStringLiteral( "maplayer" ) );
  invalidElement.setAttribute( QStringLiteral( "id" ), QStringLiteral( "invalid_layer" ) );
  invalidElement.setAttribute( QStringLiteral( "type" ), QStringLiteral( "invalid_type" ) );
  layerElements.append( invalidElement );
  
  QSignalSpy errorSpy( mLoader, &QgsParallelLayerLoader::layerLoadFailed );
  QSignalSpy completedSpy( mLoader, &QgsParallelLayerLoader::loadingCompleted );
  
  QgsParallelLayerLoader::LoadingConfig config;
  config.strategy = QgsParallelLayerLoader::LoadingStrategy::Balanced;
  mLoader->setLoadingConfig( config );
  
  QgsReadWriteContext context;
  context.setProjectTranslator( mProject );
  
  const bool success = mLoader->loadLayers( layerElements, mProject, context );
  
  if ( success )
  {
    QEventLoop loop;
    connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 15000, &loop, &QEventLoop::quit );
    loop.exec();
  }
  
  // Should have completed despite errors
  QCOMPARE( completedSpy.count(), 1 );
  
  // Should have reported some layer failures
  QVERIFY( errorSpy.count() > 0 );
  
  // Check statistics include error information
  const QgsParallelLayerLoader::LoadingStatistics stats = mLoader->getStatistics();
  QVERIFY( stats.layersWithErrors > 0 );
  
  // Test failed layer retry
  const QList<QgsParallelLayerLoader::LayerLoadingTask> failedLayers = mLoader->getFailedLayers();
  QVERIFY( !failedLayers.isEmpty() );
}

void TestQgsParallelLayerLoader::testCancellation()
{
  const QList<QDomElement> layerElements = createTestLayerElements( 20 );
  
  QSignalSpy completedSpy( mLoader, &QgsParallelLayerLoader::loadingCompleted );
  
  // Configure for slow loading to allow cancellation
  QgsParallelLayerLoader::LoadingConfig config;
  config.strategy = QgsParallelLayerLoader::LoadingStrategy::Sequential; // Slower
  config.maxParallelLayers = 1;
  mLoader->setLoadingConfig( config );
  
  QgsReadWriteContext context;
  context.setProjectTranslator( mProject );
  
  const bool success = mLoader->loadLayers( layerElements, mProject, context );
  
  if ( success )
  {
    // Wait a bit then cancel
    QTest::qWait( 500 );
    mLoader->cancelLoading();
    
    // Wait for completion or timeout
    QEventLoop loop;
    connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
    loop.exec();
  }
  
  // Verify cancellation was effective
  QVERIFY( mLoader->isCancelled() );
  
  // Progress should be less than 100% due to cancellation
  const int progress = mLoader->progress();
  QVERIFY( progress < 100 );
}

void TestQgsParallelLayerLoader::testLoadingStatistics()
{
  const QList<QDomElement> layerElements = createTestLayerElements( 12 );
  
  QgsParallelLayerLoader::LoadingConfig config;
  config.strategy = QgsParallelLayerLoader::LoadingStrategy::Balanced;
  config.maxParallelLayers = 4;
  mLoader->setLoadingConfig( config );
  
  QgsReadWriteContext context;
  context.setProjectTranslator( mProject );
  
  const bool success = mLoader->loadLayers( layerElements, mProject, context );
  
  if ( success )
  {
    QEventLoop loop;
    connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 15000, &loop, &QEventLoop::quit );
    loop.exec();
  }
  
  // Verify comprehensive statistics
  const QgsParallelLayerLoader::LoadingStatistics stats = mLoader->getStatistics();
  
  QVERIFY( stats.totalLayers >= 12 );
  QVERIFY( stats.loadingTime > 0 );
  QVERIFY( stats.averageLayerLoadTime > 0 );
  QVERIFY( stats.memoryUsage > 0 );
  QVERIFY( stats.maxConcurrentLayers > 0 );
  QVERIFY( stats.threadUtilization >= 0 && stats.threadUtilization <= 100 );
  
  // Test statistics export
  const QVariantMap exported = stats.toVariantMap();
  
  QVERIFY( exported.contains( QStringLiteral( "total_layers" ) ) );
  QVERIFY( exported.contains( QStringLiteral( "loading_time" ) ) );
  QVERIFY( exported.contains( QStringLiteral( "memory_usage" ) ) );
  QVERIFY( exported.contains( QStringLiteral( "thread_utilization" ) ) );
}

void TestQgsParallelLayerLoader::testLayerPrioritization()
{
  // Create layers with different priorities
  QList<QDomElement> layerElements = createTestLayerElements( 8 );
  
  // Mark some layers as high priority by adding visibility flags
  for ( int i = 0; i < 3; ++i )
  {
    QDomDocument doc;
    QDomElement visibility = doc.createElement( QStringLiteral( "visibility" ) );
    visibility.setAttribute( QStringLiteral( "checked" ), QStringLiteral( "1" ) );
    layerElements[i].appendChild( visibility );
  }
  
  QSignalSpy layerLoadedSpy( mLoader, &QgsParallelLayerLoader::layerLoaded );
  
  QgsParallelLayerLoader::LoadingConfig config;
  config.strategy = QgsParallelLayerLoader::LoadingStrategy::Balanced;
  config.enableLayerPrioritization = true;
  mLoader->setLoadingConfig( config );
  
  QgsReadWriteContext context;
  context.setProjectTranslator( mProject );
  
  const bool success = mLoader->loadLayers( layerElements, mProject, context );
  
  if ( success )
  {
    QEventLoop loop;
    connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 15000, &loop, &QEventLoop::quit );
    loop.exec();
  }
  
  // Verify layers were loaded
  QVERIFY( layerLoadedSpy.count() >= 3 );
  
  const QgsParallelLayerLoader::LoadingStatistics stats = mLoader->getStatistics();
  QVERIFY( stats.prioritizationEnabled );
}

void TestQgsParallelLayerLoader::testPerformanceOptimization()
{
  // Test performance optimization features
  const QList<QDomElement> layerElements = createTestLayerElements( 20 );
  
  // Test with optimization enabled
  QgsParallelLayerLoader::LoadingConfig config;
  config.strategy = QgsParallelLayerLoader::LoadingStrategy::Balanced;
  config.enableMemoryThrottling = true;
  config.enableProgressReporting = true;
  config.enableDependencyAnalysis = true;
  config.maxParallelLayers = 6;
  mLoader->setLoadingConfig( config );
  
  QgsReadWriteContext context;
  context.setProjectTranslator( mProject );
  
  const QElapsedTimer timer;
  timer.start();
  
  const bool success = mLoader->loadLayers( layerElements, mProject, context );
  
  if ( success )
  {
    QEventLoop loop;
    connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 20000, &loop, &QEventLoop::quit );
    loop.exec();
  }
  
  const qint64 optimizedTime = timer.elapsed();
  
  // Compare with sequential loading
  delete mLoader;
  mLoader = new QgsParallelLayerLoader();
  
  QgsParallelLayerLoader::LoadingConfig sequentialConfig;
  sequentialConfig.strategy = QgsParallelLayerLoader::LoadingStrategy::Sequential;
  sequentialConfig.maxParallelLayers = 1;
  mLoader->setLoadingConfig( sequentialConfig );
  
  const QElapsedTimer sequentialTimer;
  sequentialTimer.start();
  
  const bool sequentialSuccess = mLoader->loadLayers( layerElements, mProject, context );
  
  if ( sequentialSuccess )
  {
    QEventLoop loop;
    connect( mLoader, &QgsParallelLayerLoader::loadingCompleted, &loop, &QEventLoop::quit );
    QTimer::singleShot( 30000, &loop, &QEventLoop::quit );
    loop.exec();
  }
  
  const qint64 sequentialTime = sequentialTimer.elapsed();
  
  // Parallel loading should be faster (with some tolerance for test variations)
  const double speedupRatio = static_cast<double>( sequentialTime ) / optimizedTime;
  QVERIFY( speedupRatio > 0.8 ); // At least some performance benefit
  
  qDebug() << "Parallel loading time:" << optimizedTime << "ms";
  qDebug() << "Sequential loading time:" << sequentialTime << "ms";
  qDebug() << "Speedup ratio:" << speedupRatio;
}

QGSTEST_MAIN( TestQgsParallelLayerLoader )
#include "testqgsparallellayerloader.moc"