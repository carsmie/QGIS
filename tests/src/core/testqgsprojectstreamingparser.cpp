/***************************************************************************
     testqgsprojectstreamingparser.cpp
     --------------------------------
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
#include "qgsprojectstreamingparser.h"
#include <QTemporaryFile>
#include <QDomDocument>

/**
 * \ingroup UnitTests
 * Unit tests for QgsProjectStreamingParser
 */
class TestQgsProjectStreamingParser : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Core functionality tests
    void testBasicParsing();
    void testIncrementalParsing();
    void testLargeFileHandling();
    void testMemoryEfficiency();
    void testProgressReporting();
    void testElementPrioritization();
    void testErrorHandling();
    void testCancellation();
    void testParsingStatistics();
    void testPriorityQueue();

  private:
    QgsProjectStreamingParser *mParser = nullptr;
    QString createTestProjectFile( int layerCount = 10, bool complex = false );
    QByteArray createLargeXmlContent( int sizeMB = 10 );
};

void TestQgsProjectStreamingParser::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();
}

void TestQgsProjectStreamingParser::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsProjectStreamingParser::init()
{
  mParser = new QgsProjectStreamingParser();
}

void TestQgsProjectStreamingParser::cleanup()
{
  delete mParser;
  mParser = nullptr;
}

QString TestQgsProjectStreamingParser::createTestProjectFile( int layerCount, bool complex )
{
  QTemporaryFile *tempFile = new QTemporaryFile();
  tempFile->setAutoRemove( false );
  QVERIFY( tempFile->open() );
  
  QDomDocument doc;
  QDomElement root = doc.createElement( QStringLiteral( "qgis" ) );
  root.setAttribute( QStringLiteral( "version" ), QStringLiteral( "3.40.0" ) );
  doc.appendChild( root );
  
  // Add project properties
  QDomElement properties = doc.createElement( QStringLiteral( "properties" ) );
  root.appendChild( properties );
  
  // Add layer tree
  QDomElement layerTree = doc.createElement( QStringLiteral( "layer-tree-group" ) );
  layerTree.setAttribute( QStringLiteral( "name" ), QStringLiteral( "Root" ) );
  root.appendChild( layerTree );
  
  // Add layers
  QDomElement projectLayers = doc.createElement( QStringLiteral( "projectlayers" ) );
  root.appendChild( projectLayers );
  
  for ( int i = 0; i < layerCount; ++i )
  {
    QDomElement layer = doc.createElement( QStringLiteral( "maplayer" ) );
    layer.setAttribute( QStringLiteral( "id" ), QStringLiteral( "layer_%1" ).arg( i ) );
    layer.setAttribute( QStringLiteral( "type" ), QStringLiteral( "vector" ) );
    
    QDomElement layerName = doc.createElement( QStringLiteral( "layername" ) );
    layerName.appendChild( doc.createTextNode( QStringLiteral( "Test Layer %1" ).arg( i ) ) );
    layer.appendChild( layerName );
    
    if ( complex )
    {
      // Add complex styling information
      QDomElement renderer = doc.createElement( QStringLiteral( "renderer-v2" ) );
      renderer.setAttribute( QStringLiteral( "type" ), QStringLiteral( "singleSymbol" ) );
      
      for ( int j = 0; j < 5; ++j )
      {
        QDomElement symbol = doc.createElement( QStringLiteral( "symbol" ) );
        symbol.setAttribute( QStringLiteral( "name" ), QStringLiteral( "symbol_%1" ).arg( j ) );
        renderer.appendChild( symbol );
      }
      
      layer.appendChild( renderer );
    }
    
    projectLayers.appendChild( layer );
    
    // Add to layer tree
    QDomElement treeLayer = doc.createElement( QStringLiteral( "layer-tree-layer" ) );
    treeLayer.setAttribute( QStringLiteral( "id" ), QStringLiteral( "layer_%1" ).arg( i ) );
    layerTree.appendChild( treeLayer );
  }
  
  // Write to file
  const QByteArray content = doc.toByteArray();
  tempFile->write( content );
  tempFile->close();
  
  const QString fileName = tempFile->fileName();
  delete tempFile;
  
  return fileName;
}

