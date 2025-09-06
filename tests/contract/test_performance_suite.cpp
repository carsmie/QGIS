/***************************************************************************
                         test_performance_suite.cpp
                         ----------------------------
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
#include <QObject>
#include <QString>
#include <QList>
#include <QElapsedTimer>
#include <QSignalSpy>

// Performance testing framework classes that MUST be implemented
// These classes don't exist yet - the test MUST FAIL initially
#include "qgsperformancetestsuite.h"
#include "qgsperformancebenchmark.h"
#include "qgsperformancereport.h"
#include "qgsperformancemetrics.h"

/**
 * \ingroup UnitTests
 * Contract test for PerformanceTestSuite
 * 
 * This test validates the performance testing framework interface contract.
 * It MUST FAIL initially because the classes are not implemented yet.
 * 
 * Tests cover:
 * - Test suite orchestration and execution
 * - Benchmark registration and management
 * - Performance metrics collection
 * - Report generation and formatting
 * - Regression detection and analysis
 * - CI/CD integration capabilities
 */
class TestPerformanceTestSuite : public QObject
{
    Q_OBJECT

  public:
    TestPerformanceTestSuite() = default;

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Core framework contract tests
    void testSuiteClassExists();
    void testBenchmarkRegistration();
    void testMetricsCollection();
    void testReportGeneration();
    void testRegressionDetection();
    
    // Benchmark execution tests
    void testRunSingleBenchmark();
    void testRunBenchmarkSuite();
    void testBenchmarkTimeout();
    void testBenchmarkCancellation();
    
    // Metrics and analysis tests
    void testPerformanceMetrics();
    void testStatisticalAnalysis();
    void testBaselineComparison();
    void testTrendAnalysis();
    
    // Integration tests
    void testCIPipelineIntegration();
    void testReportExport();
    void testConfigurationManagement();

  private:
    QgsPerformanceTestSuite *mTestSuite = nullptr;
    QgsPerformanceBenchmark *mSampleBenchmark = nullptr;
    QString mTestOutputDir;
    QString mBaselineFile;
};

void TestPerformanceTestSuite::initTestCase()
{
  // Initialize test environment
  mTestOutputDir = QStringLiteral( "/tmp/qgis_performance_tests" );
  mBaselineFile = QStringLiteral( "/tmp/performance_baseline.json" );
  
  // Create output directory
  QDir dir;
  if ( !dir.exists( mTestOutputDir ) )
  {
    dir.mkpath( mTestOutputDir );
  }
  
  // Create sample baseline file
  QFile baseline( mBaselineFile );
  if ( baseline.open( QIODevice::WriteOnly ) )
  {
    QTextStream stream( &baseline );
    stream << "{\n";
    stream << "  \"project_loading_100mb\": {\n";
    stream << "    \"mean_time_ms\": 5000,\n";
    stream << "    \"std_deviation\": 500,\n";
    stream << "    \"sample_count\": 10\n";
    stream << "  },\n";
    stream << "  \"fgb_rendering_5pct\": {\n";
    stream << "    \"mean_time_ms\": 100,\n";
    stream << "    \"std_deviation\": 15,\n";
    stream << "    \"sample_count\": 20\n";
    stream << "  }\n";
    stream << "}\n";
    baseline.close();
  }
}

void TestPerformanceTestSuite::cleanupTestCase()
{
  // Clean up test files
  QDir dir( mTestOutputDir );
  if ( dir.exists() )
  {
    dir.removeRecursively();
  }
  QFile::remove( mBaselineFile );
}

void TestPerformanceTestSuite::init()
{
  // This will FAIL initially because QgsPerformanceTestSuite doesn't exist
  mTestSuite = new QgsPerformanceTestSuite();
  
  // Initialize test suite with output directory
  mTestSuite->setOutputDirectory( mTestOutputDir );
  mTestSuite->setBaselineFile( mBaselineFile );
  
  // Create sample benchmark
  mSampleBenchmark = new QgsPerformanceBenchmark( "sample_test" );
}

