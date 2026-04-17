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
#include <QMapLibre/Map>
#include <QMapLibre/Settings>
#endif

class QgsMapLibreVectorTileRenderer::Private
{
  public:
#ifdef WITH_MAPLIBRE
    std::unique_ptr<QMapLibre::Map> map;
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

  QMapLibre::Settings settings;
  settings.setMapMode( QMapLibre::Settings::MapMode::Static );

  mPrivate->map = std::make_unique<QMapLibre::Map>(
    nullptr,
    settings,
    viewportSize.isEmpty() ? QSize( 512, 512 ) : viewportSize,
    pixelRatio > 0 ? pixelRatio : 1.0
  );

  if ( !mStyleUrl.isEmpty() )
  {
    mPrivate->map->setStyleUrl( mStyleUrl );
  }

  QgsDebugMsgLevel(
    QStringLiteral( "QMapLibre::Map created size=%1x%2 pixelRatio=%3 styleUrl=%4" )
      .arg( viewportSize.width() ).arg( viewportSize.height() )
      .arg( pixelRatio ).arg( mStyleUrl ),
    1
  );

  // NOTE: createRenderer() requires Metal/Vulkan/GL setup which we don't wire
  // up yet. Phase 3 only verifies Map construction + style load. Phase 4 will
  // add the actual renderer and texture integration.
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
    QgsDebugMsgLevel( QStringLiteral( "Destroying QMapLibre::Map" ), 1 );
    mPrivate->map.reset();
  }
#endif
}

void QgsMapLibreVectorTileRenderer::renderBackground( QgsRenderContext &context )
{
  Q_UNUSED( context )
}

void QgsMapLibreVectorTileRenderer::renderTile( const QgsVectorTileRendererData &tile, QgsRenderContext &context )
{
  // Phase 3: still the skeleton visual marker. Actual rendering comes in Phase 4+.
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
