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
#include "qgsrendercontext.h"
#include "qgsvectortilerenderer.h"

#include <QDomElement>
#include <QPainter>
#include <QPen>

#ifdef WITH_MAPLIBRE
#include "qgsmaplibremetal_p.h"

#include <QMapLibre/Map>
#include <QMapLibre/Settings>
#endif

class QgsMapLibreVectorTileRenderer::Private
{
  public:
#ifdef WITH_MAPLIBRE
    std::unique_ptr<QMapLibre::Map> map;
    void *metalLayer = nullptr;  // opaque CAMetalLayer* on macOS
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

void QgsMapLibreVectorTileRenderer::startRender( QgsRenderContext &context, int tileZoom, const QgsTileRange &tileRange )
{
  Q_UNUSED( tileZoom )
  Q_UNUSED( tileRange )

#ifdef WITH_MAPLIBRE
  const QSize viewportSize = context.outputSize();
  const qreal pixelRatio = context.devicePixelRatio();
  const QSize effectiveSize = viewportSize.isEmpty() ? QSize( 512, 512 ) : viewportSize;

  // Allocate an offscreen CAMetalLayer so QMapLibre's Metal backend has
  // something to render into. QMapLibre takes a non-owning raw pointer to
  // the layer; we keep ownership and free it in stopRender().
  const int widthPx = static_cast<int>( effectiveSize.width() * ( pixelRatio > 0 ? pixelRatio : 1.0 ) );
  const int heightPx = static_cast<int>( effectiveSize.height() * ( pixelRatio > 0 ? pixelRatio : 1.0 ) );
  mPrivate->metalLayer = QgsMapLibreMetal::createOffscreenLayer( widthPx, heightPx );
  if ( mPrivate->metalLayer == nullptr )
  {
    QgsDebugError( QStringLiteral( "Failed to create offscreen Metal layer (size=%1x%2)" ).arg( widthPx ).arg( heightPx ) );
    return;
  }

  QMapLibre::Settings settings;
  settings.setMapMode( QMapLibre::Settings::MapMode::Static );

  mPrivate->map = std::make_unique<QMapLibre::Map>(
    nullptr,
    settings,
    effectiveSize,
    pixelRatio > 0 ? pixelRatio : 1.0
  );

  if ( !mStyleUrl.isEmpty() )
  {
    mPrivate->map->setStyleUrl( mStyleUrl );
  }

  // Phase 4a: just verify the Metal renderer can be attached without crashing.
  // Actual rendering pipeline (startStaticRender / readback) follows in 4b.
  mPrivate->map->createRenderer( mPrivate->metalLayer );

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
    QgsDebugMsgLevel( QStringLiteral( "Destroyed QMapLibre::Map + renderer" ), 1 );
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
  Q_UNUSED( context )
}

void QgsMapLibreVectorTileRenderer::renderTile( const QgsVectorTileRendererData &tile, QgsRenderContext &context )
{
  // Phase 4a: visual marker unchanged. Actual pixel transport comes in 4b.
  QPainter *p = context.painter();
  if ( !p )
    return;

  p->save();
  QPen pen( QColor( 255, 0, 0, 200 ) );
  pen.setWidth( 2 );
  p->setPen( pen );
  p->setBrush( QColor( 255, 0, 0, 20 ) );
  p->drawPolygon( tile.tilePolygon() );

  const QgsTileXYZ id = tile.id();
  const QString label = QStringLiteral( "maplibre %1/%2/%3" ).arg( id.zoomLevel() ).arg( id.column() ).arg( id.row() );
  const QPoint anchor = tile.tilePolygon().boundingRect().center();
  p->setPen( QColor( 200, 0, 0 ) );
  p->drawText( anchor, label );
  p->restore();
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