QByteArray TestQgsProjectStreamingParser::createLargeXmlContent( int sizeMB )
{
  QDomDocument doc;
  QDomElement root = doc.createElement( QStringLiteral( "qgis" ) );
  doc.appendChild( root );
  
  QDomElement projectLayers = doc.createElement( QStringLiteral( "projectlayers" ) );
  root.appendChild( projectLayers );
  
  // Calculate approximately how many layers we need for the target size
  const int targetBytes = sizeMB * 1024 * 1024;
  const int approxLayerSize = 1024; // Rough estimate
  const int layerCount = targetBytes / approxLayerSize;
  
  for ( int i = 0; i < layerCount; ++i )
  {
    QDomElement layer = doc.createElement( QStringLiteral( "maplayer" ) );
    layer.setAttribute( QStringLiteral( "id" ), QStringLiteral( "large_layer_%1" ).arg( i ) );
    
    // Add some content to make each layer larger
    QDomElement datasource = doc.createElement( QStringLiteral( "datasource" ) );
    const QString largeContent = QStringLiteral( "Large content data " ).repeated( 20 );
    datasource.appendChild( doc.createTextNode( largeContent ) );
    layer.appendChild( datasource );
    
    projectLayers.appendChild( layer );
  }
  
  return doc.toByteArray();
}

void TestQgsProjectStreamingParser::testBasicParsing()
{
  const QString testFile = createTestProjectFile( 5 );
  
  QSignalSpy elementSpy( mParser, &QgsProjectStreamingParser::elementParsed );
  QSignalSpy completeSpy( mParser, &QgsProjectStreamingParser::parsingCompleted );
  
  QVERIFY( mParser->startParsing( testFile ) );
  
  // Wait for parsing to complete
  QEventLoop loop;
  connect( mParser, &QgsProjectStreamingParser::parsingCompleted, &loop, &QEventLoop::quit );
  QTimer::singleShot( 5000, &loop, &QEventLoop::quit ); // Timeout
  loop.exec();
  
  // Verify completion signal was emitted
  QCOMPARE( completeSpy.count(), 1 );
  
  // Verify elements were parsed
  QVERIFY( elementSpy.count() > 0 );
  
  // Check parsing statistics
  const QgsProjectStreamingParser::ParsingStatistics stats = mParser->getStatistics();
  QVERIFY( stats.totalElements > 0 );
  QVERIFY( stats.parsingTime > 0 );
  
  // Cleanup
  QFile::remove( testFile );
}

void TestQgsProjectStreamingParser::testIncrementalParsing()
{
  const QString testFile = createTestProjectFile( 20 );
  
  QSignalSpy elementSpy( mParser, &QgsProjectStreamingParser::elementParsed );
  QSignalSpy progressSpy( mParser, &QgsProjectStreamingParser::progressChanged );
  
  // Configure for incremental parsing
  QgsProjectStreamingParser::ParsingConfig config;
  config.chunkSize = 1024; // Small chunks
  config.processingDelay = 10; // Short delay between chunks
  config.enableProgressReporting = true;
  mParser->setParsingConfig( config );
  
  QVERIFY( mParser->startParsing( testFile ) );
  
  // Wait for some progress
  QTest::qWait( 500 );
  
  // Should have progress updates
  QVERIFY( progressSpy.count() > 0 );
  
  // Wait for completion
  QEventLoop loop;
  connect( mParser, &QgsProjectStreamingParser::parsingCompleted, &loop, &QEventLoop::quit );
  QTimer::singleShot( 10000, &loop, &QEventLoop::quit );
  loop.exec();
  
  // Verify incremental parsing worked
  const QgsProjectStreamingParser::ParsingStatistics stats = mParser->getStatistics();
  QVERIFY( stats.chunksProcessed > 1 );
  QVERIFY( stats.totalElements >= 20 ); // Should have at least the layers
  
  QFile::remove( testFile );
}

