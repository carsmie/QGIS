/***************************************************************************
                         iperformancemonitor.cpp
                         ----------------------
    begin                : September 2025
    copyright            : (C) 2025 by QGIS
    email                : development-team@qgis.org
 ***************************************************************************/

#include "iperformancemonitor.h"
#include "qgslogger.h"
#include "qgsapplication.h"

#include <QThread>
#include <QUuid>
#include <QRegularExpression>
#include <QMutexLocker>

#ifdef Q_OS_LINUX
#include <unistd.h>
#include <fstream>
#include <sstream>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <mach/task.h>
#include <mach/mach_init.h>
#endif

/**
 * Concrete implementation of IPerformanceMonitor
 */
class QgsPerformanceMonitor : public IPerformanceMonitor
{
  public:
    QgsPerformanceMonitor( QObject *parent = nullptr );
    ~QgsPerformanceMonitor() override;

    // Core monitoring operations
    QString startOperation( const QString &operationName,
                           const QString &category = QString(),
                           const QHash<QString, QVariant> &metadata = QHash<QString, QVariant>() ) override;

    bool endOperation( const QString &operationId = QString() ) override;

    void recordMetric( const QString &name,
                      const QVariant &value,
                      const QString &unit = QString(),
                      const QString &category = QString(),
                      const QString &operationId = QString() ) override;

    MemorySnapshot recordMemoryUsage( const QString &operationId = QString() ) override;

    void recordError( const QString &errorMessage,
                     const QString &severity = QStringLiteral( "error" ),
                     const QString &operationId = QString() ) override;

    // Data retrieval and analysis
    QList<MetricData> getMetrics( const QString &category = QString(),
                                 const QDateTime &since = QDateTime() ) const override;

    OperationContext getOperationContext( const QString &operationId ) const override;

    QList<OperationContext> getActiveOperations() const override;

    PerformanceSummary getPerformanceSummary( const QString &category,
                                             const QDateTime &since = QDateTime() ) const override;

    QList<MemorySnapshot> getMemoryHistory( const QDateTime &since = QDateTime() ) const override;

    // Configuration and control
    void setConfiguration( const MonitoringConfig &config ) override;
    MonitoringConfig getConfiguration() const override;
    void reset( bool preserveConfig = true ) override;
    void enableCategory( const QString &category ) override;
    void disableCategory( const QString &category ) override;
    bool isCategoryEnabled( const QString &category ) const override;

    // Utility methods
    qint64 getCurrentMemoryUsageMB() const override;
    bool isMonitoringActive() const override;
    double getMonitoringOverhead() const override;
    QString generateOperationId() const override;

  private:
    mutable QMutex mDataMutex;
    QHash<QString, OperationContext> mActiveOperations;
    QHash<QString, OperationContext> mCompletedOperations;
    QList<MetricData> mMetrics;
    QList<MemorySnapshot> mMemoryHistory;
    QStringList mErrorLog;
    QString mCurrentOperationId;
    qint64 mMonitoringOverheadNs = 0;
    QElapsedTimer mOverheadTimer;

    void cleanupOldData();
    qint64 measureOverhead( const std::function<void()> &operation );
    MemorySnapshot createMemorySnapshot() const;
};

IPerformanceMonitor::IPerformanceMonitor( QObject *parent )
  : QObject( parent )
{
  mMonitoringStartTime = QDateTime::currentDateTime();
}

IPerformanceMonitor::~IPerformanceMonitor() = default;

bool IPerformanceMonitor::isValidOperationId( const QString &operationId ) const
{
  if ( operationId.isEmpty() )
    return false;

  // Check for valid UUID format or custom format
  QRegularExpression uuidRegex( QStringLiteral( "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$" ) );
  QRegularExpression customRegex( QStringLiteral( "^[a-zA-Z0-9_-]+$" ) );

  return uuidRegex.match( operationId ).hasMatch() || customRegex.match( operationId ).hasMatch();
}

