/***************************************************************************
                         qgsloadingprofile.cpp
                         --------------------
    begin                : September 2025
    copyright            : (C) 2025 by QGIS
    email                : development-team@qgis.org
 ***************************************************************************/

#include "qgsloadingprofile.h"
#include "qgssettings.h"
#include "qgslogger.h"

#include <QDebug>

QgsLoadingProfile::QgsLoadingProfile( const QString &profileName, QObject *parent )
  : QObject( parent )
  , mProfileName( profileName )
  , mProfileType( ProfileType::Custom )
{
  if ( mProfileName.isEmpty() )
    mProfileName = QStringLiteral( "Custom Profile" );

  resetToDefaults();
}

QgsLoadingProfile::QgsLoadingProfile( ProfileType type, QObject *parent )
  : QObject( parent )
  , mProfileType( type )
{
  initializePredefinedProfile( type );
}

QgsLoadingProfile::QgsLoadingProfile( const QgsLoadingProfile &other )
  : QObject( other.parent() )
{
  copyFrom( other );
}

QgsLoadingProfile &QgsLoadingProfile::operator=( const QgsLoadingProfile &other )
{
  if ( this != &other )
  {
    copyFrom( other );
    emit configurationChanged();
  }
  return *this;
}

QgsLoadingProfile::~QgsLoadingProfile() = default;

void QgsLoadingProfile::setProfileName( const QString &name )
{
  if ( mProfileName != name )
  {
    mProfileName = name;
    emit configurationChanged();
  }
}

void QgsLoadingProfile::setProfileType( ProfileType type )
{
  if ( mProfileType != type )
  {
    mProfileType = type;
    initializePredefinedProfile( type );
    emit profileTypeChanged( type );
    emit configurationChanged();
  }
}

void QgsLoadingProfile::setDescription( const QString &description )
{
  if ( mDescription != description )
  {
    mDescription = description;
    emit configurationChanged();
  }
}

void QgsLoadingProfile::setOptimizationFlags( OptimizationFlags flags )
{
  if ( mOptimizationFlags != flags )
  {
    mOptimizationFlags = flags;
    
    // Update profile type to custom if it was predefined
    if ( mProfileType != ProfileType::Custom )
    {
      mProfileType = ProfileType::Custom;
      emit profileTypeChanged( mProfileType );
    }
    
    emit optimizationFlagsChanged( flags );
    emit configurationChanged();
  }
}

void QgsLoadingProfile::enableOptimization( OptimizationFlag flag )
{
  setOptimizationFlags( mOptimizationFlags | flag );
}

void QgsLoadingProfile::disableOptimization( OptimizationFlag flag )
{
  setOptimizationFlags( mOptimizationFlags & ~flag );
}

bool QgsLoadingProfile::isOptimizationEnabled( OptimizationFlag flag ) const
{
  return mOptimizationFlags.testFlag( flag );
}

bool QgsLoadingProfile::enableConnectionPooling() const
{
  return isOptimizationEnabled( OptimizationFlag::EnableConnectionPooling );
}

void QgsLoadingProfile::setEnableConnectionPooling( bool enabled )
{
  if ( enabled )
    enableOptimization( OptimizationFlag::EnableConnectionPooling );
  else
    disableOptimization( OptimizationFlag::EnableConnectionPooling );
}

void QgsLoadingProfile::setConnectionTimeoutSeconds( int seconds )
{
  if ( mConnectionTimeoutSeconds != seconds )
  {
    mConnectionTimeoutSeconds = qMax( 1, seconds ); // Minimum 1 second
    emit configurationChanged();
  }
}

void QgsLoadingProfile::setMaxConcurrentConnections( int maxConnections )
{
  if ( mMaxConcurrentConnections != maxConnections )
  {
    mMaxConcurrentConnections = qMax( 1, maxConnections ); // Minimum 1 connection
    emit configurationChanged();
  }
}

bool QgsLoadingProfile::skipFeatureCount() const
{
  return isOptimizationEnabled( OptimizationFlag::SkipFeatureCount );
}

void QgsLoadingProfile::setSkipFeatureCount( bool skip )
{
  if ( skip )
    enableOptimization( OptimizationFlag::SkipFeatureCount );
  else
    disableOptimization( OptimizationFlag::SkipFeatureCount );
}