void TestPerformanceTestSuite::cleanup()
{
  delete mSampleBenchmark;
  mSampleBenchmark = nullptr;
  
  delete mTestSuite;
  mTestSuite = nullptr;
}

void TestPerformanceTestSuite::testSuiteClassExists()
{
  // Contract: QgsPerformanceTestSuite class must exist
  QVERIFY( mTestSuite != nullptr );
  
  // Contract: Should inherit from QObject for signal/slot support
  QObject *obj = dynamic_cast<QObject*>( mTestSuite );
  QVERIFY( obj != nullptr );
  
  // Contract: Must have essential configuration methods
  QVERIFY( mTestSuite->outputDirectory() == mTestOutputDir );
  QVERIFY( mTestSuite->baselineFile() == mBaselineFile );
  
  // Contract: Should provide benchmark management interface
  QVERIFY( mTestSuite->benchmarkCount() >= 0 );
  QVERIFY( mTestSuite->registeredBenchmarks().isEmpty() ); // Initially empty
}

void TestPerformanceTestSuite::testBenchmarkRegistration()
{
  // Contract: Must be able to register and manage benchmarks
  QVERIFY( mTestSuite != nullptr );
  QVERIFY( mSampleBenchmark != nullptr );
  
  // Register benchmark
  bool registered = mTestSuite->registerBenchmark( mSampleBenchmark );
  QVERIFY( registered );
  
  // Verify registration
  QCOMPARE( mTestSuite->benchmarkCount(), 1 );
  QVERIFY( mTestSuite->registeredBenchmarks().contains( "sample_test" ) );
  
  // Test duplicate registration
  QgsPerformanceBenchmark *duplicateBenchmark = new QgsPerformanceBenchmark( "sample_test" );
  bool duplicateRegistered = mTestSuite->registerBenchmark( duplicateBenchmark );
  QVERIFY( !duplicateRegistered ); // Should reject duplicates
  
  // Unregister benchmark
  bool unregistered = mTestSuite->unregisterBenchmark( "sample_test" );
  QVERIFY( unregistered );
  QCOMPARE( mTestSuite->benchmarkCount(), 0 );
  
  delete duplicateBenchmark;
}

void TestPerformanceTestSuite::testMetricsCollection()
{
  // Contract: Must collect comprehensive performance metrics
  QVERIFY( mTestSuite != nullptr );
  
  // Register sample benchmark
  mTestSuite->registerBenchmark( mSampleBenchmark );
  
  // Execute benchmark to collect metrics
  QgsPerformanceReport report = mTestSuite->runBenchmark( "sample_test" );
  QVERIFY( report.isValid() );
  
  // Verify metrics collection
  QgsPerformanceMetrics metrics = report.metrics();
  QVERIFY( metrics.isValid() );
  
  // Contract: Must collect timing metrics
  QVERIFY( metrics.hasExecutionTime() );
  QVERIFY( metrics.executionTime() >= 0 );
  
  // Contract: Must collect memory metrics
  QVERIFY( metrics.hasMemoryUsage() );
  QVERIFY( metrics.peakMemoryUsage() >= 0 );
  
  // Contract: Must collect system metrics
  QVERIFY( metrics.hasCpuUsage() );
  QVERIFY( metrics.averageCpuUsage() >= 0.0 );
  QVERIFY( metrics.averageCpuUsage() <= 100.0 );
  
  // Contract: Must provide statistical analysis
  QVERIFY( metrics.hasStatistics() );
  QVERIFY( metrics.sampleCount() > 0 );
  QVERIFY( metrics.standardDeviation() >= 0.0 );
}

