/***************************************************************************
  qgsmaplibremetal_p.h
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

#ifndef QGSMAPLIBREMETAL_P_H
#define QGSMAPLIBREMETAL_P_H

/// \cond PRIVATE

#include <QImage>

namespace QMapLibre
{
  class Map;
}

//
// Internal Metal helper used by QgsMapLibreVectorTileRenderer on macOS.
// Implemented in qgsmaplibremetal.mm (Objective-C++). Only compiled when
// WITH_MAPLIBRE is on and we are building for macOS.
//

namespace QgsMapLibreMetal
{
  /**
   * Creates an offscreen CAMetalLayer sized \a widthPx x \a heightPx (in
   * pixels, i.e. already multiplied by the device pixel ratio). Ownership
   * is transferred to the caller; the pointer must be freed by destroyLayer().
   *
   * Returns an opaque (__bridge_retained) CAMetalLayer* suitable for passing
   * to QMapLibre::Map::createRenderer().
   *
   * Returns nullptr if the system has no Metal device.
   */
  void *createOffscreenLayer( int widthPx, int heightPx );

  /**
   * Releases a layer previously obtained from createOffscreenLayer().
   * Safe to call with nullptr.
   */
  void destroyLayer( void *layer );

  /**
   * Renders one frame of \a map into \a layer's next drawable, then reads
   * back the result into a QImage (Format_ARGB32_Premultiplied, which is
   * memory-compatible with MTLPixelFormatBGRA8Unorm on little-endian).
   *
   * This is synchronous: it acquires a drawable, calls
   * QMapLibre::Map::render(), and performs a GPU->CPU readback before
   * returning. Intended for use from the QGIS parallel render workers.
   *
   * Returns an empty QImage on any failure.
   */
  QImage renderOnce( QMapLibre::Map *map, void *layer, int widthPx, int heightPx );
}

/// \endcond

#endif // QGSMAPLIBREMETAL_P_H