bool QgsLoadingProfile::skipExtentCalculation() const
{
  return isOptimizationEnabled( OptimizationFlag::SkipExtentCalculation );
}

void QgsLoadingProfile::setSkipExtentCalculation( bool skip )
{
  if ( skip )
    enableOptimization( OptimizationFlag::SkipExtentCalculation );
  else
    disableOptimization( OptimizationFlag::SkipExtentCalculation );
}

bool QgsLoadingProfile::deferStyleLoading() const
{
  return isOptimizationEnabled( OptimizationFlag::DeferStyleLoading );
}

void QgsLoadingProfile::setDeferStyleLoading( bool defer )
{
  if ( defer )
    enableOptimization( OptimizationFlag::DeferStyleLoading );
  else
    disableOptimization( OptimizationFlag::DeferStyleLoading );
}

bool QgsLoadingProfile::enableMetadataCaching() const
{
  return isOptimizationEnabled( OptimizationFlag::EnableMetadataCaching );
}

void QgsLoadingProfile::setEnableMetadataCaching( bool enabled )
{
  if ( enabled )
    enableOptimization( OptimizationFlag::EnableMetadataCaching );
  else
    disableOptimization( OptimizationFlag::EnableMetadataCaching );
}

void QgsLoadingProfile::setMetadataCacheDurationSeconds( int seconds )
{
  if ( mMetadataCacheDurationSeconds != seconds )
  {
    mMetadataCacheDurationSeconds = qMax( 0, seconds ); // 0 = no caching
    emit configurationChanged();
  }
}

void QgsLoadingProfile::setMaxMemoryUsageMB( qint64 memoryMB )
{
  if ( mMaxMemoryUsageMB != memoryMB )
  {
    mMaxMemoryUsageMB = qMax( qint64( 1024 ), memoryMB ); // Minimum 1GB
    emit configurationChanged();
  }
}

void QgsLoadingProfile::setTimeoutThresholdSeconds( int seconds )
{
  if ( mTimeoutThresholdSeconds != seconds )
  {
    mTimeoutThresholdSeconds = qMax( 1, seconds ); // Minimum 1 second
    emit configurationChanged();
  }
}

void QgsLoadingProfile::setProgressReportingIntervalMs( int intervalMs )
{
  if ( mProgressReportingIntervalMs != intervalMs )
  {
    mProgressReportingIntervalMs = qMax( 50, intervalMs ); // Minimum 50ms
    emit configurationChanged();
  }
}

Qgis::DataProviderReadFlags QgsLoadingProfile::providerReadFlags( const QString &providerName ) const
{
  auto it = mProviderReadFlags.find( providerName );
  if ( it != mProviderReadFlags.end() )
    return it.value();

  // Return default flags based on optimization settings
  Qgis::DataProviderReadFlags flags = Qgis::DataProviderReadFlag::TrustDataSource;

  if ( skipFeatureCount() )
    flags |= Qgis::DataProviderReadFlag::SkipFeatureCount;

  if ( skipExtentCalculation() )
    flags |= Qgis::DataProviderReadFlag::SkipGetExtent;

  return flags;
}

void QgsLoadingProfile::setProviderReadFlags( const QString &providerName, Qgis::DataProviderReadFlags flags )
{
  if ( mProviderReadFlags.value( providerName ) != flags )
  {
    mProviderReadFlags[providerName] = flags;
    emit configurationChanged();
  }
}

QVariant QgsLoadingProfile::providerSetting( const QString &providerName, const QString &settingName, 
                                            const QVariant &defaultValue ) const
{
  auto providerIt = mProviderSettings.find( providerName );
  if ( providerIt != mProviderSettings.end() )
  {
    auto settingIt = providerIt.value().find( settingName );
    if ( settingIt != providerIt.value().end() )
      return settingIt.value();
  }
  return defaultValue;
}

void QgsLoadingProfile::setProviderSetting( const QString &providerName, const QString &settingName, const QVariant &value )
{
  bool changed = false;
  
  if ( !mProviderSettings.contains( providerName ) )
  {
    mProviderSettings[providerName] = QVariantMap();
    changed = true;
  }
  
  if ( mProviderSettings[providerName].value( settingName ) != value )
  {
    mProviderSettings[providerName][settingName] = value;
    changed = true;
  }
  
  if ( changed )
    emit configurationChanged();
}

