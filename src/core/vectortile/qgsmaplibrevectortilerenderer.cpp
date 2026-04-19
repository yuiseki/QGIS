/***************************************************************************
  qgsmaplibrevectortilerenderer.cpp
  --------------------------------------
  Date                 : April 2026
  Copyright            : (C) 2026 by QGIS Developers
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsmaplibrevectortilerenderer.h"

#if __has_include("qgsmaplibreconfig.h")
#include "qgsmaplibreconfig.h"
#endif

#include "qgslogger.h"
#include "qgsmaptopixel.h"
#include "qgsrectangle.h"
#include "qgsrendercontext.h"
#include "qgsvectortilerenderer.h"

#include <QDebug>
#include <QDomElement>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QPen>

#include <cmath>

#ifdef WITH_MAPLIBRE
#include "qgsmaplibremetal_p.h"

#include <QDir>
#include <QStandardPaths>

#include <QMapLibre/Map>
#include <QMapLibre/Settings>
#endif

class QgsMapLibreVectorTileRenderer::Private
{
  public:
#ifdef WITH_MAPLIBRE
    std::unique_ptr<QMapLibre::Map> map;
    void *metalLayer = nullptr;  // opaque CAMetalLayer* on macOS
    int widthPx = 0;
    int heightPx = 0;
    qreal pixelRatio = 1.0;
#endif
};

QgsMapLibreVectorTileRenderer::QgsMapLibreVectorTileRenderer()
  : mPrivate( std::make_unique<Private>() )
{
}

QgsMapLibreVectorTileRenderer::~QgsMapLibreVectorTileRenderer() = default;

QString QgsMapLibreVectorTileRenderer::type() const
{
  return QStringLiteral( "maplibre" );
}

QgsMapLibreVectorTileRenderer *QgsMapLibreVectorTileRenderer::clone() const
{
  QgsMapLibreVectorTileRenderer *r = new QgsMapLibreVectorTileRenderer();
  r->mStyleUrl = mStyleUrl;
  return r;
}

#ifdef WITH_MAPLIBRE
namespace
{
  // Web Mercator <-> lat/lon helpers. Assumes the QGIS render extent is in
  // EPSG:3857 (which matches maplibre's only supported CRS).
  constexpr double MERCATOR_RADIUS = 20037508.342789244;

  double mercatorXToLon( double x )
  {
    return x / MERCATOR_RADIUS * 180.0;
  }

  double mercatorYToLat( double y )
  {
    return std::atan( std::sinh( y / MERCATOR_RADIUS * M_PI ) ) * 180.0 / M_PI;
  }

  // Slippy-map zoom level from metres-per-pixel (at the given latitude).
  double zoomFromMapUnitsPerPixel( double mupp, double latDeg )
  {
    const double metresPerPixelAtEquatorZ0 = 156543.03392804097;
    const double cosLat = std::cos( latDeg * M_PI / 180.0 );
    if ( mupp <= 0.0 || cosLat <= 0.0 )
      return 0.0;
    return std::log2( metresPerPixelAtEquatorZ0 * cosLat / mupp );
  }
}
#endif // WITH_MAPLIBRE

void QgsMapLibreVectorTileRenderer::startRender( QgsRenderContext &context, int tileZoom, const QgsTileRange &tileRange )
{
  Q_UNUSED( tileZoom )
  Q_UNUSED( tileRange )

#ifdef WITH_MAPLIBRE
  const QSize viewportSize = context.outputSize();
  const qreal pixelRatio = context.devicePixelRatio();
  const QSize effectiveSize = viewportSize.isEmpty() ? QSize( 512, 512 ) : viewportSize;
  const qreal effectivePixelRatio = pixelRatio > 0 ? pixelRatio : 1.0;

  const int widthPx = static_cast<int>( effectiveSize.width() * effectivePixelRatio );
  const int heightPx = static_cast<int>( effectiveSize.height() * effectivePixelRatio );

  mPrivate->widthPx = widthPx;
  mPrivate->heightPx = heightPx;
  mPrivate->pixelRatio = effectivePixelRatio;

  QElapsedTimer t_layer;
  t_layer.start();
  mPrivate->metalLayer = QgsMapLibreMetal::createOffscreenLayer( widthPx, heightPx );
  const qint64 ms_layer = t_layer.elapsed();
  if ( mPrivate->metalLayer == nullptr )
  {
    QgsDebugError( QStringLiteral( "Failed to create offscreen Metal layer (size=%1x%2)" ).arg( widthPx ).arg( heightPx ) );
    return;
  }

  // Continuous mode: we drive render() ourselves. Static mode would have
  // disabled label fade-in (StaticPlacement) but it does not render at
  // all when only render() is called - it expects startStaticRender +
  // staticRenderFinished, which we couldn't get to fire reliably from a
  // QGIS parallel render worker thread.
  QMapLibre::Settings settings;
  settings.setMapMode( QMapLibre::Settings::MapMode::Continuous );

  // Persist tile data across render cycles so pan/zoom to a
  // previously-visited area doesn't re-fetch tiles over the network.
  // maplibre keeps the cache in a small SQLite file.
  const QString cacheDir = QStandardPaths::writableLocation( QStandardPaths::CacheLocation );
  if ( !cacheDir.isEmpty() )
  {
    QDir().mkpath( cacheDir );
    settings.setCacheDatabasePath( cacheDir + QStringLiteral( "/maplibre-tiles.sqlite" ) );
    settings.setCacheDatabaseMaximumSize( 256 * 1024 * 1024 );  // 256 MiB
  }

  QElapsedTimer t_map;
  t_map.start();
  mPrivate->map = std::make_unique<QMapLibre::Map>(
    nullptr,
    settings,
    effectiveSize,
    effectivePixelRatio
  );
  const qint64 ms_map_ctor = t_map.elapsed();

  QElapsedTimer t_style;
  t_style.start();
  if ( !mStyleUrl.isEmpty() )
  {
    mPrivate->map->setStyleUrl( mStyleUrl );
  }
  // Disable maplibre's tile / label fade-in animations. They normally span
  // 300-500 ms and would otherwise leave readbacks captured mid-fade with
  // semi-transparent labels and missing terrain.
  mPrivate->map->setTransitionOptions( 0, 0 );
  const qint64 ms_style = t_style.elapsed();

  QElapsedTimer t_renderer;
  t_renderer.start();
  mPrivate->map->createRenderer( mPrivate->metalLayer );
  const qint64 ms_renderer = t_renderer.elapsed();

  qDebug().noquote() << QStringLiteral(
    "startRender timing: layer=%1 mapCtor=%2 setStyle=%3 createRenderer=%4 [ms]" )
    .arg( ms_layer ).arg( ms_map_ctor ).arg( ms_style ).arg( ms_renderer );

  QgsDebugMsgLevel(
    QStringLiteral( "QMapLibre::Map + Metal renderer attached size=%1x%2 physical=%3x%4 styleUrl=%5" )
      .arg( effectiveSize.width() ).arg( effectiveSize.height() )
      .arg( widthPx ).arg( heightPx )
      .arg( mStyleUrl ),
    1
  );
#else
  Q_UNUSED( context )
#endif
}

void QgsMapLibreVectorTileRenderer::stopRender( QgsRenderContext &context )
{
  Q_UNUSED( context )

#ifdef WITH_MAPLIBRE
  if ( mPrivate->map )
  {
    mPrivate->map->destroyRenderer();
    mPrivate->map.reset();
  }

  if ( mPrivate->metalLayer )
  {
    QgsMapLibreMetal::destroyLayer( mPrivate->metalLayer );
    mPrivate->metalLayer = nullptr;
  }
#endif
}

void QgsMapLibreVectorTileRenderer::renderBackground( QgsRenderContext &context )
{
#ifdef WITH_MAPLIBRE
  if ( !mPrivate->map || !mPrivate->metalLayer )
    return;

  // Map the QGIS viewport centre/scale to maplibre camera (lat/lon + zoom).
  // This assumes the project is in EPSG:3857; reprojection comes in a later
  // phase.
  const QgsRectangle extent = context.extent();
  const double cx = ( extent.xMinimum() + extent.xMaximum() ) / 2.0;
  const double cy = ( extent.yMinimum() + extent.yMaximum() ) / 2.0;
  const double lon = mercatorXToLon( cx );
  const double lat = mercatorYToLat( cy );
  const double mupp = context.mapToPixel().mapUnitsPerPixel();
  const double zoom = zoomFromMapUnitsPerPixel( mupp, lat );

  mPrivate->map->setCoordinateZoom( { lat, lon }, zoom );

  const QImage img = QgsMapLibreMetal::renderOnce(
    mPrivate->map.get(), mPrivate->metalLayer, mPrivate->widthPx, mPrivate->heightPx
  );
  if ( img.isNull() )
  {
    QgsDebugError( QStringLiteral( "renderOnce returned null image" ) );
    return;
  }

  QgsDebugMsgLevel(
    QStringLiteral( "renderOnce image: %1x%2 lat=%3 lon=%4 zoom=%5" )
      .arg( img.width() ).arg( img.height() )
      .arg( lat ).arg( lon ).arg( zoom ),
    1
  );

  QPainter *p = context.painter();
  if ( !p )
    return;

  // The QImage is in physical pixels. Tell Qt about the device pixel ratio
  // so drawImage() scales it down to logical coordinates, which matches
  // what QGIS's painter expects.
  QImage scaled = img;
  scaled.setDevicePixelRatio( mPrivate->pixelRatio );

  p->save();
  p->drawImage( QPoint( 0, 0 ), scaled );
  p->restore();
#else
  Q_UNUSED( context )
#endif
}

void QgsMapLibreVectorTileRenderer::renderTile( const QgsVectorTileRendererData &tile, QgsRenderContext &context )
{
  // Nothing to do here: the full viewport was already rendered by maplibre
  // in renderBackground(). QGIS's per-tile callbacks are a no-op for us.
  Q_UNUSED( tile )
  Q_UNUSED( context )
}

void QgsMapLibreVectorTileRenderer::renderSelectedFeatures( const QList<QgsFeature> &selection, QgsRenderContext &context )
{
  Q_UNUSED( selection )
  Q_UNUSED( context )
}

bool QgsMapLibreVectorTileRenderer::willRenderFeature( const QgsFeature &feature, int tileZoom, const QString &layerName, QgsRenderContext &context )
{
  Q_UNUSED( feature )
  Q_UNUSED( tileZoom )
  Q_UNUSED( layerName )
  Q_UNUSED( context )
  return false;
}

void QgsMapLibreVectorTileRenderer::writeXml( QDomElement &elem, const QgsReadWriteContext &context ) const
{
  Q_UNUSED( context )
  elem.setAttribute( QStringLiteral( "styleUrl" ), mStyleUrl );
}

void QgsMapLibreVectorTileRenderer::readXml( const QDomElement &elem, const QgsReadWriteContext &context )
{
  Q_UNUSED( context )
  mStyleUrl = elem.attribute( QStringLiteral( "styleUrl" ) );
}
