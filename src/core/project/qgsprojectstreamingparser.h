/***************************************************************************
                         qgsprojectstreamingparser.h
                         ---------------------------
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

#ifndef QGSPROJECTSTREAMINGPARSER_H
#define QGSPROJECTSTREAMINGPARSER_H

#include <QObject>
#include <QXmlStreamReader>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QIODevice>
#include <QTimer>
#include <functional>

#include "qgis_core.h"

class QgsProject;
class QgsMapLayer;

/**
 * \ingroup core
 * \brief Streaming XML parser for large QGIS project files
 *
 * The QgsProjectStreamingParser provides incremental parsing of QGIS project XML files
 * without loading the entire DOM tree into memory. This is particularly beneficial for
 * large project files (>100MB) where traditional DOM parsing can consume significant
 * memory and time.
 *
 * Key features:
 * - Incremental parsing with minimal memory footprint
 * - Prioritized element processing (essential elements first)
 * - Parallel processing support for independent elements
 * - Progress reporting and cancellation support
 * - Lazy loading of complex elements (layers, layouts)
 * - Memory-efficient handling of large element collections
 *
 * \since QGIS 3.34
 */
class CORE_EXPORT QgsProjectStreamingParser : public QObject
{
    Q_OBJECT

  public:

    /**
     * Priority levels for different XML elements during parsing
     */
    enum class ElementPriority
    {
      Critical = 0,    //!< Project metadata, CRS, core settings - parsed first
      High = 1,        //!< Variables, properties, basic configuration
      Medium = 2,      //!< Layer tree structure, essential layers
      Low = 3,         //!< Non-essential layers, styles
      Background = 4   //!< Layouts, annotations, auxiliary data
    };

    /**
     * Parsing strategies for different use cases
     */
    enum class ParsingStrategy
    {
      Essential,       //!< Parse only critical and high priority elements
      Progressive,     //!< Parse in priority order with progress reporting
      Complete,        //!< Parse all elements (traditional behavior)
      LazyLoad        //!< Parse structure only, defer content loading
    };

    /**
     * Configuration for streaming parser behavior
     */
    struct ParsingConfig
    {
      ParsingStrategy strategy = ParsingStrategy::Progressive;
      int maxMemoryUsageMB = 512;              //!< Maximum memory usage before forcing cleanup
      bool enableParallelProcessing = true;    //!< Enable parallel processing of independent elements
      bool enableLazyLoading = true;           //!< Enable lazy loading for large elements
      int progressReportIntervalMs = 100;     //!< Progress reporting interval
      QStringList priorityLayers;             //!< Layer IDs to prioritize during loading
      QStringList deferredElements;           //!< Element names to defer until later
    };

    /**
     * Information about a parsed element
     */
    struct ElementInfo
    {
      QString name;                    //!< Element name
      QString id;                      //!< Element ID (if applicable)
      ElementPriority priority;        //!< Element priority
      qint64 sizeBytesEstimate;       //!< Estimated memory size
      bool isDeferred = false;         //!< Whether element is deferred
      QDomElement domElement;          //!< Parsed DOM element (if fully loaded)
    };

    /**
     * Callback function type for element processing
     * \param elementInfo Information about the parsed element
     * \param project The project being loaded
     * \returns true if processing was successful, false to abort
     */
    using ElementProcessorCallback = std::function<bool( const ElementInfo &elementInfo, QgsProject *project )>;

    /**
     * Constructor
     * \param parent Parent object
     */
    explicit QgsProjectStreamingParser( QObject *parent = nullptr );

    /**
     * Destructor
     */
    ~QgsProjectStreamingParser() override;

    /**
     * Set the parsing configuration
     * \param config The configuration to use
     */
    void setParsingConfig( const ParsingConfig &config );

    /**
     * Get the current parsing configuration
     * \returns The current configuration
     */
    ParsingConfig parsingConfig() const { return mConfig; }