void TestPerformanceTestSuite::testReportGeneration()
{
  // Contract: Must generate comprehensive performance reports
  QVERIFY( mTestSuite != nullptr );
  
  // Register and run benchmark
  mTestSuite->registerBenchmark( mSampleBenchmark );
  QgsPerformanceReport report = mTestSuite->runBenchmark( "sample_test" );
  
  // Contract: Report must be valid and complete
  QVERIFY( report.isValid() );
  QVERIFY( !report.benchmarkName().isEmpty() );
  QVERIFY( report.timestamp().isValid() );
  QVERIFY( report.metrics().isValid() );
  
  // Contract: Must support multiple output formats
  QString jsonReport = report.toJson();
  QVERIFY( !jsonReport.isEmpty() );
  QVERIFY( jsonReport.contains( "sample_test" ) );
  
  QString xmlReport = report.toXml();
  QVERIFY( !xmlReport.isEmpty() );
  QVERIFY( xmlReport.contains( "sample_test" ) );
  
  QString htmlReport = report.toHtml();
  QVERIFY( !htmlReport.isEmpty() );
  QVERIFY( htmlReport.contains( "sample_test" ) );
  
  // Contract: Must save reports to files
  QString reportPath = mTestOutputDir + "/sample_test_report.json";
  bool saved = report.saveToFile( reportPath );
  QVERIFY( saved );
  QVERIFY( QFile::exists( reportPath ) );
}

void TestPerformanceTestSuite::testRegressionDetection()
{
  // Contract: Must detect performance regressions against baseline
  QVERIFY( mTestSuite != nullptr );
  
  // Run benchmark to get current metrics
  mTestSuite->registerBenchmark( mSampleBenchmark );
  QgsPerformanceReport currentReport = mTestSuite->runBenchmark( "sample_test" );
  
  // Compare against baseline
  QgsPerformanceComparison comparison = mTestSuite->compareWithBaseline( currentReport );
  QVERIFY( comparison.isValid() );
  
  // Contract: Must provide regression status
  QVERIFY( comparison.hasRegressionStatus() );
  QgsPerformanceComparison::RegressionStatus status = comparison.regressionStatus();
  QVERIFY( status == QgsPerformanceComparison::NoRegression || 
           status == QgsPerformanceComparison::MinorRegression ||
           status == QgsPerformanceComparison::MajorRegression );
  
  // Contract: Must provide percentage change
  QVERIFY( comparison.hasPercentageChange() );
  double percentChange = comparison.percentageChange();
  QVERIFY( percentChange >= -100.0 ); // Can't be less than -100%
  
  // Contract: Must provide statistical significance
  QVERIFY( comparison.hasStatisticalSignificance() );
  bool significant = comparison.isStatisticallySignificant();
  double pValue = comparison.pValue();
  QVERIFY( pValue >= 0.0 && pValue <= 1.0 );
  
  if ( significant )
  {
    QVERIFY( pValue < 0.05 ); // Standard significance threshold
  }
}

void TestPerformanceTestSuite::testRunSingleBenchmark()
{
  // Contract: Must execute individual benchmarks correctly
  QVERIFY( mTestSuite != nullptr );
  
  // Register benchmark
  mTestSuite->registerBenchmark( mSampleBenchmark );
  
  // Test successful execution
  QgsPerformanceReport report = mTestSuite->runBenchmark( "sample_test" );
  QVERIFY( report.isValid() );
  QVERIFY( report.executionStatus() == QgsPerformanceReport::Success );
  
  // Test non-existent benchmark
  QgsPerformanceReport invalidReport = mTestSuite->runBenchmark( "non_existent" );
  QVERIFY( !invalidReport.isValid() );
  QVERIFY( invalidReport.executionStatus() == QgsPerformanceReport::NotFound );
}