bool QgsLoadingProfile::validateDataIntegrity() const
{
  return isOptimizationEnabled( OptimizationFlag::ValidateDataIntegrity );
}

void QgsLoadingProfile::setValidateDataIntegrity( bool validate )
{
  if ( validate )
    enableOptimization( OptimizationFlag::ValidateDataIntegrity );
  else
    disableOptimization( OptimizationFlag::ValidateDataIntegrity );
}

bool QgsLoadingProfile::enableProgressReporting() const
{
  return isOptimizationEnabled( OptimizationFlag::EnableProgressReporting );
}

void QgsLoadingProfile::setEnableProgressReporting( bool enabled )
{
  if ( enabled )
    enableOptimization( OptimizationFlag::EnableProgressReporting );
  else
    disableOptimization( OptimizationFlag::EnableProgressReporting );
}

QVariantMap QgsLoadingProfile::toVariantMap() const
{
  QVariantMap map;
  
  map[QStringLiteral( "profileName" )] = mProfileName;
  map[QStringLiteral( "profileType" )] = static_cast<int>( mProfileType );
  map[QStringLiteral( "description" )] = mDescription;
  map[QStringLiteral( "optimizationFlags" )] = static_cast<int>( mOptimizationFlags );
  
  map[QStringLiteral( "connectionTimeoutSeconds" )] = mConnectionTimeoutSeconds;
  map[QStringLiteral( "maxConcurrentConnections" )] = mMaxConcurrentConnections;
  map[QStringLiteral( "maxMemoryUsageMB" )] = mMaxMemoryUsageMB;
  map[QStringLiteral( "timeoutThresholdSeconds" )] = mTimeoutThresholdSeconds;
  map[QStringLiteral( "progressReportingIntervalMs" )] = mProgressReportingIntervalMs;
  map[QStringLiteral( "metadataCacheDurationSeconds" )] = mMetadataCacheDurationSeconds;
  
  // Provider-specific settings
  QVariantMap providerReadFlagsMap;
  for ( auto it = mProviderReadFlags.begin(); it != mProviderReadFlags.end(); ++it )
  {
    providerReadFlagsMap[it.key()] = static_cast<int>( it.value() );
  }
  map[QStringLiteral( "providerReadFlags" )] = providerReadFlagsMap;
  
  map[QStringLiteral( "providerSettings" )] = QVariant::fromValue( mProviderSettings );
  
  return map;
}

bool QgsLoadingProfile::fromVariantMap( const QVariantMap &map )
{
  bool changed = false;
  
  QString newProfileName = map.value( QStringLiteral( "profileName" ), mProfileName ).toString();
  if ( newProfileName != mProfileName )
  {
    mProfileName = newProfileName;
    changed = true;
  }
  
  ProfileType newProfileType = static_cast<ProfileType>( map.value( QStringLiteral( "profileType" ), 
                                                                   static_cast<int>( mProfileType ) ).toInt() );
  if ( newProfileType != mProfileType )
  {
    mProfileType = newProfileType;
    changed = true;
  }
  
  QString newDescription = map.value( QStringLiteral( "description" ), mDescription ).toString();
  if ( newDescription != mDescription )
  {
    mDescription = newDescription;
    changed = true;
  }
  
  OptimizationFlags newFlags = static_cast<OptimizationFlags>( map.value( QStringLiteral( "optimizationFlags" ), 
                                                                          static_cast<int>( mOptimizationFlags ) ).toInt() );
  if ( newFlags != mOptimizationFlags )
  {
    mOptimizationFlags = newFlags;
    changed = true;
  }
  
  // Load other settings
  mConnectionTimeoutSeconds = map.value( QStringLiteral( "connectionTimeoutSeconds" ), mConnectionTimeoutSeconds ).toInt();
  mMaxConcurrentConnections = map.value( QStringLiteral( "maxConcurrentConnections" ), mMaxConcurrentConnections ).toInt();
  mMaxMemoryUsageMB = map.value( QStringLiteral( "maxMemoryUsageMB" ), mMaxMemoryUsageMB ).toLongLong();
  mTimeoutThresholdSeconds = map.value( QStringLiteral( "timeoutThresholdSeconds" ), mTimeoutThresholdSeconds ).toInt();
  mProgressReportingIntervalMs = map.value( QStringLiteral( "progressReportingIntervalMs" ), mProgressReportingIntervalMs ).toInt();
  mMetadataCacheDurationSeconds = map.value( QStringLiteral( "metadataCacheDurationSeconds" ), mMetadataCacheDurationSeconds ).toInt();
  
  // Load provider-specific settings
  QVariantMap providerReadFlagsMap = map.value( QStringLiteral( "providerReadFlags" ) ).toMap();
  mProviderReadFlags.clear();
  for ( auto it = providerReadFlagsMap.begin(); it != providerReadFlagsMap.end(); ++it )
  {
    mProviderReadFlags[it.key()] = static_cast<Qgis::DataProviderReadFlags>( it.value().toInt() );
  }
  
  mProviderSettings = map.value( QStringLiteral( "providerSettings" ) ).value<QHash<QString, QVariantMap>>();
  
  if ( changed )
    emit configurationChanged();
  
  return true;
}