bool IPerformanceMonitor::isValidMetricName( const QString &metricName ) const
{
  if ( metricName.isEmpty() )
    return false;

  // Allow alphanumeric, underscore, dot, and hyphen
  QRegularExpression regex( QStringLiteral( "^[a-zA-Z0-9._-]+$" ) );
  return regex.match( metricName ).hasMatch();
}

qint64 IPerformanceMonitor::getPlatformMemoryUsageMB() const
{
#ifdef Q_OS_LINUX
  std::ifstream statusFile( "/proc/self/status" );
  std::string line;
  while ( std::getline( statusFile, line ) )
  {
    if ( line.find( "VmRSS:" ) == 0 )
    {
      std::istringstream iss( line );
      std::string label;
      qint64 value;
      std::string unit;
      iss >> label >> value >> unit;
      return value / 1024; // Convert from KB to MB
    }
  }
  return 0;

#elif defined(Q_OS_WIN)
  PROCESS_MEMORY_COUNTERS_EX pmc;
  if ( GetProcessMemoryInfo( GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>( &pmc ), sizeof( pmc ) ) )
  {
    return pmc.WorkingSetSize / ( 1024 * 1024 ); // Convert to MB
  }
  return 0;

#elif defined(Q_OS_MACOS)
  struct task_basic_info info;
  mach_msg_type_number_t infoCount = TASK_BASIC_INFO_COUNT;
  if ( task_info( mach_task_self(), TASK_BASIC_INFO, reinterpret_cast<task_info_t>( &info ), &infoCount ) == KERN_SUCCESS )
  {
    return info.resident_size / ( 1024 * 1024 ); // Convert to MB
  }
  return 0;

#else
  // Fallback for unsupported platforms
  return 100; // Return placeholder value
#endif
}

// QgsPerformanceMonitor implementation

QgsPerformanceMonitor::QgsPerformanceMonitor( QObject *parent )
  : IPerformanceMonitor( parent )
{
  QgsDebugMsgLevel( QStringLiteral( "Performance monitor initialized" ), 2 );
  mOverheadTimer.start();
}

QgsPerformanceMonitor::~QgsPerformanceMonitor()
{
  QgsDebugMsgLevel( QStringLiteral( "Performance monitor destroyed" ), 2 );
}

QString QgsPerformanceMonitor::startOperation( const QString &operationName,
                                               const QString &category,
                                               const QHash<QString, QVariant> &metadata )
{
  QElapsedTimer overheadTimer;
  overheadTimer.start();

  if ( !mConfig.enableOperationTiming )
    return QString();

  if ( !category.isEmpty() && !isCategoryEnabled( category ) )
    return QString();

  QString operationId = generateOperationId();
  
  OperationContext context;
  context.operationId = operationId;
  context.operationName = operationName;
  context.category = category;
  context.startTime = QDateTime::currentDateTime();
  context.isActive = true;
  context.parentOperationId = mCurrentOperationId;
  context.metadata = metadata;

  {
    QMutexLocker locker( &mDataMutex );
    mActiveOperations[operationId] = context;
    mCurrentOperationId = operationId;
  }

  mMonitoringOverheadNs += overheadTimer.nsecsElapsed();

  emit operationStarted( operationId, operationName, category );

  QgsDebugMsgLevel( QStringLiteral( "Started operation: %1 [%2]" ).arg( operationName, operationId ), 3 );

  return operationId;
}

bool QgsPerformanceMonitor::endOperation( const QString &operationId )
{
  QElapsedTimer overheadTimer;
  overheadTimer.start();

  if ( !mConfig.enableOperationTiming )
    return false;

  QString targetOperationId = operationId.isEmpty() ? mCurrentOperationId : operationId;

  if ( targetOperationId.isEmpty() )
    return false;

  OperationContext context;
  bool found = false;

  {
    QMutexLocker locker( &mDataMutex );
    auto it = mActiveOperations.find( targetOperationId );
    if ( it != mActiveOperations.end() )
    {
      context = it.value();
      context.endTime = QDateTime::currentDateTime();
      context.durationMs = context.startTime.msecsTo( context.endTime );
      context.isActive = false;

      mCompletedOperations[targetOperationId] = context;
      mActiveOperations.erase( it );

      // Reset current operation to parent
      mCurrentOperationId = context.parentOperationId;
      found = true;
    }
  }

  if ( found )
  {
    mMonitoringOverheadNs += overheadTimer.nsecsElapsed();

    emit operationCompleted( targetOperationId, context.durationMs, context.category );

    QgsDebugMsgLevel( QStringLiteral( "Completed operation: %1 [%2] in %3ms" )
                      .arg( context.operationName, targetOperationId )
                      .arg( context.durationMs ), 3 );

    // Record operation duration as metric
    recordMetric( QStringLiteral( "operation_duration" ), context.durationMs,
                 QStringLiteral( "ms" ), context.category, targetOperationId );

    cleanupOldData();
    return true;
  }

  return false;
}