void TestPerformanceTestSuite::testRunBenchmarkSuite()
{
  // Contract: Must execute complete benchmark suites
  QVERIFY( mTestSuite != nullptr );
  
  // Register multiple benchmarks
  mTestSuite->registerBenchmark( mSampleBenchmark );
  
  QgsPerformanceBenchmark *benchmark2 = new QgsPerformanceBenchmark( "test_2" );
  mTestSuite->registerBenchmark( benchmark2 );
  
  QgsPerformanceBenchmark *benchmark3 = new QgsPerformanceBenchmark( "test_3" );
  mTestSuite->registerBenchmark( benchmark3 );
  
  // Run entire suite
  QList<QgsPerformanceReport> reports = mTestSuite->runAllBenchmarks();
  QCOMPARE( reports.size(), 3 );
  
  // Verify all reports are valid
  for ( const QgsPerformanceReport &report : reports )
  {
    QVERIFY( report.isValid() );
    QVERIFY( report.executionStatus() == QgsPerformanceReport::Success ||
             report.executionStatus() == QgsPerformanceReport::Warning );
  }
  
  // Test suite-level reporting
  QgsPerformanceSuiteReport suiteReport = mTestSuite->generateSuiteReport( reports );
  QVERIFY( suiteReport.isValid() );
  QCOMPARE( suiteReport.benchmarkCount(), 3 );
  QVERIFY( suiteReport.overallStatus() != QgsPerformanceSuiteReport::Unknown );
  
  delete benchmark2;
  delete benchmark3;
}

void TestPerformanceTestSuite::testBenchmarkTimeout()
{
  // Contract: Must handle benchmark timeouts gracefully
  QVERIFY( mTestSuite != nullptr );
  
  // Set short timeout
  mTestSuite->setBenchmarkTimeout( 100 ); // 100ms
  
  // Create long-running benchmark
  QgsPerformanceBenchmark *slowBenchmark = new QgsPerformanceBenchmark( "slow_test" );
  slowBenchmark->setExpectedDuration( 5000 ); // 5 seconds
  mTestSuite->registerBenchmark( slowBenchmark );
  
  // Run benchmark - should timeout
  QElapsedTimer timer;
  timer.start();
  QgsPerformanceReport report = mTestSuite->runBenchmark( "slow_test" );
  qint64 elapsed = timer.elapsed();
  
  // Should complete quickly due to timeout
  QVERIFY( elapsed < 1000 ); // Less than 1 second
  QVERIFY( report.executionStatus() == QgsPerformanceReport::Timeout );
  QVERIFY( !report.metrics().isValid() ); // No valid metrics on timeout
  
  delete slowBenchmark;
}

void TestPerformanceTestSuite::testBenchmarkCancellation()
{
  // Contract: Must support benchmark cancellation
  QVERIFY( mTestSuite != nullptr );
  
  // Register long-running benchmark
  QgsPerformanceBenchmark *longBenchmark = new QgsPerformanceBenchmark( "long_test" );
  longBenchmark->setExpectedDuration( 10000 ); // 10 seconds
  mTestSuite->registerBenchmark( longBenchmark );
  
  // Start benchmark execution in background
  QSignalSpy startedSpy( mTestSuite, &QgsPerformanceTestSuite::benchmarkStarted );
  QSignalSpy finishedSpy( mTestSuite, &QgsPerformanceTestSuite::benchmarkFinished );
  QSignalSpy cancelledSpy( mTestSuite, &QgsPerformanceTestSuite::benchmarkCancelled );
  
  // Start async execution
  mTestSuite->runBenchmarkAsync( "long_test" );
  
  // Wait for start signal
  QVERIFY( startedSpy.wait( 1000 ) );
  
  // Cancel after short delay
  QTest::qWait( 100 );
  mTestSuite->cancelBenchmark( "long_test" );
  
  // Should receive cancellation signal
  QVERIFY( cancelledSpy.wait( 1000 ) );
  
  // Should not receive finished signal
  QCOMPARE( finishedSpy.count(), 0 );
  
  delete longBenchmark;
}

