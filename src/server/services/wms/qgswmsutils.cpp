/***************************************************************************
                              qgswmsutils.cpp
                              -------------------------
  begin                : December 20 , 2016
  copyright            : (C) 2007 by Marco Hugentobler  ( parts from qgswmshandler)
                         (C) 2014 by Alessandro Pasotti ( parts from qgswmshandler)
                         (C) 2016 by David Marteau
  email                : marco dot hugentobler at karto dot baug dot ethz dot ch
                         a dot pasotti at itopen dot it
                         david dot marteau at 3liz dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                  *
 *                                                                         *
 ***************************************************************************/

#include "qgswmsutils.h"

#include "qgsexception.h"
#include "qgslayertree.h"
#include "qgsmediancut.h"
#include "qgsmodule.h"
#include "qgsproject.h"
#include "qgsserverprojectutils.h"
#include "qgswmsserviceexception.h"

#include <QRegularExpression>
#include <QString>
#include <QtEndian>

#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>
#include <zlib.h>

#ifdef HAVE_LIBDEFLATE
#include <libdeflate.h>
#endif

using namespace Qt::StringLiterals;

namespace
{
  void writePngChunk( QIODevice *device, const char *type, const unsigned char *data, quint32 length )
  {
    unsigned char header[8];
    qToBigEndian<quint32>( length, header );
    std::memcpy( header + 4, type, 4 );
    uLong crc = crc32( 0L, header + 4, 4 );
    if ( length > 0 )
      crc = crc32( crc, data, length );
    unsigned char trailer[4];
    qToBigEndian<quint32>( static_cast<quint32>( crc ), trailer );

    device->write( reinterpret_cast<const char *>( header ), 8 );
    if ( length > 0 )
      device->write( reinterpret_cast<const char *>( data ), length );
    device->write( reinterpret_cast<const char *>( trailer ), 4 );
  }