void QgsPerformanceMonitor::recordMetric( const QString &name,
                                         const QVariant &value,
                                         const QString &unit,
                                         const QString &category,
                                         const QString &operationId )
{
  QElapsedTimer overheadTimer;
  overheadTimer.start();

  if ( !mConfig.enableMetricCollection )
    return;

  if ( !category.isEmpty() && !isCategoryEnabled( category ) )
    return;

  if ( !isValidMetricName( name ) )
  {
    QgsDebugError( QStringLiteral( "Invalid metric name: %1" ).arg( name ) );
    return;
  }

  MetricData metric;
  metric.name = name;
  metric.value = value;
  metric.unit = unit;
  metric.category = category;
  metric.operationId = operationId;
  metric.timestamp = QDateTime::currentDateTime();

  {
    QMutexLocker locker( &mDataMutex );
    mMetrics.append( metric );

    // Limit metric history size
    if ( mMetrics.size() > mConfig.maxMetricHistorySize )
    {
      mMetrics.removeFirst();
    }
  }

  mMonitoringOverheadNs += overheadTimer.nsecsElapsed();

  emit metricRecorded( name, value, category );

  QgsDebugMsgLevel( QStringLiteral( "Recorded metric: %1 = %2 %3" )
                    .arg( name, value.toString(), unit ), 4 );
}

IPerformanceMonitor::MemorySnapshot QgsPerformanceMonitor::recordMemoryUsage( const QString &operationId )
{
  QElapsedTimer overheadTimer;
  overheadTimer.start();

  if ( !mConfig.enableMemoryTracking )
    return MemorySnapshot();

  MemorySnapshot snapshot = createMemorySnapshot();

  {
    QMutexLocker locker( &mDataMutex );
    mMemoryHistory.append( snapshot );

    // Limit memory history size
    if ( mMemoryHistory.size() > 1000 ) // Keep last 1000 snapshots
    {
      mMemoryHistory.removeFirst();
    }
  }

  mMonitoringOverheadNs += overheadTimer.nsecsElapsed();

  // Check for significant memory changes
  static qint64 lastReportedMemory = 0;
  qint64 memoryChange = snapshot.usedMemoryMB - lastReportedMemory;
  if ( qAbs( memoryChange ) > 10 ) // Report changes > 10MB
  {
    emit memoryUsageChanged( snapshot.usedMemoryMB, memoryChange );
    lastReportedMemory = snapshot.usedMemoryMB;
  }

  // Record as metric
  recordMetric( QStringLiteral( "memory_usage" ), snapshot.usedMemoryMB,
               QStringLiteral( "MB" ), QStringLiteral( "memory" ), operationId );

  return snapshot;
}

void QgsPerformanceMonitor::recordError( const QString &errorMessage,
                                        const QString &severity,
                                        const QString &operationId )
{
  if ( !mConfig.enableErrorTracking )
    return;

  QString errorEntry = QStringLiteral( "[%1] %2: %3 (Operation: %4)" )
                       .arg( QDateTime::currentDateTime().toString( Qt::ISODate ),
                             severity, errorMessage, operationId );

  {
    QMutexLocker locker( &mDataMutex );
    mErrorLog.append( errorEntry );

    // Limit error log size
    if ( mErrorLog.size() > 1000 )
    {
      mErrorLog.removeFirst();
    }
  }

  emit errorRecorded( errorMessage, severity, operationId );

  QgsDebugError( QStringLiteral( "Performance error recorded: %1" ).arg( errorEntry ) );
}