bool QgsLoadingProfile::saveToSettings( const QString &settingsKey ) const
{
  QgsSettings settings;
  QVariantMap map = toVariantMap();
  
  settings.beginGroup( settingsKey );
  for ( auto it = map.begin(); it != map.end(); ++it )
  {
    settings.setValue( it.key(), it.value() );
  }
  settings.endGroup();
  
  return true;
}

bool QgsLoadingProfile::loadFromSettings( const QString &settingsKey )
{
  QgsSettings settings;
  QVariantMap map;
  
  settings.beginGroup( settingsKey );
  const QStringList keys = settings.childKeys();
  for ( const QString &key : keys )
  {
    map[key] = settings.value( key );
  }
  settings.endGroup();
  
  return fromVariantMap( map );
}

bool QgsLoadingProfile::applyOptimizationSettings()
{
  QgsSettings settings;
  
  // Apply PostgreSQL connection pooling optimization
  if ( enableConnectionPooling() )
  {
    settings.setValue( QStringLiteral( "PostgreSQL/enable_connection_pooling_optimization" ), true );
    settings.setValue( QStringLiteral( "PostgreSQL/use_connection_pool" ), true );
    QgsDebugMsgLevel( QStringLiteral( "Applied PostgreSQL connection pooling optimization" ), 2 );
  }
  
  // Apply feature count skip optimization
  if ( skipFeatureCount() )
  {
    settings.setValue( QStringLiteral( "PostgreSQL/skip_feature_count" ), true );
    QgsDebugMsgLevel( QStringLiteral( "Applied feature count skip optimization" ), 2 );
  }
  
  // Apply extent calculation skip optimization
  if ( skipExtentCalculation() )
  {
    settings.setValue( QStringLiteral( "PostgreSQL/skip_extent_calculation" ), true );
    QgsDebugMsgLevel( QStringLiteral( "Applied extent calculation skip optimization" ), 2 );
  }
  
  // Apply metadata caching optimization
  if ( enableMetadataCaching() )
  {
    settings.setValue( QStringLiteral( "PostgreSQL/enable_metadata_caching" ), true );
    settings.setValue( QStringLiteral( "PostgreSQL/metadata_cache_duration" ), metadataCacheDurationSeconds() );
    QgsDebugMsgLevel( QStringLiteral( "Applied metadata caching optimization" ), 2 );
  }
  
  // Apply deferred style loading optimization
  if ( deferStyleLoading() )
  {
    settings.setValue( QStringLiteral( "qgis/defer_style_loading" ), true );
    QgsDebugMsgLevel( QStringLiteral( "Applied deferred style loading optimization" ), 2 );
  }
  
  // Apply connection timeout settings
  settings.setValue( QStringLiteral( "PostgreSQL/connection_timeout" ), connectionTimeoutSeconds() );
  
  // Apply progress reporting settings
  if ( enableProgressReporting() )
  {
    settings.setValue( QStringLiteral( "qgis/enable_progress_reporting" ), true );
    settings.setValue( QStringLiteral( "qgis/progress_reporting_interval" ), progressReportingIntervalMs() );
  }
  
  // Store profile name for reference
  settings.setValue( QStringLiteral( "qgis/active_loading_profile" ), profileName() );
  
  QgsDebugMsgLevel( QStringLiteral( "Applied optimization settings for profile: %1" ).arg( profileName() ), 2 );
  
  return true;
}

