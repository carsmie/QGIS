/***************************************************************************
                         qgsloadingprofile.h
                         ------------------
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

#ifndef QGSLOADINGPROFILE_H
#define QGSLOADINGPROFILE_H

#include "qgis_core.h"
#include "qgis.h"
#include <QObject>
#include <QString>
#include <QHash>
#include <QVariant>
#include <QDateTime>
#include <memory>

/**
 * \ingroup core
 * \class QgsLoadingProfile
 * \brief Configuration profile for optimized QGIS project loading
 * 
 * QgsLoadingProfile manages optimization settings for project loading operations,
 * providing a structured way to configure performance-related behaviors including:
 * 
 * - PostgreSQL connection pooling and reuse strategies
 * - Feature count operation skipping for large datasets
 * - Extent calculation deferral and optimization
 * - Style loading deferral and progressive application
 * - Memory usage limits and monitoring thresholds
 * - Provider-specific optimization flags
 * 
 * The profile system allows different optimization strategies for different
 * use cases, from fast loading for large projects to safe loading with
 * full validation for critical operations.
 * 
 * \since QGIS 3.30
 */
class CORE_EXPORT QgsLoadingProfile : public QObject
{
    Q_OBJECT

  public:

    /**
     * Predefined loading profile types for common use cases
     */
    enum class ProfileType
    {
      Default,          //!< Standard loading with moderate optimizations
      FastLoading,      //!< Maximum performance optimizations enabled
      SafeLoading,      //!< Conservative optimizations with full validation
      DebugLoading,     //!< Detailed monitoring and logging enabled
      Custom            //!< User-defined configuration
    };
    Q_ENUM( ProfileType )

    /**
     * Loading phase identifiers for progress tracking
     */
    enum class LoadingPhase
    {
      Initializing,           //!< Setting up loading context
      ConnectingToSources,    //!< Establishing data source connections
      LoadingLayerMetadata,   //!< Reading layer definitions and metadata
      LoadingGeometries,      //!< Loading spatial data
      ApplyingStyles,         //!< Applying cartographic styles
      BuildingIndexes,        //!< Creating spatial indexes
      Finalizing             //!< Completing project setup
    };
    Q_ENUM( LoadingPhase )

    /**
     * Configuration flags for optimization strategies
     */
    enum OptimizationFlag
    {
      NoOptimizations = 0,           //!< No optimizations applied
      EnableConnectionPooling = 1,   //!< Use PostgreSQL connection pooling
      SkipFeatureCount = 2,          //!< Skip expensive feature count operations
      SkipExtentCalculation = 4,     //!< Skip extent calculation during loading
      DeferStyleLoading = 8,         //!< Load styles after geometry registration
      EnableMetadataCaching = 16,    //!< Cache provider metadata
      EnableProgressReporting = 32,  //!< Show detailed progress information
      ValidateDataIntegrity = 64,    //!< Run integrity checks during loading
      AllOptimizations = EnableConnectionPooling | SkipFeatureCount | 
                        SkipExtentCalculation | DeferStyleLoading | 
                        EnableMetadataCaching | EnableProgressReporting
    };
    Q_DECLARE_FLAGS( OptimizationFlags, OptimizationFlag )
    Q_FLAG( OptimizationFlags )

    /**
     * Constructor for custom loading profile
     * \param profileName Human-readable profile name
     * \param parent Parent QObject
     */
    QgsLoadingProfile( const QString &profileName = QString(), QObject *parent = nullptr );

    /**
     * Constructor for predefined profile type
     * \param type Predefined profile type
     * \param parent Parent QObject
     */
    QgsLoadingProfile( ProfileType type, QObject *parent = nullptr );

    /**
     * Copy constructor
     */
    QgsLoadingProfile( const QgsLoadingProfile &other );

    /**
     * Assignment operator
     */
    QgsLoadingProfile &operator=( const QgsLoadingProfile &other );

    /**
     * Destructor
     */
    ~QgsLoadingProfile() override;

    // Profile identification and management

    /**
     * Get profile name
     * \returns Human-readable profile name
     */
    QString profileName() const { return mProfileName; }

    /**
     * Set profile name
     * \param name Human-readable profile name
     */
    void setProfileName( const QString &name );

    /**
     * Get profile type
     * \returns Profile type enum
     */
    ProfileType profileType() const { return mProfileType; }

    /**
     * Set profile type (resets configuration to predefined values)
     * \param type Profile type enum
     */
    void setProfileType( ProfileType type );

    /**
     * Get profile description
     * \returns Detailed profile description
     */
    QString description() const { return mDescription; }

    /**
     * Set profile description
     * \param description Detailed profile description
     */
    void setDescription( const QString &description );

    // Optimization configuration

    /**
     * Get optimization flags
     * \returns Current optimization flags
     */
    OptimizationFlags optimizationFlags() const { return mOptimizationFlags; }

    /**
     * Set optimization flags
     * \param flags Optimization flags to enable
     */
    void setOptimizationFlags( OptimizationFlags flags );

