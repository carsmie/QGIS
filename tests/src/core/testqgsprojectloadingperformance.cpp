/***************************************************************************
     testqgsprojectloadingperformance.cpp
     -----------------------------------
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
#include "qgsprojectloadingperformance.h"
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>

/**
 * \ingroup UnitTests
 * Unit tests for QgsProjectLoadingPerformance
 */
class TestQgsProjectLoadingPerformance : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Basic functionality tests
    void testBasicTimingCollection();
    void testComponentTiming();
    void testMemoryTracking();
    void testStatisticsCalculation();
    void testPerformanceBaseline();
    void testJsonExport();
    void testProgressReporting();
    void testRegressionDetection();
    void testFileIOPerformance();
    void testStressTest();

  private:
    QgsProjectLoadingPerformance *mPerformanceMonitor = nullptr;
};

void TestQgsProjectLoadingPerformance::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();
}

void TestQgsProjectLoadingPerformance::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsProjectLoadingPerformance::init()
{
  mPerformanceMonitor = new QgsProjectLoadingPerformance();
}

void TestQgsProjectLoadingPerformance::cleanup()
{
  delete mPerformanceMonitor;
  mPerformanceMonitor = nullptr;
}

void TestQgsProjectLoadingPerformance::testBasicTimingCollection()
{
  QVERIFY( mPerformanceMonitor );
  
  // Test basic timing operations
  mPerformanceMonitor->startTiming( QStringLiteral( "test_operation" ) );
  
  // Simulate some work
  QTest::qWait( 100 );
  
  const qint64 elapsed = mPerformanceMonitor->endTiming( QStringLiteral( "test_operation" ) );
  
  QVERIFY( elapsed >= 90 ); // Should be at least 90ms
  QVERIFY( elapsed <= 200 ); // Should not exceed 200ms (with some tolerance)
  
  // Test retrieving timing
  const qint64 retrievedTime = mPerformanceMonitor->getTimingDuration( QStringLiteral( "test_operation" ) );
  QCOMPARE( retrievedTime, elapsed );
}

void TestQgsProjectLoadingPerformance::testComponentTiming()
{
  // Test multiple component timings
  const QStringList components = {
    QStringLiteral( "xml_parsing" ),
    QStringLiteral( "layer_creation" ),
    QStringLiteral( "style_loading" ),
    QStringLiteral( "tree_setup" )
  };
  
  for ( const QString &component : components )
  {
    mPerformanceMonitor->startTiming( component );
    QTest::qWait( 50 ); // Simulate work
    mPerformanceMonitor->endTiming( component );
  }
  
  // Verify all components were recorded
  for ( const QString &component : components )
  {
    const qint64 duration = mPerformanceMonitor->getTimingDuration( component );
    QVERIFY( duration > 0 );
    QVERIFY( duration >= 40 ); // At least 40ms
  }
  
  // Test total timing calculation
  const qint64 totalTime = mPerformanceMonitor->getTotalLoadingTime();
  QVERIFY( totalTime >= 160 ); // At least sum of individual times
}

void TestQgsProjectLoadingPerformance::testMemoryTracking()
{
  // Test memory usage tracking
  const qint64 initialMemory = mPerformanceMonitor->getCurrentMemoryUsage();
  QVERIFY( initialMemory > 0 );
  
  // Record memory usage
  mPerformanceMonitor->recordMemoryUsage( QStringLiteral( "baseline" ) );
  
  // Get recorded memory
  const qint64 recordedMemory = mPerformanceMonitor->getMemoryUsage( QStringLiteral( "baseline" ) );
  QVERIFY( recordedMemory > 0 );
  
  // Test peak memory tracking
  const qint64 peakMemory = mPerformanceMonitor->getPeakMemoryUsage();
  QVERIFY( peakMemory >= initialMemory );
}