void TestQgsProjectStreamingParser::testLargeFileHandling()
{
  // Create a larger test file
  const QString testFile = createTestProjectFile( 100, true );
  
  QSignalSpy memorySpy( mParser, &QgsProjectStreamingParser::memoryThresholdExceeded );
  
  // Configure for large file handling
  QgsProjectStreamingParser::ParsingConfig config;
  config.maxMemoryUsageMB = 50; // Low memory limit
  config.enableMemoryMonitoring = true;
  config.chunkSize = 2048;
  mParser->setParsingConfig( config );
  
  QVERIFY( mParser->startParsing( testFile ) );
  
  QEventLoop loop;
  connect( mParser, &QgsProjectStreamingParser::parsingCompleted, &loop, &QEventLoop::quit );
  QTimer::singleShot( 15000, &loop, &QEventLoop::quit );
  loop.exec();
  
  // Verify parsing completed successfully
  QVERIFY( mParser->isParsedSuccessfully() );
  
  const QgsProjectStreamingParser::ParsingStatistics stats = mParser->getStatistics();
  QVERIFY( stats.totalElements >= 100 );
  QVERIFY( stats.peakMemoryUsage > 0 );
  
  QFile::remove( testFile );
}

void TestQgsProjectStreamingParser::testMemoryEfficiency()
{
  // Test memory usage doesn't grow excessively
  const QByteArray largeContent = createLargeXmlContent( 5 ); // 5MB of content
  
  QTemporaryFile tempFile;
  QVERIFY( tempFile.open() );
  tempFile.write( largeContent );
  tempFile.close();
  
  // Configure for memory efficiency
  QgsProjectStreamingParser::ParsingConfig config;
  config.maxMemoryUsageMB = 100;
  config.enableMemoryMonitoring = true;
  config.chunkSize = 1024;
  mParser->setParsingConfig( config );
  
  const qint64 initialMemory = mParser->getCurrentMemoryUsage();
  
  QVERIFY( mParser->startParsing( tempFile.fileName() ) );
  
  QEventLoop loop;
  connect( mParser, &QgsProjectStreamingParser::parsingCompleted, &loop, &QEventLoop::quit );
  QTimer::singleShot( 30000, &loop, &QEventLoop::quit );
  loop.exec();
  
  const qint64 finalMemory = mParser->getCurrentMemoryUsage();
  const qint64 memoryIncrease = finalMemory - initialMemory;
  
  // Memory increase should be reasonable (less than input file size)
  QVERIFY( memoryIncrease < largeContent.size() );
  
  const QgsProjectStreamingParser::ParsingStatistics stats = mParser->getStatistics();
  QVERIFY( stats.memoryEfficiency > 0.5 ); // Should be reasonably efficient
}

void TestQgsProjectStreamingParser::testProgressReporting()
{
  const QString testFile = createTestProjectFile( 50 );
  
  QSignalSpy progressSpy( mParser, &QgsProjectStreamingParser::progressChanged );
  
  QgsProjectStreamingParser::ParsingConfig config;
  config.enableProgressReporting = true;
  config.progressUpdateInterval = 100; // Frequent updates
  mParser->setParsingConfig( config );
  
  QVERIFY( mParser->startParsing( testFile ) );
  
  // Wait for some progress
  QTest::qWait( 1000 );
  
  // Should have received progress updates
  QVERIFY( progressSpy.count() > 0 );
  
  // Check progress signal parameters
  bool foundProgressSignal = false;
  for ( const QList<QVariant> &signal : progressSpy )
  {
    if ( signal.size() >= 2 )
    {
      const int progress = signal[0].toInt();
      const qint64 processedBytes = signal[1].toLongLong();
      
      QVERIFY( progress >= 0 && progress <= 100 );
      QVERIFY( processedBytes >= 0 );
      foundProgressSignal = true;
      break;
    }
  }
  
  QVERIFY( foundProgressSignal );
  
  // Wait for completion
  QEventLoop loop;
  connect( mParser, &QgsProjectStreamingParser::parsingCompleted, &loop, &QEventLoop::quit );
  QTimer::singleShot( 10000, &loop, &QEventLoop::quit );
  loop.exec();
  
  QFile::remove( testFile );
}