void QgsLoadingProfile::clearOptimizationSettings()
{
  QgsSettings settings;
  
  // Clear PostgreSQL optimizations
  settings.remove( QStringLiteral( "PostgreSQL/enable_connection_pooling_optimization" ) );
  settings.remove( QStringLiteral( "PostgreSQL/skip_feature_count" ) );
  settings.remove( QStringLiteral( "PostgreSQL/skip_extent_calculation" ) );
  settings.remove( QStringLiteral( "PostgreSQL/enable_metadata_caching" ) );
  settings.remove( QStringLiteral( "PostgreSQL/metadata_cache_duration" ) );
  settings.remove( QStringLiteral( "PostgreSQL/connection_timeout" ) );
  
  // Clear general optimizations
  settings.remove( QStringLiteral( "qgis/defer_style_loading" ) );
  settings.remove( QStringLiteral( "qgis/enable_progress_reporting" ) );
  settings.remove( QStringLiteral( "qgis/progress_reporting_interval" ) );
  settings.remove( QStringLiteral( "qgis/active_loading_profile" ) );
  
  QgsDebugMsgLevel( QStringLiteral( "Cleared optimization settings" ), 2 );
}

bool QgsLoadingProfile::operator==( const QgsLoadingProfile &other ) const
{
  return mProfileName == other.mProfileName &&
         mProfileType == other.mProfileType &&
         mDescription == other.mDescription &&
         mOptimizationFlags == other.mOptimizationFlags &&
         mConnectionTimeoutSeconds == other.mConnectionTimeoutSeconds &&
         mMaxConcurrentConnections == other.mMaxConcurrentConnections &&
         mMaxMemoryUsageMB == other.mMaxMemoryUsageMB &&
         mTimeoutThresholdSeconds == other.mTimeoutThresholdSeconds &&
         mProgressReportingIntervalMs == other.mProgressReportingIntervalMs &&
         mMetadataCacheDurationSeconds == other.mMetadataCacheDurationSeconds &&
         mProviderReadFlags == other.mProviderReadFlags &&
         mProviderSettings == other.mProviderSettings;
}

bool QgsLoadingProfile::operator!=( const QgsLoadingProfile &other ) const
{
  return !( *this == other );
}

QStringList QgsLoadingProfile::validate() const
{
  QStringList errors;
  
  if ( mProfileName.isEmpty() )
    errors << QStringLiteral( "Profile name cannot be empty" );
  
  if ( mConnectionTimeoutSeconds < 1 )
    errors << QStringLiteral( "Connection timeout must be at least 1 second" );
  
  if ( mMaxConcurrentConnections < 1 )
    errors << QStringLiteral( "Maximum concurrent connections must be at least 1" );
  
  if ( mMaxMemoryUsageMB < 1024 )
    errors << QStringLiteral( "Maximum memory usage must be at least 1024 MB" );
  
  if ( mTimeoutThresholdSeconds < 1 )
    errors << QStringLiteral( "Timeout threshold must be at least 1 second" );
  
  if ( mProgressReportingIntervalMs < 50 )
    errors << QStringLiteral( "Progress reporting interval must be at least 50 milliseconds" );
  
  return errors;
}

bool QgsLoadingProfile::isValid() const
{
  return validate().isEmpty();
}

QgsLoadingProfile QgsLoadingProfile::createDefaultProfile()
{
  return QgsLoadingProfile( ProfileType::Default );
}

QgsLoadingProfile QgsLoadingProfile::createFastLoadingProfile()
{
  return QgsLoadingProfile( ProfileType::FastLoading );
}

QgsLoadingProfile QgsLoadingProfile::createSafeLoadingProfile()
{
  return QgsLoadingProfile( ProfileType::SafeLoading );
}

QgsLoadingProfile QgsLoadingProfile::createDebugLoadingProfile()
{
  return QgsLoadingProfile( ProfileType::DebugLoading );
}