  /**
   * Writes \a image as 8-bit RGBA PNG using libdeflate if available, or zlib directly.
   *
   * Qt's PNG writer uses zlib's default level and libpng's adaptive row filtering,
   * and the filtering cannot be changed through the Qt API. Together they dominate
   * GetMap time for large or continuous-tone images (hillshades, rasters).
   * Here low compression levels use the cheap "up" filter, higher levels use the
   * same adaptive heuristic as libpng.
   *
   * libdeflate compresses 1.5-2x faster than zlib and usually produces smaller output,
   * but only works on a complete buffer, so the filtered image is kept in memory.
   * Very large images and levels libdeflate does not support use the zlib stream.
   */
  bool writePngFast( QIODevice *device, const QImage &image, int compressionLevel )
  {
    // un-premultiplied RGBA in byte order is exactly the PNG pixel layout
    const QImage rgba = image.convertToFormat( QImage::Format_RGBA8888 );
    if ( rgba.isNull() )
      return false;

    const int width = rgba.width();
    const int height = rgba.height();
    constexpr int bytesPerPixel = 4;
    const size_t stride = static_cast<size_t>( width ) * bytesPerPixel;

    std::vector<unsigned char> filteredImage;
#ifdef HAVE_LIBDEFLATE
    const size_t filteredImageSize = static_cast<size_t>( height ) * ( stride + 1 );
    std::unique_ptr<libdeflate_compressor, decltype( &libdeflate_free_compressor )> libdeflateCompressor( nullptr, &libdeflate_free_compressor );
    if ( filteredImageSize <= 256 * 1024 * 1024 )
      libdeflateCompressor.reset( libdeflate_alloc_compressor( compressionLevel ) );
    const bool useLibdeflate = static_cast<bool>( libdeflateCompressor );
    if ( useLibdeflate )
      filteredImage.reserve( filteredImageSize );
#else
    const bool useLibdeflate = false;
#endif

    z_stream zs {};
    if ( !useLibdeflate && deflateInit2( &zs, compressionLevel, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY ) != Z_OK )
      return false;

    device->write( "\x89PNG\r\n\x1a\n", 8 );

    unsigned char ihdr[13];
    qToBigEndian<quint32>( width, ihdr );
    qToBigEndian<quint32>( height, ihdr + 4 );
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // color type RGBA
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter method
    ihdr[12] = 0; // no interlacing
    writePngChunk( device, "IHDR", ihdr, sizeof( ihdr ) );

    if ( image.dotsPerMeterX() > 0 && image.dotsPerMeterY() > 0 )
    {
      unsigned char phys[9];
      qToBigEndian<quint32>( image.dotsPerMeterX(), phys );
      qToBigEndian<quint32>( image.dotsPerMeterY(), phys + 4 );
      phys[8] = 1; // unit is meter
      writePngChunk( device, "pHYs", phys, sizeof( phys ) );
    }

    std::vector<unsigned char> compressed( 1 << 17 );
    const std::vector<unsigned char> zeroRow( stride, 0 );
    std::vector<unsigned char> filtered[5];
    for ( std::vector<unsigned char> &row : filtered )
      row.resize( stride + 1 );

    auto deflateRow = [&]( const unsigned char *data, size_t length, int flush ) -> bool {
      zs.next_in = const_cast<Bytef *>( data );
      zs.avail_in = static_cast<uInt>( length );
      do
      {
        zs.next_out = compressed.data();
        zs.avail_out = static_cast<uInt>( compressed.size() );
        if ( deflate( &zs, flush ) == Z_STREAM_ERROR )
          return false;
        const size_t produced = compressed.size() - zs.avail_out;
        if ( produced > 0 )
          writePngChunk( device, "IDAT", compressed.data(), static_cast<quint32>( produced ) );
      } while ( zs.avail_out == 0 );
      return true;
    };

    auto filterRow = [&]( int filter, const unsigned char *row, const unsigned char *prev ) {
      unsigned char *out = filtered[filter].data();
      out[0] = static_cast<unsigned char>( filter );
      out++;
      switch ( filter )
      {
        case 0: // none
          std::memcpy( out, row, stride );
          break;
        case 1: // sub
          std::memcpy( out, row, bytesPerPixel );
          for ( size_t i = bytesPerPixel; i < stride; ++i )
            out[i] = row[i] - row[i - bytesPerPixel];
          break;
        case 2: // up
          for ( size_t i = 0; i < stride; ++i )
            out[i] = row[i] - prev[i];
          break;
        case 3: // average
          for ( size_t i = 0; i < stride; ++i )
            out[i] = row[i] - ( ( ( i >= bytesPerPixel ? row[i - bytesPerPixel] : 0 ) + prev[i] ) >> 1 );
          break;
        case 4: // paeth
          for ( size_t i = 0; i < stride; ++i )
          {
            const int a = i >= bytesPerPixel ? row[i - bytesPerPixel] : 0;
            const int b = prev[i];
            const int c = i >= bytesPerPixel ? prev[i - bytesPerPixel] : 0;
            const int p = a + b - c;
            const int pa = std::abs( p - a );
            const int pb = std::abs( p - b );
            const int pc = std::abs( p - c );
            out[i] = row[i] - ( ( pa <= pb && pa <= pc ) ? a : ( pb <= pc ? b : c ) );
          }
          break;
      }
    };

    bool ok = true;
    for ( int y = 0; y < height && ok; ++y )
    {
      const unsigned char *row = rgba.constScanLine( y );
      const unsigned char *prev = y > 0 ? rgba.constScanLine( y - 1 ) : zeroRow.data();

      int filter = compressionLevel == 0 ? 0 : 2;
      if ( compressionLevel >= 4 )
      {
        // libpng heuristic: pick the filter with the smallest sum of absolute (signed) values
        unsigned long bestSum = std::numeric_limits<unsigned long>::max();
        for ( int candidate = 0; candidate < 5; ++candidate )
        {
          filterRow( candidate, row, prev );
          const unsigned char *out = filtered[candidate].data() + 1;
          unsigned long sum = 0;
          for ( size_t i = 0; i < stride; ++i )
            sum += out[i] < 128 ? out[i] : 256 - out[i];
          if ( sum < bestSum )
          {
            bestSum = sum;
            filter = candidate;
          }
        }
      }
      else
      {
        filterRow( filter, row, prev );
      }
      if ( useLibdeflate )
        filteredImage.insert( filteredImage.end(), filtered[filter].begin(), filtered[filter].end() );
      else
        ok = deflateRow( filtered[filter].data(), stride + 1, Z_NO_FLUSH );
    }

#ifdef HAVE_LIBDEFLATE
    if ( useLibdeflate )
    {
      std::vector<unsigned char> zlibData( libdeflate_zlib_compress_bound( libdeflateCompressor.get(), filteredImage.size() ) );
      const size_t zlibSize = libdeflate_zlib_compress( libdeflateCompressor.get(), filteredImage.data(), filteredImage.size(), zlibData.data(), zlibData.size() );
      if ( zlibSize == 0 )
        return false;
      // same IDAT chunk size as the zlib stream
      for ( size_t offset = 0; offset < zlibSize; offset += compressed.size() )
        writePngChunk( device, "IDAT", zlibData.data() + offset, static_cast<quint32>( std::min( compressed.size(), zlibSize - offset ) ) );
    }
    else
#endif
    {
      ok = ok && deflateRow( nullptr, 0, Z_FINISH );
      deflateEnd( &zs );
    }
    if ( !ok )
      return false;

    writePngChunk( device, "IEND", nullptr, 0 );
    return true;
  }
} // namespace