    /**
     * Register a callback for processing specific element types
     * \param elementName The XML element name to process
     * \param callback The callback function to invoke
     */
    void registerElementProcessor( const QString &elementName, ElementProcessorCallback callback );

    /**
     * Parse a project file using streaming approach
     * \param filename Path to the project file
     * \param project The project to load data into
     * \returns true if parsing was successful
     */
    bool parseProjectFile( const QString &filename, QgsProject *project );

    /**
     * Parse from an already opened device
     * \param device The device to read from
     * \param project The project to load data into
     * \returns true if parsing was successful
     */
    bool parseFromDevice( QIODevice *device, QgsProject *project );

    /**
     * Cancel the current parsing operation
     */
    void cancelParsing();

    /**
     * Check if parsing was cancelled
     * \returns true if parsing was cancelled
     */
    bool isCancelled() const { return mCancelled; }

    /**
     * Get the current parsing progress (0-100)
     * \returns Progress percentage
     */
    int progress() const { return mProgress; }

    /**
     * Get parsing statistics
     * \returns Map of statistic name to value
     */
    QVariantMap getParsingStatistics() const;

    /**
     * Get elements that were deferred during parsing
     * \returns List of deferred element information
     */
    QList<ElementInfo> getDeferredElements() const { return mDeferredElements; }

    /**
     * Process previously deferred elements
     * \param elementNames Specific element names to process, or empty for all
     * \returns true if processing was successful
     */
    bool processDeferredElements( const QStringList &elementNames = QStringList() );

  signals:

    /**
     * Emitted when parsing progress changes
     * \param progress Progress percentage (0-100)
     * \param elementName Name of currently parsed element
     */
    void progressChanged( int progress, const QString &elementName );

    /**
     * Emitted when a parsing error occurs
     * \param error Error description
     */
    void errorOccurred( const QString &error );

    /**
     * Emitted when an element is successfully parsed
     * \param elementInfo Information about the parsed element
     */
    void elementParsed( const QgsProjectStreamingParser::ElementInfo &elementInfo );

    /**
     * Emitted when parsing is completed
     * \param success Whether parsing was successful
     * \param statistics Parsing statistics
     */
    void parsingCompleted( bool success, const QVariantMap &statistics );

  private slots:
    void updateProgress();

  private:

    //! Determine the priority of an XML element
    ElementPriority getElementPriority( const QString &elementName ) const;

    //! Estimate the memory size of an element based on its content
    qint64 estimateElementSize( const QXmlStreamReader &reader ) const;

    //! Check if an element should be deferred based on current config and memory usage
    bool shouldDeferElement( const QString &elementName, qint64 estimatedSize ) const;

    //! Parse a single element using streaming approach
    bool parseElement( QXmlStreamReader &reader, QgsProject *project );

    //! Convert streaming element to DOM element for processing
    QDomElement streamToDomElement( QXmlStreamReader &reader );

    //! Process elements according to their priority
    bool processElementsByPriority( QgsProject *project );

    //! Get the current memory usage in MB
    qint64 getCurrentMemoryUsageMB() const;

    //! Clean up parsed elements to free memory
    void cleanupMemory();

    //! Initialize element processors with default handlers
    void initializeDefaultProcessors();

    ParsingConfig mConfig;
    QMap<QString, ElementProcessorCallback> mElementProcessors;
    QList<ElementInfo> mParsedElements;
    QList<ElementInfo> mDeferredElements;
    
    // Parsing state
    bool mCancelled = false;
    int mProgress = 0;
    qint64 mTotalBytes = 0;
    qint64 mParsedBytes = 0;
    QString mCurrentElement;
    
    // Statistics
    QVariantMap mStatistics;
    QDateTime mParsingStartTime;
    qint64 mInitialMemoryUsage = 0;
    qint64 mPeakMemoryUsage = 0;
    int mElementsProcessed = 0;
    int mElementsDeferred = 0;
    
    // Progress reporting
    QTimer *mProgressTimer = nullptr;
};

#endif // QGSPROJECTSTREAMINGPARSER_H