QString QgsLoadingProfile::profileTypeName( ProfileType type )
{
  switch ( type )
  {
    case ProfileType::Default:
      return QStringLiteral( "Default" );
    case ProfileType::FastLoading:
      return QStringLiteral( "Fast Loading" );
    case ProfileType::SafeLoading:
      return QStringLiteral( "Safe Loading" );
    case ProfileType::DebugLoading:
      return QStringLiteral( "Debug Loading" );
    case ProfileType::Custom:
      return QStringLiteral( "Custom" );
  }
  return QString();
}

QString QgsLoadingProfile::profileTypeDescription( ProfileType type )
{
  switch ( type )
  {
    case ProfileType::Default:
      return QStringLiteral( "Standard loading with moderate optimizations for balanced performance and safety" );
    case ProfileType::FastLoading:
      return QStringLiteral( "Maximum performance optimizations enabled for fastest loading times" );
    case ProfileType::SafeLoading:
      return QStringLiteral( "Conservative optimizations with full validation for critical operations" );
    case ProfileType::DebugLoading:
      return QStringLiteral( "Detailed monitoring and logging enabled for troubleshooting" );
    case ProfileType::Custom:
      return QStringLiteral( "User-defined configuration with custom optimization settings" );
  }
  return QString();
}

void QgsLoadingProfile::initializePredefinedProfile( ProfileType type )
{
  resetToDefaults();
  
  mProfileType = type;
  mProfileName = profileTypeName( type );
  mDescription = profileTypeDescription( type );
  
  switch ( type )
  {
    case ProfileType::Default:
      mOptimizationFlags = OptimizationFlag::EnableConnectionPooling |
                          OptimizationFlag::EnableMetadataCaching |
                          OptimizationFlag::EnableProgressReporting;
      break;
      
    case ProfileType::FastLoading:
      mOptimizationFlags = OptimizationFlag::AllOptimizations;
      mConnectionTimeoutSeconds = 10; // Faster timeout
      mMaxConcurrentConnections = 10; // More connections
      mMetadataCacheDurationSeconds = 600; // Longer cache (10 minutes)
      break;
      
    case ProfileType::SafeLoading:
      mOptimizationFlags = OptimizationFlag::EnableProgressReporting |
                          OptimizationFlag::ValidateDataIntegrity;
      mConnectionTimeoutSeconds = 30; // Longer timeout for safety
      mMaxConcurrentConnections = 3; // Fewer connections
      mMetadataCacheDurationSeconds = 120; // Shorter cache (2 minutes)
      break;
      
    case ProfileType::DebugLoading:
      mOptimizationFlags = OptimizationFlag::EnableProgressReporting |
                          OptimizationFlag::ValidateDataIntegrity;
      mProgressReportingIntervalMs = 50; // Frequent progress updates
      break;
      
    case ProfileType::Custom:
      // Keep current settings
      break;
  }
}

void QgsLoadingProfile::copyFrom( const QgsLoadingProfile &other )
{
  mProfileName = other.mProfileName;
  mProfileType = other.mProfileType;
  mDescription = other.mDescription;
  mOptimizationFlags = other.mOptimizationFlags;
  
  mConnectionTimeoutSeconds = other.mConnectionTimeoutSeconds;
  mMaxConcurrentConnections = other.mMaxConcurrentConnections;
  mMaxMemoryUsageMB = other.mMaxMemoryUsageMB;
  mTimeoutThresholdSeconds = other.mTimeoutThresholdSeconds;
  mProgressReportingIntervalMs = other.mProgressReportingIntervalMs;
  mMetadataCacheDurationSeconds = other.mMetadataCacheDurationSeconds;
  
  mProviderReadFlags = other.mProviderReadFlags;
  mProviderSettings = other.mProviderSettings;
}

void QgsLoadingProfile::resetToDefaults()
{
  mOptimizationFlags = OptimizationFlag::NoOptimizations;
  mConnectionTimeoutSeconds = 15;
  mMaxConcurrentConnections = 5;
  mMaxMemoryUsageMB = 4096; // 4GB
  mTimeoutThresholdSeconds = 60;
  mProgressReportingIntervalMs = 100;
  mMetadataCacheDurationSeconds = 300; // 5 minutes
  
  mProviderReadFlags.clear();
  mProviderSettings.clear();
}