void TestQgsProjectLoadingPerformance::testStatisticsCalculation()
{
  // Setup some timing data
  mPerformanceMonitor->startTiming( QStringLiteral( "component1" ) );
  QTest::qWait( 100 );
  mPerformanceMonitor->endTiming( QStringLiteral( "component1" ) );
  
  mPerformanceMonitor->startTiming( QStringLiteral( "component2" ) );
  QTest::qWait( 150 );
  mPerformanceMonitor->endTiming( QStringLiteral( "component2" ) );
  
  // Get statistics
  const QgsProjectLoadingPerformance::LoadingStatistics stats = mPerformanceMonitor->getStatistics();
  
  QVERIFY( stats.totalLoadingTime >= 250 );
  QCOMPARE( stats.componentCount, 2 );
  QVERIFY( stats.averageComponentTime > 0 );
  QVERIFY( stats.memoryUsage > 0 );
  QVERIFY( stats.loadingStartTime.isValid() );
}

void TestQgsProjectLoadingPerformance::testPerformanceBaseline()
{
  // Test baseline creation and comparison
  mPerformanceMonitor->startTiming( QStringLiteral( "baseline_test" ) );
  QTest::qWait( 100 );
  mPerformanceMonitor->endTiming( QStringLiteral( "baseline_test" ) );
  
  // Create baseline
  mPerformanceMonitor->createPerformanceBaseline();
  
  // Verify baseline was created
  QVERIFY( mPerformanceMonitor->hasPerformanceBaseline() );
  
  // Test comparison
  mPerformanceMonitor->startTiming( QStringLiteral( "comparison_test" ) );
  QTest::qWait( 200 ); // Intentionally slower
  mPerformanceMonitor->endTiming( QStringLiteral( "comparison_test" ) );
  
  // This should detect a regression since the second test is slower
  const bool hasRegression = mPerformanceMonitor->detectPerformanceRegression();
  QVERIFY( hasRegression );
}

void TestQgsProjectLoadingPerformance::testJsonExport()
{
  // Setup some test data
  mPerformanceMonitor->startTiming( QStringLiteral( "export_test" ) );
  QTest::qWait( 50 );
  mPerformanceMonitor->endTiming( QStringLiteral( "export_test" ) );
  
  mPerformanceMonitor->recordMemoryUsage( QStringLiteral( "export_memory" ) );
  
  // Export to JSON
  const QVariantMap exported = mPerformanceMonitor->exportToJson();
  
  // Verify required fields
  QVERIFY( exported.contains( QStringLiteral( "total_loading_time" ) ) );
  QVERIFY( exported.contains( QStringLiteral( "component_timings" ) ) );
  QVERIFY( exported.contains( QStringLiteral( "memory_usage" ) ) );
  QVERIFY( exported.contains( QStringLiteral( "loading_start_time" ) ) );
  
  // Verify component timing data
  const QVariantMap componentTimings = exported[QStringLiteral( "component_timings" )].toMap();
  QVERIFY( componentTimings.contains( QStringLiteral( "export_test" ) ) );
  QVERIFY( componentTimings[QStringLiteral( "export_test" )].toLongLong() > 0 );
  
  // Test JSON serialization
  const QJsonDocument doc = QJsonDocument::fromVariant( exported );
  QVERIFY( !doc.isNull() );
  QVERIFY( doc.isObject() );
}

void TestQgsProjectLoadingPerformance::testProgressReporting()
{
  QSignalSpy progressSpy( mPerformanceMonitor, &QgsProjectLoadingPerformance::progressChanged );
  QSignalSpy componentSpy( mPerformanceMonitor, &QgsProjectLoadingPerformance::componentCompleted );
  
  // Enable progress reporting
  mPerformanceMonitor->setProgressReportingEnabled( true );
  
  // Perform operations that should trigger progress signals
  mPerformanceMonitor->startTiming( QStringLiteral( "progress_test1" ) );
  mPerformanceMonitor->endTiming( QStringLiteral( "progress_test1" ) );
  
  mPerformanceMonitor->startTiming( QStringLiteral( "progress_test2" ) );
  mPerformanceMonitor->endTiming( QStringLiteral( "progress_test2" ) );
  
  // Verify signals were emitted
  QVERIFY( componentSpy.count() >= 2 );
  
  // Check signal parameters
  const QList<QVariant> firstSignal = componentSpy.first();
  QCOMPARE( firstSignal.size(), 2 );
  QCOMPARE( firstSignal.first().toString(), QStringLiteral( "progress_test1" ) );
  QVERIFY( firstSignal.last().toLongLong() > 0 );
}