void TestQgsProjectStreamingParser::testElementPrioritization()
{
  const QString testFile = createTestProjectFile( 20, true );
  
  QSignalSpy elementSpy( mParser, &QgsProjectStreamingParser::elementParsed );
  
  // Configure element priorities
  QgsProjectStreamingParser::ParsingConfig config;
  config.elementPriorities[QStringLiteral( "maplayer" )] = QgsProjectStreamingParser::ElementPriority::High;
  config.elementPriorities[QStringLiteral( "renderer-v2" )] = QgsProjectStreamingParser::ElementPriority::Medium;
  config.elementPriorities[QStringLiteral( "symbol" )] = QgsProjectStreamingParser::ElementPriority::Low;
  mParser->setParsingConfig( config );
  
  QVERIFY( mParser->startParsing( testFile ) );
  
  QEventLoop loop;
  connect( mParser, &QgsProjectStreamingParser::parsingCompleted, &loop, &QEventLoop::quit );
  QTimer::singleShot( 10000, &loop, &QEventLoop::quit );
  loop.exec();
  
  // Verify elements were parsed
  QVERIFY( elementSpy.count() > 0 );
  
  // Check that high priority elements were processed
  bool foundMapLayer = false;
  for ( const QList<QVariant> &signal : elementSpy )
  {
    if ( signal.size() >= 1 )
    {
      const QString elementName = signal[0].toString();
      if ( elementName == QStringLiteral( "maplayer" ) )
      {
        foundMapLayer = true;
        break;
      }
    }
  }
  
  QVERIFY( foundMapLayer );
  
  const QgsProjectStreamingParser::ParsingStatistics stats = mParser->getStatistics();
  QVERIFY( stats.priorityProcessingEnabled );
  
  QFile::remove( testFile );
}

void TestQgsProjectStreamingParser::testErrorHandling()
{
  // Create invalid XML file
  QTemporaryFile tempFile;
  QVERIFY( tempFile.open() );
  tempFile.write( "<?xml version=\"1.0\"?><invalid><unclosed>" ); // Invalid XML
  tempFile.close();
  
  QSignalSpy errorSpy( mParser, &QgsProjectStreamingParser::parsingError );
  
  QVERIFY( mParser->startParsing( tempFile.fileName() ) );
  
  QEventLoop loop;
  connect( mParser, &QgsProjectStreamingParser::parsingCompleted, &loop, &QEventLoop::quit );
  connect( mParser, &QgsProjectStreamingParser::parsingError, &loop, &QEventLoop::quit );
  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();
  
  // Should have received error signal
  QVERIFY( errorSpy.count() > 0 );
  
  // Should not be successfully parsed
  QVERIFY( !mParser->isParsedSuccessfully() );
  
  // Test non-existent file
  QSignalSpy errorSpy2( mParser, &QgsProjectStreamingParser::parsingError );
  QVERIFY( !mParser->startParsing( QStringLiteral( "/nonexistent/file.xml" ) ) );
}