void TestPerformanceTestSuite::testPerformanceMetrics()
{
  // Contract: Must provide comprehensive performance metrics
  QVERIFY( mTestSuite != nullptr );
  
  // Create metrics object
  QgsPerformanceMetrics metrics;
  
  // Contract: Must support timing measurements
  metrics.startTiming();
  QTest::qWait( 10 ); // Simulate some work
  metrics.stopTiming();
  
  QVERIFY( metrics.hasExecutionTime() );
  QVERIFY( metrics.executionTime() >= 10 );
  
  // Contract: Must support memory tracking
  metrics.recordMemoryUsage();
  QVERIFY( metrics.hasMemoryUsage() );
  QVERIFY( metrics.currentMemoryUsage() > 0 );
  
  // Contract: Must support CPU monitoring
  metrics.startCpuMonitoring();
  QTest::qWait( 50 ); // Simulate CPU work
  metrics.stopCpuMonitoring();
  
  QVERIFY( metrics.hasCpuUsage() );
  QVERIFY( metrics.averageCpuUsage() >= 0.0 );
  
  // Contract: Must provide statistical analysis
  QList<double> samples = { 10.0, 12.0, 11.0, 13.0, 10.5, 11.5 };
  metrics.addSamples( samples );
  
  QVERIFY( metrics.sampleCount() == 6 );
  QVERIFY( qAbs( metrics.mean() - 11.33 ) < 0.1 );
  QVERIFY( metrics.standardDeviation() > 0.0 );
  QVERIFY( metrics.minimum() == 10.0 );
  QVERIFY( metrics.maximum() == 13.0 );
}

void TestPerformanceTestSuite::testStatisticalAnalysis()
{
  // Contract: Must provide statistical analysis capabilities
  QVERIFY( mTestSuite != nullptr );
  
  // Create test data sets
  QList<double> baseline = { 100.0, 102.0, 98.0, 101.0, 99.0, 103.0, 97.0, 100.5 };
  QList<double> current = { 105.0, 107.0, 103.0, 106.0, 104.0, 108.0, 102.0, 105.5 };
  
  // Perform statistical comparison
  QgsStatisticalAnalysis analysis = mTestSuite->performStatisticalAnalysis( baseline, current );
  QVERIFY( analysis.isValid() );
  
  // Contract: Must calculate means correctly
  double baselineMean = analysis.baselineMean();
  double currentMean = analysis.currentMean();
  QVERIFY( qAbs( baselineMean - 100.0625 ) < 0.1 );
  QVERIFY( qAbs( currentMean - 105.0625 ) < 0.1 );
  
  // Contract: Must perform t-test
  QVERIFY( analysis.hasTTestResults() );
  double tStatistic = analysis.tStatistic();
  double pValue = analysis.pValue();
  QVERIFY( tStatistic != 0.0 );
  QVERIFY( pValue >= 0.0 && pValue <= 1.0 );
  
  // Contract: Must detect significant differences
  bool significant = analysis.isSignificant( 0.05 );
  if ( significant )
  {
    QVERIFY( pValue < 0.05 );
  }
  
  // Contract: Must calculate effect size
  QVERIFY( analysis.hasEffectSize() );
  double effectSize = analysis.cohensD();
  QVERIFY( qAbs( effectSize ) >= 0.0 );
}

void TestPerformanceTestSuite::testBaselineComparison()
{
  // Contract: Must compare performance against saved baselines
  QVERIFY( mTestSuite != nullptr );
  
  // Run benchmark to get current performance
  mTestSuite->registerBenchmark( mSampleBenchmark );
  QgsPerformanceReport currentReport = mTestSuite->runBenchmark( "sample_test" );
  
  // Load baseline data
  QgsPerformanceBaseline baseline = mTestSuite->loadBaseline( mBaselineFile );
  QVERIFY( baseline.isValid() );
  QVERIFY( baseline.hasBenchmark( "project_loading_100mb" ) );
  QVERIFY( baseline.hasBenchmark( "fgb_rendering_5pct" ) );
  
  // Create mock baseline entry for our test
  QgsPerformanceMetrics baselineMetrics;
  baselineMetrics.setExecutionTime( 1000 ); // 1 second baseline
  baselineMetrics.setStandardDeviation( 100 );
  baselineMetrics.setSampleCount( 10 );
  baseline.addBenchmark( "sample_test", baselineMetrics );
  
  // Compare current performance with baseline
  QgsPerformanceComparison comparison = mTestSuite->compareWithBaseline( 
    currentReport, baseline.getBenchmark( "sample_test" ) );
  
  QVERIFY( comparison.isValid() );
  QVERIFY( comparison.hasPercentageChange() );
  QVERIFY( comparison.hasRegressionStatus() );
  
  // Update baseline with current results
  bool updated = mTestSuite->updateBaseline( mBaselineFile, currentReport );
  QVERIFY( updated );
  
  // Verify baseline was updated
  QgsPerformanceBaseline updatedBaseline = mTestSuite->loadBaseline( mBaselineFile );
  QVERIFY( updatedBaseline.hasBenchmark( "sample_test" ) );
}