namespace QgsWms
{
  QString implementationVersion()
  {
    return u"1.3.0"_s;
  }

  QUrl serviceUrl( const QgsServerRequest &request, const QgsProject *project, const QgsServerSettings &settings )
  {
    QUrl href;
    href.setUrl( QgsServerProjectUtils::wmsServiceUrl( project ? *project : *QgsProject::instance(), request, settings ) );

    // Build default url
    if ( href.isEmpty() )
    {
      static const QSet<QString> sFilter { u"REQUEST"_s, u"VERSION"_s, u"SERVICE"_s, u"LAYERS"_s, u"STYLES"_s, u"SLD_VERSION"_s, u"_DC"_s };

      href = request.originalUrl();
      QUrlQuery q( href );

      const QList<QPair<QString, QString>> queryItems = q.queryItems();
      for ( const QPair<QString, QString> &param : queryItems )
      {
        if ( sFilter.contains( param.first.toUpper() ) )
          q.removeAllQueryItems( param.first );
      }

      href.setQuery( q );
    }

    return href;
  }


  ImageOutputFormat parseImageFormat( const QString &format )
  {
    if ( format.compare( "png"_L1, Qt::CaseInsensitive ) == 0 || format.compare( "image/png"_L1, Qt::CaseInsensitive ) == 0 )
    {
      return ImageOutputFormat::PNG;
    }
    else if ( format.compare( "jpg "_L1, Qt::CaseInsensitive ) == 0 || format.compare( "image/jpeg"_L1, Qt::CaseInsensitive ) == 0 )
    {
      return ImageOutputFormat::JPEG;
    }
    else if ( format.compare( "webp"_L1, Qt::CaseInsensitive ) == 0 || format.compare( "image/webp"_L1, Qt::CaseInsensitive ) == 0 )
    {
      return ImageOutputFormat::WEBP;
    }
    else
    {
      // lookup for png with mode
      const thread_local QRegularExpression modeExpr = QRegularExpression( u"image/png\\s*;\\s*mode=([^;]+)"_s, QRegularExpression::CaseInsensitiveOption );

      const QRegularExpressionMatch match = modeExpr.match( format );
      const QString mode = match.captured( 1 );
      if ( mode.compare( "16bit"_L1, Qt::CaseInsensitive ) == 0 )
        return ImageOutputFormat::PNG16;
      if ( mode.compare( "8bit"_L1, Qt::CaseInsensitive ) == 0 )
        return ImageOutputFormat::PNG8;
      if ( mode.compare( "1bit"_L1, Qt::CaseInsensitive ) == 0 )
        return ImageOutputFormat::PNG1;
    }

    return ImageOutputFormat::Unknown;
  }

