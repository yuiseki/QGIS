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
#include <QElapsedTimer>
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

    QElapsedTimer t_total;
    t_total.start();
    qint64 t_drawable = 0, t_renders_total = 0, t_native_tex = 0;
    qint64 t_buffer_alloc = 0, t_blit = 0, t_memcpy = 0;
    qint64 max_render = 0, min_render = INT64_MAX;
    int render_count = 0;

    @autoreleasepool
    {
      CAMetalLayer *layer = ( __bridge CAMetalLayer * )layerPtr;

      QElapsedTimer t;
      t.start();
      id<CAMetalDrawable> drawable = [layer nextDrawable];
      t_drawable = t.elapsed();
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
      // Fixed sleep loop. We tried two shorter alternatives:
      //   - render-time-based early-exit (~350 ms): dropped terrain
      //     because render() returns fast even while tiles are still
      //     being fetched.
      //   - fixed 800 ms loop: terrain came back but labels rendered
      //     half-faded - we were catching maplibre's symbol fade-in
      //     mid-animation (default fade is ~300 ms after tile arrival).
      // 12 * 100 ms = ~1.2 s gives a tile that arrives at ~800 ms enough
      // headroom for its labels to finish fading.
      const int kIterations = 12;
      const int kSleepMs = 100;
      for ( int i = 0; i < kIterations; ++i )
      {
        QElapsedTimer ti;
        ti.start();
        map->render();
        const qint64 elapsed_render = ti.elapsed();
        t_renders_total += elapsed_render;
        if ( elapsed_render > max_render ) max_render = elapsed_render;
        if ( elapsed_render < min_render ) min_render = elapsed_render;
        ++render_count;
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( kSleepMs );
      }

      QElapsedTimer t2;
      t2.start();
      const void *nativeTex = map->nativeColorTexture();
      t_native_tex = t2.elapsed();
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
      QElapsedTimer t3;
      t3.start();
      id<MTLDevice> device = layer.device;
      id<MTLBuffer> readback = [device newBufferWithLength:totalBytes options:MTLResourceStorageModeShared];
      if ( readback == nil )
      {
        qWarning() << "QgsMapLibreMetal::renderOnce: newBufferWithLength failed";
        return QImage();
      }
      t_buffer_alloc = t3.elapsed();

      QElapsedTimer t4;
      t4.start();
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
      t_blit = t4.elapsed();

      QElapsedTimer t5;
      t5.start();
      memcpy( image.bits(), [readback contents], totalBytes );
      t_memcpy = t5.elapsed();

      Q_UNUSED( widthPx )
      Q_UNUSED( heightPx )

      qDebug().noquote() << QStringLiteral(
        "renderOnce timing: total=%1 drawable=%2 renders=%3 (count=%4 min=%5 max=%6) "
        "nativeTex=%7 bufAlloc=%8 blit=%9 memcpy=%10 [ms]" )
        .arg( t_total.elapsed() )
        .arg( t_drawable )
        .arg( t_renders_total )
        .arg( render_count )
        .arg( min_render == INT64_MAX ? 0 : min_render )
        .arg( max_render )
        .arg( t_native_tex )
        .arg( t_buffer_alloc )
        .arg( t_blit )
        .arg( t_memcpy );

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
