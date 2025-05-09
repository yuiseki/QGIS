/***************************************************************************
                             qgsmodelviewtooltemporarykeyzoom.h
                             -----------------------------------
    Date                 : March 2020
    Copyright            : (C) 2020 Nyall Dawson
    Email                : nyall dot dawson at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSMODELVIEWTOOLTEMPORARYKEYZOOM_H
#define QGSMODELVIEWTOOLTEMPORARYKEYZOOM_H

#include "qgis_sip.h"
#include "qgis_gui.h"
#include "qgsmodelviewtoolzoom.h"
#include "qgsmodelviewrubberband.h"
#include <memory>

#define SIP_NO_FILE

/**
 * \ingroup gui
 * \brief Model view tool for temporarily zooming a model while a key is depressed.
 * \since QGIS 3.14
 */
class GUI_EXPORT QgsModelViewToolTemporaryKeyZoom : public QgsModelViewToolZoom
{
    Q_OBJECT

  public:
    /**
     * Constructor for QgsModelViewToolTemporaryKeyZoom.
     */
    QgsModelViewToolTemporaryKeyZoom( QgsModelGraphicsView *view SIP_TRANSFERTHIS );

    void modelReleaseEvent( QgsModelViewMouseEvent *event ) override;
    void keyPressEvent( QKeyEvent *event ) override;
    void keyReleaseEvent( QKeyEvent *event ) override;
    void activate() override;

  private:
    QPointer<QgsModelViewTool> mPreviousViewTool;

    bool mDeactivateOnMouseRelease = false;

    void updateCursor( Qt::KeyboardModifiers modifiers );
};

#endif // QGSMODELVIEWTOOLTEMPORARYKEYZOOM_H