  // Write image response
  void writeImage( QgsServerResponse &response, QImage &img, const QString &formatStr, int imageQuality, int pngCompressionLevel )
  {
    const ImageOutputFormat outputFormat = parseImageFormat( formatStr );
    QImage result;
    QString saveFormat;
    QString contentType;
    switch ( outputFormat )
    {
      case ImageOutputFormat::PNG:
        result = img;
        contentType = u"image/png"_s;
        saveFormat = u"PNG"_s;
        break;
      case ImageOutputFormat::PNG8:
      {
        QVector<QRgb> colorTable;

        // Rendering is made with the format QImage::Format_ARGB32_Premultiplied
        // So we need to convert it in QImage::Format_ARGB32 in order to properly build
        // the color table.
        const QImage img256 = img.convertToFormat( QImage::Format_ARGB32 );
        medianCut( colorTable, 256, img256 );
        result = img256.convertToFormat( QImage::Format_Indexed8, colorTable, Qt::ColorOnly | Qt::ThresholdDither | Qt::ThresholdAlphaDither | Qt::NoOpaqueDetection );
      }
        contentType = u"image/png"_s;
        saveFormat = u"PNG"_s;
        break;
      case ImageOutputFormat::PNG16:
        result = img.convertToFormat( QImage::Format_ARGB4444_Premultiplied );
        contentType = u"image/png"_s;
        saveFormat = u"PNG"_s;
        break;
      case ImageOutputFormat::PNG1:
        result = img.convertToFormat( QImage::Format_Mono, Qt::MonoOnly | Qt::ThresholdDither | Qt::ThresholdAlphaDither | Qt::NoOpaqueDetection );
        contentType = u"image/png"_s;
        saveFormat = u"PNG"_s;
        break;
      case ImageOutputFormat::JPEG:
        result = img;
        contentType = u"image/jpeg"_s;
        saveFormat = u"JPEG"_s;
        break;
      case ImageOutputFormat::WEBP:
        result = img;
        contentType = u"image/webp"_s;
        saveFormat = u"WEBP"_s;
        break;
      case ImageOutputFormat::Unknown:
        QgsMessageLog::logMessage( u"Unsupported format string %1"_s.arg( formatStr ) );
        saveFormat = u"Unknown"_s;
        break;
    }

    // Preserve DPI, some conversions, in particular the one for 8bit will drop this information
    result.setDotsPerMeterX( img.dotsPerMeterX() );
    result.setDotsPerMeterY( img.dotsPerMeterY() );

    if ( outputFormat != ImageOutputFormat::Unknown )
    {
      response.setHeader( "Content-Type", contentType );
      if ( saveFormat == "JPEG"_L1 || saveFormat == "WEBP"_L1 )
      {
        result.save( response.io(), qPrintable( saveFormat ), imageQuality );
      }
      else if ( outputFormat == ImageOutputFormat::PNG && pngCompressionLevel >= 0 && pngCompressionLevel <= 9 )
      {
        if ( !writePngFast( response.io(), result, pngCompressionLevel ) )
        {
          throw QgsException( u"Failed to encode PNG image"_s );
        }
      }
      else
      {
        result.save( response.io(), qPrintable( saveFormat ) );
      }
    }
    else
    {
      QgsWmsParameter parameter( QgsWmsParameter::FORMAT );
      parameter.mValue = formatStr;
      throw QgsBadRequestException( QgsServiceException::OGC_InvalidFormat, parameter );
    }
  }