void TestPerformanceTestSuite::testTrendAnalysis()
{
  // Contract: Must analyze performance trends over time
  QVERIFY( mTestSuite != nullptr );
  
  // Create historical performance data
  QList<QgsPerformanceReport> historicalReports;
  
  for ( int i = 0; i < 10; ++i )
  {
    QgsPerformanceReport report;
    report.setBenchmarkName( "trend_test" );
    report.setTimestamp( QDateTime::currentDateTime().addDays( -i ) );
    
    QgsPerformanceMetrics metrics;
    metrics.setExecutionTime( 1000 + i * 50 ); // Increasing trend
    report.setMetrics( metrics );
    
    historicalReports.append( report );
  }
  
  // Perform trend analysis
  QgsPerformanceTrend trend = mTestSuite->analyzeTrend( historicalReports );
  QVERIFY( trend.isValid() );
  
  // Contract: Must detect trend direction
  QVERIFY( trend.hasTrendDirection() );
  QgsPerformanceTrend::Direction direction = trend.direction();
  QVERIFY( direction == QgsPerformanceTrend::Increasing ||
           direction == QgsPerformanceTrend::Decreasing ||
           direction == QgsPerformanceTrend::Stable );
  
  // Contract: Must calculate trend slope
  QVERIFY( trend.hasSlope() );
  double slope = trend.slope();
  QVERIFY( slope != 0.0 ); // Should detect increasing trend
  
  // Contract: Must provide confidence metrics
  QVERIFY( trend.hasConfidence() );
  double confidence = trend.confidence();
  QVERIFY( confidence >= 0.0 && confidence <= 1.0 );
  
  // Contract: Must predict future performance
  QDateTime futureDate = QDateTime::currentDateTime().addDays( 7 );
  double prediction = trend.predictValue( futureDate );
  QVERIFY( prediction > 0.0 );
}

void TestPerformanceTestSuite::testCIPipelineIntegration()
{
  // Contract: Must integrate with CI/CD pipelines
  QVERIFY( mTestSuite != nullptr );
  
  // Configure for CI environment
  mTestSuite->setCIMode( true );
  mTestSuite->setFailOnRegression( true );
  mTestSuite->setRegressionThreshold( 10.0 ); // 10% threshold
  
  // Register benchmarks
  mTestSuite->registerBenchmark( mSampleBenchmark );
  
  // Run CI performance validation
  QgsPerformanceCIResult ciResult = mTestSuite->runCIValidation();
  QVERIFY( ciResult.isValid() );
  
  // Contract: Must provide CI-friendly exit codes
  int exitCode = ciResult.exitCode();
  QVERIFY( exitCode == 0 || exitCode == 1 ); // Success or failure
  
  // Contract: Must generate CI-friendly reports
  QString ciReport = ciResult.generateCIReport();
  QVERIFY( !ciReport.isEmpty() );
  QVERIFY( ciReport.contains( "PASS" ) || ciReport.contains( "FAIL" ) );
  
  // Contract: Must support multiple CI formats
  QString junitXml = ciResult.toJUnitXML();
  QVERIFY( !junitXml.isEmpty() );
  QVERIFY( junitXml.contains( "<testsuite" ) );
  
  QString githubActions = ciResult.toGitHubActions();
  QVERIFY( !githubActions.isEmpty() );
  
  // Contract: Must provide performance badges
  QString badge = ciResult.generatePerformanceBadge();
  QVERIFY( !badge.isEmpty() );
}