    /**
     * Enable specific optimization flag
     * \param flag Optimization flag to enable
     */
    void enableOptimization( OptimizationFlag flag );

    /**
     * Disable specific optimization flag
     * \param flag Optimization flag to disable
     */
    void disableOptimization( OptimizationFlag flag );

    /**
     * Check if optimization is enabled
     * \param flag Optimization flag to check
     * \returns True if optimization is enabled
     */
    bool isOptimizationEnabled( OptimizationFlag flag ) const;

    // Connection and provider settings

    /**
     * Check if connection pooling is enabled
     * \returns True if connection pooling is enabled
     */
    bool enableConnectionPooling() const;

    /**
     * Set connection pooling enabled state
     * \param enabled Whether to enable connection pooling
     */
    void setEnableConnectionPooling( bool enabled );

    /**
     * Get connection timeout in seconds
     * \returns Connection timeout in seconds
     */
    int connectionTimeoutSeconds() const { return mConnectionTimeoutSeconds; }

    /**
     * Set connection timeout
     * \param seconds Connection timeout in seconds
     */
    void setConnectionTimeoutSeconds( int seconds );

    /**
     * Get maximum concurrent connections per provider
     * \returns Maximum concurrent connections
     */
    int maxConcurrentConnections() const { return mMaxConcurrentConnections; }

    /**
     * Set maximum concurrent connections per provider
     * \param maxConnections Maximum concurrent connections
     */
    void setMaxConcurrentConnections( int maxConnections );

    // Data loading optimization

    /**
     * Check if feature count operations should be skipped
     * \returns True if feature count should be skipped
     */
    bool skipFeatureCount() const;

    /**
     * Set skip feature count enabled state
     * \param skip Whether to skip feature count operations
     */
    void setSkipFeatureCount( bool skip );

    /**
     * Check if extent calculation should be skipped
     * \returns True if extent calculation should be skipped
     */
    bool skipExtentCalculation() const;

    /**
     * Set skip extent calculation enabled state
     * \param skip Whether to skip extent calculation
     */
    void setSkipExtentCalculation( bool skip );

    /**
     * Check if style loading should be deferred
     * \returns True if style loading should be deferred
     */
    bool deferStyleLoading() const;

    /**
     * Set deferred style loading enabled state
     * \param defer Whether to defer style loading
     */
    void setDeferStyleLoading( bool defer );

    /**
     * Check if metadata caching is enabled
     * \returns True if metadata caching is enabled
     */
    bool enableMetadataCaching() const;

    /**
     * Set metadata caching enabled state
     * \param enabled Whether to enable metadata caching
     */
    void setEnableMetadataCaching( bool enabled );

    /**
     * Get metadata cache duration in seconds
     * \returns Cache duration in seconds
     */
    int metadataCacheDurationSeconds() const { return mMetadataCacheDurationSeconds; }

    /**
     * Set metadata cache duration
     * \param seconds Cache duration in seconds
     */
    void setMetadataCacheDurationSeconds( int seconds );

    // Memory and performance limits

    /**
     * Get maximum memory usage limit in MB
     * \returns Maximum memory usage in MB
     */
    qint64 maxMemoryUsageMB() const { return mMaxMemoryUsageMB; }

    /**
     * Set maximum memory usage limit
     * \param memoryMB Maximum memory usage in MB
     */
    void setMaxMemoryUsageMB( qint64 memoryMB );

    /**
     * Get operation timeout threshold in seconds
     * \returns Timeout threshold in seconds
     */
    int timeoutThresholdSeconds() const { return mTimeoutThresholdSeconds; }

    /**
     * Set operation timeout threshold
     * \param seconds Timeout threshold in seconds
     */
    void setTimeoutThresholdSeconds( int seconds );

    /**
     * Get progress reporting interval in milliseconds
     * \returns Progress reporting interval
     */
    int progressReportingIntervalMs() const { return mProgressReportingIntervalMs; }

    /**
     * Set progress reporting interval
     * \param intervalMs Progress reporting interval in milliseconds
     */
    void setProgressReportingIntervalMs( int intervalMs );

    // Provider-specific settings

    /**
     * Get provider-specific read flags
     * \param providerName Data provider name (e.g., "postgres", "ogr")
     * \returns Read flags for the provider
     */
    Qgis::DataProviderReadFlags providerReadFlags( const QString &providerName ) const;

    /**
     * Set provider-specific read flags
     * \param providerName Data provider name
     * \param flags Read flags for the provider
     */
    void setProviderReadFlags( const QString &providerName, Qgis::DataProviderReadFlags flags );

    /**
     * Get provider-specific setting
     * \param providerName Data provider name
     * \param settingName Setting name
     * \param defaultValue Default value if setting not found
     * \returns Setting value
     */
    QVariant providerSetting( const QString &providerName, const QString &settingName, 
                             const QVariant &defaultValue = QVariant() ) const;