void TestQgsProjectStreamingParser::testCancellation()
{
  const QString testFile = createTestProjectFile( 100, true );
  
  QSignalSpy cancelledSpy( mParser, &QgsProjectStreamingParser::parsingCancelled );
  
  // Configure for slow parsing
  QgsProjectStreamingParser::ParsingConfig config;
  config.processingDelay = 50; // Slow parsing
  config.chunkSize = 512;
  mParser->setParsingConfig( config );
  
  QVERIFY( mParser->startParsing( testFile ) );
  
  // Wait a bit then cancel
  QTest::qWait( 200 );
  mParser->cancelParsing();
  
  // Wait for cancellation
  QEventLoop loop;
  connect( mParser, &QgsProjectStreamingParser::parsingCancelled, &loop, &QEventLoop::quit );
  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();
  
  // Verify cancellation signal was emitted
  QCOMPARE( cancelledSpy.count(), 1 );
  
  // Verify parsing was cancelled
  QVERIFY( mParser->isCancelled() );
  
  QFile::remove( testFile );
}

void TestQgsProjectStreamingParser::testParsingStatistics()
{
  const QString testFile = createTestProjectFile( 25, true );
  
  QVERIFY( mParser->startParsing( testFile ) );
  
  QEventLoop loop;
  connect( mParser, &QgsProjectStreamingParser::parsingCompleted, &loop, &QEventLoop::quit );
  QTimer::singleShot( 10000, &loop, &QEventLoop::quit );
  loop.exec();
  
  const QgsProjectStreamingParser::ParsingStatistics stats = mParser->getStatistics();
  
  // Verify basic statistics
  QVERIFY( stats.totalElements >= 25 ); // At least the layers
  QVERIFY( stats.parsingTime > 0 );
  QVERIFY( stats.fileSize > 0 );
  QVERIFY( stats.chunksProcessed > 0 );
  QVERIFY( stats.bytesProcessed > 0 );
  QVERIFY( stats.elementsPerSecond > 0 );
  QVERIFY( stats.averageChunkProcessingTime > 0 );
  
  // Test statistics export
  const QVariantMap exported = mParser->exportStatistics();
  
  QVERIFY( exported.contains( QStringLiteral( "total_elements" ) ) );
  QVERIFY( exported.contains( QStringLiteral( "parsing_time_ms" ) ) );
  QVERIFY( exported.contains( QStringLiteral( "file_size_bytes" ) ) );
  QVERIFY( exported.contains( QStringLiteral( "memory_efficiency" ) ) );
  
  QFile::remove( testFile );
}

void TestQgsProjectStreamingParser::testPriorityQueue()
{
  const QString testFile = createTestProjectFile( 15, true );
  
  QSignalSpy elementSpy( mParser, &QgsProjectStreamingParser::elementParsed );
  
  // Configure with priority processing
  QgsProjectStreamingParser::ParsingConfig config;
  config.enablePriorityProcessing = true;
  config.elementPriorities[QStringLiteral( "properties" )] = QgsProjectStreamingParser::ElementPriority::Critical;
  config.elementPriorities[QStringLiteral( "maplayer" )] = QgsProjectStreamingParser::ElementPriority::High;
  config.elementPriorities[QStringLiteral( "layer-tree-group" )] = QgsProjectStreamingParser::ElementPriority::Medium;
  mParser->setParsingConfig( config );
  
  QVERIFY( mParser->startParsing( testFile ) );
  
  QEventLoop loop;
  connect( mParser, &QgsProjectStreamingParser::parsingCompleted, &loop, &QEventLoop::quit );
  QTimer::singleShot( 10000, &loop, &QEventLoop::quit );
  loop.exec();
  
  // Verify priority processing was enabled
  const QgsProjectStreamingParser::ParsingStatistics stats = mParser->getStatistics();
  QVERIFY( stats.priorityProcessingEnabled );
  
  // Verify elements were parsed
  QVERIFY( elementSpy.count() > 0 );
  
  QFile::remove( testFile );
}

QGSTEST_MAIN( TestQgsProjectStreamingParser )
#include "testqgsprojectstreamingparser.moc"