void TestPerformanceTestSuite::testReportExport()
{
  // Contract: Must export reports in multiple formats
  QVERIFY( mTestSuite != nullptr );
  
  // Run benchmark to get report data
  mTestSuite->registerBenchmark( mSampleBenchmark );
  QgsPerformanceReport report = mTestSuite->runBenchmark( "sample_test" );
  
  // Test JSON export
  QString jsonPath = mTestOutputDir + "/report.json";
  bool jsonExported = report.exportToJson( jsonPath );
  QVERIFY( jsonExported );
  QVERIFY( QFile::exists( jsonPath ) );
  
  // Test XML export
  QString xmlPath = mTestOutputDir + "/report.xml";
  bool xmlExported = report.exportToXml( xmlPath );
  QVERIFY( xmlExported );
  QVERIFY( QFile::exists( xmlPath ) );
  
  // Test HTML export
  QString htmlPath = mTestOutputDir + "/report.html";
  bool htmlExported = report.exportToHtml( htmlPath );
  QVERIFY( htmlExported );
  QVERIFY( QFile::exists( htmlPath ) );
  
  // Test CSV export for data analysis
  QString csvPath = mTestOutputDir + "/metrics.csv";
  bool csvExported = report.exportToCsv( csvPath );
  QVERIFY( csvExported );
  QVERIFY( QFile::exists( csvPath ) );
  
  // Verify file contents
  QFile jsonFile( jsonPath );
  if ( jsonFile.open( QIODevice::ReadOnly ) )
  {
    QByteArray jsonContent = jsonFile.readAll();
    QVERIFY( jsonContent.contains( "sample_test" ) );
    QVERIFY( jsonContent.contains( "execution_time" ) );
  }
}

void TestPerformanceTestSuite::testConfigurationManagement()
{
  // Contract: Must support flexible configuration management
  QVERIFY( mTestSuite != nullptr );
  
  // Test configuration file loading
  QString configPath = mTestOutputDir + "/test_config.json";
  QFile configFile( configPath );
  if ( configFile.open( QIODevice::WriteOnly ) )
  {
    QTextStream stream( &configFile );
    stream << "{\n";
    stream << "  \"output_directory\": \"" << mTestOutputDir << "\",\n";
    stream << "  \"baseline_file\": \"" << mBaselineFile << "\",\n";
    stream << "  \"benchmark_timeout\": 30000,\n";
    stream << "  \"regression_threshold\": 5.0,\n";
    stream << "  \"ci_mode\": false,\n";
    stream << "  \"enabled_benchmarks\": [\"sample_test\"]\n";
    stream << "}\n";
    configFile.close();
  }
  
  // Load configuration
  bool configLoaded = mTestSuite->loadConfiguration( configPath );
  QVERIFY( configLoaded );
  
  // Verify configuration was applied
  QVERIFY( mTestSuite->outputDirectory() == mTestOutputDir );
  QVERIFY( mTestSuite->baselineFile() == mBaselineFile );
  QCOMPARE( mTestSuite->benchmarkTimeout(), 30000 );
  QCOMPARE( mTestSuite->regressionThreshold(), 5.0 );
  QVERIFY( !mTestSuite->isCIMode() );
  
  // Test configuration saving
  QString savedConfigPath = mTestOutputDir + "/saved_config.json";
  bool configSaved = mTestSuite->saveConfiguration( savedConfigPath );
  QVERIFY( configSaved );
  QVERIFY( QFile::exists( savedConfigPath ) );
  
  // Test environment variable override
  qputenv( "QGIS_PERF_OUTPUT_DIR", "/tmp/env_override" );
  mTestSuite->loadEnvironmentOverrides();
  QVERIFY( mTestSuite->outputDirectory() == "/tmp/env_override" );
  
  qunsetenv( "QGIS_PERF_OUTPUT_DIR" );
}

QGSTEST_MAIN( TestPerformanceTestSuite )
#include "test_performance_suite.moc"