QList<IPerformanceMonitor::MetricData> QgsPerformanceMonitor::getMetrics( const QString &category,
                                                                          const QDateTime &since ) const
{
  QMutexLocker locker( &mDataMutex );

  QList<MetricData> result;
  for ( const MetricData &metric : mMetrics )
  {
    if ( !category.isEmpty() && metric.category != category )
      continue;

    if ( since.isValid() && metric.timestamp < since )
      continue;

    result.append( metric );
  }

  return result;
}

IPerformanceMonitor::OperationContext QgsPerformanceMonitor::getOperationContext( const QString &operationId ) const
{
  QMutexLocker locker( &mDataMutex );

  // Check active operations first
  auto activeIt = mActiveOperations.find( operationId );
  if ( activeIt != mActiveOperations.end() )
    return activeIt.value();

  // Check completed operations
  auto completedIt = mCompletedOperations.find( operationId );
  if ( completedIt != mCompletedOperations.end() )
    return completedIt.value();

  return OperationContext(); // Return invalid context
}

QList<IPerformanceMonitor::OperationContext> QgsPerformanceMonitor::getActiveOperations() const
{
  QMutexLocker locker( &mDataMutex );
  return mActiveOperations.values();
}

IPerformanceMonitor::PerformanceSummary QgsPerformanceMonitor::getPerformanceSummary( const QString &category,
                                                                                     const QDateTime &since ) const
{
  QMutexLocker locker( &mDataMutex );

  PerformanceSummary summary;
  summary.category = category;

  QList<OperationContext> operations;
  for ( const OperationContext &op : mCompletedOperations )
  {
    if ( !category.isEmpty() && op.category != category )
      continue;

    if ( since.isValid() && op.endTime < since )
      continue;

    operations.append( op );
  }

  if ( operations.isEmpty() )
    return summary;

  summary.totalOperations = operations.size();
  summary.firstOperationTime = operations.first().startTime;
  summary.lastOperationTime = operations.last().endTime;

  qint64 totalDuration = 0;
  qint64 minDuration = std::numeric_limits<qint64>::max();
  qint64 maxDuration = 0;

  for ( const OperationContext &op : operations )
  {
    totalDuration += op.durationMs;
    minDuration = qMin( minDuration, op.durationMs );
    maxDuration = qMax( maxDuration, op.durationMs );
  }

  summary.totalDurationMs = totalDuration;
  summary.averageDurationMs = totalDuration / operations.size();
  summary.minDurationMs = minDuration;
  summary.maxDurationMs = maxDuration;
  summary.lastDurationMs = operations.last().durationMs;

  // Calculate operations per second
  qint64 timeSpanMs = summary.firstOperationTime.msecsTo( summary.lastOperationTime );
  if ( timeSpanMs > 0 )
  {
    summary.operationsPerSecond = ( double( operations.size() ) * 1000.0 ) / timeSpanMs;
  }

  return summary;
}

QList<IPerformanceMonitor::MemorySnapshot> QgsPerformanceMonitor::getMemoryHistory( const QDateTime &since ) const
{
  QMutexLocker locker( &mDataMutex );

  if ( !since.isValid() )
    return mMemoryHistory;

  QList<MemorySnapshot> result;
  for ( const MemorySnapshot &snapshot : mMemoryHistory )
  {
    if ( snapshot.timestamp >= since )
      result.append( snapshot );
  }

  return result;
}

void QgsPerformanceMonitor::setConfiguration( const MonitoringConfig &config )
{
  QMutexLocker locker( &mConfigMutex );
  mConfig = config;
  emit configurationChanged( config );

  QgsDebugMsgLevel( QStringLiteral( "Performance monitor configuration updated" ), 2 );
}

IPerformanceMonitor::MonitoringConfig QgsPerformanceMonitor::getConfiguration() const
{
  QMutexLocker locker( &mConfigMutex );
  return mConfig;
}