  /**
   * Collects the \a acceptableLayersAndRequestNames recursively, a hash of all the layers that can be rendered and for each a list of the layer names requesting it.
   * It needs the \a project for properties and the \a group to analyze the current layer tree. Also the \a requestedLayerNames. If no \a requestedLayerNames are passed,
   * you will receive back all the layers except the ones hidden in an opaque group.
   * When an opaque group is in the \a requestedLayerNames, the children of this opaque group are passed back as well.
   * The \a requestedParentNames are used for the recursive collecting of the list of requested layers and groups.
   * When the \a groupIsAnOpaqueChild, it should continue to allow the layers to be rendered but not add the following group and layer names to the list of requested names.
  */
  void _collectAcceptableLayersAndRequestNames(
    QHash<const QgsMapLayer *, QStringList> &acceptableLayersAndRequestNames,
    const QgsProject &project,
    const QStringList &requestedLayerNames,
    const QgsLayerTreeGroup *group,
    QStringList requestedParentNames = QStringList(),
    bool groupIsAnOpaqueChild = false
  )
  {
    //get group nickname
    QString groupName = group->serverProperties()->shortName();
    if ( groupName.isEmpty() )
      groupName = group->name();

    bool projectIsRequested = ( requestedLayerNames.contains( QgsServerProjectUtils::wmsRootName( project ) ) || requestedLayerNames.contains( project.title() ) );
    bool groupIsRequested = requestedLayerNames.contains( groupName );

    // append the group to the list, when it's explicitly requested and it's not already a child of an opaque group
    if ( groupIsRequested && !groupIsAnOpaqueChild )
      requestedParentNames << groupName;

    // the group should not be opaque or explicitly requested (by the groupname or by the project name)
    if ( ( group->wmsGroupRequestMode() != Qgis::WmsGroupRequestMode::Opaque ) || groupIsRequested || projectIsRequested )
    {
      // when the current group is opaque or the previous groups have been opaque it is an opaque child and should not be requestable
      bool isOpaqueChild = ( group->wmsGroupRequestMode() == Qgis::WmsGroupRequestMode::Opaque ) || groupIsAnOpaqueChild;

      for ( QgsLayerTreeNode *child : group->children() )
      {
        if ( QgsLayerTree::isGroup( child ) )
        {
          auto subgroup = static_cast<const QgsLayerTreeGroup *>( child );
          _collectAcceptableLayersAndRequestNames( acceptableLayersAndRequestNames, project, requestedLayerNames, subgroup, requestedParentNames, isOpaqueChild );
        }
        else if ( QgsLayerTree::isLayer( child ) )
        {
          auto layernode = static_cast<const QgsLayerTreeLayer *>( child );
          const QgsMapLayer *layer = layernode->layer();
          if ( !layer )
            continue;

          //get layer nickname
          QString name = layer->serverProperties()->shortName();
          if ( QgsServerProjectUtils::wmsUseLayerIds( project ) )
          {
            name = layer->id();
          }
          else if ( name.isEmpty() )
          {
            name = layer->name();
          }

          QStringList requestedNames = requestedParentNames;
          // when the layer is explicitly requested and it's not an opaque child, then add it to the requested names
          if ( requestedLayerNames.contains( name ) && !isOpaqueChild )
          {
            requestedNames << name;
          }
          // we add the layer to the map when it's requested (or no requestedLayerNames are passed)
          if ( !requestedNames.isEmpty() || requestedLayerNames.isEmpty() || projectIsRequested )
            acceptableLayersAndRequestNames.insert( layer, requestedNames );
        }
      }
    }
  }

  void collectAcceptableLayersAndRequestNames( QHash<const QgsMapLayer *, QStringList> &acceptableLayersAndRequestNames, const QgsProject &project, const QStringList &requestedLayerNames )
  {
    //Call function used for recursive collect based on the layer tree root
    _collectAcceptableLayersAndRequestNames( acceptableLayersAndRequestNames, project, requestedLayerNames, project.layerTreeRoot() );
  }

} // namespace QgsWms