void TestQgsProjectLoadingPerformance::testRegressionDetection()
{
  // Create initial baseline
  mPerformanceMonitor->startTiming( QStringLiteral( "baseline_operation" ) );
  QTest::qWait( 100 );
  mPerformanceMonitor->endTiming( QStringLiteral( "baseline_operation" ) );
  mPerformanceMonitor->createPerformanceBaseline();
  
  // Test normal performance (should not trigger regression)
  mPerformanceMonitor->startTiming( QStringLiteral( "normal_operation" ) );
  QTest::qWait( 90 ); // Slightly faster
  mPerformanceMonitor->endTiming( QStringLiteral( "normal_operation" ) );
  
  QVERIFY( !mPerformanceMonitor->detectPerformanceRegression() );
  
  // Test performance regression (significantly slower)
  mPerformanceMonitor->startTiming( QStringLiteral( "slow_operation" ) );
  QTest::qWait( 300 ); // Much slower
  mPerformanceMonitor->endTiming( QStringLiteral( "slow_operation" ) );
  
  QVERIFY( mPerformanceMonitor->detectPerformanceRegression() );
}

void TestQgsProjectLoadingPerformance::testFileIOPerformance()
{
  // Test performance monitoring with actual file operations
  QTemporaryFile tempFile;
  QVERIFY( tempFile.open() );
  
  // Write test data
  const QString testData = QStringLiteral( "Test performance data for QGIS project loading" ).repeated( 1000 );
  tempFile.write( testData.toUtf8() );
  tempFile.close();
  
  // Monitor file reading performance
  mPerformanceMonitor->startTiming( QStringLiteral( "file_read" ) );
  
  QFile file( tempFile.fileName() );
  QVERIFY( file.open( QIODevice::ReadOnly ) );
  const QByteArray readData = file.readAll();
  file.close();
  
  const qint64 readTime = mPerformanceMonitor->endTiming( QStringLiteral( "file_read" ) );
  
  QVERIFY( readTime > 0 );
  QVERIFY( !readData.isEmpty() );
  
  // Test memory usage during file operations
  const qint64 memoryAfterRead = mPerformanceMonitor->getCurrentMemoryUsage();
  QVERIFY( memoryAfterRead > 0 );
}

void TestQgsProjectLoadingPerformance::testStressTest()
{
  // Test performance monitoring under stress
  const int numOperations = 100;
  QStringList operationNames;
  
  // Create many concurrent timing operations
  for ( int i = 0; i < numOperations; ++i )
  {
    const QString opName = QStringLiteral( "stress_test_%1" ).arg( i );
    operationNames.append( opName );
    
    mPerformanceMonitor->startTiming( opName );
    
    // Simulate varying work loads
    QTest::qWait( i % 10 + 1 );
    
    mPerformanceMonitor->endTiming( opName );
  }
  
  // Verify all operations were recorded
  for ( const QString &opName : operationNames )
  {
    const qint64 duration = mPerformanceMonitor->getTimingDuration( opName );
    QVERIFY( duration > 0 );
  }
  
  // Test statistics with many operations
  const QgsProjectLoadingPerformance::LoadingStatistics stats = mPerformanceMonitor->getStatistics();
  QCOMPARE( stats.componentCount, numOperations );
  QVERIFY( stats.totalLoadingTime > 0 );
  QVERIFY( stats.averageComponentTime > 0 );
  
  // Test JSON export with large dataset
  const QVariantMap exported = mPerformanceMonitor->exportToJson();
  const QVariantMap componentTimings = exported[QStringLiteral( "component_timings" )].toMap();
  QCOMPARE( componentTimings.size(), numOperations );
  
  // Verify memory usage is reasonable
  const qint64 currentMemory = mPerformanceMonitor->getCurrentMemoryUsage();
  QVERIFY( currentMemory > 0 );
  QVERIFY( currentMemory < 1024 * 1024 * 1024 ); // Should be less than 1GB
}

QGSTEST_MAIN( TestQgsProjectLoadingPerformance )
#include "testqgsprojectloadingperformance.moc"