void QgsPerformanceMonitor::reset( bool preserveConfig )
{
  QMutexLocker locker( &mDataMutex );

  mActiveOperations.clear();
  mCompletedOperations.clear();
  mMetrics.clear();
  mMemoryHistory.clear();
  mErrorLog.clear();
  mCurrentOperationId.clear();
  mMonitoringOverheadNs = 0;

  if ( !preserveConfig )
  {
    QMutexLocker configLocker( &mConfigMutex );
    mConfig = MonitoringConfig();
  }

  mMonitoringStartTime = QDateTime::currentDateTime();

  QgsDebugMsgLevel( QStringLiteral( "Performance monitor reset" ), 2 );
}

void QgsPerformanceMonitor::enableCategory( const QString &category )
{
  QMutexLocker locker( &mConfigMutex );

  if ( !mConfig.enabledCategories.contains( category ) )
    mConfig.enabledCategories.append( category );

  mConfig.disabledCategories.removeAll( category );

  emit configurationChanged( mConfig );
}

void QgsPerformanceMonitor::disableCategory( const QString &category )
{
  QMutexLocker locker( &mConfigMutex );

  mConfig.enabledCategories.removeAll( category );

  if ( !mConfig.disabledCategories.contains( category ) )
    mConfig.disabledCategories.append( category );

  emit configurationChanged( mConfig );
}

bool QgsPerformanceMonitor::isCategoryEnabled( const QString &category ) const
{
  QMutexLocker locker( &mConfigMutex );

  // If specific categories are enabled, check if this one is included
  if ( !mConfig.enabledCategories.isEmpty() )
    return mConfig.enabledCategories.contains( category );

  // Otherwise, check if it's not explicitly disabled
  return !mConfig.disabledCategories.contains( category );
}

qint64 QgsPerformanceMonitor::getCurrentMemoryUsageMB() const
{
  return getPlatformMemoryUsageMB();
}

bool QgsPerformanceMonitor::isMonitoringActive() const
{
  QMutexLocker locker( &mDataMutex );
  return !mActiveOperations.isEmpty() || mConfig.enableMetricCollection || mConfig.enableMemoryTracking;
}

double QgsPerformanceMonitor::getMonitoringOverhead() const
{
  if ( mOverheadTimer.elapsed() == 0 )
    return 0.0;

  // Convert nanoseconds to milliseconds and calculate percentage
  double overheadMs = mMonitoringOverheadNs / 1000000.0;
  double totalMs = mOverheadTimer.elapsed();

  return ( overheadMs / totalMs ) * 100.0;
}

QString QgsPerformanceMonitor::generateOperationId() const
{
  return QUuid::createUuid().toString( QUuid::WithoutBraces );
}

void QgsPerformanceMonitor::cleanupOldData()
{
  QMutexLocker locker( &mDataMutex );

  // Keep completed operations history limited
  if ( mCompletedOperations.size() > 1000 )
  {
    // Remove oldest operations
    auto it = mCompletedOperations.begin();
    while ( mCompletedOperations.size() > 800 && it != mCompletedOperations.end() )
    {
      it = mCompletedOperations.erase( it );
    }
  }
}

IPerformanceMonitor::MemorySnapshot QgsPerformanceMonitor::createMemorySnapshot() const
{
  MemorySnapshot snapshot;
  snapshot.timestamp = QDateTime::currentDateTime();
  snapshot.processName = QgsApplication::applicationName();
  snapshot.usedMemoryMB = getPlatformMemoryUsageMB();

  // Calculate peak memory
  static qint64 peakMemory = 0;
  if ( snapshot.usedMemoryMB > peakMemory )
  {
    peakMemory = snapshot.usedMemoryMB;
  }
  snapshot.peakMemoryMB = peakMemory;

  // Get system memory info (simplified)
  snapshot.totalMemoryMB = 8192; // Placeholder - would get actual system memory
  snapshot.availableMemoryMB = snapshot.totalMemoryMB - snapshot.usedMemoryMB;

  return snapshot;
}

// Factory function implementation
std::unique_ptr<IPerformanceMonitor> createPerformanceMonitor( QObject *parent )
{
  return std::make_unique<QgsPerformanceMonitor>( parent );
}