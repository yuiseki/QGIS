/***************************************************************************
  qgsmaplibremetal.mm
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

#include "qgsmaplibremetal_p.h"

#include <QtGlobal>

#ifdef Q_OS_MACOS

#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace QgsMapLibreMetal
{

  void *createOffscreenLayer( int widthPx, int heightPx )
  {
    if ( widthPx <= 0 || heightPx <= 0 )
      return nullptr;

    const id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if ( device == nil )
      return nullptr;

    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;   // required for readback
    layer.displaySyncEnabled = NO;
    if ( [layer respondsToSelector:@selector( setAllowsNextDrawableTimeout: )] )
    {
      layer.allowsNextDrawableTimeout = NO;
    }
    layer.drawableSize = CGSizeMake( widthPx, heightPx );

    // Transfer ownership to caller: __bridge_retained balanced by CFRelease in destroyLayer.
    return ( void * )CFBridgingRetain( layer );
  }

  void destroyLayer( void *layer )
  {
    if ( layer == nullptr )
      return;
    CFRelease( ( CFTypeRef )layer );
  }

} // namespace QgsMapLibreMetal

#else // !Q_OS_MACOS

namespace QgsMapLibreMetal
{
  void *createOffscreenLayer( int, int ) { return nullptr; }
  void destroyLayer( void * ) {}
}

#endif // Q_OS_MACOS
