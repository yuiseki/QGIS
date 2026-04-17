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

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QThread>
#include <QtGlobal>
#include <QDebug>

#ifdef Q_OS_MACOS

#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <QMapLibre/Map>
#include <QMapLibre/Settings>

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
    layer.framebufferOnly = NO;   // required for blit-readback
    layer.displaySyncEnabled = NO;
    if ( [layer respondsToSelector:@selector( setAllowsNextDrawableTimeout: )] )
    {
      layer.allowsNextDrawableTimeout = NO;
    }
    layer.drawableSize = CGSizeMake( widthPx, heightPx );

    return ( void * )CFBridgingRetain( layer );
  }

  void destroyLayer( void *layer )
  {
    if ( layer == nullptr )
      return;
    CFRelease( ( CFTypeRef )layer );
  }

  QImage renderOnce( QMapLibre::Map *map, void *layerPtr, int widthPx, int heightPx )
  {
    if ( map == nullptr || layerPtr == nullptr || widthPx <= 0 || heightPx <= 0 )
      return QImage();

    @autoreleasepool
    {
      CAMetalLayer *layer = ( __bridge CAMetalLayer * )layerPtr;

      id<CAMetalDrawable> drawable = [layer nextDrawable];
      if ( drawable == nil )
      {
        qWarning() << "QgsMapLibreMetal::renderOnce: nextDrawable returned nil";
        return QImage();
      }

      map->setCurrentDrawable( ( void * )drawable.texture );

      // maplibre loads tiles asynchronously on its own worker threads. One
      // render() call draws only what's already loaded, which is usually
      // nothing on the first frame. Sleep + pump events + re-render until
      // tiles have had a chance to arrive.
      //
      // 15 iterations * 100 ms = ~1.5 s budget per render cycle. That is
      // enough for a disk-cached revisit to a previously-fetched area
      // (tile open + decode + upload runs well inside a second) while
      // keeping the worst case from dominating user-perceived latency.
      // Genuinely cold tiles may render incomplete on the first pan; a
      // second pan picks them up from the on-disk cache.
      //
      // A needsRendering-signal-driven loop was tried and immediately
      // destabilised the process (rendering blank / shutdown hang), so
      // the fixed-iteration sleep stays in place for now.
      const int kIterations = 15;
      const int kSleepMs = 100;
      for ( int i = 0; i < kIterations; ++i )
      {
        map->render();
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( kSleepMs );
      }

      const void *nativeTex = map->nativeColorTexture();
      id<MTLTexture> tex = ( __bridge id<MTLTexture> )( nativeTex ? nativeTex : ( void * )drawable.texture );
      if ( tex == nil )
        return QImage();

      const NSUInteger texW = tex.width;
      const NSUInteger texH = tex.height;
      const NSUInteger bytesPerRow = texW * 4;
      const NSUInteger totalBytes = bytesPerRow * texH;

      QImage image( static_cast<int>( texW ), static_cast<int>( texH ), QImage::Format_ARGB32_Premultiplied );
      if ( image.isNull() )
        return QImage();

      // Blit-copy the texture into a shared MTLBuffer and memcpy from there.
      // Direct [tex getBytes:...] was observed to hang / never return on
      // CAMetalDrawable textures in Managed storage mode; going via an
      // intermediate MTLBuffer sidesteps that.
      id<MTLDevice> device = layer.device;
      id<MTLBuffer> readback = [device newBufferWithLength:totalBytes options:MTLResourceStorageModeShared];
      if ( readback == nil )
      {
        qWarning() << "QgsMapLibreMetal::renderOnce: newBufferWithLength failed";
        return QImage();
      }

      id<MTLCommandQueue> queue = [device newCommandQueue];
      id<MTLCommandBuffer> cmd = [queue commandBuffer];
      id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
      [blit copyFromTexture:tex
              sourceSlice:0
              sourceLevel:0
              sourceOrigin:MTLOriginMake( 0, 0, 0 )
              sourceSize:MTLSizeMake( texW, texH, 1 )
              toBuffer:readback
              destinationOffset:0
              destinationBytesPerRow:bytesPerRow
              destinationBytesPerImage:totalBytes];
      [blit endEncoding];
      [cmd commit];
      [cmd waitUntilCompleted];

      memcpy( image.bits(), [readback contents], totalBytes );

      Q_UNUSED( widthPx )
      Q_UNUSED( heightPx )

      return image;
    }
  }

} // namespace QgsMapLibreMetal

#else // !Q_OS_MACOS

namespace QgsMapLibreMetal
{
  void *createOffscreenLayer( int, int ) { return nullptr; }
  void destroyLayer( void * ) {}
  QImage renderOnce( QMapLibre::Map *, void *, int, int ) { return QImage(); }
}

#endif // Q_OS_MACOS
