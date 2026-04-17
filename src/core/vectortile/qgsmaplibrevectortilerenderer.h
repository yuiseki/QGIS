/***************************************************************************
  qgsmaplibrevectortilerenderer.h
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

#ifndef QGSMAPLIBREVECTORTILERENDERER_H
#define QGSMAPLIBREVECTORTILERENDERER_H

#include "qgis_core.h"
#include "qgis_sip.h"
#include "qgsvectortilerenderer.h"

#include <memory>

/**
 * \ingroup core
 * \brief Vector tile renderer backed by maplibre-native (via QMapLibre).
 *
 * Phase 3 skeleton: instantiates QMapLibre::Map during startRender() and
 * tears it down in stopRender() so we can verify the lifecycle works.
 * Actual map drawing is added in later phases.
 *
 * \since QGIS 4.1
 */
class CORE_EXPORT QgsMapLibreVectorTileRenderer : public QgsVectorTileRenderer
{
  public:
    QgsMapLibreVectorTileRenderer();
    ~QgsMapLibreVectorTileRenderer() override;

    QString type() const override;
    QgsMapLibreVectorTileRenderer *clone() const override SIP_FACTORY;
    void startRender( QgsRenderContext &context, int tileZoom, const QgsTileRange &tileRange ) override;
    void stopRender( QgsRenderContext &context ) override;
    void renderBackground( QgsRenderContext &context ) override;
    void renderTile( const QgsVectorTileRendererData &tile, QgsRenderContext &context ) override;
    void renderSelectedFeatures( const QList<QgsFeature> &selection, QgsRenderContext &context ) override;
    bool willRenderFeature( const QgsFeature &feature, int tileZoom, const QString &layerName, QgsRenderContext &context ) override;
    void writeXml( QDomElement &elem, const QgsReadWriteContext &context ) const override;
    void readXml( const QDomElement &elem, const QgsReadWriteContext &context ) override;

  private:
    QString mStyleUrl;

    // Pimpl: QMapLibre types are only exposed in the .cpp (behind WITH_MAPLIBRE)
    class Private;
    std::unique_ptr<Private> mPrivate;
};

#endif // QGSMAPLIBREVECTORTILERENDERER_H