    /**
     * Set provider-specific setting
     * \param providerName Data provider name
     * \param settingName Setting name
     * \param value Setting value
     */
    void setProviderSetting( const QString &providerName, const QString &settingName, const QVariant &value );

    // Validation and quality assurance

    /**
     * Check if data integrity validation is enabled
     * \returns True if data integrity validation is enabled
     */
    bool validateDataIntegrity() const;

    /**
     * Set data integrity validation enabled state
     * \param validate Whether to validate data integrity
     */
    void setValidateDataIntegrity( bool validate );

    /**
     * Check if progress reporting is enabled
     * \returns True if progress reporting is enabled
     */
    bool enableProgressReporting() const;

    /**
     * Set progress reporting enabled state
     * \param enabled Whether to enable progress reporting
     */
    void setEnableProgressReporting( bool enabled );

    // Serialization and persistence

    /**
     * Serialize profile to variant map
     * \returns Variant map containing profile configuration
     */
    QVariantMap toVariantMap() const;

    /**
     * Deserialize profile from variant map
     * \param map Variant map containing profile configuration
     * \returns True if deserialization was successful
     */
    bool fromVariantMap( const QVariantMap &map );

    /**
     * Save profile to settings
     * \param settingsKey Settings key for storing profile
     * \returns True if save was successful
     */
    bool saveToSettings( const QString &settingsKey ) const;

    /**
     * Load profile from settings
     * \param settingsKey Settings key for loading profile
     * \returns True if load was successful
     */
    bool loadFromSettings( const QString &settingsKey );

    /**
     * Apply profile optimization settings to QgsSettings
     * This enables the profile's optimizations system-wide for data providers
     * \returns True if settings were applied successfully
     */
    bool applyOptimizationSettings();

    /**
     * Clear optimization settings from QgsSettings
     * Restores default behavior by removing profile-specific settings
     */
    void clearOptimizationSettings();

    // Comparison and validation

    /**
     * Compare with another profile
     * \param other Profile to compare with
     * \returns True if profiles are equivalent
     */
    bool operator==( const QgsLoadingProfile &other ) const;

    /**
     * Inequality operator
     * \param other Profile to compare with
     * \returns True if profiles are different
     */
    bool operator!=( const QgsLoadingProfile &other ) const;

    /**
     * Validate profile configuration
     * \returns List of validation errors (empty if valid)
     */
    QStringList validate() const;

    /**
     * Check if profile is valid
     * \returns True if profile configuration is valid
     */
    bool isValid() const;

    // Static factory methods

    /**
     * Create default loading profile
     * \returns Default loading profile
     */
    static QgsLoadingProfile createDefaultProfile();

    /**
     * Create fast loading profile with all optimizations enabled
     * \returns Fast loading profile
     */
    static QgsLoadingProfile createFastLoadingProfile();

    /**
     * Create safe loading profile with conservative optimizations
     * \returns Safe loading profile
     */
    static QgsLoadingProfile createSafeLoadingProfile();

    /**
     * Create debug loading profile with detailed monitoring
     * \returns Debug loading profile
     */
    static QgsLoadingProfile createDebugLoadingProfile();

    /**
     * Get profile type name
     * \param type Profile type
     * \returns Human-readable profile type name
     */
    static QString profileTypeName( ProfileType type );

    /**
     * Get profile type description
     * \param type Profile type
     * \returns Detailed profile type description
     */
    static QString profileTypeDescription( ProfileType type );

  signals:

    /**
     * Emitted when profile configuration changes
     */
    void configurationChanged();

    /**
     * Emitted when optimization flags change
     * \param flags New optimization flags
     */
    void optimizationFlagsChanged( QgsLoadingProfile::OptimizationFlags flags );

    /**
     * Emitted when profile type changes
     * \param type New profile type
     */
    void profileTypeChanged( QgsLoadingProfile::ProfileType type );

  private:

    QString mProfileName;
    ProfileType mProfileType = ProfileType::Default;
    QString mDescription;
    OptimizationFlags mOptimizationFlags = OptimizationFlag::NoOptimizations;

    // Connection and provider settings
    int mConnectionTimeoutSeconds = 15;
    int mMaxConcurrentConnections = 5;

    // Memory and performance limits
    qint64 mMaxMemoryUsageMB = 4096; // 4GB default limit
    int mTimeoutThresholdSeconds = 60;
    int mProgressReportingIntervalMs = 100;

    // Metadata caching
    int mMetadataCacheDurationSeconds = 300; // 5 minutes

    // Provider-specific settings
    QHash<QString, Qgis::DataProviderReadFlags> mProviderReadFlags;
    QHash<QString, QVariantMap> mProviderSettings;

    // Internal methods
    void initializePredefinedProfile( ProfileType type );
    void copyFrom( const QgsLoadingProfile &other );
    void resetToDefaults();
};

Q_DECLARE_OPERATORS_FOR_FLAGS( QgsLoadingProfile::OptimizationFlags )

#endif // QGSLOADINGPROFILE_H